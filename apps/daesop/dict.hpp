#ifndef DICT_H
#define DICT_H

#include "resstr.hpp"
#include "utils.hpp"
#include "rentry.hpp"

// just a sensible value to check when working with dictionaries
#define MAX_NUMBER_OF_DICTIONARY_ITEMS 30000

#define MAX_IMPORT_TABLE_ITEMS 1000
#define MAX_EXPORT_TABLE_ITEMS 1000

#define NUMBER_OF_SPECIAL_TABLES 5

// categories when displaying an import dictionary
#define IMPORT_DISPLAY_NOTHING         0
#define IMPORT_DISPLAY_RUNTIME         1
#define IMPORT_DISPLAY_VARIABLE        2
#define IMPORT_DISPLAY_UNKNOWN         3

// categories when displaying an export dictionary
#define EXPORT_DISPLAY_NOTHING         10
#define EXPORT_DISPLAY_NAME            11
#define EXPORT_DISPLAY_PARENT          12
#define EXPORT_DISPLAY_HANDLER         13
#define EXPORT_DISPLAY_VARIABLE        14
#define EXPORT_DISPLAY_ARRAY           15
#define EXPORT_DISPLAY_UNKNOWN         16

// import entry types
#define IMPORT_ENTRY_UNDEFINED          0
#define IMPORT_ENTRY_RUNTIME_FUNCTION   1
#define IMPORT_ENTRY_BYTE               2
#define IMPORT_ENTRY_WORD               3
#define IMPORT_ENTRY_LONG               4
#define IMPORT_ENTRY_ARRAY_BYTE         5
#define IMPORT_ENTRY_ARRAY_WORD         6
#define IMPORT_ENTRY_ARRAY_LONG         7

// export entry types
#define EXPORT_ENTRY_UNDEFINED          10
#define EXPORT_ENTRY_OBJECT_NAME        11
#define EXPORT_ENTRY_PARENT_NUMBER      12
#define EXPORT_ENTRY_MESSAGE_HANDLER    13
#define EXPORT_ENTRY_BYTE               14
#define EXPORT_ENTRY_WORD               15
#define EXPORT_ENTRY_LONG               16
#define EXPORT_ENTRY_ARRAY_BYTE         17
#define EXPORT_ENTRY_ARRAY_WORD         18
#define EXPORT_ENTRY_ARRAY_LONG         19

// resource types
#define RESOURCE_TYPE_UNKNOWN   0
#define RESOURCE_TYPE_SPECIAL   1
#define RESOURCE_TYPE_STRING    2
#define RESOURCE_TYPE_CODE      3
#define RESOURCE_TYPE_IMPORT    4
#define RESOURCE_TYPE_EXPORT    5
#define RESOURCE_TYPE_BITMAP    6
#define RESOURCE_TYPE_PALETTE   7
#define RESOURCE_TYPE_SOUND     8
#define RESOURCE_TYPE_MUSIC     9
#define RESOURCE_TYPE_FONT      10
#define RESOURCE_TYPE_MAP       11

#define PALETTE_PREFIX_UPPERCASE "PALETTE:"
#define MAP_PREFIX_UPPERCASE     "MAP:"


#define IMPORT_EXTENSION  ".IMPT"
#define EXPORT_EXTENSION ".EXPT"

#define BITMAP1_FILE_EXTENSION  ".BMP" // EOB 3
#define BITMAP2_FILE_EXTENSION  ".SHP" // prepared for future
#define SOUND_FILE_EXTENSION    ".SND"
#define MUSIC_FILE_EXTENSION    ".XMI"
#define FONT_FILE_EXTENSION     ".FNT"

#define MAX_LENGTH_OF_TESTED_STRING_RESOURCE 2000

struct INTERNAL_DICTIONARY_ENTRY
{
    char *first;
    char *second;
};

typedef struct INTERNAL_DICTIONARY_ENTRY *DICTENTRYPOINTER;

struct INTERNAL_IMPORT_ENTRY
{
    int importType;
    // name
    char *firstOriginal;
    // the second entry format depends on the "importType" (it is decoded into the settings below)    
    char *secondOriginal;
    // used for runtime functions entries
    int runtimeFunctionNumber;
    // used for imported variable/array entries
    int importedVariableNumber;
    int originalResourceNumber;
    char *originalResourceName;
};

typedef struct INTERNAL_IMPORT_ENTRY *IMPORTENTRYPOINTER;

