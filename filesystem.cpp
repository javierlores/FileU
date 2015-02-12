#include "filesystem.h"
#include <iostream>
#include <string>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <list>
#include <cctype>
#include <map>
#include <cmath>
#include <ctime>
#include <algorithm>

using namespace FAT_FS;

// *********************************************************
// *********************************************************
// *           CONSTRUCTORS AND DESTRUCTORS                *
// *********************************************************
// *********************************************************

FileSystem::FileSystem(std::string file_system_image)
{
    // Setup file descriptor
    m_file_descriptor = ::open(file_system_image.c_str(), O_RDWR);

    if (m_file_descriptor < 0)
    {
        m_error = true;
        return;
    }
    else
        m_error = false;

    // Get file size
    struct stat file_status;
    stat(file_system_image.c_str(), &file_status);
    m_file_system_size = file_status.st_size;

    // Map the fat system
    m_file_system_data = (uint8_t*) mmap(0, m_file_system_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_file_descriptor, 0);

    // Read bios parameter block
    m_bpb.bytes_per_sector = readFromFileSystem<uint16_t>(11, 2);
    m_bpb.sectors_per_cluster = readFromFileSystem<uint8_t>(13, 1);
    m_bpb.reserved_sector_count = readFromFileSystem<uint16_t>(14, 2);
    m_bpb.num_FATS = readFromFileSystem<uint8_t>(16, 1);
    m_bpb.total_sectors = readFromFileSystem<uint32_t>(32, 4);
    m_bpb.FATSz = readFromFileSystem<uint32_t>(36, 4);
    m_bpb.root_cluster = readFromFileSystem<uint32_t>(44, 4);
    m_bpb.fsinfo = readFromFileSystem<uint32_t>(48, 2);

    // Read fsinfo
    m_fsinfo.free_cluster_count = readFromFileSystem<uint32_t>(m_bpb.fsinfo * m_bpb.bytes_per_sector + 488, 4);
    m_fsinfo.first_free_cluster = readFromFileSystem<uint32_t>(m_bpb.fsinfo * m_bpb.bytes_per_sector + 492, 4);

    // Calculate other necessary information
    m_bytes_per_cluster = m_bpb.bytes_per_sector * m_bpb.sectors_per_cluster;
    m_first_data_sector = m_bpb.reserved_sector_count + (m_bpb.num_FATS * m_bpb.FATSz);

    // Set current directory information
    m_current_directory_cluster = m_bpb.root_cluster;
    m_current_directory_name = ROOT;
}

FileSystem::~FileSystem()
{
    munmap(m_file_system_data, m_file_system_size);
    if (m_file_descriptor > 0)
        ::close(m_file_descriptor);
}

// *********************************************************
// *********************************************************
// *                  PUBLIC FUNCTIONS                     *
// *********************************************************
// *********************************************************

void FileSystem::fsinfo()
{
    cout << "Bytes Per Sector: " << m_bpb.bytes_per_sector << endl;
    cout << "Sectors Per Cluster: " << (uint32_t)m_bpb.sectors_per_cluster << endl;
    cout << "Total Sectors: " << m_bpb.total_sectors << endl;
    cout << "Number of FATS: " << (uint32_t)m_bpb.num_FATS << endl;
    cout << "Sectors per FAT: " << m_bpb.FATSz << endl;
    cout << "Number of Free Sectors: " << m_fsinfo.free_cluster_count * m_bpb.sectors_per_cluster << endl;
}

void FileSystem::open(std::string file_name, std::string mode)
{
    if (!isValidEntryName(file_name))
    {
        cout << "Error: file name may not contain /." << endl;
        return;
    }

    string mode_type;
    if (mode == READ)
        mode_type = "read-only";
    else if (mode == WRITE)
        mode_type = "write-only";
    else if (mode == READ_WRITE)
        mode_type = "read-write";
    else
    {
        cout << "Error: Invalid mode. Valid modes are r, w, and rw." << endl;
        return;
    }

    DirectoryEntry file;
    if (findDirectoryEntry(file_name, m_current_directory_cluster, file))
    {
        if (isFile(file))
        {
            if (m_open_file_table.find(file) == m_open_file_table.end())
            {
                m_open_file_table[file] = mode;
                cout << "'" << file_name << "' has been opened with " << mode_type << " permission."<< endl;
            }
            else
                cout << "Error: '" << file_name << "' is already open." << endl;
        }
        else
            cout << "Error: '" << file_name << "' is not a file." << endl;
    }
    else
        cout << "Error: '" << file_name << "' not found." << endl;
}

