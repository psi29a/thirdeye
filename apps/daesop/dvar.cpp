#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dasm.hpp"
#include "damap.hpp"

#include "dvar.hpp"

// static variables
STATVARPOINTER *myStaticVariablesTable = NULL;
// tables
TABLECONSTPOINTER *myConstantTables = NULL;
// local variable references
LOCALVARIABLEREFERENCEPOINTER *myLocalVariableReferences = NULL;
int myLocalVariableReferencesCounter = 0;

/*
Returns the external variable name for a variable number
*/
char *getExternalVariableNameForVariableNumber(char *aResult, long aVariableNumber,
    IMPORTENTRYPOINTER *aFullImportResourceDictionary)
{
    int i;
    if (aFullImportResourceDictionary == NULL)
    {
        printf("getExternalVariableNameForVariableNumber(): the import resource dictionary is NULL!\n");
        return NULL;
    }    
    for( i = 0; i < MAX_IMPORT_TABLE_ITEMS && aFullImportResourceDictionary[i] != NULL; i++)
    { 
        IMPORTENTRYPOINTER loCurrentImportEntry;
        int loCurrentImportEntryType;
        
        loCurrentImportEntry = aFullImportResourceDictionary[i];
        loCurrentImportEntryType = loCurrentImportEntry->importType;
        switch(loCurrentImportEntryType)
        {
            case IMPORT_ENTRY_BYTE:
            case IMPORT_ENTRY_WORD:
            case IMPORT_ENTRY_LONG:            
            case IMPORT_ENTRY_ARRAY_BYTE:
            case IMPORT_ENTRY_ARRAY_WORD:
            case IMPORT_ENTRY_ARRAY_LONG:
                if (loCurrentImportEntry ->importedVariableNumber == aVariableNumber)
                {
                    // found the entry, return the name
                    strcpy(aResult, loCurrentImportEntry->firstOriginal);
                    return aResult;
                }
                break;                
            default:
                // nothing
                break;                        
        }
    }
    // not found
    printf("The external variable with the index %ld was not found!\n", aVariableNumber);
    return NULL;    
}

/*
Change the import entry type from a variable to array
*/
int changeImportEntryVariableToArray(long aVariableNumber, IMPORTENTRYPOINTER *aFullImportResourceDictionary)
{
    int i;
    if (aFullImportResourceDictionary == NULL)
    {
        printf("changeImportEntryVariableToArray(): the import resource dictionary is NULL!\n");
        return false;
    }    
    for( i = 0; i < MAX_IMPORT_TABLE_ITEMS && aFullImportResourceDictionary[i] != NULL; i++)
    { 
        IMPORTENTRYPOINTER loCurrentImportEntry;
        int loCurrentImportEntryType;
        
        loCurrentImportEntry = aFullImportResourceDictionary[i];
        loCurrentImportEntryType = loCurrentImportEntry->importType;
        switch(loCurrentImportEntryType)
        {
            case IMPORT_ENTRY_BYTE:
            case IMPORT_ENTRY_WORD:
            case IMPORT_ENTRY_LONG:            
                if (loCurrentImportEntry ->importedVariableNumber == aVariableNumber)
                {
                    // change it
                    if (loCurrentImportEntryType == IMPORT_ENTRY_BYTE)
                    {
                        loCurrentImportEntry->importType = IMPORT_ENTRY_ARRAY_BYTE;    
                    }
                    else if (loCurrentImportEntryType == IMPORT_ENTRY_WORD)
                    {
                        loCurrentImportEntry->importType = IMPORT_ENTRY_ARRAY_WORD;    
                    }
                    else if (loCurrentImportEntryType == IMPORT_ENTRY_LONG)
                    {
                        loCurrentImportEntry->importType = IMPORT_ENTRY_ARRAY_LONG;    
                    }                    
                    return true;
                }                               
                break;
            case IMPORT_ENTRY_ARRAY_BYTE:
            case IMPORT_ENTRY_ARRAY_WORD:
            case IMPORT_ENTRY_ARRAY_LONG:
                if (loCurrentImportEntry ->importedVariableNumber == aVariableNumber)
                {            
                    // it is an array already
                    return true;
                }
                break;                             
            default:
                if (loCurrentImportEntry ->importedVariableNumber == aVariableNumber)
                {            
                    // error
                    printf("The extern variable with the index %ld cannot be changed to an array!\n", aVariableNumber);
                    return false;
                }
                break;                  
        }
    }
    // not found
    printf("The external variable with the index %ld was not found!\n", aVariableNumber);
    return false;
}

