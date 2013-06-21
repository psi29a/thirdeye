#ifndef RESSTR_H
#define RESSTR_H

#include "tdefs.hpp"

#define AESOP_ID "AESOP/16 V1.00"

#define DIRECTORY_BLOCK_ITEMS 128
#define MAX_DIRECTORIES 200

struct RESGlobalHeader
{
  BYTE  signature[16];          // must be == "AESOP/16 V1.00\0" + 1 garbage character
  ULONG file_size;              // the total size of the .RES file
  ULONG lost_space;         
  ULONG first_directory_block;  // offset of first directory block within the file
  ULONG create_time;            // DOS format (32 bit)
  ULONG modify_time;            // DOS format (32 bit)
};

struct RESDirectoryBlock
{
  ULONG next_directory_block;  // the offset of the next RESDirectoryBlock struct in the file
  UBYTE data_attributes[DIRECTORY_BLOCK_ITEMS];   // =1 if the corresponding entry is unused (free)
  ULONG entry_header_index[DIRECTORY_BLOCK_ITEMS]; // exactly 128 file entries follow
};

struct RESEntryHeader
{
  ULONG storage_time;
  ULONG data_attributes;
  ULONG data_size;
};

typedef struct RESDirectoryBlock *DIRPOINTER;

#endif