struct INTERNAL_EXPORT_ENTRY
{
    int exportType;
    // name
    char *firstOriginal;
    // the second entry format depends on the "exportType" (it is decoded into the settings below)
    char *secondOriginal;
    // used for message handlers entries
    int messageHandlerStart;
    char *messageHandlerName;
    // used for an object name entry
    char *objectName;
    // used for a parent object name entry
    int parentResourceNumber;
    char *parentResourceName;
    // used for exported variables entries
    int exportedVariablePosition;
    // used for arrays
    int exportedArrayPosition;
    int exportedArraySizeInElements;
};

typedef struct INTERNAL_EXPORT_ENTRY *EXPORTENTRYPOINTER;

struct INTERNAL_RESOURCE_INFO
{
    int resourceType;
    int number;
    char *name;
    char *infoFromResource1;
    char *infoFromResource2;
    char *stringValue;
};

typedef struct INTERNAL_RESOURCE_INFO *RESINFOPOINTER;


// auxiliary
int getResourceNumberFromNumberString(char *aResource);
int getResourceNumberFromNameString(char *aResource, DICTENTRYPOINTER* aResourceNameArray);
// common
DICTENTRYPOINTER *readTheDictionary(int aResource, int aMaxDictionaryEntries, FILE *aResFile,
    DIRPOINTER *aDirectoryPointers, int *aResourceSize);
int getNumberOfItems(DICTENTRYPOINTER *aArray);
int sortDictionaryAccordingToSecondNumber(DICTENTRYPOINTER *aArray);
int sortDictionaryAccordingToSecondString(DICTENTRYPOINTER *aArray);
int sortDictionaryAccordingToFirstString(DICTENTRYPOINTER *aArray);
void displayDictionary(char *aText, char *aHeader, char *aFormat, FILE *aOutputFile,
    DICTENTRYPOINTER *aDictionary, int aSecondIsDecimalNumber);
// Special tables
char *getResourceName(char *aResult, int aResourceNumber);
DICTENTRYPOINTER *getResourceNameArray(FILE *aResFile, DIRPOINTER *aDirectoryPointers);
DICTENTRYPOINTER *getRawImportArray(int aImportResourceNumber, FILE *aResFile,
    DIRPOINTER *aDirectoryPointers, int *aImportResourceSize);
DICTENTRYPOINTER *getRawExportArray(int aExportResourceNumber, FILE *aResFile,
    DIRPOINTER *aDirectoryPointers, int *aExportResourceSize);
IMPORTENTRYPOINTER *getFullImportArray(DICTENTRYPOINTER *aRawImportArray, FILE *aResFile, DIRPOINTER *aDirectoryPointers);
EXPORTENTRYPOINTER *getFullExportArray(DICTENTRYPOINTER *aRawExportArray, FILE *aResFile, DIRPOINTER *aDirectoryPointers);
void displayImportDictionary(char *aImportResourceName, int aImportResourceNumber, FILE *aOutputFile,
    IMPORTENTRYPOINTER *aFullImportResourceDictionary, int aSecondIsDecimalNumber);
void displayExportDictionary(char *aExportResourceName, int aExportResourceNumber, FILE *aOutputFile,
    EXPORTENTRYPOINTER *aFullExportResourceDictionary, int aSecondIsDecimalNumber);
DICTENTRYPOINTER *getSpecialArray(int aResourceNumber, FILE *aResFile, DIRPOINTER *aDirectoryPointers);
char *getMessageName(char *aResult, char *aExportTableString, FILE *aResFile, DIRPOINTER *aDirectoryPointers);
RESINFOPOINTER *getResourcesInformationTable(FILE *aResFile, DIRPOINTER *aDirectoryPointers, int aLookForStringResources);
char* getSecondEntryForTheFirstEntry(DICTENTRYPOINTER *aResourceTable, char *aResourceName);            
int getResourceType(FILE *aResFile, DIRPOINTER *aDirectoryPointers, DICTENTRYPOINTER *aResourceNameArray,
                int aResourceNumber, char *aResourceName, char *aInfoString1, char *aInfoString2,
                int aLookForStringResources, char **aFoundStringPointer);
void displayResourcesInfoEntries(FILE *aOutputFile, RESINFOPOINTER *aResourcesInfoTable);
void getResourceTypeString(char* aResult,  int aType);
void displayResourcesTypeWarning(FILE *aOutputFile);
void displayResourcesTypeSummary(RESINFOPOINTER *aResourcesInfoTable, FILE *aOutputFile);
#endif
