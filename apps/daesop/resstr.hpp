#ifndef RESSTR_H
#define RESSTR_H

#define AESOP_ID "AESOP/16 V1.00"

#define DIRECTORY_BLOCK_ITEMS 128
#define MAX_DIRECTORIES 200

struct RESGlobalHeader
{
  signed char  signature[16];          // must be == "AESOP/16 V1.00\0" + 1 garbage character
  unsigned int file_size;              // the total size of the .RES file
  unsigned int lost_space;         
  unsigned int first_directory_block;  // offset of first directory block within the file
  unsigned int create_time;            // DOS format (32 bit)
  unsigned int modify_time;            // DOS format (32 bit)
};

struct RESDirectoryBlock
{
  unsigned int next_directory_block;  // the offset of the next RESDirectoryBlock struct in the file
  unsigned char data_attributes[DIRECTORY_BLOCK_ITEMS];   // =1 if the corresponding entry is unused (free)
  unsigned int entry_header_index[DIRECTORY_BLOCK_ITEMS]; // exactly 128 file entries follow
};

struct RESEntryHeader
{
  unsigned int storage_time;
  unsigned int data_attributes;
  unsigned int data_size;
};

typedef struct RESDirectoryBlock *DIRPOINTER;

#endif
