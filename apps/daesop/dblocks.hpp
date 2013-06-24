///////////////////////////////////////////////////////////////////////////////
//
// DAESOP
// using code from AESOP engine and ReWiki website
// (c) Mirek Luza
// public domain software
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DBLOCKS_H
#define DBLOCKS_H

#include <stdio.h>

#include "tdefs.hpp"
#include "resstr.hpp"

int readDirectoryBlocks(FILE *aResFile, DIRPOINTER * loDirectoryPointers);
int getNumberOfDirectoryBlocks(DIRPOINTER *aDirectoryPointers);
FILE* openAESOPResourceAndSetToFirstDirectoryBlock(char *aResName, const char *aMode, struct RESGlobalHeader *aHeaderPointer);

#endif

