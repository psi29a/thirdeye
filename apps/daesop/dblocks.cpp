#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dblocks.hpp"

/*
 Read directory blocks
 */
int readDirectoryBlocks(FILE *aResFile, DIRPOINTER * loDirectoryPointers) {
	int loDirBlockSize;
	int loReadSize;
	int loDirPos;
	int loDirCounter;
	loDirBlockSize = sizeof(struct RESDirectoryBlock);
	loDirCounter = -1;
	loDirPos = ftell(aResFile);
	while (loDirPos != 0) {
		loDirCounter++;
		if (loDirCounter > MAX_DIRECTORIES) {
			printf("Too much directory blocks, only %d of them were read!",
					MAX_DIRECTORIES);
			return (true);
		}
		printf("Reading the directory block %d from the position %ld\n",
				loDirCounter, ftell(aResFile));
		loDirectoryPointers[loDirCounter] = (DIRPOINTER) malloc(loDirBlockSize);
		loReadSize = fread(loDirectoryPointers[loDirCounter], 1, loDirBlockSize,
				aResFile);
		if (loReadSize != loDirBlockSize) {
			free(loDirectoryPointers[loDirCounter]);
			loDirectoryPointers[loDirCounter] = NULL;
			printf("Reading of the directory block failed!\n");
			return (false);
		}
		// set the file position to the next block

		loDirPos = (loDirectoryPointers[loDirCounter])->next_directory_block;
		if (fseek(aResFile, loDirPos, SEEK_SET) != 0) {
			printf(
					"Failure to set the file position %d when reading a directory block!\n",
					loDirPos);
			return (false);
		}
	}
	printf("The total number of directory blocks read: %d\n", loDirCounter + 1);
	return (true);
}

/*
 Gets number of directory blocks (including empty ones)
 */
int getNumberOfDirectoryBlocks(DIRPOINTER *aDirectoryPointers) {
	int loNumberOfDirBlocks;
	loNumberOfDirBlocks = 0;
	while (loNumberOfDirBlocks < MAX_DIRECTORIES
			&& aDirectoryPointers[loNumberOfDirBlocks] != NULL) {
		loNumberOfDirBlocks++;
	}
	return (loNumberOfDirBlocks);
}

/*
 Opens RES file, sets file pointer on the first directory block
 */
FILE* openAESOPResourceAndSetToFirstDirectoryBlock(char *aResName,
		const char *aMode, struct RESGlobalHeader *aHeaderPointer) {
	FILE *loResFile;
	int loHeaderSize;
	int loReadSize;
	unsigned int loLength;

	loResFile = fopen(aResName, aMode);
	if (loResFile == NULL) {
		printf("The file could not be opened: %s!\n", aResName);
		return (NULL);
	}
	printf("The resource file %s was opened in the mode \"%s\".\n", aResName,
			aMode);

	// set the pointer to the beginning of the file
	fseek(loResFile, 0, SEEK_SET);

	loHeaderSize = sizeof(struct RESGlobalHeader);
	loReadSize = fread(aHeaderPointer, 1, loHeaderSize, loResFile);
	if (loHeaderSize != loReadSize) {
		printf("The file header could not be read!\n");
		fclose(loResFile);
		return (NULL);
	}

	if (strcmp((char *) (aHeaderPointer->signature), AESOP_ID) != 0) {
		printf("The resource file does not start with the signature %s!\n",
				AESOP_ID);
		fclose(loResFile);
		return (NULL);
	}

	fseek(loResFile, 0, SEEK_END);
	loLength = ftell(loResFile);
	if (loLength != aHeaderPointer->file_size) {
		printf(
				"The real length of %u bytes does not agree with the length %u bytes in the header!\n",
				loLength, aHeaderPointer->file_size);
		fclose(loResFile);
		return (NULL);
	}
	// set the pointer on the first directory block
	fseek(loResFile, aHeaderPointer->first_directory_block, SEEK_SET);
	return (loResFile);

}