void FileSystem::close(std::string file_name)
{
    if (!isValidEntryName(file_name))
    {
        cout << "Error: file name may not contain /." << endl;
        return;
    }

    std::map<DirectoryEntry, std::string>::iterator iterator;
    for (iterator = m_open_file_table.begin(); iterator != m_open_file_table.end(); iterator++)
    {
        if (iterator->first.name == file_name)
        {
            m_open_file_table.erase(iterator);
            cout << "'" << file_name << "' is now closed." << endl;
            return;
        }
    }

    cout << "'" << file_name << "' not found in the open file table" << endl;
    return;
}

void FileSystem::create(std::string file_name)
{
    // Ensure valid entry name
    if (!isValidEntryName(file_name))
    {
        cout << "Error: file name may not contain /." << endl;
        return;
    }

    // Ensure valid file name
    for (int i = 0; i < file_name.length(); i++)
    {
        if (file_name[i] == SPECIAL_INVALID_CHAR && i != 0)
        {
            cout << "Error: file name cannot contain '" << file_name[i] << "'" << endl;
            return;
        }

        for (int j = 0; j < INVALID_CHAR_LIST_SIZE; j++)
        {
            if (file_name[i] == INVALID_CHAR_LIST[j])
            {
                cout << "Error: file name cannot contain '" << file_name[i] << "'" << endl;
                return;
            }
        }
    }

    if (file_name == "." || file_name == "..")
    {
       cout << "Error: cannot create '" << file_name << "'" << endl;
       return;
    }

    size_t dot_sep_loc = file_name.find(".");

    if (dot_sep_loc != std::string::npos)
    {
        std::string main = file_name.substr(0, dot_sep_loc);
        std::string extension = file_name.substr(dot_sep_loc + 1, file_name.length() - 1);

        if (main.length() > 8 || extension.length() > 3  )
        {
            cout << "Error: main or extension is too long" << endl;
            return;
        }
    }
    else if (file_name.length() > 11)
    {
        cout << "Error: file name is too long" << endl;
        return;
    }

    // Ensure file doesn't already exists
    if (directoryEntryExists(file_name, m_current_directory_cluster))
    {
        cout << "'" << file_name << "' already exists." << endl;
        return;
    }

    createDirectoryEntry(file_name, m_current_directory_cluster, FILE);
}

void FileSystem::read(std::string file_name, uint32_t start_pos, uint32_t num_bytes)
{
    if (!isValidEntryName(file_name))
    {
        cout << "Error: file name may not contain /." << endl;
        return;
    }

    // Search for file in open file table
    std::map<DirectoryEntry, std::string>::iterator iterator;
    for (iterator = m_open_file_table.begin(); iterator != m_open_file_table.end(); iterator++)
    {
        DirectoryEntry file = iterator->first;
        std::string file_mode = iterator->second;

        if (file.name == file_name)
        {
            // Make sure file is readable
            if(file_mode != READ && file_mode != READ_WRITE)
            {
                cout << "'" << file_name << "' is not open for reading." << endl;
                return;
            }
            else if (!isFile(file))
            {
                cout << "'" << file_name << "' is not a file." << endl;
                return;
            }
            // Make sure the start position is not greater than the file size
            else if (start_pos > file.size)
            {
                cout << start_pos << " is greater than the file size." << endl;
                return;
            }
            // Read the file
            else
            {
                if (num_bytes > file.size)
                    num_bytes = file.size;

                std::vector<uint32_t> cluster_chain = getClusterChain(file.cluster);
                uint32_t bytes_read = 0;

                for (size_t i = (start_pos / m_bytes_per_cluster); i < cluster_chain.size() && bytes_read <= num_bytes; i++)
                {
                    uint32_t cluster_pos = getFirstDataSector(cluster_chain[i]) * m_bpb.bytes_per_sector;
                    uint32_t cluster_end = cluster_pos + m_bytes_per_cluster;

                    if (i == (start_pos / m_bytes_per_cluster))
                        cluster_pos += start_pos % m_bytes_per_cluster;

                    for (size_t j = cluster_pos; j < cluster_end && bytes_read <= num_bytes; bytes_read++, j++)
                        cout << readFromFileSystem<char>(j, 1);
                }

                cout << endl;
                return;
            }
        }
    }

    cout << "'" << file_name << "' not found in the open file table" << endl;
    return;
}

