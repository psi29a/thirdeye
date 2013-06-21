///////////////////////////////////////////////////////////////////////////////
//
// DAESOP
// using code from AESOP engine and ReWiki website
// (c) Mirek Luza
// public domain software
//
///////////////////////////////////////////////////////////////////////////////

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dict.hpp"

int readDictionaryStringList(DICTENTRYPOINTER *aDictionaryArray, int aMaxDictionaryEntries, int *aCurrentIndexInDictionary, FILE *aResFile, ULONG aStringListIndex);
void storeIntoDictionaryArray(DICTENTRYPOINTER *aDictionaryArray, int *aCurrentIndexInDictionary, char *aFirstString, char *aSecondString);
int compareAccordingToSecondNumber(void const *aFirstItem, void const *aSecondItem);
int compareAccordingToSecondString(void const *aFirstItem, void const *aSecondItem);
int compareAccordingToFirstString(void const *aFirstItem, void const *aSecondItem);
DICTENTRYPOINTER *readMessageNamesDictionary(FILE *aResFile, DIRPOINTER *aDirectoryPointers);

DICTENTRYPOINTER *myMessageNamesDictionary = NULL;
DICTENTRYPOINTER *myResourceNamesDictionary = NULL;

/*
Gets resource number from number string
*/
int getResourceNumberFromNumberString(char *aResource)
{
    int loExtractedResourceNumber = atoi(aResource);
    if (loExtractedResourceNumber == 0 && strcmp(aResource,"0") != 0)
    {
        printf("Failure to convert the resource number: %s!\n", aResource);
        return -1;                 
    }
    if (loExtractedResourceNumber < 0)
    {
        printf("Invalid resource number: %s!\n", aResource);
        return -1;                 
    }
    return loExtractedResourceNumber;    
}

/*
Gets the resource number from name string
*/
int getResourceNumberFromNameString(char *aResource, DICTENTRYPOINTER* aResourceNameArray)
{
    int i;
    for(i = 0; i < MAX_NUMBER_OF_DICTIONARY_ITEMS && aResourceNameArray[i] != NULL; i++)
    {

        // The resource names are case sentitive!
        // In EOB 3 there are resources: "Holy symbol" and "holy symbol"
        if (strcmp(aResource, aResourceNameArray[i]->first) == 0)            
//      if (strcmpCS(aResource, aResourceNameArray[i]->first) == 0)  // case insensitive
        {
            char *loResourceNumberString = aResourceNameArray[i]->second;
            int loResourceNumber = atoi(loResourceNumberString);
            if (loResourceNumber == 0 && strcmp(loResourceNumberString,"0") != 0)
            {
                printf("Failure to convert the resource number: %s!\n", loResourceNumberString);
                return -1;                 
            }                    
            return loResourceNumber;
        }
    }
    return -1;    
}

/*
Read the dictionary resource (one of special AESOP resources 0..4 or import/export resources)
*/
DICTENTRYPOINTER *readTheDictionary(int aResource, int aMaxDictionaryEntries, FILE *aResFile,
    DIRPOINTER *aDirectoryPointers, int *aResourceSize)
{
    struct RESEntryHeader *loDictionaryResourceHeader;
    long loDictionaryStart;
    UWORD loDictionaryStringListsNumber;
    ULONG loStringListIndex;
    int loReadSize;
    DICTENTRYPOINTER *loDictionaryArray;
    int loDictionaryArraySizeInBytes;
    int loCurrentIndexInDictionary;
    int i;
    int loAllocatedDictionaryEntries;
    

    if (aMaxDictionaryEntries > MAX_NUMBER_OF_DICTIONARY_ITEMS)
    {
        printf("The maximum allowed length of the dictionary is: %d!\n", MAX_NUMBER_OF_DICTIONARY_ITEMS);
        return NULL;
    }

    // There is always at least one NULL entry (end marker)    
    loAllocatedDictionaryEntries = aMaxDictionaryEntries + 1;

    // allocate space for the dictionary array
    loDictionaryArraySizeInBytes = loAllocatedDictionaryEntries*sizeof(struct INTERNAL_DICTIONARY_ENTRY);
    loDictionaryArray = (DICTENTRYPOINTER *)malloc(loDictionaryArraySizeInBytes);
    if (loDictionaryArray == NULL)
    {
        printf("The allocation of the dictionary array with the size %d failed!\n", loAllocatedDictionaryEntries);
        return NULL;
    }
        
    // set everything to NULL
    for(i = 0; i < loAllocatedDictionaryEntries; i++)
    {
        loDictionaryArray[i] = NULL;
    }
            
    // now read the resource header 
    loDictionaryResourceHeader = getResourceEntryHeader(aResource, aResFile, aDirectoryPointers);
    if (loDictionaryResourceHeader == NULL)
    {
        printf("Failure while reading dictionary header for the dictionary resource: %d!\n", aResource);
        free(loDictionaryArray);
        return NULL;
    }

    // return the resource length if required
    if (aResourceSize != NULL)
    {
        *aResourceSize = loDictionaryResourceHeader->data_size;
    }    
    
    loDictionaryStart = getResourceEntryIndex(aResource, aDirectoryPointers) + sizeof(struct RESEntryHeader);        
    // now read the dictionary entries
    if (fseek(aResFile, loDictionaryStart, SEEK_SET) != 0)
    {
        printf("Failure to set the file position %ld when reading the dictionary resource %d!\n", loDictionaryStart, aResource);
        free(loDictionaryArray);
        return NULL;
    }
    
    loReadSize = fread( &loDictionaryStringListsNumber, 1, sizeof(UWORD), aResFile);
    if (loReadSize != sizeof(UWORD) || loDictionaryStringListsNumber > 2048) // 512 is the standard in AESOP
    {
        printf("Failure to read the number of dictionary string lists from the dictionary!\n");
        free(loDictionaryArray);
        return NULL;           
    }

    //printf("The number of string lists: %d\n", (int) loDictionaryStringListsNumber);

    // now read the indexes of string lists
    loCurrentIndexInDictionary = 0;
    for(i = 0; i < loDictionaryStringListsNumber; i++)
    {
        // DICTIONARY_ENTRY + sizeof(number_of_string_lists) + i * sizeof(list_index_entry) 
        if (fseek(aResFile, loDictionaryStart + sizeof(UWORD) + i * sizeof(ULONG), SEEK_SET) != 0)
        {
            printf("Failure to set the file position %ld when reading the dictionary resource %d!\n", loDictionaryStart, aResource);
            return NULL;
        }        
        loReadSize = fread( &loStringListIndex, 1, sizeof(ULONG), aResFile);
        if (loReadSize != sizeof(ULONG))
        {
            printf("Failure to read a string list index from the dictionary resource %d!\n", aResource);
            return NULL;           
        }
        if (loStringListIndex != 0)
        {
            // not empty
            loStringListIndex += loDictionaryStart; // add the start of the index table to get the real offset
            //printf("String list index: %d\n", (int)loStringListIndex);
            if (readDictionaryStringList(loDictionaryArray, aMaxDictionaryEntries, &loCurrentIndexInDictionary, aResFile, loStringListIndex) != true)
            {
                // something failed
                printf("Error while reading dictionary resource: %d\n", aResource);
                return NULL;
            }
        }
    }
    return loDictionaryArray;
}

/*
Read the list of strings in dictionary
*/
int readDictionaryStringList(DICTENTRYPOINTER *aDictionaryArray, int aMaxDictionaryEntries, int *aCurrentIndexInDictionary, FILE *aResFile, ULONG aStringListIndex)
{
    UWORD loStringLength;
    char loReadString[256];
    char loPreviousString[256];
    int loReadSize;
    int loCounter = 0;
    
    if (fseek(aResFile, aStringListIndex, SEEK_SET) != 0)
    {
        printf("Failure to set the file position %ld when reading a dictionary string list!\n", aStringListIndex);
        return false;
    }

    for(;;)
    {
        loReadSize = fread( &loStringLength, 1, sizeof(UWORD), aResFile);
        if (loReadSize != sizeof(UWORD))
        {
            printf("Failure to read the string length from the dictionary!\n");
            return false;
        }
        if (loStringLength == 0)
        {
            // end of the string list
            break;
        }
        loReadSize = fread( loReadString, 1, loStringLength, aResFile);
        if (loReadSize != loStringLength)
        {
            printf("Failure to read the string from the dictionary!\n");
            return false;
        }                
        loReadString[loStringLength] = '\0';
        //printf("%s\n", loReadString);
        loCounter++;
        if (loCounter % 2 == 0)
        {
            if (aMaxDictionaryEntries == (*aCurrentIndexInDictionary))
            {
                printf("The max number of the entries in this dictionary is %d!\n", aMaxDictionaryEntries);
                return false;
            }            
            storeIntoDictionaryArray(aDictionaryArray, aCurrentIndexInDictionary, loPreviousString, loReadString);            
        }
        else
        {
            // store the string for the future
            strcpy(loPreviousString, loReadString);
        }
    }

    return true;
}