/*
Write Information about the runtime functions and external variables used by the resource
*/ 
int writeExternalReferencesInfo(IMPORTENTRYPOINTER *aFullImportResourceDictionary, FILE *aOutputFile)
{
    int i;
    if (aFullImportResourceDictionary == NULL)
    {
        printf("writeExternalReferencesInfo(): the import resource dictionary is NULL!\n");
        return false;
    }            
    for( i = 0; i < MAX_IMPORT_TABLE_ITEMS && aFullImportResourceDictionary[i] != NULL; i++)
    { 
        IMPORTENTRYPOINTER loCurrentImportEntry;
        int loCurrentImportEntryType;
        char loTmp[256];
        char loTmp2[256];
        
        loCurrentImportEntry = aFullImportResourceDictionary[i];
        loCurrentImportEntryType = loCurrentImportEntry->importType;
        switch(loCurrentImportEntryType)
        {
            case IMPORT_ENTRY_RUNTIME_FUNCTION:
                sprintf(loTmp, "\"%s\"", loCurrentImportEntry->firstOriginal);
                fprintf(aOutputFile, "%s %-32s ;runtime_function: %d\n",
                    META_IMPORTED_RUNTIME_FUNCTION, loTmp,
                    loCurrentImportEntry->runtimeFunctionNumber);            
                break;
            case IMPORT_ENTRY_BYTE:
            case IMPORT_ENTRY_WORD:
            case IMPORT_ENTRY_LONG:
                sprintf(loTmp, "\"%s\",", loCurrentImportEntry->firstOriginal);
                sprintf(loTmp2, "\"%s\"", loCurrentImportEntry->originalResourceName);
                fprintf(aOutputFile, "%s %-18s  %5d, %-14s ;original_resource: %d\n",
                    META_IMPORTED_VARIABLE, loTmp,
                    loCurrentImportEntry->importedVariableNumber, loTmp2,
                    loCurrentImportEntry->originalResourceNumber);
                break;
            case IMPORT_ENTRY_ARRAY_BYTE:
            case IMPORT_ENTRY_ARRAY_WORD:
            case IMPORT_ENTRY_ARRAY_LONG:
                sprintf(loTmp, "\"%s\",", loCurrentImportEntry->firstOriginal);
                sprintf(loTmp2, "\"%s\"", loCurrentImportEntry->originalResourceName);
                fprintf(aOutputFile, "%s %-18s  %d, %d, %-14s ;original_resource: %d\n",
                    META_IMPORTED_ARRAY, loTmp,
                    loCurrentImportEntry->importedVariableNumber, -1, loTmp2,
                    loCurrentImportEntry->originalResourceNumber);
                break;                
            default:
                // nothing
                break;                        
        }
    }
    return true;
}

/*
Initializes static variable list
(and adds public variables from export dictionary)
*/
int initializeStaticVariableList(EXPORTENTRYPOINTER *aFullExportResourceDictionary)
{
    int loStaticVariablesTableSizeInBytes;
    int i;
    
    loStaticVariablesTableSizeInBytes = sizeof(struct STATIC_VARIABLE_ENTRY) * (MAX_STATIC_VARIABLES + 1);
    myStaticVariablesTable = (STATVARPOINTER *)malloc(loStaticVariablesTableSizeInBytes);
    if (myStaticVariablesTable == NULL)
    {
        printf("Failure to allocate %d bytes for the static variables table!\n", loStaticVariablesTableSizeInBytes);
        return false;
    }

    // set NULL to everything
    for(i = 0; i < MAX_STATIC_VARIABLES + 1; i++)
    {
        myStaticVariablesTable[i] =NULL;
    }
    
    if (aFullExportResourceDictionary == NULL)
    {
        // the export table should  exist
        printf("initializeStaticVariableList(): the export resource dictionary is NULL!\n");
        free(myStaticVariablesTable);
        myStaticVariablesTable = NULL;
        return false;
    }
    else
    {
        // exported variables are public static variables so add them there
        int i;
        int loStaticVariablesTableIndex = 0;
        for( i = 0; i < MAX_EXPORT_TABLE_ITEMS && aFullExportResourceDictionary[i] != NULL; i++)
        {
            EXPORTENTRYPOINTER loCurrentExportEntry;
            int loCurrentExportEntryType;
            STATVARPOINTER loStatVarEntry = NULL;

            if (loStaticVariablesTableIndex >= MAX_STATIC_VARIABLES)
            {
                printf("There can be max %d static/extern variables!\n", MAX_STATIC_VARIABLES);
                return false;
            }
        
            loCurrentExportEntry = aFullExportResourceDictionary[i];
            loCurrentExportEntryType = loCurrentExportEntry->exportType;
            switch(loCurrentExportEntryType)
            {
                case EXPORT_ENTRY_BYTE:
                case EXPORT_ENTRY_WORD:
                case EXPORT_ENTRY_LONG:
                    loStatVarEntry = (STATVARPOINTER)malloc(sizeof (struct STATIC_VARIABLE_ENTRY));
                    if (loStatVarEntry == NULL)
                    {
                        printf("Failure to allocate the memory for a static variable entry!\n");
                        return false;
                    }
                    loStatVarEntry->isPublic = true;
                    loStatVarEntry->isArray = false;
                    loStatVarEntry->name = makeString (loCurrentExportEntry->firstOriginal);
                    loStatVarEntry->variableIndex = loCurrentExportEntry->exportedVariablePosition;
                    loStatVarEntry->numberOfElements = -1;
                    // store into the table of static variables
                    myStaticVariablesTable[loStaticVariablesTableIndex++] = loStatVarEntry;
                    break;
                case EXPORT_ENTRY_ARRAY_BYTE:
                case EXPORT_ENTRY_ARRAY_WORD:
                case EXPORT_ENTRY_ARRAY_LONG:
                    loStatVarEntry = (STATVARPOINTER)malloc(sizeof (struct STATIC_VARIABLE_ENTRY));
                    if (loStatVarEntry == NULL)
                    {
                        printf("Failure to allocate the memory for a static variable entry!\n");
                        return false;
                    }
                    loStatVarEntry->isPublic = true;
                    loStatVarEntry->isArray = true;
                    loStatVarEntry->name = makeString (loCurrentExportEntry->firstOriginal);
                    loStatVarEntry->variableIndex = loCurrentExportEntry->exportedArrayPosition;
                    loStatVarEntry->numberOfElements = loCurrentExportEntry->exportedArraySizeInElements;
                    // store into the table of static variables
                    myStaticVariablesTable[loStaticVariablesTableIndex++] = loStatVarEntry;                    
                    break;                
                default:
                    // nothing
                    break;                            
            }
        }        
    }
    return true;
}

