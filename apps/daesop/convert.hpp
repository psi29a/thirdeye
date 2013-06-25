#ifndef CONVERT_H
#define CONVERT_H

#include <stdio.h>
#include "resstr.hpp"
#include "rentry.hpp"
#include "dblocks.hpp"
#include "dict.hpp"
#include "utils.hpp"

// leave reserve for future
#define MAX_BITMAP_WIDTH 2500
#define MAX_BITMAP_HEIGHT 2500

// font limits
#define MAX_FONT_HEIGHT 50
#define MAX_CHARACTERS_IN_OLD_FONT 256

// new bitmap constants (see "VFX shape File" documentation in VFX library)
#define MAX_NEW_RUN_LENGTH    127
#define MAX_NEW_STRING_LENGTH 127
#define MAX_NEW_SKIP_LENGTH   255

// new bitmap structures
struct NEW_BITMAP_GLOBAL_HEADER
{
    char version1;
    char version2;
    char version3;
    char version4;            
    int  number_of_shapes; 
};

struct NEW_BITMAP_SUBPICTURE_HEADER
{
    unsigned short boundsy; 
    unsigned short boundsx;
    unsigned short originy;
    unsigned short originx;
    int  xmin;
    int  ymin;
    int  xmax;
    int  ymax;   
};

// TODO: The old font header is almost certainly incomplete, fix it!
struct OLD_FONT_HEADER
{
    unsigned short char_count;
    unsigned short char_height;
    unsigned char header4;
    unsigned char header5;
    unsigned char header6;
    unsigned char header7;        
};

// new font header
struct NEW_FONT_HEADER
{
    unsigned char version1;
    unsigned char version2;
    unsigned char version3;
    unsigned char version4;
    unsigned int char_count;
    unsigned int char_height;
    unsigned int font_background;
};

// bitmap testing
int testOneOldBitmap(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aBitmapName,
    int aBitmapNumber, FILE *aOutputFile);
// replacement of resources
int replaceResourceByResourceFromMemory(FILE *aResFile, char *aResourceName, int aResourceNumber,
        unsigned char *aAddedResourceBuffer, unsigned int aAddedResourceSize, char *aNewFileName, int aNewResourceHasHeader);
int replaceResourceInOpenedFile(char *aResourceName, int aResourceNumber, unsigned char *aAddedResourceBuffer,
        unsigned int aAddedResourceSize, FILE *aNewFile, struct RESGlobalHeader *aNewFileHeader,
        DIRPOINTER *aNewFileDirectoryPointers, int aNewResourceHasHeader);
// conversion of bitmaps
int convertOneOldBitmap(FILE *aNewFile, DIRPOINTER *aNewFileDirectoryPointers, struct RESGlobalHeader *aNewFileHeader,
        int aResourceNumber, char *aResourceName);
unsigned char *getNewBitmapForOldBitmap(unsigned char *aOldResourceBuffer, unsigned int aOldResourceLength,
    int *aNewResourceLength);
unsigned char *allocateNewBitmapBuffer(int aOldBitmapHeaderSize, int *aNewBitmapBufferLength);
int prepareNewBitmapGlobalHeader(unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength, int *aNewBitmapBufferPointer,
    int aSubpictures);
void storeNewBitmapSubpicturePointer(unsigned char *aNewBitmapBuffer, int aIndex, unsigned int aPointer);    
int prepareNewBitmapSubpictureHeader(unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength, int *aNewBitmapBufferPointer,
    int aWidth, int aHeight);
int convertOneOldSubpicture(unsigned char *aOldResourceBuffer, int aOldResourceBufferLength, unsigned int aOldPictureStart,
        unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength, int *aNewBitmapBufferPointer);
int generateSkipSequencesAndEmptyLines(int *aFuturePositionX, int *aFuturePositionY,
    int aOldX, int aOldY, unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
    int *aNewBitmapBufferPointer);        
int processOldSubpictureCopySequence(int aOldX, int aOldY,
        unsigned char *aOldResourceBuffer,  int aOldResourceBufferLength,
        int aPos, int aAmount, unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
        int *aNewBitmapBufferPointer, int *aFuturePositionX, int *aFuturePositionY);
int processOldSubpictureFillSequence(int aOldX, int aOldY,
        unsigned char *aOldResourceBuffer, int aOldResourceBufferLength,
        char aValue, int aAmount, unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
        int *aNewBitmapBufferPointer, int *aFuturePositionX, int *aFuturePositionY);
int addNewEndToken(unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
    int *aNewBitmapBufferPointer, int *aFuturePositionX, int *aFuturePositionY);
int addNewSkipToken(unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
    int *aNewBitmapBufferPointer, int aLength, int *aFuturePositionX);
int addNewStringToken(unsigned char *aOldResourceBuffer, int aOldResourceBufferLength, int aPos,
    int aAmount, unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
    int *aNewBitmapBufferPointer, int *aFuturePositionX);
int addNewRunToken(unsigned char aValue, int aAmount, unsigned char *aNewBitmapBuffer,
    int aNewBitmapBufferLength, int *aNewBitmapBufferPointer, int *aFuturePositionX);    
// conversion of fonts
int convertOneOldFont(FILE *aNewFile, DIRPOINTER *aNewFileDirectoryPointers, struct RESGlobalHeader *aNewFileHeader,
        int aResourceNumber, char *aResourceName);
unsigned char *allocateNewFontBuffer(unsigned int aOldFontHeaderSize, unsigned int *aNewFontBufferLength);
void freeOldCharacterDefinitionTable(unsigned char **aOldCharacterDefinitionTable);
unsigned char *getNewFontForOldFont(unsigned char *aOldResourceBuffer, unsigned int aOldResourceLength,
    int *aNewResourceLength);
int readOldCharacterDefinition(unsigned char **aResult, int aStartPos, int aChar, unsigned char *aOldResourceBuffer,
    unsigned int aOldResourceLength, int aHeight);
int prepareNewFontHeader(int aNumberOfCharacters, int aFontHeight, unsigned char *aNewFontBuffer,
        unsigned int aNewFontBufferLength, int *aNewFontBufferPointer);
int convertOldAndStoreNewCharacter(unsigned char *aOldCharacterDefinition, int aChar, int aFontHeight,
        unsigned char *aNewFontBuffer, unsigned int aNewFontBufferLength, int *aNewFontBufferPointer,
        int aNewOffsetsStart);
int patchEOB3MenuInOpenedFile(FILE *aNewFile, DIRPOINTER *aNewFileDirectoryPointers, struct RESGlobalHeader *aNewFileHeader);
int patchOneByte(unsigned char *aBinary, int aBinaryLength, int aAddress, unsigned char aOldValue,
    unsigned char aNewValue);
                          
#endif