/*
Stores data into dictionary
*/
void storeIntoDictionaryArray(DICTENTRYPOINTER *aDictionaryArray, int *aCurrentIndexInDictionary, char *aFirstString, char *aSecondString)
{
    aDictionaryArray[*aCurrentIndexInDictionary] = (DICTENTRYPOINTER)malloc(sizeof(struct INTERNAL_DICTIONARY_ENTRY));
    aDictionaryArray[*aCurrentIndexInDictionary]->first = makeString(aFirstString);
    aDictionaryArray[*aCurrentIndexInDictionary]->second = makeString(aSecondString);
    (*aCurrentIndexInDictionary)++;
}

/*
Used by qsort for comparing dictionary items
*/
int compareAccordingToSecondNumber (void const *aFirstItem, void const *aSecondItem)
{
  DICTENTRYPOINTER loFirstEntry = *(DICTENTRYPOINTER *)aFirstItem;
  DICTENTRYPOINTER loSecondEntry = *(DICTENTRYPOINTER *)aSecondItem;  
    
  int loTemp;
  int aFirstNumber = atoi(loFirstEntry->second);
  int aSecondNumber = atoi(loSecondEntry->second);
  if (aFirstNumber == 0 && strcmp(loFirstEntry->second,"0") != 0)
  {
    printf("compareAccordingToSecondNumber: loFirstEntry->second is not a number for the loFirstEntry->first: %s!\n",
        loFirstEntry->first);
  }
  if (aSecondNumber == 0 && strcmp(loSecondEntry->second,"0") != 0)
  {
    printf("compareAccordingToSecondNumber: loSecondEntry->second is not a number for the loSecondEntry->first: %s!\n",
        loSecondEntry->first);
  }
  
  loTemp  = aFirstNumber - aSecondNumber;
  if (loTemp > 0)
    return 1;
  else if (loTemp < 0)
    return -1;
  else
    return 0;
}

/*
Used by qsort for comparing dictionary items
*/
int compareAccordingToSecondString (void const *aFirstItem, void const *aSecondItem)
{
  DICTENTRYPOINTER loFirstEntry = *(DICTENTRYPOINTER *)aFirstItem;
  DICTENTRYPOINTER loSecondEntry = *(DICTENTRYPOINTER *)aSecondItem;  
  
  return strcmp(loFirstEntry->second, loSecondEntry->second);
}

/*
Used by qsort for comparing dictionary items
*/
int compareAccordingToFirstString (void const *aFirstItem, void const *aSecondItem)
{
  DICTENTRYPOINTER loFirstEntry = *(DICTENTRYPOINTER *)aFirstItem;
  DICTENTRYPOINTER loSecondEntry = *(DICTENTRYPOINTER *)aSecondItem;  
  
  return strcmp(loFirstEntry->first, loSecondEntry->first);
}

/*
Gets the numer of items in the dictionary
*/
int getNumberOfItems(DICTENTRYPOINTER *aArray)
{
    int loItems = 0;
    while (aArray[loItems] != NULL)
    {
        loItems++;
        if (loItems >  MAX_NUMBER_OF_DICTIONARY_ITEMS)
        {
            // this is suspicious, probably an error
            return -1;
        }  
    }
    return loItems;    
}

/*
Sorts dictionary according to the second item interpreted as a number
*/
int sortDictionaryAccordingToSecondNumber(DICTENTRYPOINTER *aArray)
{
    int loItems;
    if (aArray == NULL)
    {
        printf("The dictionary array is NULL!\n");
        return false;
    }
    loItems = getNumberOfItems(aArray);
    if (loItems == -1)
    {
        printf("Unable to determine the number of items when sorting!");
        return false;
    }
    qsort (aArray, loItems, sizeof(DICTENTRYPOINTER), compareAccordingToSecondNumber);
    return true;
}

/*
Sorts dictionary according to the second item
*/
int sortDictionaryAccordingToSecondString(DICTENTRYPOINTER *aArray)
{
    int loItems;
    if (aArray == NULL)
    {
        printf("The dictionary array is NULL!\n");
        return false;
    }
    loItems = getNumberOfItems(aArray);
    if (loItems == -1)
    {
        printf("Unable to determine the number of items when sorting!");
        return false;
    }
    qsort (aArray, loItems, sizeof(DICTENTRYPOINTER), compareAccordingToSecondString);
    return true;
}

/*
Sorts dictionary according to the first item
*/
int sortDictionaryAccordingToFirstString(DICTENTRYPOINTER *aArray)
{
    int loItems;
    if (aArray == NULL)
    {
        printf("The dictionary array is NULL!\n");
        return false;
    }
    loItems = getNumberOfItems(aArray);
    if (loItems == -1)
    {
        printf("Unable to determine the number of items when sorting!");
        return false;
    }
    qsort (aArray, loItems, sizeof(DICTENTRYPOINTER), compareAccordingToFirstString);
    return true;
}


/*
Display the content of the dictionary
*/
void displayDictionary(char *aText, char *aHeader, char *aFormat, FILE *aOutputFile,
    DICTENTRYPOINTER *aDictionary, int aSecondIsDecimalNumber)
{
    int i;
    char loFormat[256];

    if (aText != NULL)
    {
        fprintf(aOutputFile,"%s\n", aText) ;
    }
    if (aHeader != NULL)
    {
        fprintf(aOutputFile,"%s\n", aHeader) ;
    }
        
    if (aDictionary == NULL)
    {
        fprintf(aOutputFile, "The dictionary pointer is NULL!\n");
        return;
    }

    if (aFormat != NULL)
    {
        strcpy(loFormat, aFormat);        
    }
    else
    {
        strcpy(loFormat, "%30s     %20s");        
    }
    strcat(loFormat, "\n");
    
    for( i = 0; i < MAX_NUMBER_OF_DICTIONARY_ITEMS && aDictionary[i] != NULL; i++)
    {
        char loTmp[256];
        if (aSecondIsDecimalNumber == true)
        {
            if (strchr(aDictionary[i]->second, ',') != NULL)
            {
                // not a number
                strcpy(loTmp, aDictionary[i]->second);
            }
            else
            {
                // probably a number
                int loNum = atoi(aDictionary[i]->second);
                if (loNum == 0 && strcmp(aDictionary[i]->second, "0") != 0)
                {
                    // not a number
                    strcpy(loTmp, aDictionary[i]->second);
                }
                else
                {
                    // number
                    sprintf(loTmp, "%4s  (%04x)", aDictionary[i]->second, loNum);
                }
            }
        }
        else
        {
            strcpy(loTmp, aDictionary[i]->second);            
        }
        fprintf(aOutputFile, loFormat, aDictionary[i]->first, loTmp);
    }
    return;
}

/*
Get the resource name for the specified resource number
*/
char *getResourceName(char *aResult, int aResourceNumber)
{
    int i;
    int loNumberOfItems;
    if (myResourceNamesDictionary == NULL)
    {
        printf("The resource names dictionary was not initialized yet!\n");
        return NULL;       
    }
    loNumberOfItems = getNumberOfItems(myResourceNamesDictionary);
    for( i = 0; i < loNumberOfItems; i++)
    {
        char *loNumberString = myResourceNamesDictionary[i]->second;
        int loNumber = atoi(loNumberString);
        if (loNumber == 0 && strcmp(loNumberString,"0") != 0)
        {
            printf("Failure to convert the resource number: %s!\n", loNumberString);
            return NULL;                             
        }
        if (loNumber == aResourceNumber)
        {
            // result
            strcpy(aResult, myResourceNamesDictionary[i]->first);
            return aResult;
        }
    }
    printf("The resource name was not found for the resource number %d!\n", aResourceNumber);
    return NULL;
}