void FileSystem::write(std::string file_name, uint32_t start_pos, std::string quoted_data)
{
    if (!isValidEntryName(file_name))
    {
        cout << "Error: file name may not contain /." << endl;
        return;
    }

    std::map<DirectoryEntry, std::string>::iterator iterator;
    for (iterator = m_open_file_table.begin(); iterator != m_open_file_table.end(); iterator++)
    {
        DirectoryEntry file = iterator->first;
        std::string file_mode = iterator->second;

        if (file.name == file_name)
        {
            if(file_mode != WRITE && file_mode != READ_WRITE)
            {
                cout << "'" << file_name << "' is not open for writing." << endl;
                return;
            }
            else
            {
                std::vector<uint32_t> cluster_chain = getClusterChain(file.cluster);

                uint32_t write_request_size = start_pos + quoted_data.length();
                uint32_t file_alloc_size = cluster_chain.size() * m_bytes_per_cluster;

                // Ensure sufficient space in cluster chain for write request, allocate space if necessary
                if (write_request_size > file_alloc_size)
                {
                    uint32_t cluster_alloc_size = ceil(static_cast<double>(write_request_size - file_alloc_size) / m_bytes_per_cluster);

                    if (m_fsinfo.free_cluster_count < cluster_alloc_size)
                    {
                        cout << "Error: insufficient space for write request."<< endl;
                        return;
                    }
                    else
                        resizeClusterChain(cluster_chain.size() + cluster_alloc_size, cluster_chain);
                }

                if (write_request_size > file.size)
                    updateFile(file, write_request_size, cluster_chain);


                // Write the data to the file system
                uint32_t bytes_written = 0;

                for (size_t i = (start_pos / m_bytes_per_cluster); i < cluster_chain.size() && bytes_written < quoted_data.length(); i++)
                {
                    uint32_t cluster_pos = getFirstDataSector(cluster_chain[i]) * m_bpb.bytes_per_sector;
                    uint32_t cluster_end = cluster_pos + m_bytes_per_cluster;

                    if (i == (start_pos / m_bytes_per_cluster))
                        cluster_pos += start_pos % m_bytes_per_cluster;

                    for (size_t j = cluster_pos; j < cluster_end && bytes_written < quoted_data.length(); bytes_written++, j++)
                        writeToFileSystem<char>(quoted_data[bytes_written], j, 1);
                }

                cout << "Wrote \"" << quoted_data << "\" to " << start_pos << ":" << file_name << " of length " << quoted_data.length() << endl;
                return;
            }
        }
    }

    cout << "'" << file_name << "' not found in the open file table." << endl;
    return;

}

void FileSystem::rm(std::string file_name)
{
    if (!isValidEntryName(file_name))
    {
        cout << "Error: file name may not contain /." << endl;
        return;
    }

    DirectoryEntry file;
    if (findDirectoryEntry(file_name, m_current_directory_cluster, file))
    {
        if (isFile(file))
        {
            if (m_open_file_table.find(file) != m_open_file_table.end())
                m_open_file_table.erase(file);
            deleteDirectoryEntry(file_name, m_current_directory_cluster, file);
        }
        else
           cout << "Error: '" << file_name << "' is not a file." << endl;
    }
    else
        cout << "Error: '" << file_name << "' not found." << endl;
}

void FileSystem::cd(std::string dir_name)
{
    if (!isValidEntryName(dir_name))
    {
        cout << "Error: directory name may not contain /." << endl;
        return;
    }

    DirectoryEntry directory;
    if (findDirectoryEntry(dir_name, m_current_directory_cluster, directory))
    {
        if (isDirectory(directory))
        {
            m_current_directory_cluster = directory.cluster;
            m_current_directory_name = dir_name;
        }
        else
           cout << "Error: '" << dir_name << "' is not a directory." << endl;
    }
    else
        cout << "Error: '" << dir_name << "' not found." << endl;
}

void FileSystem::ls(std::string dir_name)
{
    if (!isValidEntryName(dir_name))
    {
        cout << "Error: directory name may not contain /." << endl;
        return;
    }

    DirectoryEntry directory;
    if (findDirectoryEntry(dir_name, m_current_directory_cluster, directory))
    {
        if (isDirectory(directory))
        {
            std::list<DirectoryEntry> dir_entry_list = getDirectoryEntries(directory.cluster);

            std::list<DirectoryEntry>::iterator iterator;
            for (iterator = dir_entry_list.begin(); iterator != dir_entry_list.end(); iterator++)
                cout << iterator->name << " ";
            cout << endl;
        }
        else
           cout << "Error: '" << dir_name << "' is not a directory." << endl;
    }
    else
        cout << "Error: '" << dir_name << "' not found." << endl;
}

