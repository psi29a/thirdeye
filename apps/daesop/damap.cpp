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

#include "damap.hpp"

// here will be the code map
unsigned char *myCodeMap = NULL;
int myCodeMapLength = 0;

/*
Creates the initial code map
*/
int createCodeMap(int aLength)
{
    int loMapSize;
    int i;
    // allocate map
    loMapSize = aLength * sizeof(unsigned char);
    myCodeMap = (unsigned char *)malloc(loMapSize);
    if (myCodeMap == NULL)
    {
        printf("Failure to allocate %d for the code map during the disaasembling process!", loMapSize);
        return FALSE;
    }
    myCodeMapLength = aLength;
    // initialize everything to MAP_DATA_BYTE (just 14 bytes at the beginning are special)
    for(i = 0; i < myCodeMapLength; i++)
    {
        if (i < 14)
        {
            myCodeMap[i] = MAP_SPECIAL;
        }
        else
        {
            myCodeMap[i] = MAP_DATA_BYTE;
        }    
    }
    return TRUE;    
}

/*
Sets the code map entry
*/
int setCodeMapAddress(int aAddress, unsigned char aValue)
{
    int loLabel;
    if (aAddress < 0 || aAddress >= myCodeMapLength)
    {
        printf("The code map address %d is out of range in setCodeMapAddress()!\n", aAddress);
        return FALSE;
    }    
    if (aValue >= LABEL_MASK)
    {
        printf("The code map value %d is not allowed!\n", (int)aValue);
        return FALSE;
    }   
    // keep the setting for label
    loLabel = hasAddressLabel(aAddress);
    switch(aValue)
    {
        case MAP_DATA_BYTE:
        case MAP_DATA_WORD:
        case MAP_DATA_LONG:
        case MAP_DATA_STRING:
        case MAP_DATA_CONTINUE:
        case MAP_BYTE_TABLE_START:
        case MAP_WORD_TABLE_START:
        case MAP_LONG_TABLE_START:
        case MAP_MESSAGE_HANDLER_NOT_DONE:
        case MAP_MESSAGE_HANDLER_PROCESSED:
        case MAP_CODE_START_NOT_DONE:
        case MAP_CODE_START_PROCESSED:
        case MAP_CODE_PROCESSED:
        case MAP_PROCEDURE_START_NOT_DONE:
        case MAP_PROCEDURE_START_PROCESSED:
        case MAP_SPECIAL:
        case MAP_ERROR:
                myCodeMap[aAddress] = aValue;
                break;
        default:
                printf("The code map value %d is not allowed!\n", (int)aValue);
                return FALSE;
    }
    // restore label
    if (loLabel == TRUE)
    {
        setLabelForAddress(aAddress);
    }
    return TRUE;
}

/*
Gets the value of the code map address
*/
int getCodeMapAddressValue(int aAddress)
{
    if (aAddress < 0 || aAddress >= myCodeMapLength)
    {
        printf("The code map address %d is out of range in getCodeMapAddress()!\n", aAddress);
        return -1;
    }
    return myCodeMap[aAddress] & (unsigned char)VALUE_MASK;
}

/*
Set label for the address
*/
int setLabelForAddress(int aAddress)
{
    if (aAddress < 0 || aAddress >= myCodeMapLength)
    {
        printf("The code map address %d is out of range in setLabelForAddress()!\n", aAddress);
        return FALSE;
    }
    myCodeMap[aAddress] |= LABEL_MASK;
    return TRUE;
}

/*
Informs whether the address has a label
*/
int hasAddressLabel(int aAddress)
{
    int loLabel;
    if (aAddress < 0 || aAddress >= myCodeMapLength)
    {
        printf("The code map address %d is out of range in hasAddressLabel()!\n", aAddress);
        return FALSE;
    }
    loLabel = myCodeMap[aAddress] & (unsigned char)LABEL_MASK;
    if (loLabel == 0)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }    
}

/*
Get the first address for disassembling
*/
int findFirstAddressForDisassembling(void)
{
    int i;
    for(i = 0; i < myCodeMapLength; i++)
    {
        int loValue;
        loValue = getCodeMapAddressValue(i);
        if (loValue == -1)
        {
            // error
            return -1;
        }
        if (loValue == MAP_MESSAGE_HANDLER_NOT_DONE || loValue == MAP_CODE_START_NOT_DONE ||
            loValue == MAP_PROCEDURE_START_NOT_DONE)
        {
            return i;
        }    
    }
    return -1;    
}