/*
Adds a static variable with the specified index into the static variable table if missing, otherwise nothing
*/
int addPrivateStaticVariableIfNotExisting(int aVariableIndex, int aIsArray, char aVariableType)
{
    int loStaticVariablesTableIndex;
    char loNewName[256];
    STATVARPOINTER loNewStatVarEntry;

    if (myStaticVariablesTable == NULL)
    {
        printf("addPrivateStaticVariableIfNotExisting(): static variables table is NULL!\n");
        return false;
    }    

    for( loStaticVariablesTableIndex = 0; loStaticVariablesTableIndex < MAX_STATIC_VARIABLES &&
            myStaticVariablesTable[loStaticVariablesTableIndex] != NULL; loStaticVariablesTableIndex++)
    {
        STATVARPOINTER loStatVarEntry;
        loStatVarEntry = myStaticVariablesTable[loStaticVariablesTableIndex];
        if (loStatVarEntry->variableIndex == aVariableIndex)
        {
            // the variable already exists
            // check whether it is not a variable for which we did not know the type before, but we know it now
            // (warning: this code relies on the fact that the variable name contains type specification <type_char>:  !
            char *loName = loStatVarEntry->name;
            char *loTypeCharacterPointer; 
            if ((loTypeCharacterPointer = strstr(loName, "?:")) != NULL && aVariableType != '?')
            {
                // fix the type
                *loTypeCharacterPointer = aVariableType;
                //fix the array/variable (it may be wrong - LESA instruction defaults to an array)
                loStatVarEntry->isArray = aIsArray;
                 
            }
            return true;
        }
    }
    if (loStaticVariablesTableIndex >= MAX_STATIC_VARIABLES)
    {
        printf("There can be max %d static/extern variables!\n", MAX_STATIC_VARIABLES);
        return false;
    }

    // ok, it is not there so make a new one
    // WARNING: this code does not handle well the instruction LESA (it defaults to an an array, but we cannot be sure - the instruction does not imply the type)    
    if (aIsArray == true)
    {
        sprintf(loNewName, "%c:%s%d", aVariableType, STATIC_ARRAY_PREFIX, aVariableIndex);
    }
    else
    {
       sprintf(loNewName, "%c:%s%d", aVariableType, STATIC_VARIABLE_PREFIX, aVariableIndex);        
    }
    loNewStatVarEntry = (STATVARPOINTER)malloc(sizeof (struct STATIC_VARIABLE_ENTRY));
    if (loNewStatVarEntry == NULL)
    {
        printf("Failure to allocate the memory for a static variable entry!\n");
        return false;
    }
    loNewStatVarEntry->isPublic = false;
    loNewStatVarEntry->isArray = aIsArray;
    loNewStatVarEntry->name = makeString (loNewName);
    loNewStatVarEntry->variableIndex = aVariableIndex;
    loNewStatVarEntry->numberOfElements = -1;  // unknown or not used
    // store the entry
    myStaticVariablesTable[loStaticVariablesTableIndex] =  loNewStatVarEntry;
    return true;
}