/*
Get the table with the names of resources
*/
DICTENTRYPOINTER *getResourceNameArray(FILE *aResFile, DIRPOINTER *aDirectoryPointers)
{
    int loMaxResourceEntries;
    DICTENTRYPOINTER *loResult;
    int loBehindTheEnd;

    if (myResourceNamesDictionary != NULL)
    {
        // it was already read before
        return myResourceNamesDictionary;
    }

    // read it
    printf("Reading resource name array ...\n");
    loMaxResourceEntries = getMaxNumberOfResourceEntries(aDirectoryPointers);

    // there should be space for special tables (they are also in directory), adding of NUMBER_OF_SPECIAL_TABLES in not needed
    loResult = readTheDictionary(0, loMaxResourceEntries + NUMBER_OF_SPECIAL_TABLES, aResFile, aDirectoryPointers, NULL);
    if (loResult == NULL)
    {
        printf("The attemp to read the resource name files failed!\n");
    }

    // add info about 5 special tables
    loBehindTheEnd = getNumberOfItems(loResult);
    storeIntoDictionaryArray(loResult, &loBehindTheEnd, makeString("Special table 0: Resource names"), makeString("0"));
    storeIntoDictionaryArray(loResult, &loBehindTheEnd, makeString("Special table 1 "), makeString("1"));
    storeIntoDictionaryArray(loResult, &loBehindTheEnd, makeString("Special table 2 "), makeString("2"));
    storeIntoDictionaryArray(loResult, &loBehindTheEnd, makeString("Special table 3: Low level functions"), makeString("3"));
    storeIntoDictionaryArray(loResult, &loBehindTheEnd, makeString("Special table 4: Message names"), makeString("4"));
        
    sortDictionaryAccordingToSecondNumber(loResult);
    printf("The reading of the resource name array finished.\n");
    myResourceNamesDictionary = loResult;
    return loResult;
} 

/*
Read the raw import dictionary (just what is in the resource, not resolving anything)
*/
DICTENTRYPOINTER *getRawImportArray(int aImportResourceNumber, FILE *aResFile,
    DIRPOINTER *aDirectoryPointers, int *aImportResourceSize)
{
    DICTENTRYPOINTER *loResult;
    printf("Reading of the import array from the resource entry %d started ...\n", aImportResourceNumber);
    loResult = readTheDictionary(aImportResourceNumber, MAX_IMPORT_TABLE_ITEMS, aResFile,
        aDirectoryPointers, aImportResourceSize);
    printf("The reading of the import array finished.\n");    
    return loResult;
}

/*
Get the full import array (resolve the import entry items)
*/
IMPORTENTRYPOINTER *getFullImportArray(DICTENTRYPOINTER *aRawImportArray, FILE *aResFile, DIRPOINTER *aDirectoryPointers)
{
    IMPORTENTRYPOINTER *loResult;
    int loNumberOfItems;
    int loImportArraySizeInBytes;
    int loAllocatedImportEntries;
    int i;

    printf("Conversion of the raw import array started ...\n");    
    if (aRawImportArray == NULL)
    {
        printf("The raw import array is NULL!\n");
        return NULL;
    }    
    loNumberOfItems = getNumberOfItems(aRawImportArray);
    if (loNumberOfItems == -1)
    {
        printf("Unable to determine the number of the items in the raw import array!");
        return NULL;
    }

    // at the end of the array there is always NULL element
    loAllocatedImportEntries = loNumberOfItems + 1;
    loImportArraySizeInBytes = loAllocatedImportEntries * sizeof(struct INTERNAL_IMPORT_ENTRY);
    loResult = (IMPORTENTRYPOINTER *)malloc(loImportArraySizeInBytes);

    if (loResult == NULL)
    {
        printf("The allocation of the array for the import array with the size %d failed!\n", loAllocatedImportEntries);
        return NULL;
    }

    // set everything to NULL
    for(i = 0; i < loAllocatedImportEntries; i++)
    {
        loResult[i] = NULL;
    }

    // resolve messages
    for( i = 0; i < loNumberOfItems; i++)
    {
        char loType;
        char *loCommaPos;
        char *loFirst;
        char *loSecond;
        loFirst = aRawImportArray[i]->first;                        
        loSecond = aRawImportArray[i]->second;                
        loResult[i] = (IMPORTENTRYPOINTER)malloc(sizeof(struct INTERNAL_IMPORT_ENTRY));
        loResult[i]->importType = EXPORT_ENTRY_UNDEFINED;
        loResult[i]->firstOriginal = makeString(loFirst);
        loResult[i]->secondOriginal = makeString(loSecond);
        loResult[i]->runtimeFunctionNumber = -1;
        loResult[i]->importedVariableNumber = -1;
        loResult[i]->originalResourceNumber = -1;
        loResult[i]->originalResourceName = NULL;                

        loType = loFirst[0];
        loCommaPos = strchr(loSecond, ',');
      
        if (loType == 'C' && loCommaPos == NULL)
        {
            // runtime function
            int loRuntimeFunctionNumber;
            loResult[i]->importType = IMPORT_ENTRY_RUNTIME_FUNCTION;
            loRuntimeFunctionNumber = atoi(loSecond);
            if (loRuntimeFunctionNumber == 0 && strcmp(loSecond, "0") != 0)
            {
                printf("Error when converting the runtime function number %s from string to a number!\n",
                    loSecond);
                loRuntimeFunctionNumber = -1;
            }
            loResult[i]->runtimeFunctionNumber = loRuntimeFunctionNumber;     
        }
        else if ((loType == 'B' || loType == 'W' || loType == 'L') && loCommaPos != NULL)
        {
            // imported variable (simple or array - we cannot distinguis it yet)
            int loImportedVariableNumber;
            int loOriginalResourceNumber;
            char loTmp[256];
            int loNumberLength;            

            if (loType == 'B')
            {
                loResult[i]->importType = IMPORT_ENTRY_BYTE;
            }
            else if (loType == 'W')
            {
                loResult[i]->importType = IMPORT_ENTRY_WORD;
            }
              else if (loType == 'L')
            {
                loResult[i]->importType = IMPORT_ENTRY_LONG;
            }
                                                
            // convert the imported variable number
            loNumberLength = loCommaPos - loSecond;
            strncpy(loTmp, loSecond, loNumberLength);
            loTmp[loNumberLength] = '\0';
            loImportedVariableNumber = atoi(loTmp);
            if (loImportedVariableNumber == 0 && strcmp(loTmp,"0") != 0)
            {
                printf("Failure to convert the imported variable number: %s!\n", loTmp);                 
            }
            else
            {
                // set the value
                loResult[i]->importedVariableNumber = loImportedVariableNumber;
            }
            // convert the number of the resource from which the variable is imported
            strcpy(loTmp, loCommaPos + 1);
            loOriginalResourceNumber = atoi(loTmp);
            if (loOriginalResourceNumber == 0 && strcmp(loTmp,"0") != 0)
            {
                printf("Failure to convert the resource number from which the variable is imported: %s!\n", loTmp);                 
            }
            else
            {
                // set the value
                char loOriginalResourceName[256];
                loResult[i]->originalResourceNumber = loOriginalResourceNumber;
                if (getResourceName(loOriginalResourceName, loOriginalResourceNumber) == NULL)                    
                {
                    printf("Failure to find the name from for the resource %d from which the variable %s is imported!\n",
                            loOriginalResourceNumber, loFirst);
                    strcpy(loOriginalResourceName,"__unknown_resource_name__"); 
                }
                loResult[i]->originalResourceName = makeString(loOriginalResourceName);
            }
            
            //printf("\nImported variable original entries: %s   %s\n", loFirst, loSecond);
            //printf("Imported variable decoded:\n  number:%d resource number: %d resource name: %s\n\n",
            //        loResult[i]->importedVariableNumber, loResult[i]->originalResourceNumber,
            //        ((loResult[i]->originalResourceName == NULL)?"NULL":loResult[i]->originalResourceName));
        }
        else
        {
            printf("Unknown import entry type: %s %s!\n", loFirst, loSecond);
        }                                   
    }
    printf("Conversion of the raw import array finished.\n");
    return loResult;
}