/*
Should the address be disassembled
*/
int shouldBeAddressDisassembled(int aAddress)
{
    int loValue;
    if (aAddress < 0 || aAddress >= myCodeMapLength)
    {
        printf("The code map address %d is out of range in shouldBeAddressDisassembled()!\n", aAddress);
        return FALSE;
    }
    loValue = getCodeMapAddressValue(aAddress);
    if (loValue == -1)
    {
        // error
        return FALSE;
    }
    if (loValue == MAP_MESSAGE_HANDLER_NOT_DONE)
    {
        return TRUE;
    }
    else if (loValue == MAP_CODE_START_NOT_DONE)
    {
        return TRUE;
    }
    else if (loValue == MAP_PROCEDURE_START_NOT_DONE)
    {
        return TRUE;
    }
    else if (loValue == MAP_DATA_BYTE)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }    
}

/*
Mark address as disassembled
*/
int markAddressAsDisassembled(int aAddress)
{
    int loValue;
    if (aAddress < 0 || aAddress >= myCodeMapLength)
    {
        printf("The code map address %d is out of range in markAddressAsDisassembled()!\n", aAddress);
        return FALSE;
    }
    loValue = getCodeMapAddressValue(aAddress);
    if (loValue == -1)
    {
        // error
        return FALSE;
    }
    if (loValue == MAP_MESSAGE_HANDLER_NOT_DONE)
    {
        return setCodeMapAddress(aAddress, MAP_MESSAGE_HANDLER_PROCESSED);
    }
    if (loValue == MAP_PROCEDURE_START_NOT_DONE)
    {
        return setCodeMapAddress(aAddress, MAP_PROCEDURE_START_PROCESSED);
    }    
    else if (loValue == MAP_CODE_START_NOT_DONE)
    {
        return setCodeMapAddress(aAddress, MAP_CODE_START_PROCESSED);
    }
    else if (loValue == MAP_DATA_BYTE)
    {
        return setCodeMapAddress(aAddress, MAP_CODE_PROCESSED);
    }
    else
    {
        printf("Setting the code map address %d (value %d) as disassembled is not possible!\n", aAddress, (int)loValue);
        return FALSE;
    }    
}

/*
Sets jump target
*/
int setJumpTarget(int aAddress)
{
    int loValue;
    if (aAddress < 0 || aAddress >= myCodeMapLength)
    {
        printf("The code map address %d is out of range in setJumpTarget()!\n", aAddress);
        return FALSE;
    }
    loValue = getCodeMapAddressValue(aAddress);
    if (loValue == -1)
    {
        // error
        return FALSE;
    }
    setLabelForAddress(aAddress);
    if (loValue == MAP_DATA_BYTE)
    {
        setCodeMapAddress(aAddress, MAP_CODE_START_NOT_DONE);
    }
    else if (loValue == MAP_CODE_PROCESSED)
    {
        // this part was already disassembled
        setCodeMapAddress(aAddress, MAP_CODE_START_PROCESSED);
    }
    else if (loValue == MAP_CODE_START_NOT_DONE || loValue == MAP_CODE_START_PROCESSED)
    {
        // nothing, it is already marked
    }   
    else
    {
        printf("Setting the jump target for the address %d (value %d) is not possible!\n", aAddress, (int)loValue);        
        return FALSE;
    }
    return TRUE;        
}

/*
Sets procedure start
*/
int setProcedureStart(int aAddress)
{
    int loValue;
    if (aAddress < 0 || aAddress >= myCodeMapLength)
    {
        printf("The code map address %d is out of range in setProcedureStart()!\n", aAddress);
        return FALSE;
    }
    loValue = getCodeMapAddressValue(aAddress);
    if (loValue == -1)
    {
        // error
        return FALSE;
    }
    if (loValue == MAP_DATA_BYTE)
    {
        setCodeMapAddress(aAddress, MAP_PROCEDURE_START_NOT_DONE);
    }
    else if (loValue == MAP_PROCEDURE_START_NOT_DONE || loValue == MAP_PROCEDURE_START_PROCESSED)
    {
        // nothing, it is already marked
    }   
    else
    {
        printf("Setting the procedure start for the address %d (value %d) is not possible!\n", aAddress, (int)loValue);        
        return FALSE;
    }
    return TRUE;        
}

/*
Display code map (for debugging)
*/
void displayCodeTableMap(FILE *aOutputFile)
{
    int i;
    if (aOutputFile == NULL)
    {    
        printf("\nTHE CODE MAP TABLE DUMP\n");
    }
    else
    {
        fprintf(aOutputFile, "THE CODE MAP TABLE DUMP\n");
    }
    for(i = 0; i < myCodeMapLength; i++)
    {
        int loValue;
        int loLabel;
        loValue = getCodeMapAddressValue(i);
        loLabel = hasAddressLabel(i);
        if (loValue == -1)
        {
            // error
            return;
        }
        if (aOutputFile == NULL)
        {
            printf("%5d:  %d  label %s\n", i, loValue, (loLabel==TRUE)?"yes":"no");
        }
        else
        {
            fprintf(aOutputFile, "%5d:  %d  label %s\n", i, loValue, (loLabel==TRUE)?"yes":"no");
        }
    }
    return;    
}