/*
Gets the static variable name or NULL
*/ 
char *getStaticVariableNameForIndex(char *aResult, int aVariableIndex)
{
    int loStaticVariablesTableIndex;

    if (myStaticVariablesTable == NULL)
    {
        printf("getStaticVariableNameForIndex(): static variables table is NULL!\n");
        return NULL;
    }

    for( loStaticVariablesTableIndex = 0; loStaticVariablesTableIndex < MAX_STATIC_VARIABLES &&
            myStaticVariablesTable[loStaticVariablesTableIndex] != NULL; loStaticVariablesTableIndex++)
    {
        STATVARPOINTER loStatVarEntry;
        loStatVarEntry = myStaticVariablesTable[loStaticVariablesTableIndex];
        if (loStatVarEntry->variableIndex == aVariableIndex)
        {
            // found, return
            strcpy(aResult, loStatVarEntry->name);
            return aResult;
        }
    }
    printf("The static variable %d was not found!\n", aVariableIndex);
    return NULL;    
}

/*
Write Information about the exported variables/arrays
*/ 
int writeExportedVariablesInfo(FILE *aOutputFile)
{
    int i;
    if (myStaticVariablesTable == NULL)
    {
        printf("writeExportedVariablesInfo(): the static variables table is NULL!\n");
        return false;
    }            
    for( i = 0; i < MAX_STATIC_VARIABLES && myStaticVariablesTable[i] != NULL; i++)
    {
        STATVARPOINTER loCurrentEntry;
        int loIsPublic;
        int loIsArray;
        char loName[256];

        loCurrentEntry = myStaticVariablesTable[i];
        loIsPublic = loCurrentEntry->isPublic;
        loIsArray = loCurrentEntry->isArray;
        sprintf(loName, "\"%s\"", loCurrentEntry->name);
        if (loIsPublic == true)
        {
            // public (exported) arrays/variables
            if (loIsArray == true)
            {
                // public array
                fprintf(aOutputFile, "%s %-18s  %d, %d\n", META_PUBLIC_STATIC_ARRAY, loName,
                loCurrentEntry->variableIndex, loCurrentEntry->numberOfElements);                
            }
            else
            {
                // public variable
                fprintf(aOutputFile, "%s %-18s  %d\n", META_PUBLIC_STATIC_VARIABLE, loName,
                loCurrentEntry->variableIndex);                
            }
        }
        else
        {
            // private arrays/variables
            if (loIsArray == true)
            {
                // private array
                fprintf(aOutputFile, "%s %-18s  %d, %d\n", META_PRIVATE_STATIC_ARRAY, loName,
                loCurrentEntry->variableIndex, loCurrentEntry->numberOfElements);                 
            }
            else
            {
                // private variable
                fprintf(aOutputFile, "%s %-18s  %d\n", META_PRIVATE_STATIC_VARIABLE, loName,
                loCurrentEntry->variableIndex);                 
            }            
        }
 
    }
    return true;
}


/*
Initializes table list
*/
int initializeConstantTableList(void)
{
    int loConstantTablesTableSizeInBytes;
    int i;
    
    loConstantTablesTableSizeInBytes = sizeof(struct TABLE_CONSTANT_ENTRY) * (MAX_CONSTANT_TABLES + 1);
    myConstantTables = (TABLECONSTPOINTER *)malloc(loConstantTablesTableSizeInBytes);
    if (myConstantTables == NULL)
    {
        printf("Failure to allocate %d bytes for the table constants table!\n", loConstantTablesTableSizeInBytes);
        return false;
    }

    // set NULL to everything
    for(i = 0; i < MAX_CONSTANT_TABLES + 1; i++)
    {
        myConstantTables[i] =NULL;
    }
    return true;
}

/*
Get the name of the specified constant table
*/
char *getConstantTableName(char *aResult, int aTableIndex)
{
    int loTableIndex;

    if (myConstantTables == NULL)
    {
        printf("getConstantTableName(): the table of constant tables is NULL!\n");
        return NULL;
    }    
    for(loTableIndex = 0; loTableIndex < MAX_CONSTANT_TABLES && myConstantTables[loTableIndex]  != NULL; loTableIndex++)
    {
        TABLECONSTPOINTER loCurrentEntry;
        loCurrentEntry = myConstantTables[loTableIndex];
        if (loCurrentEntry->tableIndex == aTableIndex)
        {
            // found
            strcpy(aResult, loCurrentEntry->name);
            return aResult;
        }
    }
    printf("The constant table name was not found for the table index %d!\n", aTableIndex);
    return NULL;    
}