void FileSystem::mkdir(std::string dir_name)
{
    // Ensure valid entry name
    if (!isValidEntryName(dir_name))
    {
        cout << "Error: directory name may not contain /." << endl;
        return;
    }

    // Ensure valid dir name
    for (int i = 0; i < dir_name.length(); i++)
    {
        if (dir_name[i] == SPECIAL_INVALID_CHAR && i != 0)
        {
            cout << "Error: directory name cannot contain '" << dir_name[i] << "'" << endl;
            return;
        }

        for (int j = 0; j < INVALID_CHAR_LIST_SIZE; j++)
        {
            if (dir_name[i] == INVALID_CHAR_LIST[j])
            {
                cout << "Error: directory name cannot contain '" << dir_name[i] << "'" << endl;
                return;
            }
        }
    }

    size_t dot_sep_loc = dir_name.find(".");

    if (dot_sep_loc != std::string::npos)
    {
        std::string main = dir_name.substr(0, dot_sep_loc);
        std::string extension = dir_name.substr(dot_sep_loc + 1, dir_name.length() - 1);

        if (main.length() > 8 || extension.length() > 3  )
        {
            cout << "Error: main or extension is too long" << endl;
            return;
        }
    }
    else if (dir_name.length() > 11)
    {
        cout << "Error: directory name is too long" << endl;
        return;
    }

    // Ensure directory doesn't already exists
    if (directoryEntryExists(dir_name, m_current_directory_cluster))
    {
        cout << "'"<< dir_name << "' already exists." << endl;
        return;
    }

    createDirectoryEntry(dir_name, m_current_directory_cluster, DIRECTORY);
}

void FileSystem::rmdir(std::string dir_name)
{
    if (!isValidEntryName(dir_name))
    {
        cout << "Error: directory name may not contain /." << endl;
        return;
    }

    DirectoryEntry directory;
    if (findDirectoryEntry(dir_name, m_current_directory_cluster, directory))
    {
        if (isDirectory(directory))
        {
            std::list<DirectoryEntry> dir_entry_list = getDirectoryEntries(directory.cluster);

            std::list<DirectoryEntry>::iterator iterator;
            for (iterator = dir_entry_list.begin(); iterator != dir_entry_list.end(); iterator++)
            {
                if (iterator->name != "." && iterator->name != "..")
                {
                    cout << "Error: '" << dir_name << "' is not empty." << endl;
                    return;
                }
            }

            deleteDirectoryEntry(dir_name, m_current_directory_cluster, directory);
        }
        else
           cout << "Error: '" << dir_name << "' is not a directory." << endl;
    }
    else
        cout << "Error: '" << dir_name << "' not found." << endl;
}

void FileSystem::size(std::string entry_name)
{
    if (!isValidEntryName(entry_name))
    {
        cout << "Error: directory or file name may not contain /." << endl;
        return;
    }

    DirectoryEntry dir_entry;
    if (findDirectoryEntry(entry_name, m_current_directory_cluster, dir_entry))
    {
        std::vector<uint32_t> cluster_chain = getClusterChain(dir_entry.cluster);
        cout << "'"<< entry_name << "' has " << (cluster_chain.size() * m_bytes_per_cluster) << " allocated bytes." << endl;
    }
    else
        cout << "Error: '" << entry_name << "' not found." << endl;
}

void FileSystem::undelete()
{
    int file_recovered_count = 0;
    std::vector<uint32_t> cluster_chain = getClusterChain(m_current_directory_cluster);

    std::vector<uint32_t>::iterator iterator;
    for (iterator = cluster_chain.begin(); iterator != cluster_chain.end(); iterator++)
    {
        uint32_t sector = getFirstDataSector(*iterator) * m_bpb.bytes_per_sector;

        for (int i = m_bytes_per_cluster - DIR_ENTRY_SIZE; i >= 0; i -= DIR_ENTRY_SIZE)
        {
            DirectoryEntry dir_entry = readDirectoryEntry(sector + i);

            if (isFreeEntry(dir_entry) && isFile(dir_entry) && dir_entry.cluster != 0)
            {
                setFATEntry(dir_entry.cluster, EOC);
                setFreeClusterCount(m_fsinfo.free_cluster_count - 1);

                dir_entry.name = "undel." + std::to_string(++file_recovered_count);
                if (dir_entry.size > m_bytes_per_cluster)
                    dir_entry.size = m_bytes_per_cluster;
                writeDirectoryEntry(dir_entry);
            }
        }
    }
}

// *********************************************************
// *********************************************************
// *                  PRIVATE FUNCTIONS                    *
// *********************************************************
// *********************************************************