/*
Read the raw export dictionary (just what is in the resource, not resolving anything)
*/
DICTENTRYPOINTER *getRawExportArray(int aExportResourceNumber, FILE *aResFile,
    DIRPOINTER *aDirectoryPointers, int *aExportResourceSize)
{
    DICTENTRYPOINTER *loResult;
    printf("Reading of the export array from the resource entry %d started ...\n", aExportResourceNumber);
    loResult = readTheDictionary(aExportResourceNumber, MAX_EXPORT_TABLE_ITEMS, aResFile,
        aDirectoryPointers, aExportResourceSize);
    printf("The reading of the export array finished.\n");    
    return loResult;
}

/*
Get the full export array (resolve the export entry items)
*/
EXPORTENTRYPOINTER *getFullExportArray(DICTENTRYPOINTER *aRawExportArray, FILE *aResFile, DIRPOINTER *aDirectoryPointers)
{
    EXPORTENTRYPOINTER *loResult;
    int loNumberOfItems;
    int loExportArraySizeInBytes;
    int loAllocatedExportEntries;
    int i;

    printf("Conversion of the raw export array started ...\n");    
    if (aRawExportArray == NULL)
    {
        printf("The raw export array is NULL!\n");
        return NULL;
    }    
    loNumberOfItems = getNumberOfItems(aRawExportArray);
    if (loNumberOfItems == -1)
    {
        printf("Unable to determine the number of the items in the raw export array!");
        return NULL;
    }

    // at the end of the array there is always NULL element
    loAllocatedExportEntries = loNumberOfItems + 1;
    loExportArraySizeInBytes = loAllocatedExportEntries * sizeof(struct INTERNAL_EXPORT_ENTRY);
    loResult = (EXPORTENTRYPOINTER *)malloc(loExportArraySizeInBytes);

    if (loResult == NULL)
    {
        printf("The allocation of the array for the export array with the size %d failed!\n", loAllocatedExportEntries);
        return NULL;
    }

    // set everything to NULL
    for(i = 0; i < loAllocatedExportEntries; i++)
    {
        loResult[i] = NULL;
    }

    // resolve messages
    for( i = 0; i < loNumberOfItems; i++)
    {
        char loType;
        char *loCommaPos;
        char *loFirst;
        char *loSecond;
        loFirst = aRawExportArray[i]->first;                        
        loSecond = aRawExportArray[i]->second;
                
        loResult[i] = (EXPORTENTRYPOINTER)malloc(sizeof(struct INTERNAL_EXPORT_ENTRY));
        loResult[i]->exportType = EXPORT_ENTRY_UNDEFINED;
        loResult[i]->firstOriginal = makeString(loFirst);
        loResult[i]->secondOriginal = makeString(loSecond);
        loResult[i]->messageHandlerStart = -1;
        loResult[i]->messageHandlerName = NULL;
        loResult[i]->objectName = NULL;
        loResult[i]->parentResourceNumber = -1;
        loResult[i]->parentResourceName = NULL;
        loResult[i]->exportedVariablePosition = -1;
        loResult[i]->exportedArrayPosition = -1;
        loResult[i]->exportedArraySizeInElements = -1;               

        loType = loFirst[0];
        loCommaPos = strchr(loSecond, ',');
        
        if (strcmp(loFirst,"N:OBJECT") == 0)
        {
            loResult[i]->exportType = EXPORT_ENTRY_OBJECT_NAME;
            loResult[i]->objectName = makeString(loSecond);   
        }
        else if (strcmp(loFirst,"N:PARENT") == 0)
        {
            int loParentResourceNumber;
            char loParentResourceName[256];
            loResult[i]->exportType = EXPORT_ENTRY_PARENT_NUMBER;
            loParentResourceNumber = atoi(loSecond);
            if (loParentResourceNumber == 0 && strcmp(loSecond, "0") != 0)
            {
                // failed numeric conversion
                loParentResourceNumber = -1;
                printf("Failure to convert the parent number %s in the export resource!\n",
                    loSecond);
            }
            else
            {
                // assign value
                loResult[i]->parentResourceNumber = loParentResourceNumber;
            }
            if (loParentResourceNumber == -1 || getResourceName(loParentResourceName, loParentResourceNumber) == NULL)
            {
                strcpy(loParentResourceName, "__unknown_parent_name__");
            }
            loResult[i]->parentResourceName = makeString(loParentResourceName);   
        }                
        else if (loType == 'M' && loCommaPos == NULL)
        {
            // it is a message
            char loMessageName[256];
            int loMessageHandlerStart;
            loResult[i]->exportType = EXPORT_ENTRY_MESSAGE_HANDLER;
            if (getMessageName(loMessageName, loFirst, aResFile, aDirectoryPointers) != NULL)
            {
                loResult[i]->messageHandlerName = makeString(loMessageName);                
            }
            else
            {
                printf("Failure to resolve the message hadler reference %s in the export resource!\n", aRawExportArray[i]->first);
                loResult[i]->messageHandlerName = makeString("__unknown_message_handler__");
            }
            loMessageHandlerStart = atoi(loSecond);
            if (loMessageHandlerStart == 0 && strcmp(loSecond, "0") != 0)
            {
                // failed numeric conversion
                printf("Failure to convert the message handler start position %s in the export resource!\n",
                    loSecond);
                loMessageHandlerStart = - 1;
            }
            loResult[i]->messageHandlerStart = loMessageHandlerStart;            
        }
        else if ((loType == 'B' || loType == 'W' || loType == 'L') && loCommaPos == NULL)
        {
            // simple exported variables (not arrays)
            int loExportedVariablePosition;

            if (loType == 'B')
            {
                loResult[i]->exportType = EXPORT_ENTRY_BYTE;
            }
            else if (loType == 'W')
            {
                loResult[i]->exportType = EXPORT_ENTRY_WORD;
            }
              else if (loType == 'L')
            {
                loResult[i]->exportType = EXPORT_ENTRY_LONG;
            }                                    
                      
            loExportedVariablePosition = atoi(loSecond);
            if (loExportedVariablePosition == 0 && strcmp(loSecond,"0") != 0)
            {
                printf("Failure to convert the exported variable position: %s!\n", loSecond);                 
            }
            else
            {
                // set the value
                loResult[i]->exportedVariablePosition = loExportedVariablePosition;
            }

            //printf("\nExported variable original entries: %s   %s\n", loFirst, loSecond);
            //printf("Exported variable decoded - position: %d\n\n", loResult[i]->exportedVariablePosition);
        }
        else if ((loType == 'B' || loType == 'W' || loType == 'L') && loCommaPos != NULL)
        {
            // exported arrays
            int loExportedArrayPosition;
            int loExportedArraySizeInElements;
            int loNumberLength;
            char loTmp[256];

            if (loType == 'B')
            {
                loResult[i]->exportType = EXPORT_ENTRY_ARRAY_BYTE;
            }
            else if (loType == 'W')
            {
                loResult[i]->exportType = EXPORT_ENTRY_ARRAY_WORD;
            }
              else if (loType == 'L')
            {
                loResult[i]->exportType = EXPORT_ENTRY_ARRAY_LONG;
            }
            // convert the exported array position
            loNumberLength = loCommaPos - loSecond;
            strncpy(loTmp, loSecond, loNumberLength);
            loTmp[loNumberLength] = '\0';
            loExportedArrayPosition = atoi(loTmp);
            if (loExportedArrayPosition == 0 && strcmp(loTmp,"0") != 0)
            {
                printf("Failure to convert the exported array position: %s!\n", loTmp);                 
            }
            else
            {
                // set the value
                loResult[i]->exportedArrayPosition = loExportedArrayPosition;
            }
            // convert the number of the resource from which the variable is imported
            strcpy(loTmp, loCommaPos + 1);
            loExportedArraySizeInElements = atoi(loTmp);
            if (loExportedArraySizeInElements == 0 && strcmp(loTmp,"0") != 0)
            {
                printf("Failure to convert the size of the exported array: %s!\n", loTmp);                 
            }
            else
            {
                // set the value
                loResult[i]->exportedArraySizeInElements = loExportedArraySizeInElements;
            }
            //printf("\nExported array original entries: %s   %s\n", loFirst, loSecond);
            //printf("Exported array decoded - position: %d size in elements: %d\n\n",
            //    loResult[i]->exportedArrayPosition, loResult[i]->exportedArraySizeInElements);            
            
        }        
        else
        {
            printf("Unknown export entry type: %s %s!\n", loFirst, loSecond);
        }
    }

    printf("Conversion of the raw export array finished.\n");
    return loResult;
}

