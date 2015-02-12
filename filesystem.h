#ifndef FILESYSTEM_H
#define FILESYSTEM_H
#include <iostream>
#include <string>
#include <cstdint>
#include <list>
#include <vector>
#include <map>

using namespace std;

namespace FAT_FS
{
    const std::string READ = "r";
    const std::string WRITE = "w";
    const std::string READ_WRITE = "rw";
    const std::string ROOT = "/";

    const size_t INVALID_CHAR_LIST_SIZE = 35;
    const uint8_t INVALID_CHAR_LIST[INVALID_CHAR_LIST_SIZE] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x06,
                                                                0x07, 0x08, 0x09, 0x10, 0x11, 0x12,
                                                                0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                                                                0x19, 0x20, 0x22, 0x2A, 0x2B, 0x2C,
                                                                0x2F, 0x3A, 0x3B, 0x3C, 0x3d, 0x3E,
                                                                0x3F, 0x5B, 0x5C, 0x5D, 0x7C };
    const uint8_t SPECIAL_INVALID_CHAR = 0x05;

    const uint8_t DIRECTORY = 0;
    const uint8_t FILE = 1;

    const uint8_t FREE_DIR_ENTRY = 0xE5;
    const uint8_t LAST_FREE_DIR_ENTRY = 0x00;
    const uint8_t SHORT_NAME_SPACE_PAD = 0x20;

    const uint8_t ATTR_READ_ONLY = 0x01;
    const uint8_t ATTR_HIDDEN = 0x02;
    const uint8_t ATTR_SYSTEM = 0x04;
    const uint8_t ATTR_VOLUME_ID = 0x08;
    const uint8_t ATTR_DIRECTORY = 0x10;
    const uint8_t ATTR_ARCHIVE = 0x20;
    const uint8_t ATTR_LONG = ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID;

    const uint32_t FAT_MASK = 0x0FFFFFFF;
    const uint32_t FREE_CLUSTER = 0x00000000;
    const uint32_t EOC = 0x0FFFFFF8;
    const uint32_t DIR_ENTRY_SIZE = 0x20;

    struct BIOSParameterBlock
    {
        uint8_t sectors_per_cluster;
        uint8_t num_FATS;
        uint16_t bytes_per_sector;
        uint16_t reserved_sector_count;
        uint16_t fsinfo;
        uint32_t total_sectors;
        uint32_t FATSz;
        uint32_t root_cluster;
    };

    struct FSInfo
    {
        uint32_t free_cluster_count;
        uint32_t first_free_cluster;
    };

    struct DirectoryEntry
    {
        std::string name;
        uint8_t attribute;
        uint16_t write_time;
        uint16_t write_date;
        uint32_t cluster;
        uint32_t size;
        uint32_t mem_location;
    };

    bool operator<(const DirectoryEntry& left, const DirectoryEntry& right);

    class FileSystem
    {
        public:
            FileSystem(std::string file_system_image);
            ~FileSystem();

            std::string getCurrentDirectoryName() { return m_current_directory_name; };
            bool hasError() { return m_error; }

            void fsinfo();
            void open(std::string file_name, std::string mode);
            void close(std::string file_name);
            void create(std::string file_name);
            void read(std::string file_name, uint32_t start_pos, uint32_t num_bytes);
            void write(std::string file_name, uint32_t start_pos, std::string quoted_data);
            void rm(std::string file_name);
            void cd(std::string dir_name);
            void ls(std::string dir_name);
            void mkdir(std::string dir_name);
            void rmdir(std::string dir_name);
            void size(std::string entry_name);
            void undelete();
        private:
            template<typename T>
            T readFromFileSystem(size_t offset, size_t bytes);
            template<typename T>
            void writeToFileSystem(T data, size_t offset, size_t bytes);
            std::list<DirectoryEntry> getDirectoryEntries(uint32_t cluster);
            std::vector<uint32_t> getClusterChain(uint32_t cluster);
            uint32_t resizeClusterChain(uint32_t size, std::vector<uint32_t>& cluster_chain);
            uint32_t allocateCluster(uint32_t cluster = 0);
            uint32_t getDirectoryCluster(std::string dir_name);
            uint32_t getFirstDataSector(uint32_t cluster);
            uint32_t getFATEntry(uint32_t cluster);
            uint32_t getFATSector(uint32_t cluster);
            uint32_t getFATEntOffset(uint32_t cluster);
            uint32_t getFreeCluster();
            void setFATEntry(uint32_t cluster, uint32_t value);
            void setFreeClusterCount(uint32_t count);
            void updateFile(DirectoryEntry& dir_entry, uint32_t increased_file_size, std::vector<uint32_t> cluster_chain);
            void createDirectoryEntry(std::string entry_name, uint32_t cluster, uint8_t entry_type);
            void deleteDirectoryEntry(std::string entry_name, uint32_t cluster, DirectoryEntry& dir_entry);

            std::string convertToShortName(std::string name);
            std::string convertFromShortName(std::string name);
            DirectoryEntry readDirectoryEntry(uint32_t sector);
            void writeDirectoryEntry(DirectoryEntry& dir_entry);
            void setDirectoryEntryTime(DirectoryEntry& dir_entry);
            uint32_t formCluster(uint16_t high_cluster, uint16_t low_cluster);
            bool findDirectoryEntry(std::string dir_entry_name, uint32_t cluster, DirectoryEntry& dir_entry);
            bool directoryEntryExists(std::string dir_entry_name, uint32_t cluster);
            bool isFile(const DirectoryEntry& dir_entry) const;
            bool isDirectory(const DirectoryEntry& dir_entry) const;
            bool isLongName(const DirectoryEntry& dir_entry) const;
            bool isFreeEntry(const DirectoryEntry& dir_entry) const;
            bool isFreeCluster(uint32_t cluster);
            bool isValidEntryName(std::string entry_name);

            uint8_t* m_file_system_data;
            size_t m_file_system_size;
            int m_file_descriptor;
            BIOSParameterBlock m_bpb;
            FSInfo m_fsinfo;
            std::map<DirectoryEntry, std::string> m_open_file_table;

            bool m_error;
            uint32_t m_bytes_per_cluster;
            uint32_t m_first_data_sector;
            uint32_t m_current_directory_cluster;
            std::string m_current_directory_name;
    };
}

#endif
