#ifndef DVAR_H
#define DVAR_H

#include "dict.hpp"

#define MAX_STATIC_VARIABLES 1000
#define MAX_CONSTANT_TABLES 1000

// number of references to local variables in the whole code resource (not variables!)
#define MAX_LOCAL_VARIABLE_REFERENCES 20000

#define STATIC_VARIABLE_PREFIX "staticVar"
#define STATIC_ARRAY_PREFIX    "staticArray"
#define CONSTANT_TABLE_PREFIX  "table"
#define LOCAL_VARIABLE_PREFIX      "locVar_"
#define LOCAL_ARRAY_PREFIX         "locArray_"
#define PARAMETER_VARIABLE_PREFIX  "paramVar_"
#define PARAMETER_ARRAY_PREFIX     "paramArray_"

// max number of function parameters or local variables
#define MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES 10000

struct STATIC_VARIABLE_ENTRY
{
    int isPublic;
    int isArray;
    char *name;
    int variableIndex;     // used for both variables/arrays
    int numberOfElements;  // used for arrays 
};

typedef struct STATIC_VARIABLE_ENTRY *STATVARPOINTER;

struct TABLE_CONSTANT_ENTRY
{
    char *name;
    int tableIndex;     
    char elementType;
    int numberOfElements; 
};

typedef struct TABLE_CONSTANT_ENTRY *TABLECONSTPOINTER;


struct LOCAL_VARIABLE_REFERENCE
{
    int address;     
    int instruction;
    int index;
    int isArray;
    int isLocalVariableOrArray; // it is parametrer or local variable 
};

typedef struct LOCAL_VARIABLE_REFERENCE *LOCALVARIABLEREFERENCEPOINTER;

struct LOCAL_VARIABLE
{
    char* name;
    int index;
    int isArray;
};

typedef struct LOCAL_VARIABLE *LOCALVARIABLEPOINTER;

char *getExternalVariableNameForVariableNumber(char *aResult, long aVariableNumber,
    IMPORTENTRYPOINTER *aFullImportResourceDictionary);
int changeImportEntryVariableToArray(long aVariableNumber, IMPORTENTRYPOINTER *aFullImportResourceDictionary);    
int writeExternalReferencesInfo(IMPORTENTRYPOINTER *aFullImportResourceDictionary, FILE *aOutputFile);    

int initializeStaticVariableList(EXPORTENTRYPOINTER *aFullExportResourceDictionary);
int addPrivateStaticVariableIfNotExisting(int aVariableIndex, int aIsArray, char aVariableType);
char *getStaticVariableNameForIndex(char *aResult, int aVariableIndex);
int writeExportedVariablesInfo(FILE *aOutputFile);

int initializeConstantTableList(void);
char *getConstantTableName(char *aResult, int aTableIndex);
int addConstantTableEntryIfNotExisting(int aTableIndex, char aVariableType);
void processConstantTableList(void);

void getAutoVariableNameForIndex(char *aResult, int aCurrentInstruction, int aVariableIndex);
int initializeLocalVariableReferencesList(void);
int addAutoVariableReference(int aAddress, int aInstruction, int aVariableIndex, int aIsArray);
int sortLocalVariableReferencesList(void);
void displayLocalVariableReferencesList(void);
int getParametersOrLocalVariables(LOCALVARIABLEPOINTER aResultArray[], int aGetLocalVariables,
    int aStartAddress, int aEndAddress);
void writeParameters(FILE *aOutputFile, int aCurrentAddress, int aEndAddress);
void writeLocalVariables(FILE *aOutputFile, int aCurrentAddress, int aEndAddress);
#endif