/*
Displays the import dictionary
*/
void displayImportDictionary(char *aImportResourceName, int aImportResourceNumber, FILE *aOutputFile,
    IMPORTENTRYPOINTER *aFullImportResourceDictionary, int aSecondIsDecimalNumber)
{
    int i;
    int loPreviouslyDisplayedType;
    fprintf(aOutputFile, "\n*** IMPORT DICTIONARY (resource name %s, resource number: %d) ***\n", aImportResourceName, aImportResourceNumber) ;
    if (aSecondIsDecimalNumber == true)
    {
        fprintf(aOutputFile, "(for some decadic numbers their hexadecimal values are provided in () )\n");
    }
    if (aFullImportResourceDictionary == NULL)
    {
        fprintf(aOutputFile, "The import dictionary pointer is NULL!\n");
        return;
    }
    loPreviouslyDisplayedType = IMPORT_DISPLAY_NOTHING;    
    for( i = 0; i < MAX_IMPORT_TABLE_ITEMS && aFullImportResourceDictionary[i] != NULL; i++)
    { 
        IMPORTENTRYPOINTER loCurrentImportEntry;
        int loCurrentImportEntryType;
        int loCurrentlyDisplayedType;
        char loFirst[256];
        char loSecond[256];
        char loTmp[256];
        char loTmp2[256];
        
        loCurrentImportEntry = aFullImportResourceDictionary[i];
        loCurrentImportEntryType = loCurrentImportEntry->importType;
        sprintf(loFirst, "%s", loCurrentImportEntry->firstOriginal);
        sprintf(loSecond, "%s", loCurrentImportEntry->secondOriginal);         

        // the second can be a decimal number and we may want to add a hexadecimal value to it
        if (aSecondIsDecimalNumber == true && strchr(loCurrentImportEntry->secondOriginal, ',') == NULL)
        {
            // probably a number            
            int loNum = atoi(loCurrentImportEntry->secondOriginal);
            if (loNum == 0 && strcmp(loCurrentImportEntry->secondOriginal, "0") != 0)
            {
                // not a number, do not change the value
            }
            else
            {
                // number
                sprintf(loSecond, "%4s  (%04x)", loCurrentImportEntry->secondOriginal, loNum);
            }
        }
                   
        switch(loCurrentImportEntryType)
        {
            case IMPORT_ENTRY_RUNTIME_FUNCTION:
                loCurrentlyDisplayedType = IMPORT_DISPLAY_RUNTIME;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* RUNTIME FUNCTIONS:\n");
                    fprintf(aOutputFile, "%-35s     %-10s\n", "FIRST ENTRY (FUNCTION)", "SECOND ENTRY (NUMBER)");                                        
                }
                sprintf(loTmp, "\"%s\"", loFirst);
                fprintf(aOutputFile, "%-35s     %-10s\n", loTmp, loSecond);                 
                break;
            case IMPORT_ENTRY_BYTE:
            case IMPORT_ENTRY_WORD:
            case IMPORT_ENTRY_LONG:
            case IMPORT_ENTRY_ARRAY_BYTE:
            case IMPORT_ENTRY_ARRAY_WORD:
            case IMPORT_ENTRY_ARRAY_LONG:
                // The fact whether the imported variable is a simple variable or an array can be determined
                // only during disassembly (by checking instructions working with it).
                // Therefore it is not differentiated here.
                // (At the moment the import dictionary is shown before disassembly, so entries are
                // not marked properly yet).
                // (Well, the simple/array question could be also decided by checking the export resource
                // for the original resource which exports the variable - do it in future!)
                loCurrentlyDisplayedType = IMPORT_DISPLAY_VARIABLE;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* IMPORTED VARIABLES (not differentiating between simple variables and arrays!):\n");
                    fprintf(aOutputFile, "%-20s  %-26s  %-20s\n",
                        "FIRST ENTRY (NAME)", "SECOND ENTRY (NUMBER,FROM)", "ORIGINAL RESOURCE NAME");                                        
                }
                sprintf(loTmp, "\"%s\"", loFirst);
                sprintf(loTmp2, "\"%s\"", loCurrentImportEntry->originalResourceName);
                fprintf(aOutputFile, "%-20s  %-26s  %-20s\n", loTmp, loSecond, loTmp2); 
                break;
          default:
                loCurrentlyDisplayedType = IMPORT_DISPLAY_UNKNOWN;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* UNKNOWN IMPORT ENTRIES:\n");
                    fprintf(aOutputFile, "%30s     %30s\n", "FIRST ENTRY", "SECOND ENTRY");                                        
                }
                fprintf(aOutputFile, "%30s     %30s\n", loFirst, loSecond); 
                break;                    
        }
        loPreviouslyDisplayedType = loCurrentlyDisplayedType;
    }
    return;    
}

/*
Displays the export dictionary
*/
void displayExportDictionary(char *aExportResourceName, int aExportResourceNumber, FILE *aOutputFile,
    EXPORTENTRYPOINTER *aFullExportResourceDictionary,  int aSecondIsDecimalNumber)
{
    int i;
    int loPreviouslyDisplayedType;
    fprintf(aOutputFile,"\n*** EXPORT DICTIONARY (resource name %s, resource number: %d) ***\n", aExportResourceName, aExportResourceNumber) ;
    if (aSecondIsDecimalNumber == true)
    {
        fprintf(aOutputFile, "(for some decadic numbers their hexadecimal values are provided in () )\n");
    }
    if (aFullExportResourceDictionary == NULL)
    {
        fprintf(aOutputFile, "The export dictionary pointer is NULL!\n");
        return;
    }

    loPreviouslyDisplayedType = EXPORT_DISPLAY_NOTHING;    
    for( i = 0; i < MAX_EXPORT_TABLE_ITEMS && aFullExportResourceDictionary[i] != NULL; i++)
    { 
        EXPORTENTRYPOINTER loCurrentExportEntry;
        int loCurrentExportEntryType;
        int loCurrentlyDisplayedType;
        char loFirst[256];
        char loSecond[256];
        char loTmp[256];
        
        loCurrentExportEntry = aFullExportResourceDictionary[i];
        loCurrentExportEntryType = loCurrentExportEntry->exportType;
        sprintf(loFirst, "%s", loCurrentExportEntry->firstOriginal);
        sprintf(loSecond, "%s", loCurrentExportEntry->secondOriginal);         

        // the second can be a decimal number and we may want to add a hexadecimal value to it
        if (aSecondIsDecimalNumber == true && strchr(loCurrentExportEntry->secondOriginal, ',') == NULL)
        {
            // probably a number            
            int loNum = atoi(loCurrentExportEntry->secondOriginal);
            if (loNum == 0 && strcmp(loCurrentExportEntry->secondOriginal, "0") != 0)
            {
                // not a number, do not change the value
            }
            else
            {
                // number
                sprintf(loSecond, "%4s  (%04x)", loCurrentExportEntry->secondOriginal, loNum);
            }
        }

        switch(loCurrentExportEntryType)
        {
            case EXPORT_ENTRY_OBJECT_NAME:
                loCurrentlyDisplayedType = EXPORT_DISPLAY_NAME;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* OBJECT NAME:\n");
                    fprintf(aOutputFile, "%-20s  %-20s\n", "FIRST ENTRY", "SECOND ENTRY (NAME)");                                        
                }
                sprintf(loTmp, "\"%s\"", loSecond);
                fprintf(aOutputFile, "%-20s  %-20s\n", loFirst, loTmp);                 
                break;
            case EXPORT_ENTRY_PARENT_NUMBER:
                loCurrentlyDisplayedType = EXPORT_DISPLAY_PARENT;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* PARENT NUMBER:\n");
                    fprintf(aOutputFile, "%-20s  %-20s  %-20s\n", "FIRST ENTRY", "SECOND ENTRY", "NAME");                                        
                }
                sprintf(loTmp, "\"%s\"", loCurrentExportEntry->parentResourceName);
                fprintf(aOutputFile, "%-20s  %-20s  %-20s\n", loFirst, loSecond, loTmp);                 
                break;
            case EXPORT_ENTRY_MESSAGE_HANDLER:
                loCurrentlyDisplayedType = EXPORT_DISPLAY_HANDLER;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* MESSAGE HANDLERS:\n");
                    fprintf(aOutputFile, "%-20s  %-20s  %-30s\n", "FIRST ENTRY", "SECOND ENTRY (START)", "NAME");                                        
                }
                sprintf(loTmp, "\"%s\"", loCurrentExportEntry->messageHandlerName);
                fprintf(aOutputFile, "%-20s  %-20s  %-30s\n", loFirst, loSecond, loTmp);                 
                break;                            
            case EXPORT_ENTRY_BYTE:
            case EXPORT_ENTRY_WORD:
            case EXPORT_ENTRY_LONG:
                loCurrentlyDisplayedType = EXPORT_DISPLAY_VARIABLE;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* EXPORTED VARIABLES:\n");
                    fprintf(aOutputFile, "%-20s  %-20s\n",
                        "FIRST ENTRY (NAME)", "SECOND ENTRY (POSITION)");                                        
                }
                sprintf(loTmp, "\"%s\"", loFirst);
                fprintf(aOutputFile, "%-20s  %-20s\n", loTmp, loSecond); 
                break;        
            case EXPORT_ENTRY_ARRAY_BYTE:
            case EXPORT_ENTRY_ARRAY_WORD:
            case EXPORT_ENTRY_ARRAY_LONG:
                loCurrentlyDisplayedType = EXPORT_DISPLAY_ARRAY;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* EXPORTED ARRAYS:\n");
                    fprintf(aOutputFile, "%-20s  %-20s\n",
                        "FIRST ENTRY (NAME)", "SECOND ENTRY (POSITION, ELEMENTS)");                                        
                }
                sprintf(loTmp, "\"%s\"", loFirst);
                fprintf(aOutputFile, "%-20s  %-20s\n", loTmp, loSecond); 
                break;
          default:
                loCurrentlyDisplayedType = EXPORT_DISPLAY_UNKNOWN;
                if (loCurrentlyDisplayedType != loPreviouslyDisplayedType)
                {
                    // header
                    fprintf(aOutputFile, "* UNKNOWN EXPORT ENTRIES:\n");
                    fprintf(aOutputFile, "%30s     %30s\n", "FIRST ENTRY", "SECOND ENTRY");                                        
                }
                fprintf(aOutputFile, "%30s     %30s\n", loFirst, loSecond); 
                break;                    
        }
        loPreviouslyDisplayedType = loCurrentlyDisplayedType;
    }
    return;    
}

