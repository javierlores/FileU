#include <iostream>
#include <string>
#include <vector>
#include "filesystem.h"

using namespace std;

std::vector<std::string> tokenize(std::string input);

int main(int argc, char **argv)
{
    // Check for proper number of arguments
    if (argc != 2)
    {
        std::cout << "Usage: fmod <fat image>"  << endl;
        exit(EXIT_FAILURE);
    }

    // Declare variables
    std::string file_system_image = std::string(argv[1]);
    FAT_FS::FileSystem file_system(file_system_image);

    if (file_system.hasError())
    {
        cout << "Error setting up file system." << endl;
        return(EXIT_FAILURE);
    }

    for(;;)
    {
        std::string input;

        // Print prompt
        std::cout << "[" << file_system_image << "]> ";
        std::getline(std::cin, input);

        std::vector<std::string> tokenized_input = tokenize(input);

        // Execute command
        if (tokenized_input[0] == "exit")
        {
            if (tokenized_input.size() == 1)
                return(EXIT_SUCCESS);
            else
                cout << "Usage: exit" << endl;
        }
        else if (tokenized_input[0] == "fsinfo")
        {
            if (tokenized_input.size() == 1)
                file_system.fsinfo();
            else
                cout << "Usage: fsinfo" << endl;
        }
        else if (tokenized_input[0] == "ls")
        {
            if (tokenized_input.size() == 1)
                file_system.ls(file_system.getCurrentDirectoryName());
            else if (tokenized_input.size() == 2)
                file_system.ls(tokenized_input[1]);
            else
                cout << "Usage: ls <dir_name>" << endl;
        }
        else if (tokenized_input[0] == "cd")
        {
            if (tokenized_input.size() == 2)
                file_system.cd(tokenized_input[1]);
            else
                cout << "Usage: cd <dir_name>" << endl;
        }
        else if (tokenized_input[0] == "size")
        {
            if (tokenized_input.size() == 2)
                file_system.size(tokenized_input[1]);
            else
                cout << "Usage: size <file_name>" << endl;
        }
        else if (tokenized_input[0] == "open")
        {
            if (tokenized_input.size() == 3)
                file_system.open(tokenized_input[1], tokenized_input[2]);
            else
                cout << "Usage: open <file_name> <mode>" << endl;
        }
        else if (tokenized_input[0] == "close")
        {
            if (tokenized_input.size() == 2)
                file_system.close(tokenized_input[1]);
            else
                cout << "Usage: close <file_name>" << endl;
        }
        else if (tokenized_input[0] == "read")
        {
            if (tokenized_input.size() == 4)
                file_system.read(tokenized_input[1],
                                 std::stoi(tokenized_input[2]),
                                 std::stoi(tokenized_input[3]));
            else
                cout << "Usage: read <file_name> <start_pos> <num_bytes>" << endl;
        }
        else if (tokenized_input[0] == "write")
        {
            if (tokenized_input.size() == 4)
            {
                if(tokenized_input[3][0] != '\"' || tokenized_input[3][tokenized_input[3].length() - 1] != '\"')
                    cout << "Error: data must be quoted." << endl;
                else
                    file_system.write(tokenized_input[1],
                                      std::stoi(tokenized_input[2]),
                                      tokenized_input[3].substr(1, tokenized_input[3].length() - 2));
            }
            else
                cout << "Usage: write <file_name> <start_pos> <quoted_data>" << endl;
        }
        else if (tokenized_input[0] == "create")
        {
            if (tokenized_input.size() == 2)
                file_system.create(tokenized_input[1]);
            else
                cout << "Usage: create <file_name>" << endl;
        }
        else if (tokenized_input[0] == "rm")
        {
            if (tokenized_input.size() == 2)
                file_system.rm(tokenized_input[1]);
            else
                cout << "Usage: rm <file_name>" << endl;
        }
        else if (tokenized_input[0] == "mkdir")
        {
            if (tokenized_input.size() == 2)
                file_system.mkdir(tokenized_input[1]);
            else
                cout << "Usage: mkdir <directory_name>" << endl;
        }
        else if (tokenized_input[0] == "rmdir")
        {
            if (tokenized_input.size() == 2)
                file_system.rmdir(tokenized_input[1]);
            else
                cout << "Usage: rmdir <directory_name>" << endl;
        }
        else if (tokenized_input[0] == "undelete")
        {
            file_system.undelete();
        }
        else
            std::cout << "Invalid commmand" << endl;
    }

    return 0;
}

std::vector<std::string> tokenize(std::string input)
{
    std::vector<std::string> tokenized_input;
    std::string token;
    bool found_quote = false;

    std::string::iterator iterator;
    for (iterator = input.begin(); iterator != input.end(); iterator++)
    {
        if (*iterator == '\"')
        {
            found_quote = true;
            token += *iterator;
        }
        else if (std::isspace(*iterator) && !found_quote)
        {
            if (!token.empty())
                tokenized_input.push_back(token);
            token.clear();
        }
        else
            token += *iterator;
    }

    if (!token.empty())
        tokenized_input.push_back(token);

    return tokenized_input;
}
