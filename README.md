# FileU

  This program is a user-space program that accesses and manipulates a
  FAT32 file system image.

  Supported commands:
      fsinfo
      open <file_name> <mode>
      close <file_name>
      create <file_name>
      read <file_name> <start_pos> <num_bytes>
      write <file_name> <start_pos> <quoted_data>
      rm <file_name>
      cd <dir_name>
      ls <dir_name>
      mkdir <dir_name>
      rmdir <dir_name>
      size <entry_name>
      undelete

  Developers:
    Javier Lores
    Alexander Windelberg

  Building instructions:
    To compile this program on linprog4 simply type 'make'
    at the terminal to build the program.

  Source Code:
    src/
      filesystem.h    : The header file for the filesystem.
      filesystem.cpp  : The definitions for the filesystem class.
      main.cpp        : The main program.
      Makefile        : The makefile to build the program.