/*
Add a contant table entry if it does not exist yet
*/
int addConstantTableEntryIfNotExisting(int aTableIndex, char aVariableType)
{
    int loTableIndex;
    char loNewName[256];
    TABLECONSTPOINTER loNewEntry;

    if (myConstantTables == NULL)
    {
        printf("addTableEntryIfNotExisting(): the table of constant tables is NULL!\n");
        return false;
    } 
    
    for(loTableIndex = 0; loTableIndex < MAX_CONSTANT_TABLES && myConstantTables[loTableIndex]  != NULL; loTableIndex++)
    {
        TABLECONSTPOINTER loCurrentEntry;
        loCurrentEntry = myConstantTables[loTableIndex];
        if (loCurrentEntry->tableIndex == aTableIndex)
        {
            // found
            // check whether it is not a table for which we did not know the type before, but we know it now
            if (loCurrentEntry->elementType == '?' && aVariableType != '?')
            {
                // set the type
                loCurrentEntry->elementType = aVariableType;
            }            
            return true;
        }
    }
    if (loTableIndex >= MAX_CONSTANT_TABLES)
    {
        printf("There can be max %d constant tables!\n", MAX_CONSTANT_TABLES);
        return false;
    }
    // create a new entry
    loNewEntry = (TABLECONSTPOINTER)malloc(sizeof(struct TABLE_CONSTANT_ENTRY));
    if (loNewEntry == NULL)
    {
        printf("Unable to allocate a contant table entry!\n");
        return false;
    }
    sprintf(loNewName, "%c:%s%d", aVariableType, CONSTANT_TABLE_PREFIX, aTableIndex);
    loNewEntry->name = makeString(loNewName);
    loNewEntry->tableIndex = aTableIndex;
    loNewEntry->elementType = aVariableType;
    loNewEntry->numberOfElements = -1; // unknown
    myConstantTables[loTableIndex] = loNewEntry;
    return true;
}

/*
Process constant table list and set code map values properly to show constant tables
*/
void processConstantTableList(void)
{
    int i;
    printf("Constant table list processing started...\n");
    if (myConstantTables == NULL)
    {
        printf("processConstantTableList(): the table of constant tables is NULL!\n");
        return;
    }     
    for(i = 0; i < MAX_CONSTANT_TABLES + 1 && myConstantTables[i] != NULL; i++)
    {
        TABLECONSTPOINTER loCurrentEntry;       
        int loTableIndex;
        char loElementType;

        loCurrentEntry = myConstantTables[i];
        loTableIndex = loCurrentEntry->tableIndex;
        loElementType = loCurrentEntry->elementType;

        if (loElementType == 'B' || loElementType == '?')  // byte or unknown type
        {
            setCodeMapAddress(loTableIndex, MAP_BYTE_TABLE_START);
        }
        else if (loElementType == 'W')
        {
            setCodeMapAddress(loTableIndex, MAP_WORD_TABLE_START);
        }
        else if (loElementType == 'L')
        {
            setCodeMapAddress(loTableIndex, MAP_LONG_TABLE_START);
        }
        else
        {
            printf("processConstantTableList(): unknown constant table type %c\n", loElementType);
        }                     
    }
    fixCodeTableForConstantTables();
    printf("Constant table list processing finished.\n");    
}