template <typename T>
T FileSystem::readFromFileSystem(size_t offset, size_t bytes)
{
    T value = 0;
    for(size_t i = 0; i < bytes; i++)
        value |= (m_file_system_data[offset + i] << (i * 8));

    return value;
}

template <typename T>
void FileSystem::writeToFileSystem(T data, size_t offset, size_t bytes)
{
    T mask = 0xFF;

    for(size_t i = 0; i < bytes; i++)
    {
        m_file_system_data[offset + i] =  (uint8_t)(data & mask) ;
        data = data >> 8;
    }
}

std::list<DirectoryEntry> FileSystem::getDirectoryEntries(uint32_t cluster)
{
    std::list<DirectoryEntry> dir_entry_list;

    do
    {
        uint32_t sector = getFirstDataSector(cluster) * m_bpb.bytes_per_sector;
        std::list<DirectoryEntry> cluster_entry_list;

        for (int i = m_bytes_per_cluster - DIR_ENTRY_SIZE; i >= 0; i -= DIR_ENTRY_SIZE)
        {
            DirectoryEntry dir_entry = readDirectoryEntry(sector + i);

            if (!isLongName(dir_entry) && !isFreeEntry(dir_entry))
               cluster_entry_list.push_front(dir_entry);
        }

        dir_entry_list.insert(dir_entry_list.end(), cluster_entry_list.begin(), cluster_entry_list.end());
    } while((cluster = getFATEntry(cluster)) < EOC);

    return dir_entry_list;
}

std::vector<uint32_t> FileSystem::getClusterChain(uint32_t cluster)
{
    std::vector<uint32_t> cluster_chain;

    do
    {
        cluster_chain.push_back(cluster);
    } while ((cluster = getFATEntry(cluster)) < EOC);

    return cluster_chain;
}

uint32_t FileSystem::resizeClusterChain(uint32_t size, std::vector<uint32_t>& cluster_chain)
{
    if (size <= cluster_chain.size())
        return cluster_chain.size();

    if (cluster_chain[0] == 0)
    {
        uint32_t alloc_cluster = allocateCluster();

        cluster_chain.pop_back();
        cluster_chain.push_back(alloc_cluster);

        size--;
    }

    for (int i = 0; i < (size - cluster_chain.size()); i++)
    {
        uint32_t cluster = cluster_chain.back();
        uint32_t alloc_cluster = allocateCluster(cluster);

        cluster_chain.pop_back();
        cluster_chain.push_back(cluster);
        cluster_chain.push_back(alloc_cluster);
    }

    return cluster_chain.size();
}

uint32_t FileSystem::allocateCluster(uint32_t cluster)
{
    uint32_t free_cluster = getFreeCluster();

    if (cluster != 0)
        setFATEntry(cluster, free_cluster);
    setFATEntry(free_cluster, EOC);

    setFreeClusterCount(m_fsinfo.free_cluster_count - 1);

    return free_cluster;
}

uint32_t FileSystem::getDirectoryCluster(std::string dir_name)
{
    if (dir_name == ROOT)
        return m_bpb.root_cluster;

    std::list<DirectoryEntry> dir_entry_list = getDirectoryEntries(m_current_directory_cluster);

    std::list<DirectoryEntry>::iterator iterator;
    for(iterator = dir_entry_list.begin(); iterator != dir_entry_list.end(); iterator++)
        if (iterator->name == dir_name)
            return iterator->cluster;

    return -1;
}

uint32_t FileSystem::getFATEntry(uint32_t cluster)
{
    uint32_t FAT_sector = getFATSector(cluster);
    uint32_t FAT_ent_offset = getFATEntOffset(cluster);
    uint32_t FAT_entry = readFromFileSystem<uint32_t>(FAT_sector * m_bpb.bytes_per_sector + FAT_ent_offset, 4);

    return (FAT_MASK & FAT_entry);
}

uint32_t FileSystem::getFirstDataSector(uint32_t cluster)
{
    return ((cluster - 2) * m_bpb.sectors_per_cluster + m_first_data_sector);
}

uint32_t FileSystem::getFATSector(uint32_t cluster)
{
    return (m_bpb.reserved_sector_count + ((cluster * 4) / m_bpb.bytes_per_sector));
}

uint32_t FileSystem::getFATEntOffset(uint32_t cluster)
{
    return ((cluster * 4) % m_bpb.bytes_per_sector);
}

uint32_t FileSystem::getFreeCluster()
{
    if (m_fsinfo.free_cluster_count == 0)
        throw std::exception();

    uint32_t total_cluster_count = ((m_bpb.total_sectors - m_first_data_sector) / m_bpb.sectors_per_cluster) + 2;
    for(uint32_t cluster = 2; cluster < total_cluster_count; cluster++)
        if (isFreeCluster(cluster))
            return cluster;
    return 0;
}