/*
Read a special array (AESOP resources 0..4)
*/
DICTENTRYPOINTER *getSpecialArray(int aResourceNumber, FILE *aResFile, DIRPOINTER *aDirectoryPointers)
{
    DICTENTRYPOINTER *loResult;
    printf("Reading of the special array from the resource entry %d started ...\n", aResourceNumber);
    loResult = readTheDictionary(aResourceNumber, MAX_NUMBER_OF_DICTIONARY_ITEMS, aResFile, aDirectoryPointers, NULL);
    printf("The reading of the special array finished.\n");
    return loResult;        
}

/*
Gets message name for the export table string M:<number>
*/
char* getMessageName(char *aResult, char *aExportTableString, FILE *aResFile, DIRPOINTER *aDirectoryPointers)
{
    char loNumberString[256];
    int loNumber;
    int i;
    int loMessages;

    if (myMessageNamesDictionary == NULL)
    {
        if (readMessageNamesDictionary(aResFile, aDirectoryPointers) == NULL)
        {
            char *loError = "Unable to read the message names dictionary (resource 4)!\n";
            printf("%s\n", loError);
            return NULL;
        }        
    }    
    
    if (aExportTableString == NULL || strlen(aExportTableString) <= 2)
    {
        // NULL or too short
        printf("Error: getMessageName used on NULL or too short string!\n");
        return NULL;        
    }
    if ((aExportTableString[0] == 'M' && aExportTableString[1] == ':') == 0)
    {
        // not M:<number>
        printf("Error: getMessageName used on the string %s!\n", aExportTableString);
        return NULL;
    }
    strcpy(loNumberString, aExportTableString + 2);  // copy behind M:
    loNumber = atoi(loNumberString);
    if (loNumber == 0 && strcmp(loNumberString, "0") != 0)
    {
        printf("Error while converting the number %s from the import table reference %s\n", loNumberString, aExportTableString);
        return NULL;
    }
    loMessages = getNumberOfItems(myMessageNamesDictionary);
    if (loMessages == -1)
    {
        printf("Error counting the numbers of messafe name table!\n");
        return NULL;
    }
    for(i = 0; i < loMessages; i++)
    {
        int loTestedMsgNumber;
        loTestedMsgNumber = atoi(myMessageNamesDictionary[i]->second);
        if (loTestedMsgNumber == 0 && strcmp(myMessageNamesDictionary[i]->second, "0") != 0)
        {
            printf("Error converting the number %s from the message names table!\n", myMessageNamesDictionary[i]->second);
            continue;
        }
        if (loTestedMsgNumber == loNumber)
        {
            strcpy(aResult, myMessageNamesDictionary[i]->first); 
            return aResult;
        }        
    }
    printf("The message name was not found for the export table string: %s!\n", aExportTableString);
    return NULL;
}

/*
Read the message names dictionary
*/
DICTENTRYPOINTER *readMessageNamesDictionary(FILE *aResFile, DIRPOINTER *aDirectoryPointers)
{
    // read the array with the message names (resource 4)
    myMessageNamesDictionary = getSpecialArray(4, aResFile, aDirectoryPointers);
    return myMessageNamesDictionary;    
}

/*
Gets the information about resources
*/
RESINFOPOINTER *getResourcesInformationTable(FILE *aResFile, DIRPOINTER *aDirectoryPointers, int aLookForStringResources)
{
    int loNumberOfResources;
    int loNumberOfEntriesInTable1;
    int loNumberOfEntriesInTable2;
    RESINFOPOINTER *loResult;
    int loAllocatedInfoEntries;
    int loAllocatedInfoEntriesSizeInBytes;
    int i;
    DICTENTRYPOINTER *loResourceNameArray;
    DICTENTRYPOINTER *loResourceTable1;
    DICTENTRYPOINTER *loResourceTable2;    

    loResourceNameArray = getResourceNameArray(aResFile, aDirectoryPointers);
    if (loResourceNameArray == NULL)
    {
        printf("Unable to read the resource name dictionary (resource 0)!\n");
        return NULL;
    }

    // AESOP resource 1
    loResourceTable1 = getSpecialArray(1, aResFile, aDirectoryPointers);
    if (loResourceTable1 == NULL)
    {
        printf("Unable to read the special AESOP resource 1!\n");
        return NULL;
    }

    // AESOP resource 2
    loResourceTable2 = getSpecialArray(2, aResFile, aDirectoryPointers);
    if (loResourceTable2 == NULL)
    {
        printf("Unable to read the special AESOP resource 2\n");
        return NULL;        
    }
        
    loNumberOfResources = getNumberOfItems(loResourceNameArray);
    if (loNumberOfResources == -1)
    {
        printf("Failed to get the number of entries in the table of resource names.\n");
        return NULL;
    }
    loNumberOfEntriesInTable1 = getNumberOfItems(loResourceTable1);
    if (loNumberOfEntriesInTable1 == -1)
    {
        printf("Failed to get the number of entries in the resource table 1.\n");
        return NULL;
    }
    loNumberOfEntriesInTable2 = getNumberOfItems(loResourceTable2);
    if (loNumberOfEntriesInTable2 == -1)
    {
        printf("Failed to get the number of entries in the resource table 2.\n");
        return NULL;
    }
    printf("The number of entries in resource 0: %d, resource 1: %d, resource 2: %d\n",
            loNumberOfResources, loNumberOfEntriesInTable1, loNumberOfEntriesInTable2);

    // There is always at least one NULL entry (end marker)    
    loAllocatedInfoEntries = loNumberOfResources + 1;

    // allocate space for the dictionary array
    loAllocatedInfoEntriesSizeInBytes = loAllocatedInfoEntries*sizeof(struct INTERNAL_RESOURCE_INFO);
    loResult = (RESINFOPOINTER *)malloc(loAllocatedInfoEntriesSizeInBytes);
    if (loResult == NULL)
    {
        printf("The allocation of the resource info array with the size %d failed!\n",
            loAllocatedInfoEntries);
        return NULL;
    }
        
    // set everything to NULL
    for(i = 0; i < loAllocatedInfoEntries; i++)
    {
        loResult[i] = NULL;
    }

    // now make the entries
    for(i = 0; i < loNumberOfResources; i++)
    {
        
        RESINFOPOINTER loInfoEntry;
        char *loResourceName;
        char *loResourceNumberAsString;
        int loResourceNumber;
        char *loStringValue = NULL;
        
        loResourceName = loResourceNameArray[i]->first;
        loResourceNumberAsString = loResourceNameArray[i]->second;
        loResourceNumber = getResourceNumberFromNumberString(loResourceNumberAsString);
        if (loResourceNumber == -1)
        {
            printf("Unable to get the resource number for the resource: %s!\n", loResourceName);
            return NULL;
        }
        loInfoEntry = (RESINFOPOINTER)malloc(sizeof(struct INTERNAL_RESOURCE_INFO));
        if (loInfoEntry == NULL)
        {
            printf("The allocation of a resource info entry failed!\n");
            return NULL;
        }
        
        loInfoEntry->number = loResourceNumber;
        loInfoEntry->name = makeString(loResourceName);
        loInfoEntry->infoFromResource1 = getSecondEntryForTheFirstEntry(loResourceTable1, loResourceName);
        loInfoEntry->infoFromResource2 = getSecondEntryForTheFirstEntry(loResourceTable2, loResourceName);
        loInfoEntry->resourceType = getResourceType(aResFile, aDirectoryPointers, loResourceNameArray,
                loResourceNumber, loResourceName, loInfoEntry->infoFromResource1, loInfoEntry->infoFromResource2,
                aLookForStringResources, &loStringValue);
        if (loInfoEntry->resourceType == RESOURCE_TYPE_STRING && loStringValue != NULL)
        {
            loInfoEntry->stringValue = loStringValue;
        }
        else
        {
            loInfoEntry->stringValue = NULL;
        }
                
        loResult[i] = loInfoEntry;       
    }          
    return loResult;
}