/*
Gets the local (auto) variable name or null
*/ 
void getAutoVariableNameForIndex(char *aResult, int aCurrentInstruction, int aVariableIndex)
{
   char loTmp[256];
   strcpy(aResult, "");    
   if (aVariableIndex == 2)
   {
       // THIS pointer
       strcat(aResult, "THIS");
       return;
   }
          
   switch(aCurrentInstruction)
   {
       case INSTRUCTION_LAB:
       case INSTRUCTION_SAB:
            if (aVariableIndex >= LOCAL_VARIABLES_START_INDEX && aVariableIndex <= LOCAL_VARIABLES_END_INDEX)
            {
                strcat(aResult, LOCAL_VARIABLE_PREFIX);
            }
            else
            {
                strcat(aResult, PARAMETER_VARIABLE_PREFIX);
            }
            strcat(aResult, "B");
            break;
       case INSTRUCTION_LAW:
       case INSTRUCTION_SAW:       
            if (aVariableIndex >= LOCAL_VARIABLES_START_INDEX && aVariableIndex <= LOCAL_VARIABLES_END_INDEX)
            {
                strcat(aResult, LOCAL_VARIABLE_PREFIX);
            }
            else
            {
                strcat(aResult, PARAMETER_VARIABLE_PREFIX);
            }
            strcat(aResult, "W");
            break;
       case INSTRUCTION_LAD:
       case INSTRUCTION_SAD:       
            if (aVariableIndex >= LOCAL_VARIABLES_START_INDEX && aVariableIndex <= LOCAL_VARIABLES_END_INDEX)
            {
                strcat(aResult, LOCAL_VARIABLE_PREFIX);
            }
            else
            {
                strcat(aResult, PARAMETER_VARIABLE_PREFIX);
            }
            strcat(aResult, "L");
            break;            
       case INSTRUCTION_LABA:
       case INSTRUCTION_SABA:       
            if (aVariableIndex >= LOCAL_VARIABLES_START_INDEX && aVariableIndex <= LOCAL_VARIABLES_END_INDEX)
            {
                strcat(aResult, LOCAL_ARRAY_PREFIX);
            }
            else
            {
                strcat(aResult, PARAMETER_ARRAY_PREFIX);
            }
            strcat(aResult, "B");
            break;
       case INSTRUCTION_LAWA:
       case INSTRUCTION_SAWA:       
            if (aVariableIndex >= LOCAL_VARIABLES_START_INDEX && aVariableIndex <= LOCAL_VARIABLES_END_INDEX)
            {
                strcat(aResult, LOCAL_ARRAY_PREFIX);
            }
            else
            {
                strcat(aResult, PARAMETER_ARRAY_PREFIX);
            }
            strcat(aResult, "W");
            break;
       case INSTRUCTION_LADA:
       case INSTRUCTION_SADA:       
            if (aVariableIndex >= LOCAL_VARIABLES_START_INDEX && aVariableIndex <= LOCAL_VARIABLES_END_INDEX)
            {
                strcat(aResult, LOCAL_ARRAY_PREFIX);
            }
            else
            {
                strcat(aResult, PARAMETER_ARRAY_PREFIX);
            }
             strcat(aResult, "L");
            break;
       case INSTRUCTION_LEAA:
            // WARNING: this is not correct, we do not know whether it is an array or not for LEAA!       
            if (aVariableIndex >= LOCAL_VARIABLES_START_INDEX && aVariableIndex <= LOCAL_VARIABLES_END_INDEX)
            {
                strcat(aResult, LOCAL_ARRAY_PREFIX);
            }
            else
            {
                strcat(aResult, PARAMETER_ARRAY_PREFIX);
            }
             strcat(aResult, "?");
            break;            
       default:
            sprintf(loTmp, "getAutoVariableNameForIndex(): unknown instruction %d!", aCurrentInstruction);
            printf("%s\n", loTmp);
            strcat(aResult, loTmp);
            return; 
   }
   // add the index to the name
   sprintf(loTmp, ":%d", aVariableIndex);
   strcat(aResult, loTmp);    
}

/*
Initializes local variable references list
*/
int initializeLocalVariableReferencesList(void)
{
    int loLocalVariableReferencesTableSizeInBytes;
    int i;
    
    loLocalVariableReferencesTableSizeInBytes = sizeof(struct LOCAL_VARIABLE_REFERENCE) * (MAX_LOCAL_VARIABLE_REFERENCES + 1);
    myLocalVariableReferences = (LOCALVARIABLEREFERENCEPOINTER *)malloc(loLocalVariableReferencesTableSizeInBytes);
    if (myLocalVariableReferences == NULL)
    {
        printf("Failure to allocate %d bytes for the table of local variable references!\n", loLocalVariableReferencesTableSizeInBytes);
        return false;
    }

    // set NULL to everything
    for(i = 0; i < MAX_LOCAL_VARIABLE_REFERENCES + 1; i++)
    {
        myLocalVariableReferences[i] =NULL;
    }
    myLocalVariableReferencesCounter = 0;
    return true;
}

/**
Store the auto variable reference
*/
int addAutoVariableReference(int aAddress, int aInstruction, int aVariableIndex, int aIsArray)
{
    LOCALVARIABLEREFERENCEPOINTER loNewEntry;
    if (aVariableIndex == 2)
    {
        // THIS pointer, ignore
        return true;
    }
    if (myLocalVariableReferencesCounter >= MAX_LOCAL_VARIABLE_REFERENCES)
    {
        printf("There can be only %d local variable references in a code resource!\n", MAX_LOCAL_VARIABLE_REFERENCES);
        return false;
    }

    loNewEntry = (LOCALVARIABLEREFERENCEPOINTER)malloc(sizeof(struct LOCAL_VARIABLE_REFERENCE));
    if (loNewEntry == NULL)
    {
        printf("Unable to allocate memory for a local variable reference entry!\n");
        return false;
    }

    loNewEntry->address = aAddress;
    loNewEntry->instruction = aInstruction;
    loNewEntry->index = aVariableIndex;
    loNewEntry->isLocalVariableOrArray =
        (aVariableIndex >= LOCAL_VARIABLES_START_INDEX && aVariableIndex <= LOCAL_VARIABLES_END_INDEX)?true:false;
    loNewEntry->isArray = aIsArray;
    myLocalVariableReferences[myLocalVariableReferencesCounter++] = loNewEntry;    
    return true;
}


