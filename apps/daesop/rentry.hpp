#ifndef RENTRY_H
#define RENTRY_H

#include <stdio.h>

#include "tdefs.hpp"
#include "resstr.hpp"

struct RESEntryHeader *getResourceEntryHeader(int aNumber, FILE *aResFile, DIRPOINTER * aDirectoryPointers);

long 	getResourceEntryIndex(int i, DIRPOINTER * loDirectoryPointers);
int 	getMaxNumberOfResourceEntries(DIRPOINTER *aDirectoryPointers);
char 	*readResourceBinary(int aResourceNumber, FILE *aResFile, DIRPOINTER *aDirectoryPointers, int *aResourceLength);

#endif