/*
Gets the information for the specified resource name if available in the dictinary
*/
char* getSecondEntryForTheFirstEntry(DICTENTRYPOINTER *aResourceTable, char *aResourceName)
{
    char loResultBuffer[512] = "";
    int i;
    for (i = 0; i < MAX_NUMBER_OF_DICTIONARY_ITEMS && aResourceTable[i] != NULL; i++)
    {
        char *loFirst;
        char *loSecond;
        loFirst = aResourceTable[i]->first;
        loSecond = aResourceTable[i]->second;
        if (strcmp(aResourceName, loFirst) == 0)
        {
            // found a record
            if (strlen(loResultBuffer) > 0)
            {
                // I am not sure whether there can be more records for one resource, but let's handle it
                strcat(loResultBuffer, "; ");
            }
            if (strlen(loResultBuffer) + strlen(loSecond) >= 512)
            {
                // I hope this never happens
                printf("getSecondEntryForTheFirstEntry(): the result buffer is too small for all the resource information of the resource %s!\n",
                    aResourceName);
                return makeString(loResultBuffer);
            }
            // add it to the result buffer
            strcat(loResultBuffer, loSecond);
        }
    }
    if (strlen(loResultBuffer) == 0)
    {
        // nothing here
        return NULL;
    }
    else
    {
        return makeString(loResultBuffer);
    }
}

/*
Get resource type
*/
int getResourceType(FILE *aResFile, DIRPOINTER *aDirectoryPointers, DICTENTRYPOINTER *aResourceNameArray,
                int aResourceNumber, char *aResourceName, char *aInfoString1, char *aInfoString2,
                int aLookForStringResources, char **aFoundStringPointer)
{
    char loExportResource[256];
    char aInfoString1UpperCase[512];
    char aInfoString2UpperCase[512];    
    int i;

    if (aResourceNumber < NUMBER_OF_SPECIAL_TABLES)
    {
        return RESOURCE_TYPE_SPECIAL;
    }    

    // info string 1
    if (aInfoString1 != NULL)
    {
        strcpy(aInfoString1UpperCase, aInfoString1);
    }
    else
    {
        strcpy(aInfoString1UpperCase, "");
    }
    toUpperCase(aInfoString1UpperCase);

    if (strstr(aInfoString1UpperCase, PALETTE_PREFIX_UPPERCASE) == aInfoString1UpperCase)
    {
        // palette definition
        return RESOURCE_TYPE_PALETTE;
    }

    if (strstr(aInfoString1UpperCase, MAP_PREFIX_UPPERCASE) == aInfoString1UpperCase)
    {
        // EOB 3 map definition
        return RESOURCE_TYPE_MAP;
    }    

    // info string 2
    if (aInfoString2 != NULL)
    {
        strcpy(aInfoString2UpperCase, aInfoString2);
    }
    else
    {
        strcpy(aInfoString2UpperCase, "");
    }
    toUpperCase(aInfoString2UpperCase);
    
    // test file extensions
    if (stringEndsWith(aResourceName, IMPORT_EXTENSION) == true)
    {
        return RESOURCE_TYPE_IMPORT;
    }
    if (stringEndsWith(aResourceName, EXPORT_EXTENSION)== true)
    {
        return RESOURCE_TYPE_EXPORT;
    }
    if (stringEndsWith(aInfoString2UpperCase, BITMAP1_FILE_EXTENSION) == true ||
        stringEndsWith(aInfoString2UpperCase, BITMAP2_FILE_EXTENSION) == true)
    {
        return RESOURCE_TYPE_BITMAP;
    }
    if (stringEndsWith(aInfoString2UpperCase, SOUND_FILE_EXTENSION) == true)
    {
        return RESOURCE_TYPE_SOUND;
    }
    if (stringEndsWith(aInfoString2UpperCase, MUSIC_FILE_EXTENSION) == true)
    {
        return RESOURCE_TYPE_MUSIC;
    }
    if (stringEndsWith(aInfoString2UpperCase, FONT_FILE_EXTENSION) == true)
    {
        return RESOURCE_TYPE_FONT;
    }                
    strcpy(loExportResource, aResourceName);
    strcat(loExportResource, EXPORT_EXTENSION);
    for(i = 0; i < MAX_NUMBER_OF_DICTIONARY_ITEMS && aResourceNameArray[i] != NULL; i++)
    {
        if (strcmp(loExportResource, aResourceNameArray[i]->first) == 0)
        {
            // found a corresponding, so it is a code resource
            return RESOURCE_TYPE_CODE;
        }
    }

    if (aLookForStringResources == true)
    {
        // check whether it is not a string
        struct RESEntryHeader *loResEntryHeader;
        ULONG loDataSize;
        char *loBuffer;
        int loDummy;
        loResEntryHeader = getResourceEntryHeader(aResourceNumber, aResFile, aDirectoryPointers);
        if (loResEntryHeader == NULL)
        {
            printf("Unable to read the header for the resource number: %d!\n", aResourceNumber);
            return RESOURCE_TYPE_UNKNOWN;
        }
        loDataSize = loResEntryHeader->data_size;
        if (loDataSize < 2 && loDataSize > MAX_LENGTH_OF_TESTED_STRING_RESOURCE)
        {
            // too short or too long to be string, silently ignore
            free(loResEntryHeader);
            return RESOURCE_TYPE_UNKNOWN;
        }
        loBuffer = readResourceBinary(aResourceNumber, aResFile, aDirectoryPointers, &loDummy);
        if (loBuffer == NULL)
        {
            printf("The reading of the resource %d failed!\n", aResourceNumber);
            free(loResEntryHeader);
            return RESOURCE_TYPE_UNKNOWN;
        }
        if (loBuffer[0] == 'S' && loBuffer[1] == ':')
        {
            // it is a string
            if (aFoundStringPointer != 0)
            {
                *aFoundStringPointer = makeString(loBuffer + 2);
                if (*aFoundStringPointer == NULL)
                {
                    printf("Unable to allocate the space for the string read from the resource string: %d\n", aResourceNumber);
                }
            }
            free(loResEntryHeader);
            free(loBuffer);
            return RESOURCE_TYPE_STRING;
        }
        free(loResEntryHeader);
        free(loBuffer);               
    }      
    return RESOURCE_TYPE_UNKNOWN;
}