/*
Set the code map entries properly for the contant tables (change DB to DB/DW/DL as needed)
*/
void fixCodeTableForConstantTables(void)
{
    int i;
    for(i = 0; i < myCodeMapLength; i++)
    {
        int loValue;
        loValue = getCodeMapAddressValue(i);
        if (loValue == MAP_BYTE_TABLE_START || loValue == MAP_WORD_TABLE_START || loValue == MAP_LONG_TABLE_START)
        {
            // it is a code map start
            int loStart = i;
            int loBehindEnd;

            int loTableType = loValue;
            int loNumberOfElements;
            for(loBehindEnd = loStart + 1; loBehindEnd < myCodeMapLength; loBehindEnd++)
            {
                int loTestedValue;
                loTestedValue = getCodeMapAddressValue(loBehindEnd);
                if (loTestedValue != MAP_DATA_BYTE)
                {
                    // something else then byte - end
                    break;
                }             
            }
            loNumberOfElements = getNumberOfTableElementsInRange(loTableType, loStart, loBehindEnd);
            if (loNumberOfElements <= 0)
            {
                printf("Error when determining the number of elements for the table on the address %d!\n", loStart);
                setCodeMapAddress(loStart, MAP_ERROR);
            }
            else
            {
                if (fillTheMapInCodeTable(loTableType, loStart, loNumberOfElements) == FALSE)
                {
                    printf("Error when filling the table map in the code map for the table on the address %d!\n", loStart);
                    setCodeMapAddress(loStart, MAP_ERROR);                    
                }
            }
        }
    }    
}

/*
Get number of table elements which fill the range
*/
int getNumberOfTableElementsInRange(int aTableType, int aStart, int aBehindEnd)
{
    int loMaxSpace;
    int loUsableElements;
    int loUsableLength;
    // the maximum space available for the table
    loMaxSpace = aBehindEnd - aStart;
    // taking into account the size of table elements, try to put there as many elements as possible
    if (aTableType == MAP_BYTE_TABLE_START)
    {
        // everything is usable
        loUsableElements = loMaxSpace;
        loUsableLength = loMaxSpace;
    }
    else if (aTableType == MAP_WORD_TABLE_START)
    {
        // elements have size 2
        loUsableElements = loMaxSpace/2;
        loUsableLength = loUsableElements * 2;                    
    }
    else if (aTableType == MAP_LONG_TABLE_START)
    {
        // elements have size 4
        loUsableElements = loMaxSpace/4;
        loUsableLength = loUsableElements * 4;                    
    }
    else
    {
        printf("getNumberOfTableElementsInRange(): unknown table type: %d\n", aTableType);
        return -1;
    }
    if (loUsableLength > loMaxSpace)
    {
        printf("Error in getNumberOfElementsInRange()\n");
        return -1;
    }
    return loUsableElements;
}

/*
Fill the space for the constant table map in the code map
*/
int fillTheMapInCodeTable(int aTableType, int aStart, int aNumberOfElements)
{
    int loFillingData;
    int loFillingDataLength;
    int i;
    if (aTableType != getCodeMapAddressValue(aStart))
    {
        printf("fillTheMapInCodeTable(): disagreement between parameter and code map!\n");
        return FALSE;
    }
    if (aTableType == MAP_BYTE_TABLE_START)
    {
        loFillingData = MAP_DATA_BYTE;
        loFillingDataLength = 1;
    }
    else if (aTableType == MAP_WORD_TABLE_START)
    {
        loFillingData = MAP_DATA_WORD;
        loFillingDataLength = 2;
    }
    else if (aTableType == MAP_LONG_TABLE_START)
    {
        loFillingData = MAP_DATA_LONG;
        loFillingDataLength = 4;
    }
    else
    {
        printf("fillTheMapInCodeTable(): unknown filling type: %d!\n", aTableType);
        return FALSE;        
    }
    // fill it
    for(i = 0; i < aNumberOfElements * loFillingDataLength; i++)
    {
        int loCurrentAddress = aStart + i;
        
        // some checking to be sure I do not overwrite (it should not be needed, but let's be sure)
        int loCurrentValue = getCodeMapAddressValue(loCurrentAddress);
        if (loCurrentValue != aTableType && loCurrentValue != MAP_DATA_BYTE)
        {
            printf("fillTheMapInCodeTable(): attempt to overwrite the value %d on the address %d of the code map!\n",
            loCurrentValue, loCurrentAddress);
            return FALSE;
        }
        
        if (i % loFillingDataLength == 0)
        {
            // set what data it is
            setCodeMapAddress(loCurrentAddress, loFillingData);
        }
        else
        {
            // continuation of the previous
            setCodeMapAddress(loCurrentAddress, MAP_DATA_CONTINUE); 
        }
    }
    // restore the original first byte of the table in the code map
    setCodeMapAddress(aStart, aTableType);
    return TRUE;
}


