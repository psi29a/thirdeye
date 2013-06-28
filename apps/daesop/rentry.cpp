#include <malloc.h>

#include "dblocks.hpp"

#include "rentry.hpp"

/*
Get the file index of the specified resource (including the resource header)
*/
long getResourceEntryIndex(int aNumber, DIRPOINTER * aDirectoryPointers)
{
    int loDirBlockNumber = aNumber / DIRECTORY_BLOCK_ITEMS;
    int loNumberInDirBlock = aNumber % DIRECTORY_BLOCK_ITEMS;
    unsigned char loDirItemAttr;
    unsigned int loDirItemIndex;
    
    if (loDirBlockNumber >= MAX_DIRECTORIES || aDirectoryPointers[loDirBlockNumber] == NULL)
    {
        printf("There is not enough directory blocks for the resource number %d!\n", aNumber);
        return -1;
    }    
    loDirItemAttr = aDirectoryPointers[loDirBlockNumber]->data_attributes[loNumberInDirBlock];
    loDirItemIndex = aDirectoryPointers[loDirBlockNumber]->entry_header_index[loNumberInDirBlock];

    if (loDirItemAttr == 1 || loDirItemIndex == 0)
    {
        printf("The resource number %d is empty!\n", aNumber);
        return -1;
    }
    return loDirItemIndex;
}

/*
Reads the resource entry header
*/
struct RESEntryHeader *getResourceEntryHeader(int aNumber, FILE *aResFile, DIRPOINTER * aDirectoryPointers)
{
    long loResourceEntryIndex;
    unsigned int loEntryHeaderSize;
    unsigned int loReadSize;
    struct RESEntryHeader *loEntryHeader = NULL;
    
    loResourceEntryIndex = getResourceEntryIndex(aNumber, aDirectoryPointers);
    if (loResourceEntryIndex == -1)
    {
        // error
        return NULL;
    }
    if (fseek(aResFile, loResourceEntryIndex, SEEK_SET) != 0)
    {
        // error
        printf("Failure to set the file position %ld when getting a resource entry header!\n", loResourceEntryIndex);
        return NULL;
    }

    loEntryHeaderSize = sizeof(struct RESEntryHeader);
    loEntryHeader = (struct RESEntryHeader *)malloc(loEntryHeaderSize);
    loReadSize = fread(loEntryHeader, 1, loEntryHeaderSize, aResFile);
    if (loEntryHeaderSize != loReadSize)
    {
        free(loEntryHeader);
        printf("The resource entry header could not be read!\n");
        return NULL;
    }
    return loEntryHeader;    
}

/*
Gets maximum number of resource entries (including empty ones)
*/
int getMaxNumberOfResourceEntries(DIRPOINTER *aDirectoryPointers)
{  
    return getNumberOfDirectoryBlocks(aDirectoryPointers) * DIRECTORY_BLOCK_ITEMS;
}

/*
Read the resource binary to memory
*/
unsigned char *readResourceBinary(int aResourceNumber, FILE *aResFile, DIRPOINTER *aDirectoryPointers, int *aResourceLength)
{
    struct RESEntryHeader *loResEntryHeader;
    unsigned int loDataSize;
    long loResourceEntryIndex;
    unsigned char *loBuffer;
    unsigned int loReadSize;
    
    // start of the resource
    loResourceEntryIndex = getResourceEntryIndex(aResourceNumber, aDirectoryPointers);
    // length
    loResEntryHeader = getResourceEntryHeader(aResourceNumber, aResFile, aDirectoryPointers);
    if (loResourceEntryIndex == -1 || loResEntryHeader == NULL)
    {
        printf("Unable to access the resource: %d\n", aResourceNumber);
        return NULL;
    }
    loResourceEntryIndex += sizeof(struct RESEntryHeader); // behind the header
    loDataSize = loResEntryHeader->data_size;

    // now read the resource
    printf("Reading %ld bytes from the position %ld...\n", (long)loDataSize, (long)loResourceEntryIndex);    

    // now read the resource from the position loResourceEntryIndex with the length loDataSize
    loBuffer = (unsigned char *)malloc(loDataSize);
    if (loBuffer == NULL)
    {
        char loError[256];
        sprintf(loError, "Unable to allocate %d bytes while reading the resource %d into memory!", loDataSize, aResourceNumber);
        printf("%s\n", loError);
        free(loResEntryHeader);                
        return NULL;
    }
    if (fseek(aResFile, loResourceEntryIndex, SEEK_SET) != 0)
    {
        printf("Failure to set the file position %ld when reading a resource!\n", loResourceEntryIndex);
        free(loBuffer);
        free(loResEntryHeader);
        return NULL;
    }        
    loReadSize = fread(loBuffer, 1, loDataSize, aResFile);
    if (loReadSize != loDataSize)
    {
        printf("The resource could not be read!\n");
        free(loBuffer);
        free(loResEntryHeader);
        return NULL;        
    }
    free(loResEntryHeader); 
    *aResourceLength = loDataSize;
    return loBuffer;
}

