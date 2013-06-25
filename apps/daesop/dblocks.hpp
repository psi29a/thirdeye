#ifndef DBLOCKS_H
#define DBLOCKS_H

#include <stdio.h>

#include "resstr.hpp"

int readDirectoryBlocks(FILE *aResFile, DIRPOINTER * loDirectoryPointers);
int getNumberOfDirectoryBlocks(DIRPOINTER *aDirectoryPointers);
FILE* openAESOPResourceAndSetToFirstDirectoryBlock(char *aResName, const char *aMode, struct RESGlobalHeader *aHeaderPointer);

#endif