void FileSystem::setFATEntry(uint32_t cluster, uint32_t value)
{

    for (uint8_t i = 0; i < m_bpb.num_FATS; i++)
    {
        uint32_t FAT_sector = getFATSector(cluster);
        uint32_t FAT_ent_offset = getFATEntOffset(cluster);
        uint32_t FAT_entry_location =  (FAT_sector + (i * m_bpb.FATSz)) * m_bpb.bytes_per_sector + FAT_ent_offset;

        uint32_t FAT_entry = readFromFileSystem<uint32_t>(FAT_entry_location, 4);

        value &= FAT_MASK;
        FAT_entry &= ~FAT_MASK;
        FAT_entry |= value;

        writeToFileSystem<uint32_t>(FAT_entry, FAT_entry_location, 4);
    }
}

void FileSystem::setFreeClusterCount(uint32_t count)
{
    m_fsinfo.free_cluster_count = count;
    writeToFileSystem<uint32_t>(count, m_bpb.fsinfo * m_bpb.bytes_per_sector + 488, 4);
}

void FileSystem::updateFile(DirectoryEntry& file, uint32_t new_file_size, std::vector<uint32_t> cluster_chain)
{
    uint16_t high_cluster = cluster_chain[0] >> 16;
    uint16_t low_cluster = cluster_chain[0] & 0x0000FFFF;

    // Create new file
    DirectoryEntry new_file = file;
    new_file.attribute |= ATTR_ARCHIVE;
    new_file.cluster = formCluster(high_cluster, low_cluster);
    new_file.size = new_file_size;

    // Remove old file and add new file to open file table
    std::string mode = m_open_file_table[file];
    m_open_file_table.erase(file);
    m_open_file_table[new_file] = mode;

    // Update file in memory
    writeToFileSystem<uint16_t>(high_cluster, file.mem_location + 20, 2);
    writeToFileSystem<uint16_t>(low_cluster, file.mem_location + 26, 2);
    writeToFileSystem<uint32_t>(new_file.size, file.mem_location + 28, 4);
}

void FileSystem::createDirectoryEntry(std::string entry_name, uint32_t cluster, uint8_t entry_type)
{
    // Get memory location to create directory entry
    uint32_t mem_location;
    bool found_mem_location = false;

    if (entry_name != ROOT)
    {
        // Find next available slot in directory to store file
        std::vector<uint32_t> cluster_chain = getClusterChain(cluster);

        std::vector<uint32_t>::iterator iterator;
        for (iterator = cluster_chain.begin(); iterator != cluster_chain.end(); iterator++)
        {
            uint32_t sector = getFirstDataSector(*iterator) * m_bpb.bytes_per_sector;

            for (int i = m_bytes_per_cluster - DIR_ENTRY_SIZE; i >= 0; i -= DIR_ENTRY_SIZE)
            {
                DirectoryEntry dir_entry = readDirectoryEntry(sector + i);
                if (isFreeEntry(dir_entry))
                {
                    mem_location = sector + i;
                    found_mem_location = true;
                }
            }
        }

        if (!found_mem_location)
            mem_location = getFirstDataSector(allocateCluster(cluster_chain.back())) * m_bpb.bytes_per_sector;
    }

    // Create directory entry
    DirectoryEntry dir_entry;

    dir_entry.name = entry_name;
    dir_entry.attribute = (entry_type == DIRECTORY) ? ATTR_DIRECTORY : ATTR_ARCHIVE;
    dir_entry.cluster = allocateCluster();
    dir_entry.size = 0;
    dir_entry.mem_location = mem_location;
    setDirectoryEntryTime(dir_entry);

    // Write directory entry to file system
    writeDirectoryEntry(dir_entry);

    //create . and .. files
    if (entry_type == DIRECTORY && entry_name != ROOT)
    {
        DirectoryEntry dot_dir_entry;
        DirectoryEntry dot_dot_dir_entry;

        dot_dir_entry.name = ".";
        dot_dir_entry.attribute = ATTR_DIRECTORY;
        dot_dir_entry.write_time = dir_entry.write_time;
        dot_dir_entry.write_date = dir_entry.write_date;
        dot_dir_entry.cluster = dir_entry.cluster;
        dot_dir_entry.size = 0;
        dot_dir_entry.mem_location = getFirstDataSector(dir_entry.cluster) * m_bpb.bytes_per_sector;

        dot_dot_dir_entry.name = "..";
        dot_dot_dir_entry.attribute = ATTR_DIRECTORY;
        dot_dot_dir_entry.write_time = dir_entry.write_time;
        dot_dot_dir_entry.write_date = dir_entry.write_date;
        dot_dot_dir_entry.cluster = cluster;
        dot_dot_dir_entry.size = 0;
        dot_dot_dir_entry.mem_location = getFirstDataSector(dir_entry.cluster) * m_bpb.bytes_per_sector + 32;

        writeDirectoryEntry(dot_dir_entry);
        writeDirectoryEntry(dot_dot_dir_entry);
    }
}