/*
Used by qsort for comparing local variable references
*/
int compareLocaleVariableReferences (void const *aFirstItem, void const *aSecondItem)
{
  LOCALVARIABLEREFERENCEPOINTER loFirstEntry = *(LOCALVARIABLEREFERENCEPOINTER *)aFirstItem;
  LOCALVARIABLEREFERENCEPOINTER loSecondEntry = *(LOCALVARIABLEREFERENCEPOINTER *)aSecondItem;  
    
  int loTemp;  
  loTemp  = loFirstEntry->address - loSecondEntry->address;
  if (loTemp > 0)
    return 1;
  else if (loTemp < 0)
    return -1;
  else
    return 0;
}

/*
Sorts local vaiable references
*/
int sortLocalVariableReferencesList(void)
{
    if (myLocalVariableReferences == NULL)
    {
        printf("sortLocalVariableReferencesList(...): myLocalVariableReferences is NULL!\n");
        return false;
    }

    if (myLocalVariableReferencesCounter == 0)
    {
        // nothing to do
        return true;
    }
    printf("Sorting local variable references...\n");
    qsort (myLocalVariableReferences, myLocalVariableReferencesCounter, sizeof(LOCALVARIABLEREFERENCEPOINTER), compareLocaleVariableReferences);
    return true;
}

/*
Dumps local variable references list (for debugging)
*/
void displayLocalVariableReferencesList()
{
    int i;
    if (myLocalVariableReferences == NULL)
    {
        printf("dumpLocalVariableReferencesList(...): myLocalVariableReferences is NULL!\n");
        return;
    }
    printf("*** Local variable references list start ***\n");
    for(i = 0; i < myLocalVariableReferencesCounter; i++)
    {
        LOCALVARIABLEREFERENCEPOINTER loOneEntry = myLocalVariableReferences[i];
        printf("  Address: %d, Instruction: %d,Index: %d, Local variable: %s, Array: %s\n", loOneEntry->address,
            loOneEntry->instruction, loOneEntry->index, (loOneEntry->isLocalVariableOrArray == true)?"YES":"NO",
            (loOneEntry->isArray == true)?"YES":"NO");
    }
    printf("*** Local variable references list end ***\n");
}

/*
Used by qsort for comparing local variabless
*/
int compareLocaleVariables(void const *aFirstItem, void const *aSecondItem)
{
  LOCALVARIABLEPOINTER loFirstEntry = *(LOCALVARIABLEPOINTER *)aFirstItem;
  LOCALVARIABLEPOINTER loSecondEntry = *(LOCALVARIABLEPOINTER *)aSecondItem;  
    
  int loTemp;  
  loTemp  = loFirstEntry->index - loSecondEntry->index;
  if (loTemp > 0)
    return 1;
  else if (loTemp < 0)
    return -1;
  else
    return 0;
}


/*
Used by qsort for comparing local variabless
*/
int compareParameters(void const *aFirstItem, void const *aSecondItem)
{
  LOCALVARIABLEPOINTER loFirstEntry = *(LOCALVARIABLEPOINTER *)aFirstItem;
  LOCALVARIABLEPOINTER loSecondEntry = *(LOCALVARIABLEPOINTER *)aSecondItem;  
  /*
  The first parameter has value 0, others have values from 65535 or lower...
  */
  int loFirstValue = loFirstEntry->index;
  int loSecondValue = loSecondEntry->index;   
  if (loFirstValue == 0)
  {
      // first 0
      if (loSecondValue == 0)
      {
          // both 0 (should not happen)
          return 0;
      }
      else
      {
          // only first 0
          return -1;
      }
  }
  else if (loSecondValue == 0)
  {
      // second 0, first not 0
      return 1;
  }
  else if (loFirstValue > loSecondValue)
  {
      // first entry  is lower parameter
      return -1;
  }
  else if (loFirstValue < loSecondValue)
  {
      // second entry is lower parameter
      return 1; 
  }
  else
  {
      // both the same (should not happen)
      return 0;
  }
}

