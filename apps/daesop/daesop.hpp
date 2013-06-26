#ifndef DAESOP_H
#define DAESOP_H

#include "config.hpp"
#include "dblocks.hpp"
#include "dict.hpp"
#include "utils.hpp"
#include "rentry.hpp"
#include "dasm.hpp"
#include "convert.hpp"

#define NOTHING                             0
#define GET_INFORMATION                     1
#define GET_RESOURCES_INFORMATION           2
#define GET_OFFSET_INFORMATION              3
#define GET_RESOURCE_INFORMATION_BY_NAME    4
#define GET_RESOURCE_INFORMATION_BY_NUMBER  5
#define GET_RESOURCE_BY_NAME                6
#define GET_RESOURCE_WITH_HEADER_BY_NAME    7
#define GET_RESOURCE_BY_NUMBER              8
#define GET_RESOURCE_WITH_HEADER_BY_NUMBER  9
#define TEST_OLD_BITMAPS                    10
#define REPLACE_BY_RESOURCE                 11
#define REPLACE_BY_RESOURCE_WITH_HEADER     12
#define CREATE_TBL_FILE                     13
#define CONVERT_OLD_BITMAPS                 14
#define CONVERT_OLD_BITMAPS_IGNORE_ERRORS   15
#define CONVERT_OLD_FONTS                   16
#define CONVERT_OLD_FONTS_IGNORE_ERRORS     17
#define PATCH_EOB3_MENU                     18
#define CONVERT_EOB3_TO_AESOP32             19

#define DUMP_LINE_SIZE 16

// sequence of 0 at the end of a TBL file
#define TBL_END_MARKER_LENGTH 36

// syntax
void syntaxInformation(void);

// Commands
int getInformation(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aOutputFilename);
int getResource(FILE *aResFile, DIRPOINTER *aDirectoryPointers, int aFunction, char *aResource, char *aOutputFilename);
void displayCodeResourceInformation(FILE *aResFile, DIRPOINTER *aDirectoryPointers, FILE *aOutputFile, int aResourceType,char *aResourceName, DICTENTRYPOINTER* aResourceNameArray);
void displaySpecialAESOPResource(FILE *aResFile, DIRPOINTER *aDirectoryPointers, FILE *aOutputFile, int aResourceNumber);
void displayHexadecimalDump(int aResourceNumber, FILE *aResFile, DIRPOINTER *aDirectoryPointers, FILE *aOutputFile);
void displayHexadecimalDumpOfMemoryBuffer(unsigned char *aBuffer, int aLength, FILE *aOutputFile);
int getResourceInformation(FILE *aResFile, DIRPOINTER *aDirectoryPointers, int aFunction, char *aResource, char *aOutputFilename);
void displayCodeResource(int aCodeResourceNumber, char *aCodeResourceName, IMPORTENTRYPOINTER *aFullImportResourceDictionary,
            int aImportResourceSize, EXPORTENTRYPOINTER *aFullExportResourceDictionary, int aExportResourceSize,
            FILE *aResFile, DIRPOINTER *aDirectoryPointers, FILE *aOutputFile);
int getResourcesInformation(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aOutputFilename);
int testOldBitmaps(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aOutputFilename);
int replaceResourceByResourceFromFile(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aResourceNumberString,
    char *aAddedResourceFileName, char *aNewFileName, int aNewResourceHasHeader);
int getOffsetInformation(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aOffsetString,
    char *aOutputFileName);
int createTblFile(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aTblFileName);
int convertOldBitmaps(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aNewFileName, int aIgnoreErrors);
int convertOldFonts(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aNewFileName, int aIgnoreErrors);
int patchEOB3Menu(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aNewFileName);
int convertEOB3toAESOP32(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aNewFileName);
#endif