void FileSystem::deleteDirectoryEntry(std::string entry_name, uint32_t cluster, DirectoryEntry& dir_entry)
{
    std::vector<uint32_t> cluster_chain = getClusterChain(dir_entry.cluster);

    std::vector<uint32_t>::reverse_iterator riterator;
    for (riterator = cluster_chain.rbegin(); riterator != cluster_chain.rend(); riterator++)
    {
        setFATEntry(*riterator, FREE_CLUSTER);
        setFreeClusterCount(m_fsinfo.free_cluster_count + 1);
    }

    dir_entry.name.clear();
    dir_entry.name.push_back((char)LAST_FREE_DIR_ENTRY);
    writeDirectoryEntry(dir_entry);
}

std::string FileSystem::convertToShortName(std::string name)
{
    if (name == ".")
        return ".          ";
    if (name == "..")
        return "..         ";

    size_t dot_sep_loc = name.find(".");
    std::string short_entry_name;

    if (dot_sep_loc != std::string::npos)
    {
        std::string main = name.substr(0, dot_sep_loc);
        std::string extension = name.substr(dot_sep_loc + 1, name.length() - 1);

        for (int i = 0; i < 8; i++)
        {
            if (i < main.length())
                short_entry_name.push_back(std::toupper(main[i]));
            else
                short_entry_name.push_back(' ');
        }

        for (int i = 0; i < 3; i++)
        {
            if (i < extension.length())
                short_entry_name.push_back(std::toupper(extension[i]));
            else
                short_entry_name.push_back(' ');
        }
    }
    else
    {
        for (int i = 0; i < 11; i++)
            if (i < name.length())
                short_entry_name.push_back(std::toupper(name[i]));
            else
                short_entry_name.push_back(' ');
    }

    return short_entry_name;
}

std::string FileSystem::convertFromShortName(std::string name)
{
    std::string short_name;
    bool add_dot = false;
    bool added_dot = false;

    for (size_t i = 0; i < name.length(); i++)
    {
        if (name[i] == SHORT_NAME_SPACE_PAD)
            add_dot = true;
        else
        {
            if (add_dot && !added_dot)
            {
                short_name.push_back('.');
                added_dot = true;
            }
            short_name.push_back(tolower(name[i]));
        }
    }
    return short_name;
}

void FileSystem::setDirectoryEntryTime(DirectoryEntry& dir_entry)
{
    time_t timer;
    struct tm* local_time;

    uint16_t write_time;
    uint16_t write_date;

    time(&timer);
    local_time = localtime(&timer);

    write_date = local_time->tm_mday;
    write_date |= ((local_time->tm_mon + 1) << 5);
    write_date |= ((local_time->tm_year - 80) << 0);

    uint8_t time = local_time->tm_sec / 2;
    write_time = (time > 29) ? 29 : time;
    write_time |= (local_time->tm_min << 5);
    write_time |= (local_time->tm_hour << 11);

    dir_entry.write_time = write_time;
    dir_entry.write_date = write_date;
}

DirectoryEntry FileSystem::readDirectoryEntry(uint32_t location)
{
    DirectoryEntry dir_entry;

    for (size_t j = 0; j < 11; j++)
    {
        char character = readFromFileSystem<char>(location + j, 1);
            if (std::isspace(character) || std::ispunct(character) || std::isalnum(character))
                dir_entry.name.push_back(character);
    }
    dir_entry.name = convertFromShortName(dir_entry.name);
    dir_entry.attribute = readFromFileSystem<uint8_t>(location + 11, 1);

    uint16_t high_cluster = readFromFileSystem<uint16_t>(location + 20, 2);
    uint16_t low_cluster = readFromFileSystem<uint16_t>(location + 26, 2);
    dir_entry.cluster = formCluster(high_cluster, low_cluster);

    dir_entry.size = readFromFileSystem<uint32_t>(location + 28, 4);
    dir_entry.mem_location = location;

    return dir_entry;
}