/*
Get parameters or local variables used in the specified range
*/
int getParametersOrLocalVariables(LOCALVARIABLEPOINTER aResultArray[], int aGetLocalVariables,
    int aStartAddress, int aEndAddress)
{
    int i;
    int loItemCounter = 0;
    for(i = 0; i < myLocalVariableReferencesCounter; i++)
    {
        LOCALVARIABLEREFERENCEPOINTER loVariableReferenceEntry = myLocalVariableReferences[i];
        if (loVariableReferenceEntry->address < aStartAddress || loVariableReferenceEntry->address > aEndAddress)
        {
            // outside of range, ignore
            continue;
        }

        if ((aGetLocalVariables == false && loVariableReferenceEntry->isLocalVariableOrArray == false) ||
            (aGetLocalVariables == true && loVariableReferenceEntry->isLocalVariableOrArray == true) )
        {
            // ok it is what we want
            int j;
            for(j = 0; j < loItemCounter; j++)
            {
                LOCALVARIABLEPOINTER loOneItem = aResultArray[j];
                if (loVariableReferenceEntry->index == loOneItem->index)
                {
                    // parameter/variable is already in the parameter array
                    break;
                }
            }
            if (j == loItemCounter)
            {
                char loTmp[256];
                // it is a new parameter
                LOCALVARIABLEPOINTER loNewItem;
                if (loItemCounter >= MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES)
                {
                    printf("There can be only %d function parameters/local variables!\n", MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES);
                    return false;
                }
                loNewItem = (LOCALVARIABLEPOINTER)malloc(sizeof(struct LOCAL_VARIABLE));
                if (loNewItem == NULL)
                {
                    printf("Unable to allocate the memory for a local variable/parameter item!\n");
                    return false;
                }
                loNewItem->index = loVariableReferenceEntry->index;
                getAutoVariableNameForIndex(loTmp, loVariableReferenceEntry->instruction,
                    loVariableReferenceEntry->index);
                loNewItem->name = makeString(loTmp);
                loNewItem->isArray = loVariableReferenceEntry->isArray;
                aResultArray[loItemCounter++] = loNewItem;
            }
        }
        
    }
    if (loItemCounter > 1)
    {
        // needs sorting
        if (aGetLocalVariables == true)
        {
            printf("Sorting local variables...\n");
            qsort (aResultArray, loItemCounter,
                sizeof(LOCALVARIABLEPOINTER), compareLocaleVariables);
        }
        else
        {
            printf("Sorting parameters ...\n");
            qsort (aResultArray, loItemCounter,
                sizeof(LOCALVARIABLEPOINTER), compareParameters);            
        }
    }
    return true;
}

/*
Write message handler/procedure parameters
*/
void writeParameters(FILE *aOutputFile, int aCurrentAddress, int aEndAddress)
{
    LOCALVARIABLEPOINTER loParameters[MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES + 1];
    int i;
    for(i = 0; i < MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES + 1; i++)
    {
        loParameters[i] = NULL;
    }
    
    if (getParametersOrLocalVariables(loParameters, false, aCurrentAddress, aEndAddress) == false)
    {
        printf("Error: unable to get function parameters!\n");
    }
    // write out the parameters
    for(i = 0; loParameters[i] != NULL; i++)
    {
        if (loParameters[i]->isArray == true)
        {
            // parameter array
            fprintf(aOutputFile, "%s %-18s  %d, %d\n", META_PARAMETER_ARRAY, loParameters[i]->name,
            loParameters[i]->index, -1);                 
        }
        else
        {
            // parameter variable
            fprintf(aOutputFile, "%s %-18s  %d\n", META_PARAMETER_VARIABLE, loParameters[i]->name,
            loParameters[i]->index);                
        }
    }

    // free parameters
    for(i = 0; i < MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES + 1; i++)
    {
        if (loParameters[i] != NULL)
        {
            if (loParameters[i]->name != NULL)
            {
                free(loParameters[i]->name);
            }
            free(loParameters[i]);
            loParameters[i] = NULL;
        }        
    } 
}

/*
Write message handler/procedure local variables
*/
void writeLocalVariables(FILE *aOutputFile, int aCurrentAddress, int aEndAddress)
{
    LOCALVARIABLEPOINTER loLocalVariables[MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES + 1];
    int i;
    for(i = 0; i < MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES + 1; i++)
    {
        loLocalVariables[i] = NULL;        
    }
    
    if (getParametersOrLocalVariables(loLocalVariables, true, aCurrentAddress, aEndAddress) == false)
    {
        printf("Error: unable to get local variables!\n");
    }
    // write out the local variables
    for(i = 0; loLocalVariables[i] != NULL; i++)
    {
        if (loLocalVariables[i]->isArray == true)
        {
            // local array
            fprintf(aOutputFile, "%s %-18s  %d, %d\n", META_LOCAL_ARRAY, loLocalVariables[i]->name,
            loLocalVariables[i]->index, -1);                 
        }
        else
        {
            // local variable
            fprintf(aOutputFile, "%s %-18s  %d\n", META_LOCAL_VARIABLE, loLocalVariables[i]->name,
            loLocalVariables[i]->index);                
        }
    }

    // free local variables
    for(i = 0; i < MAX_FUNCTION_PARAMETERS_OR_LOCAL_VARIABLES + 1; i++)
    {
        if (loLocalVariables[i] != NULL)
        {
            if (loLocalVariables[i]->name != NULL)
            {
                free(loLocalVariables[i]->name);
            }
            free(loLocalVariables[i]);
            loLocalVariables[i] = NULL;
        }        
    }    
}