/*
Display info entries
*/
void displayResourcesInfoEntries(FILE *aOutputFile, RESINFOPOINTER *aResourcesInfoTable)
{
    int i;

    if (aResourcesInfoTable == NULL)
    {
        printf("The resources info table is NULL!\n");
        return;
    }
    displayResourcesTypeWarning(aOutputFile);
    displayResourcesTypeSummary(aResourcesInfoTable, aOutputFile);
    fprintf(aOutputFile, "\n*** INFORMATION ABOUT RESOURCES AND THEIR TYPES ***\n\n") ;
     fprintf(aOutputFile, "%-5s %-10s %-45s\n               %s\n               %s\n               %s\n\n",
        "NUM", "TYPE", "NAME", "INFO1 (if existing)", "INFO2 (if existing)", "STRING_VALUE (for strings, in \"\")");
    for(i = 0; i < MAX_NUMBER_OF_DICTIONARY_ITEMS && aResourcesInfoTable[i] != NULL; i++)
    {
        char loType[256];
        char *loName;
        int loNumber;
        char *loInfoFromResource1;
        char *loInfoFromResource2;
        char *loStringValue;
        int loAdditionalInfoPresent = false;
        getResourceTypeString(loType,  aResourcesInfoTable[i]->resourceType);
        loName = aResourcesInfoTable[i]->name;
        loNumber = aResourcesInfoTable[i]->number;
        loInfoFromResource1 = aResourcesInfoTable[i]->infoFromResource1;
        loInfoFromResource2 = aResourcesInfoTable[i]->infoFromResource2;
        loStringValue = aResourcesInfoTable[i]->stringValue;
        if (loInfoFromResource1 != NULL || loInfoFromResource1 != NULL)
        {
            loAdditionalInfoPresent = true;
        }
        // basic info        
        fprintf(aOutputFile, "%-5d %-10s %-45s %s\n", loNumber, loType, loName,
            ((loAdditionalInfoPresent == true || loStringValue != NULL)?"(see below)":""));
            
        if (loAdditionalInfoPresent == true)
        {
            fprintf(aOutputFile, "               %s\n               %s\n",
            ((loInfoFromResource1 == NULL)?"-":loInfoFromResource1),
            ((loInfoFromResource2 == NULL)?"-":loInfoFromResource2));           
        }
        if (loStringValue != NULL)
        {
            int i;
            int loStringValueLength;
            loStringValueLength = strlen(loStringValue);
            fprintf(aOutputFile, "String value: \"");
            for(i = 0; i < loStringValueLength; i++)
            {
                switch(loStringValue[i])
                {
                    case '\n':
                        fprintf(aOutputFile, "%s", "\\n");
                        break;
                    case '\r':
                        fprintf(aOutputFile, "%s", "\\r");
                        break;
                    case '\t':
                        fprintf(aOutputFile, "%t", "\\t");
                        break;
                    case '\"':
                        fprintf(aOutputFile, "%s", "\\\"");
                        break;
                    case '\\':
                        fprintf(aOutputFile, "%s", "\\\\");
                        break;
                    default:
                        fprintf(aOutputFile, "%c", loStringValue[i]);
                        break;                                                                                                                   
                }
            }
            fprintf(aOutputFile, "\"\n");
                       
        }
        if (loAdditionalInfoPresent == true || loStringValue != NULL)
        {
            // one more empty line
            fprintf(aOutputFile, "\n");
        }                        
    }
}

/*
Get resource entry type name
*/
void getResourceTypeString(char* aResult,  int aType)
{
    switch(aType)
    {
        case RESOURCE_TYPE_UNKNOWN:
            strcpy(aResult, "unknown");
            break;
        case RESOURCE_TYPE_SPECIAL:
            strcpy(aResult, "special");
            break;
        case RESOURCE_TYPE_STRING:
            strcpy(aResult, "string");
            break;
        case RESOURCE_TYPE_CODE:
            strcpy(aResult, "code");
            break;
        case RESOURCE_TYPE_IMPORT:
            strcpy(aResult, "import");
            break;
        case RESOURCE_TYPE_EXPORT:
            strcpy(aResult, "export");
            break;
        case RESOURCE_TYPE_BITMAP:
            strcpy(aResult, "bitmap");
            break;
        case RESOURCE_TYPE_PALETTE:
            strcpy(aResult, "palette");
            break;
        case RESOURCE_TYPE_SOUND:
            strcpy(aResult, "sound");
            break;
        case RESOURCE_TYPE_MUSIC:
            strcpy(aResult, "music");
            break;
        case RESOURCE_TYPE_FONT:
            strcpy(aResult, "font");
            break;
        case RESOURCE_TYPE_MAP:
            strcpy(aResult, "map");
            break;                        
        default:
            strcpy(aResult,"__err__");
            printf("Unknown resource entry type: %d\n", aType);
            break;                                                                      
    }
}

/*
Display a resource type warning
*/
void displayResourcesTypeWarning(FILE *aOutputFile)
{
    fprintf(aOutputFile, "\nWarnings:"
        "\na) The type of some resources is determined by the extension of the original file\n"
        "(this may be not be precise, the AESOP does not enforce any naming conventions!):\n"
        "bitmap(.BMP, .SHP), sound (.SND), music (.XMI), font(.FNT).\n");
    fprintf(aOutputFile, "b) The type of code/import/export resources is determined by the extensions\n"
        ".IMPT/.EXPT of names of import/export resources.\n");
}

/*
Displays resource type summary
*/
void displayResourcesTypeSummary(RESINFOPOINTER *aResourcesInfoTable, FILE *aOutputFile)
{
    int i;
    int loUnknown = 0;
    int loSpecial = 0;
    int loString = 0;
    int loCode = 0;
    int loImport = 0;
    int loExport = 0;
    int loBitmap = 0;
    int loPalette = 0;
    int loSound = 0;
    int loMusic = 0;
    int loFont = 0;
    int loMap = 0;
 
    fprintf(aOutputFile, "\nThe numbers of resources of different types in the file:\n");
    for(i = 0; i < MAX_NUMBER_OF_DICTIONARY_ITEMS && aResourcesInfoTable[i] != NULL; i++)
    {
        int loResourceType = aResourcesInfoTable[i]->resourceType;
        switch(loResourceType)
        {
            case RESOURCE_TYPE_UNKNOWN:
                loUnknown++;
                break;
            case RESOURCE_TYPE_SPECIAL:
                loSpecial++;
                break;
            case RESOURCE_TYPE_STRING:
                loString++;
                break;
            case RESOURCE_TYPE_CODE:
                loCode++;
                break;
            case RESOURCE_TYPE_IMPORT:
                loImport++;
                break;
            case RESOURCE_TYPE_EXPORT:
                loExport++;
                break;
            case RESOURCE_TYPE_BITMAP:
                loBitmap++;
                break;
            case RESOURCE_TYPE_PALETTE:
                loPalette++;
                break;
            case RESOURCE_TYPE_SOUND:
                loSound++;
                break;
            case RESOURCE_TYPE_MUSIC:
                loMusic++;
                break;                
            case RESOURCE_TYPE_FONT:
                loFont++;
                break;
            case RESOURCE_TYPE_MAP:
                loMap++;
                break;                
            default:
                printf("displayResourcesTypeSummary(): unexpected resource type %d!\n", loResourceType);
                break;
        }
    }
    fprintf(aOutputFile, "%10s    %d\n", "unknown", loUnknown);
    fprintf(aOutputFile, "%10s    %d\n", "special", loSpecial);
    fprintf(aOutputFile, "%10s    %d\n", "string", loString);
    fprintf(aOutputFile, "%10s    %d\n", "code", loCode);
    fprintf(aOutputFile, "%10s    %d\n", "import", loImport);
    fprintf(aOutputFile, "%10s    %d\n", "export", loExport);
    fprintf(aOutputFile, "%10s    %d\n", "bitmap", loBitmap);
    fprintf(aOutputFile, "%10s    %d\n", "palette", loPalette);
    fprintf(aOutputFile, "%10s    %d\n", "sound", loSound);
    fprintf(aOutputFile, "%10s    %d\n", "music", loMusic);
    fprintf(aOutputFile, "%10s    %d\n", "font", loFont);
    fprintf(aOutputFile, "%10s    %d\n", "map", loMap);        
    fprintf(aOutputFile, "\n");
}