void FileSystem::writeDirectoryEntry(DirectoryEntry& dir_entry)
{
    std::string short_entry_name = convertToShortName(dir_entry.name);

    for(int i = 0; i < 11; i++)
        writeToFileSystem(short_entry_name[i], dir_entry.mem_location + i, 1);

    writeToFileSystem(dir_entry.attribute, dir_entry.mem_location + 11, 1);
    writeToFileSystem((uint8_t)0, dir_entry.mem_location + 13, 1);
    writeToFileSystem((uint16_t)0, dir_entry.mem_location + 14, 2);
    writeToFileSystem((uint16_t)0, dir_entry.mem_location + 16, 2);
    writeToFileSystem((uint16_t)0, dir_entry.mem_location + 18, 2);
    uint16_t high_cluster = (dir_entry.cluster & 0xFFFF0000) >> 4;
    writeToFileSystem(high_cluster, dir_entry.mem_location + 20, 2);
    writeToFileSystem(dir_entry.write_time, dir_entry.mem_location + 22, 2);
    writeToFileSystem(dir_entry.write_date, dir_entry.mem_location + 24, 2);
    uint16_t low_cluster = (dir_entry.cluster & 0x0000FFFF);
    writeToFileSystem(low_cluster, dir_entry.mem_location + 26, 2);
    writeToFileSystem(dir_entry.size, dir_entry.mem_location + 28, 4);
}

uint32_t FileSystem::formCluster(uint16_t high_cluster, uint16_t low_cluster)
{
    return (((uint32_t)low_cluster) | high_cluster << 16);
}

bool FileSystem::findDirectoryEntry(std::string dir_entry_name, uint32_t cluster, DirectoryEntry& dir_entry)
{
    if (dir_entry_name == ROOT || dir_entry_name == "." || dir_entry_name == "..")
    {
        dir_entry.name = ROOT;
        dir_entry.attribute |= ATTR_DIRECTORY;
        dir_entry.cluster = m_bpb.root_cluster;
        dir_entry.size = 0;
        return true;
    }

    if (dir_entry_name == m_current_directory_name)
    {
        dir_entry.name = m_current_directory_name;
        dir_entry.attribute |= ATTR_DIRECTORY;
        dir_entry.cluster = m_current_directory_cluster;
        return true;
    }

    std::list<DirectoryEntry> dir_entry_list = getDirectoryEntries(cluster);

    std::list<DirectoryEntry>::iterator iterator;
    for(iterator = dir_entry_list.begin(); iterator != dir_entry_list.end(); iterator++)
    {
        if (iterator->name == dir_entry_name)
        {
            dir_entry = *iterator;
            return true;
        }
    }
    return false;
}

bool FileSystem::directoryEntryExists(std::string dir_entry_name, uint32_t cluster)
{
    if (dir_entry_name == ROOT)
        return true;

    std::list<DirectoryEntry> dir_entry_list = getDirectoryEntries(cluster);

    std::list<DirectoryEntry>::iterator iterator;
    for(iterator = dir_entry_list.begin(); iterator != dir_entry_list.end(); iterator++)
        if (iterator->name == dir_entry_name)
            return true;
    return false;
}

bool FileSystem::isFile(const DirectoryEntry& dir_entry) const
{
    return ((dir_entry.attribute & ATTR_DIRECTORY) == 0x00);
}

bool FileSystem::isDirectory(const DirectoryEntry& dir_entry) const
{
    return ((dir_entry.attribute & ATTR_DIRECTORY) == ATTR_DIRECTORY);
}

bool FileSystem::isLongName(const DirectoryEntry& dir_entry) const
{
    return ((dir_entry.attribute & ATTR_LONG) == ATTR_LONG);
}

bool FileSystem::isFreeEntry(const DirectoryEntry& dir_entry) const
{
    return (dir_entry.name[0] == LAST_FREE_DIR_ENTRY || dir_entry.name[0] == FREE_DIR_ENTRY);
}

bool FileSystem::isFreeCluster(uint32_t cluster)
{
    return (getFATEntry(cluster) == FREE_CLUSTER);
}

bool FileSystem::isValidEntryName(std::string entry_name)
{
    return (entry_name.find("/") == std::string::npos || (entry_name.find( "/") == 0 && entry_name.length() == 1));
}

// *********************************************************
// *********************************************************
// *                  NON-CLASS-FUNCTIONS                  *
// *********************************************************
// *********************************************************

bool FAT_FS::operator<(const DirectoryEntry& left, const DirectoryEntry& right)
{
    return operator<(left.name, right.name);
}
