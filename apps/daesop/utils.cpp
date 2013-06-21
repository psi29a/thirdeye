#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "utils.hpp"

/*
Allocate a string
*/
char *makeString(char *aString)
{
    char *loResult = NULL;
    if (aString == NULL)
    {
        return loResult;
    }
    loResult = (char *)malloc(strlen(aString) +1);
    strcpy(loResult, aString);
    return loResult;    
}



/*
Get the date
*/
char *unpackDate(ULONG aDate, char *aDateString)
{
    static char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    sprintf(aDateString, "%s %d, %4d %02d:%02d:%02d", 
    months[((aDate >> 21) & 0x000f)-1], 
    (aDate >> 16) & 0x001f,
    1980 + ((aDate >> 25) & 0x003f),
    (aDate >> 11) & 0x001f,
    (aDate >> 5) & 0x001f,
    (aDate & 0x001f) << 1);

  return aDateString;
}

/*
Case insensitive comparison
(warning: do not use for resource names - they are case sensitive))
*/
int strcmpCS(const char *s1, const char *s2)
{
    for (; *s1 && *s2 && (toupper((unsigned char)*s1) == toupper((unsigned char)*s2)); ++s1, ++s2);
    return *s1 - *s2;
}

/*
Convert to upper case
(warning: do not use for resource names - they are case sensitive))
*/
void toUpperCase(char *aS)
{
    for (; *aS;++aS)
    {
        *aS = toupper((unsigned char)*aS);
    }    
}

/*
Get the character representation for file dump
*/
char getCharacterForDump(char aChar)
{
    int loCharNumber = (unsigned char)aChar;
    if (loCharNumber < 32)
    {
        // instead of unprintable characters
        return '.';        
    }
    else
    {
        return aChar;
    }
}

/*
Checks whether the one string ends with another string (case insensitive)
*/
int stringEndsWith(char *aFullString, char *aEndString)
{
    int loFullStringLength;
    int loEndStringLength;
    int loResult = false;
    char *loFullStringEndPointer;
    
    if (aFullString == NULL || aEndString == NULL)
    {
        // at least one is null
        printf("stringEndsWith(): at least one parameter is NULL!\n");
        return loResult;
    }
    loFullStringLength = strlen(aFullString);
    loEndStringLength = strlen(aEndString);    
    if (loFullStringLength < loEndStringLength)
    {
        // the second string is longer
        return loResult;
    }
    loFullStringEndPointer = aFullString + (loFullStringLength - loEndStringLength); 
    if (strcmpCS(loFullStringEndPointer, aEndString) == 0)
    {
        // ok, the ending agrees
        loResult = true;
    }
    return loResult;
}

/*
Copy the file into a new file
*/
int copyFile(FILE *aSourceFile, char *aNewFileName)
{
    FILE *loNewFile;
    int loReadSize;
    char loBuffer[MAX_COPY_BUFFER];    
    
    // new file
    printf("Opening the target file: %s\n", aNewFileName);
    loNewFile = fopen(aNewFileName, "wb");
    if (loNewFile == NULL)
    {
        printf("The file could not be opened: %s!\n", aNewFileName);
        return false;
    }

    // copy the aResFile file into aNewFile
    fseek(aSourceFile, 0, SEEK_SET);
    while ((loReadSize = fread( &loBuffer, 1, MAX_COPY_BUFFER, aSourceFile)) != 0)
    {
        if (fwrite(loBuffer, 1, loReadSize, loNewFile) != loReadSize)
        {
            printf("Unable to write to the file: %s\n", aNewFileName);
            fclose(loNewFile);
            return false;
        }
    }

    // close the file after copying
    fclose(loNewFile);
    printf("Finished copying of the original into the file: %s\n", aNewFileName);
    return true;
}
