#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "convert.hpp"

// set this to get more information about font conversion
//#define FONT_CONVERSION_DEBUG

int testOneOldBitmap(FILE *aResFile, DIRPOINTER *aDirectoryPointers, char *aBitmapName,
    int aBitmapNumber, FILE *aOutputFile)
{
    int loResourceSize;
    unsigned char *loBuffer;
    int loBitmapHeaderSize;
    unsigned int loSubPictures;
    unsigned int *loStartOffsets;
    unsigned int i;

    fprintf(aOutputFile, "Testing the bitmap: \"%s\"\n", aBitmapName);
    
    loBuffer = readResourceBinary(aBitmapNumber, aResFile, aDirectoryPointers, &loResourceSize);
    if (loBuffer == NULL)
    {
        char loError[256];
        sprintf(loError, "Error: failure while reading the bitmap resource number %d, name: %s!",
            aBitmapNumber, aBitmapName);
        fprintf(aOutputFile, "%s\n", loError);
        printf("%s\n", loError);
        return false;
    }

    loBitmapHeaderSize = loBuffer[0] | (loBuffer[1]<<8) | (loBuffer[2]<<16) | (loBuffer[3]<<24);
    if (loBitmapHeaderSize!=loResourceSize)
    {
        char loError[256];
        sprintf(loError, "Error: size in the bitmap header does not agree with the size of the resource!");
        fprintf(aOutputFile, "%s\n", loError);
        printf("%s\n", loError);        
        free(loBuffer);
        return false;
    }
    else
    {
        fprintf(aOutputFile, "  Bitmap resource size: %d bytes.\n", loResourceSize); 
    }

    loSubPictures = loBuffer[4] | (loBuffer[5]<<8);
    fprintf(aOutputFile, "  %d sub picture(s) found\n", loSubPictures);
    loStartOffsets = (unsigned int *)malloc (sizeof(unsigned int) * loSubPictures);
    if (loStartOffsets == NULL)
    {
        printf("Error: failure to allocate a field of %d start offsets!\n", (int)loSubPictures);
        free(loBuffer);
        return false;
        
    }
    for (i = 0; i < loSubPictures; i++)
    {
        unsigned int loPos;
        int loWidth;
        int loHeight;
        unsigned char* loIndexedBitmap;
        loStartOffsets[i]=loBuffer[6+i*4+0] | (loBuffer[6+i*4+1]<<8) | (loBuffer[6+i*4+2]<<16) | (loBuffer[6+i*4+3]<<24);
        loPos=loStartOffsets[i];         
        fprintf(aOutputFile, "    Sub picture 0x%04x starts at offset 0x%08x\n", i, loStartOffsets[i]);
        loWidth=loBuffer[loPos+0] | (loBuffer[loPos+1]<<8);
        if (loWidth <= 0 || loWidth > MAX_BITMAP_WIDTH)
        {
            char loError[256];
            sprintf(loError, "Error: wrong bitmap width: %d!", loWidth);
            printf("%s\n", loError);
            fprintf(aOutputFile, "%s\n", loError);
            free(loBuffer);
            free(loStartOffsets);
            return false;
        }        
        loHeight=loBuffer[loPos+2] | (loBuffer[loPos+3]<<8);
        if (loHeight <= 0 || loHeight > MAX_BITMAP_WIDTH)
        {
            char loError[256];
            sprintf(loError, "Error: wrong bitmap height: %d!", loHeight);
            printf("%s\n", loError);
            fprintf(aOutputFile, "%s\n", loError);
            free(loBuffer);
            free(loStartOffsets);
            return false;
        }        
        loPos+=4;       
        fprintf(aOutputFile, "    Image size is %dx%d\n", loWidth, loHeight);
        
        loIndexedBitmap=(unsigned char*) malloc(loWidth*loHeight);
        if (loIndexedBitmap == NULL)
        {
            printf("Error: failure to allocate a field for a bitmap!\n");
            free(loBuffer);
            free(loStartOffsets);
            return false;
        }
        memset(loIndexedBitmap,0,loWidth*loHeight);   // Default bgcolor??? Probably defined in the header...
        for(;;)
        {
            int loY;
            loY = loBuffer[loPos];
            if (loY==0xff)
            {
                break;
            }

            if ((loY<0) || (loY>=loHeight))
            {
                char loError[256];
                sprintf(loError, "Error: probably out of sync. Reported y-coord: %d", loY);
                printf("%s\n", loError);
                fprintf(aOutputFile, "%s\n", loError);                
                free(loIndexedBitmap);
                free(loStartOffsets);
                free(loBuffer);                
                return false;
            }
            loPos++;

            for(;;)
            {
                int loX;
                int loIsLast;
                int loRLE_width;
                //int loRLE_bytes;
                
                loX=loBuffer[loPos];
                loPos++;

                loIsLast=loBuffer[loPos];
                loPos++;

                loRLE_width=loBuffer[loPos];
                loPos++;

                //loRLE_bytes=loBuffer[loPos];
                loPos++;

                while(loRLE_width>0)
                {
                    int loMode=loBuffer[loPos]&1;
                    int loAmount=(loBuffer[loPos]>>1)+1;
                    loPos++;
                        
                    if (loMode==0)    // Copy
                    {
                        memcpy(loIndexedBitmap+loX+loY*loWidth, loBuffer+loPos, loAmount);
                        loPos+=loAmount;
                    }
                    else if (loMode==1) // Fill
                    {
                        int loValue=loBuffer[loPos];  
                        loPos++;
                        memset(loIndexedBitmap+loX+loY*loWidth,loValue, loAmount);
                    }
                    loX+=loAmount;
                    loRLE_width-=loAmount;
                }

                if (loRLE_width!=0)
                {
                    char loError[256];
                    sprintf(loError, "Error: out of sync while depacking RLE (rle_width=%d).", loRLE_width);
                    printf("%s\n", loError);
                    fprintf(aOutputFile, "%s\n", loError);                    
                    free(loIndexedBitmap);
                    free(loStartOffsets);
                    free(loBuffer);                    
                    return false;
                }

                if (loIsLast==0x80)
                {
                    break;
                }
            }           
        }
        // the decoded image is in the loIndexedBitmap
        free(loIndexedBitmap);
        fprintf(aOutputFile, "    Subpicture decoding ok.\n");
                
    }
    free(loStartOffsets);
    free(loBuffer);
    return true;
}

/*
Replaces a resource by a new resource read from a memory buffer (with or without a header)
*/
int replaceResourceByResourceFromMemory(FILE *aResFile, char *aResourceName, int aResourceNumber,
        unsigned char *aAddedResourceBuffer, unsigned int aAddedResourceSize, char *aNewFileName, int aNewResourceHasHeader)
{
    FILE *loNewFile;
    int loResult;
    DIRPOINTER loNewFileDirectoryPointers[MAX_DIRECTORIES];
    struct RESGlobalHeader loNewFileHeader;
    int i;

    if (copyFile(aResFile, aNewFileName) == false)
    {
        printf("Copying of the original file failed!\n");
        return false;
    }

    // empty the directory table
    for(i = 0; i < MAX_DIRECTORIES; i++)
    {
        loNewFileDirectoryPointers[i] = NULL;
    }

    // open the copied file and read the header
    loNewFile = openAESOPResourceAndSetToFirstDirectoryBlock(aNewFileName, "r+b", &loNewFileHeader);
    if (loNewFile == NULL)
    {
        printf("The file could not be opened: %s!\n", aNewFileName);
        return false;
    }

    // read the directory
    if (readDirectoryBlocks(loNewFile, loNewFileDirectoryPointers) == false)
    {
       printf("The reading of directory blocks in the new file failed!\n");
       fclose(loNewFile);
       return false;
    }

    loResult = replaceResourceInOpenedFile(aResourceName, aResourceNumber, aAddedResourceBuffer, aAddedResourceSize, loNewFile, &loNewFileHeader, loNewFileDirectoryPointers, aNewResourceHasHeader);
    fclose(loNewFile);
    return loResult;
}

/*
Replace one resource in an opened file aNewFile
*/
int replaceResourceInOpenedFile(char *aResourceName, int aResourceNumber, unsigned char *aAddedResourceBuffer,
        unsigned int aAddedResourceSize, FILE *aNewFile, struct RESGlobalHeader *aNewFileHeader,
        DIRPOINTER *aNewFileDirectoryPointers, int aNewResourceHasHeader)
{    
    int loNewFileLength;
    int loNewFileOriginalLength;
    unsigned int loFileHeaderSize;
    int loDirBlockNumber;
    int loNumberInDirBlock;
    long loDirBlockStart;
    unsigned int loDirBlockSize;
    DIRPOINTER loModifiedDirectoryBlock;

    fseek(aNewFile, 0, SEEK_END);  // to the end of the new file
    loNewFileOriginalLength = ftell(aNewFile);
    loNewFileLength = loNewFileOriginalLength;

    // add header if needed (use the original, just fix the length)
    if (aNewResourceHasHeader == false)
    {
        struct RESEntryHeader *loResEntryHeader;
        unsigned int loResourceHeaderSize = sizeof(struct RESEntryHeader);
        
        loResEntryHeader = getResourceEntryHeader(aResourceNumber, aNewFile, aNewFileDirectoryPointers);
        if (loResEntryHeader == NULL)
        {
            printf("Unable to read the resource entry header for the resource number: %d\n", aResourceNumber);
            return false;
        }
        // fix the data size
        loResEntryHeader->data_size = aAddedResourceSize;
        // write the header
        fseek(aNewFile, 0, SEEK_END);  // to the end of the new file        
        if (fwrite(loResEntryHeader, 1, loResourceHeaderSize, aNewFile) != loResourceHeaderSize)
        {
            printf("Unable to write the resource entry header for the replaced resource: %d\n", aResourceNumber);
            return false;
        }
        loNewFileLength += loResourceHeaderSize;       
    }

    // now write the resource
    fseek(aNewFile, 0, SEEK_END);  // to the end of the new file       
    if (fwrite(aAddedResourceBuffer, 1, aAddedResourceSize, aNewFile) != aAddedResourceSize)
    {
        printf("Unable to write the content of the replaced resource: %d\n", aResourceNumber);
        return false;
    }    
    
    // fix the header (the length of the file)
    loFileHeaderSize = sizeof(struct RESGlobalHeader);
    
    // fix the length of the file
    loNewFileLength += aAddedResourceSize;
    aNewFileHeader->file_size = loNewFileLength;
    // write the header
    fseek(aNewFile, 0, SEEK_SET);  // to the beginning of the new file
    if (fwrite(aNewFileHeader, 1, loFileHeaderSize, aNewFile) != loFileHeaderSize)
    {
        printf("The new file header could not be written!\n");
        return false;
    }

    // fix the pointer to the resource
    loDirBlockNumber = aResourceNumber / DIRECTORY_BLOCK_ITEMS;
    loNumberInDirBlock = aResourceNumber % DIRECTORY_BLOCK_ITEMS;
    if (loDirBlockNumber >= MAX_DIRECTORIES || aNewFileDirectoryPointers[loDirBlockNumber] == NULL)
    {
        printf("There is not enough directory blocks for the resource number %d!\n", aResourceNumber);
        return -1;
    }

    if (loDirBlockNumber == 0)
    {
        loDirBlockStart = aNewFileHeader->first_directory_block;
    }
    else
    {
        loDirBlockStart = aNewFileDirectoryPointers[loDirBlockNumber - 1]->next_directory_block;
    }

    // modified directory block
    loModifiedDirectoryBlock = aNewFileDirectoryPointers[loDirBlockNumber];

    // set the pointer to the new resource
    loModifiedDirectoryBlock->entry_header_index[loNumberInDirBlock] = loNewFileOriginalLength;

    // write the directory block
    loDirBlockSize = sizeof(struct RESDirectoryBlock);
    fseek(aNewFile, loDirBlockStart, SEEK_SET); // set to the directory block
    if (fwrite(loModifiedDirectoryBlock, 1, loDirBlockSize, aNewFile) != loDirBlockSize)
    {
        printf("Writing of the directory block failed!\n");
        return false;
    }    
    
    return true;
}

/*
Converts the bitmap resource aResourceNumber from old (EOB 3) format to the new (AESOP/32) format
(changes the resource file)
*/
int convertOneOldBitmap(FILE *aNewFile, DIRPOINTER *aNewFileDirectoryPointers, struct RESGlobalHeader *aNewFileHeader,
        int aResourceNumber, char *aResourceName)
{
    unsigned char *loOldResourceBuffer;
    int loOldResourceLength;
    unsigned char *loNewResourceBuffer;
    int loNewResourceLength;
    
    printf("Converting the bitmap resource number %d, name \"%s\" ...\n", aResourceNumber,
        aResourceName);
    loOldResourceBuffer = readResourceBinary(aResourceNumber, aNewFile, aNewFileDirectoryPointers,
        &loOldResourceLength);
    if (loOldResourceBuffer == NULL)
    {
        printf("Failed to read the resource number %d!\n", aResourceNumber);
        return false;
    }

    // get the converted bitmap
    loNewResourceBuffer = getNewBitmapForOldBitmap(loOldResourceBuffer, loOldResourceLength, &loNewResourceLength);
    if (loNewResourceBuffer == NULL)
    {
        printf("Failed to get the converted bitmap for the resource number %d!\n", aResourceNumber);
        free(loOldResourceBuffer);
        return false;
    }
    else
    {
        printf("The converted bitmap length is: %d bytes\n", loNewResourceLength);
    }

    // replace the old bitmap by a new one
    if (replaceResourceInOpenedFile(aResourceName, aResourceNumber, loNewResourceBuffer,
            loNewResourceLength, aNewFile, aNewFileHeader, aNewFileDirectoryPointers, false) == false)
    {
        printf("Failed to replace the old bitmap by a new one (resource number %d)!\n", aResourceNumber);
        free(loOldResourceBuffer);
        free(loNewResourceBuffer);
        return false;
    }

    free(loOldResourceBuffer);
    free(loNewResourceBuffer);
    return true;
}

/*
Convert the old bitmap to a new one
*/
unsigned char *getNewBitmapForOldBitmap(unsigned char *aOldResourceBuffer, unsigned int aOldResourceLength,
    int *aNewResourceLength)
{
    unsigned char *loNewBitmapBuffer = NULL;
    int loNewBitmapBufferLength = 0;
    int loNewBitmapBufferPointer = 0;

    unsigned int loOldBitmapHeaderSize;
    unsigned int loSubPictures;
    unsigned int *loOldStartOffsets;
    unsigned int i;    
            
    loOldBitmapHeaderSize = aOldResourceBuffer[0] | (aOldResourceBuffer[1]<<8) | (aOldResourceBuffer[2]<<16) | (aOldResourceBuffer[3]<<24);
    if (loOldBitmapHeaderSize != aOldResourceLength)
    {
        char loError[256];
        sprintf(loError, "Error: size in the bitmap header does not agree with the size of the resource!");
        printf("%s\n", loError);        
        return NULL;
    }
    loSubPictures = aOldResourceBuffer[4] | (aOldResourceBuffer[5]<<8);
    printf("  %d sub picture(s) found\n", loSubPictures);
    loOldStartOffsets = (unsigned int *)malloc (sizeof(unsigned int) * loSubPictures);
    if (loOldStartOffsets == NULL)
    {
        printf("Error: failure to allocate a field of %d start offsets!\n", (int)loSubPictures);
        return NULL;
        
    }

    // allocate the space for a new bitmap
    loNewBitmapBuffer = allocateNewBitmapBuffer(loOldBitmapHeaderSize, &loNewBitmapBufferLength);
    if (loNewBitmapBuffer == NULL)
    {
        printf("Error: failure to allocate a new bitmap buffer!\n");
        free(loOldStartOffsets);
        return NULL;
        
    }

    // create new header
    if (prepareNewBitmapGlobalHeader( loNewBitmapBuffer, loNewBitmapBufferLength, &loNewBitmapBufferPointer, loSubPictures) ==false)
    {
        printf("Failure while generating the new bitmap global header\n");
        free(loOldStartOffsets);
        free(loNewBitmapBuffer);        
        return NULL;
    }

    // go through all images
    for (i = 0; i < loSubPictures; i++)
    {
        unsigned int loOldPictureStart;
        loOldPictureStart=aOldResourceBuffer[6+i*4+0] | (aOldResourceBuffer[6+i*4+1]<<8) | \
            (aOldResourceBuffer[6+i*4+2]<<16) | (aOldResourceBuffer[6+i*4+3]<<24);
        printf("    Sub picture 0x%04x starts at offset 0x%08x\n", i, loOldPictureStart);
        // store the pointer to the subpicture
        storeNewBitmapSubpicturePointer(loNewBitmapBuffer, i, (unsigned int)loNewBitmapBufferPointer);
        if (convertOneOldSubpicture(aOldResourceBuffer, aOldResourceLength, loOldPictureStart, loNewBitmapBuffer,
            loNewBitmapBufferLength, &loNewBitmapBufferPointer) == false)
        {
            printf("Failure while processing the subpicture %d!\n", i);
            free(loOldStartOffsets);
            free(loNewBitmapBuffer);
            return NULL;            
        }
    }

    free(loOldStartOffsets);
    // loNewBitmapBufferPointer points to the first not used byte in the new buffer (so it is in fact the length)
    *aNewResourceLength = loNewBitmapBufferPointer;
    return loNewBitmapBuffer;
}

/*
Allocate a big buffer for the new bitmap
*/
unsigned char *allocateNewBitmapBuffer(int aOldBitmapHeaderSize, int *aNewBitmapBufferLength)
{
    int loAllocatedSize;
    unsigned char *loResult;
    // just some big value
    loAllocatedSize = 2 * (aOldBitmapHeaderSize + 5000);
    loResult = (unsigned char*)malloc(loAllocatedSize);
    if (loResult == NULL)
    {
        printf("Unable to allocate %d bytes for a new bitmap buffer!\n", loAllocatedSize);
        return NULL;
    }
    else
    {
        *aNewBitmapBufferLength = loAllocatedSize;
        return loResult;
    }    
}

/*
Prepare new bitmap header
*/
int prepareNewBitmapGlobalHeader(unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength, int *aNewBitmapBufferPointer,
    int aSubpictures)
{
    struct NEW_BITMAP_GLOBAL_HEADER loHeader;
    int loNeededSize;
    
    loHeader.version1 = '1';
    loHeader.version2 = '.';
    loHeader.version3 = '1';
    loHeader.version4 = '0';
    loHeader.number_of_shapes = aSubpictures;

    // version, count, count * [shape, color]
    loNeededSize = sizeof(struct NEW_BITMAP_GLOBAL_HEADER) + aSubpictures * (4 + 4);    
    if (loNeededSize > aNewBitmapBufferLength)
    {
        printf("prepareNewBitmapGlobalHeader: too small buffer for a new bitmap!\n");
        return false;
    }
    // zero
    memset(aNewBitmapBuffer, 0, loNeededSize);
    // header
    memcpy(aNewBitmapBuffer, &loHeader, sizeof(struct NEW_BITMAP_GLOBAL_HEADER));
    // set pointer
    (*aNewBitmapBufferPointer) = loNeededSize;
    return true;
}

/*
Store a pointer to a subpicture
*/
void storeNewBitmapSubpicturePointer(unsigned char *aNewBitmapBuffer, int aIndex, unsigned int aPointer)
{
    int loPos = sizeof(struct NEW_BITMAP_GLOBAL_HEADER) + (aIndex * (4 + 4));
    printf("    Storing a subpicture pointer %d to the position %d.\n", aPointer, loPos);
    memcpy(aNewBitmapBuffer + loPos, &aPointer, 4);
}

/*daesop /rh eye_cn1.res 3 3h.bin eye_cn2.res

Prepare new subpicture header
*/
int prepareNewBitmapSubpictureHeader(unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength, int *aNewBitmapBufferPointer,
    int aWidth, int aHeight)
{
    struct NEW_BITMAP_SUBPICTURE_HEADER loHeader;
    int loNewPointerValue;

    loNewPointerValue = (*aNewBitmapBufferPointer) + sizeof(struct NEW_BITMAP_SUBPICTURE_HEADER);
    if (loNewPointerValue > aNewBitmapBufferLength)
    {
        printf("prepareNewBitmapSubpictureHeader: too small buffer for a new bitmap!\n");
        return false;
    }

    loHeader.boundsy = aHeight - 1;    
    loHeader.boundsx = aWidth - 1;
    loHeader.originy = 0;
    loHeader.originx = 0;    
    loHeader.xmin = 0;
    loHeader.ymin = 0;
    loHeader.xmax = aWidth - 1;
    loHeader.ymax = aHeight - 1;

    // header
    memcpy(aNewBitmapBuffer + (*aNewBitmapBufferPointer), &loHeader, sizeof(struct NEW_BITMAP_SUBPICTURE_HEADER));
    // move pointer
    *aNewBitmapBufferPointer = loNewPointerValue; 
    
    return true;
}


/*
Process one old subpicture
*/
int convertOneOldSubpicture(unsigned char *aOldResourceBuffer, int aOldResourceBufferLength, unsigned int aOldPictureStart,
        unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength, int *aNewBitmapBufferPointer)
{
    unsigned int loPos;
    int loWidth;
    int loHeight;
    int loFuturePositionX;
    int loFuturePositionY;
    
    loPos=aOldPictureStart;         
    loWidth=aOldResourceBuffer[loPos+0] | (aOldResourceBuffer[loPos+1]<<8);
    if (loWidth <= 0 || loWidth > MAX_BITMAP_WIDTH)
    {
        char loError[256];
        sprintf(loError, "Error: wrong bitmap width: %d!", loWidth);
        printf("%s\n", loError);
        return false;
    }        
    loHeight=aOldResourceBuffer[loPos+2] | (aOldResourceBuffer[loPos+3]<<8);
    if (loHeight <= 0 || loHeight > MAX_BITMAP_WIDTH)
    {
        char loError[256];
        sprintf(loError, "Error: wrong bitmap height: %d!", loHeight);
        printf("%s\n", loError);
        return false;
    }        
    loPos+=4;       
    printf("    Image size is %dx%d\n", loWidth, loHeight);

    if (prepareNewBitmapSubpictureHeader(aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer,
        loWidth, loHeight) == false)
    {
        printf("Failure while generating the new bitmap subpicture header\n");        
        return false;
    }

    // zero position indicators
    loFuturePositionX = 0;
    loFuturePositionY = 0;

    // go through the picture
    for(;;)
    {
        int loY;
        loY = aOldResourceBuffer[loPos];
        
        #ifdef BITMAP_CONVERSION_DEBUG
        printf("      Y value: %d\n", loY);
        #endif
        
        if (loY==0xff)
        {
            // end of a subpicture
            // add the last end of line and remaining empty lines if needed
            // (till the height of the picture is filled)
            #ifdef BITMAP_CONVERSION_DEBUG
            printf("            going to generate skip sequences before ending...\n");
            #endif
            if (generateSkipSequencesAndEmptyLines(&loFuturePositionX, &loFuturePositionY, 0, loHeight,
                    aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer) == false)
            {
                printf("Failure while generating lines to finish the picture!\n");
                return false;
            }            
            break;
        }

        if ((loY<0) || (loY>=loHeight))
        {
            char loError[256];
            sprintf(loError, "Error: probably out of sync. Reported y-coord: %d", loY);
            printf("%s\n", loError);                
            return false;
        }
        loPos++;

        for(;;)
        {
            int loX;
            int loIsLast;
            int loRLE_width;
            //int loRLE_bytes;
                
            loX=aOldResourceBuffer[loPos];
            
            #ifdef BITMAP_CONVERSION_DEBUG
            printf("        X value: %d\n", loX);
            #endif
            
            loPos++;

            loIsLast=aOldResourceBuffer[loPos];
            loPos++;

            loRLE_width=aOldResourceBuffer[loPos];
            loPos++;

            //loRLE_bytes=aOldResourceBuffer[loPos];
            loPos++;

            while(loRLE_width>0)
            {
                int loMode=aOldResourceBuffer[loPos]&1;
                int loAmount=(aOldResourceBuffer[loPos]>>1)+1;
                loPos++;
                        
                if (loMode==0)    // Copy
                {
                    //memcpy(loIndexedBitmap+loX+loY*loWidth, loBuffer+loPos, loAmount);
                    if (processOldSubpictureCopySequence(loX, loY,
                        aOldResourceBuffer, aOldResourceBufferLength, loPos, loAmount,
                        aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer,
                        &loFuturePositionX, &loFuturePositionY) == false)
                    {
                        printf("Processing of an old copy sequence failed!\n");
                        return false;
                    }                    
                    loPos+=loAmount;
                }
                else if (loMode==1) // Fill
                {
                    int loValue=aOldResourceBuffer[loPos];
                    loPos++;
                    //memset(loIndexedBitmap+loX+loY*loWidth,loValue, loAmount);
                    if (processOldSubpictureFillSequence(loX, loY,
                        aOldResourceBuffer, aOldResourceBufferLength, loValue, loAmount,
                        aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer,
                        &loFuturePositionX, &loFuturePositionY) == false)
                    {
                        printf("Processing of an old fill sequence failed!\n");
                        return false;
                    }
                }
                loX+=loAmount;
                loRLE_width-=loAmount;
            }

            if (loRLE_width!=0)
            {
                char loError[256];
                sprintf(loError, "Error: out of sync while depacking RLE (rle_width=%d).", loRLE_width);
                printf("%s\n", loError);                   
                return false;
            }

            if (loIsLast==0x80)
            {
                // end of line
                break;
            }
        }           
    }
    return true;
}


/*
Generate skip sequences and empty lines between aFuturePositionX, aFuturePositionY and aOldX, aOldY
(not including aOldX, aOldY)
*/
int generateSkipSequencesAndEmptyLines(int *aFuturePositionX, int *aFuturePositionY,
    int aOldX, int aOldY, unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
    int *aNewBitmapBufferPointer)
{
    int i;
    int loSkipLength;

    #ifdef BITMAP_CONVERSION_DEBUG
    printf("            generating skip sequences and end of lines\n");
    #endif
    if (*aFuturePositionY > aOldY)
    {
        // the current position in the image is on higher line that where we want to add skip tokens...
        printf("generateSkipSequencesAndEmptyLines: aFuturePositionY > aOldY: %d > %d!\n",
        *aFuturePositionY, aOldY);
        return false;
    }
        
    // add end tokens (end of lines) for all lines lower than aOldY
    for(i = *aFuturePositionY; i < aOldY; i++)
    {
        if (addNewEndToken(aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer,
            aFuturePositionX, aFuturePositionY) == false)
        {
            printf("Unable to add a new end token in generateSjipSequencesAndEmptyLines!\n");            
            return false;
        }
    }
    loSkipLength = aOldX - *aFuturePositionX;
    if (loSkipLength > 0)
    {
        // add a skip token if needed
        if (addNewSkipToken(aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer,
            loSkipLength, aFuturePositionX) == false)
        {
            printf("Unable to add a new skip token in generateSjipSequencesAndEmptyLines!\n");            
            return false;
        }        
    }
    return true;
}

/*
Processes an "old" copy sequence
*/
int processOldSubpictureCopySequence(int aOldX, int aOldY,
        unsigned char *aOldResourceBuffer,  int aOldResourceBufferLength,
        int aPos, int aAmount, unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
        int *aNewBitmapBufferPointer, int *aFuturePositionX, int *aFuturePositionY)
{
    #ifdef BITMAP_CONVERSION_DEBUG
    printf("          Copy sequence, length %d\n", aAmount);
    printf("            going to generate skip sequences before string...\n");
    #endif
    if (generateSkipSequencesAndEmptyLines(aFuturePositionX, aFuturePositionY, aOldX, aOldY,
            aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer) == false)
    {
        printf("Failure while generating skip sequences and empty lines before processing an old copy sequence!\n");
        return false;
    }
    if (addNewStringToken(aOldResourceBuffer, aOldResourceBufferLength, aPos, aAmount,
        aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer, aFuturePositionX) == false)
    {
        printf("Unable to add a new string token!\n");
        return false;
    }
    return true;
}

/*
Processes an old "fill" sequence
*/
int processOldSubpictureFillSequence(int aOldX, int aOldY,
        unsigned char *aOldResourceBuffer, int aOldResourceBufferLength,
        char aValue, int aAmount, unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
        int *aNewBitmapBufferPointer, int *aFuturePositionX, int *aFuturePositionY)
{
    #ifdef BITMAP_CONVERSION_DEBUG
    printf("          Fill sequence, length %d, value %d\n", aAmount, (int)aValue);
    printf("            going to generate skip sequences before run...\n");
    #endif
    if (generateSkipSequencesAndEmptyLines(aFuturePositionX, aFuturePositionY, aOldX, aOldY,
            aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer) == false)
    {
        printf("Failure while generating skip sequences and empty lines before processing an old fill sequence!\n");
        return false;
    }
    if (addNewRunToken(aValue, aAmount, aNewBitmapBuffer, aNewBitmapBufferLength,
        aNewBitmapBufferPointer, aFuturePositionX) == false)
    {
        printf("Unable to add a new run token!\n");
        return false;
    }        
    return true;
}

/*
Adds a new end (end of line) token
*/
int addNewEndToken(unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
    int *aNewBitmapBufferPointer, int *aFuturePositionX, int *aFuturePositionY)
{
    #ifdef BITMAP_CONVERSION_DEBUG
    printf("              addNewEndToken\n");
    #endif
    if (*aNewBitmapBufferPointer >= aNewBitmapBufferLength)
    {
        printf("addNewEndToken: too small buffer for a new bitmap!\n");
        return false;
    }
    aNewBitmapBuffer[*aNewBitmapBufferPointer] = 0;
    (*aNewBitmapBufferPointer)++;
    // beginning of the next line
    (*aFuturePositionX) = 0;
    (*aFuturePositionY)++;
    return true;
}

/*
Add a new skip token
*/
int addNewSkipToken(unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
    int *aNewBitmapBufferPointer, int aLength, int *aFuturePositionX)
{
    #ifdef BITMAP_CONVERSION_DEBUG
    printf("              addNewSkipToken length: %d\n", aLength);
    #endif
    if (aLength <= 0)
    {
        printf("The length of the new skip token is zero or negative!\n");
        return false;
    }
    if (aLength > MAX_NEW_SKIP_LENGTH)
    {
        // handle too long skip sequences        
        // make skip sequences with half of the maximum length as long as needed
        int loOneHalf = MAX_NEW_SKIP_LENGTH/2;
        #ifdef BITMAP_CONVERSION_DEBUG
        printf("            too long skip token (it will be splitted): %d\n", aLength);
        #endif        
        while (aLength > MAX_NEW_SKIP_LENGTH)
        {
            if (addNewSkipToken(aNewBitmapBuffer, aNewBitmapBufferLength, aNewBitmapBufferPointer,
                loOneHalf, aFuturePositionX) == false)
            {
                printf("Creation of of half length skip token failed!\n");
                return false;
            }
            aLength = aLength - loOneHalf;  
        }        
    }
    if ((*aNewBitmapBufferPointer) + 2 >= aNewBitmapBufferLength)
    {
        printf("addNewSkipToken: too small buffer for a new bitmap!\n");
        return false;
    }
    aNewBitmapBuffer[*aNewBitmapBufferPointer] = 1;
    (*aNewBitmapBufferPointer)++;
    aNewBitmapBuffer[*aNewBitmapBufferPointer] = aLength;
    (*aNewBitmapBufferPointer)++;
    // position behind the skip
    (*aFuturePositionX) += aLength;
    #ifdef BITMAP_CONVERSION_DEBUG
    printf("              aFuturePositionX: %d\n", *aFuturePositionX);
    #endif
    return true;
}

/*
Add new string token
*/
int addNewStringToken(unsigned char *aOldResourceBuffer, int aOldResourceBufferLength, int aPos,
    int aAmount, unsigned char *aNewBitmapBuffer, int aNewBitmapBufferLength,
    int *aNewBitmapBufferPointer, int *aFuturePositionX)
{
    int loStringMarker;
    int i;

    #ifdef BITMAP_CONVERSION_DEBUG
    printf("            addNewStringToken length: %d\n", aAmount);
    #endif
    if (aPos + aAmount >= aOldResourceBufferLength)
    {
        printf("The old string sequence goes beyond the buffer containing the old bitmap!\n");
        return false;
    }
    if (aAmount <= 0)
    {
        printf("The length of the new string token is zero or negative!\n");
        return false;
    }

    if (aAmount > MAX_NEW_STRING_LENGTH)
    {
        // handle too long string sequences        
        // make string sequences with half of the maximum length as long as needed
        int loOneHalf = MAX_NEW_STRING_LENGTH/2;
        #ifdef BITMAP_CONVERSION_DEBUG
        printf("            too long string token (it will be splitted): %d\n", aAmount);
        #endif        
        while (aAmount > MAX_NEW_STRING_LENGTH)
        {
            if (addNewStringToken(aOldResourceBuffer, aOldResourceBufferLength, aPos,
                loOneHalf, aNewBitmapBuffer, aNewBitmapBufferLength,
                aNewBitmapBufferPointer, aFuturePositionX) == false)
            {
                printf("Creation of of half length string token failed!\n");
                return false;
            }
            aPos = aPos + loOneHalf;
            aAmount = aAmount - loOneHalf;  
        }        
    }

    if ((*aNewBitmapBufferPointer) + aAmount + 1 >= aNewBitmapBufferLength)
    {
        printf("addNewStringToken: too small buffer for a new bitmap!\n");
        return false;
    }    
    loStringMarker = 2 * aAmount + 1;
    aNewBitmapBuffer[*aNewBitmapBufferPointer] = loStringMarker;
    (*aNewBitmapBufferPointer)++;
    // copy the string
    for( i = 0; i < aAmount; i++)
    {
        aNewBitmapBuffer[(*aNewBitmapBufferPointer) + i] = aOldResourceBuffer[aPos + i];
    }
    // set the new bitmap pointer
    (*aNewBitmapBufferPointer) += aAmount;
    // position behind the string
    (*aFuturePositionX) += aAmount;
    #ifdef BITMAP_CONVERSION_DEBUG
    printf("              aFuturePositionX: %d\n", *aFuturePositionX);
    #endif
    return true;
}

/*
Add new run token
*/
int addNewRunToken(unsigned char aValue, int aAmount, unsigned char *aNewBitmapBuffer,
    int aNewBitmapBufferLength, int *aNewBitmapBufferPointer, int *aFuturePositionX)
{
    int loRunMarker;
    #ifdef BITMAP_CONVERSION_DEBUG
    printf("            addNewRunToken length: %d, value: %d\n", aAmount, (int)aValue);
    #endif
    if (aAmount <= 0)
    {
        printf("The length of the new fill token is zero or negative!\n");
        return false;
    }

    if (aAmount > MAX_NEW_RUN_LENGTH)
    {
        // handle too long string sequences       
        // make string sequences with half of the maximum length as long as needed
        int loOneHalf = MAX_NEW_RUN_LENGTH/2;
        #ifdef BITMAP_CONVERSION_DEBUG
        printf("            too long run token (it will be splitted): %d\n", aAmount);
        #endif         
        while (aAmount > MAX_NEW_RUN_LENGTH)
        {
            if (addNewRunToken(aValue, loOneHalf, aNewBitmapBuffer, aNewBitmapBufferLength,
                aNewBitmapBufferPointer, aFuturePositionX) == false)
            {
                printf("Creation of of half length run token failed!\n");
                return false;
            }
            aAmount = aAmount - loOneHalf;  
        }        
    }
   
    if ((*aNewBitmapBufferPointer) + aAmount + 1 >= aNewBitmapBufferLength)
    {
        printf("addNewRunToken: too small buffer for a new bitmap!\n");
        return false;
    }
    loRunMarker = 2 * aAmount;
    aNewBitmapBuffer[*aNewBitmapBufferPointer] = loRunMarker;
    (*aNewBitmapBufferPointer)++;
    // run pixel
    aNewBitmapBuffer[*aNewBitmapBufferPointer] = aValue;
    (*aNewBitmapBufferPointer)++;
    // position behind the run
    (*aFuturePositionX) += aAmount;
    #ifdef BITMAP_CONVERSION_DEBUG
    printf("              aFuturePositionX: %d\n", *aFuturePositionX);
    #endif
    return true;
}

/*
Converts the font resource aResourceNumber from old (EOB 3) format to the new (AESOP/32) format
(changes the resource file)
*/
int convertOneOldFont(FILE *aNewFile, DIRPOINTER *aNewFileDirectoryPointers, struct RESGlobalHeader *aNewFileHeader,
        int aResourceNumber, char *aResourceName)
{
    unsigned char *loOldResourceBuffer;
    int loOldResourceLength;
    unsigned char *loNewResourceBuffer;
    int loNewResourceLength;
    
    printf("Converting the font resource number %d, name \"%s\" ...\n", aResourceNumber,
        aResourceName);
    loOldResourceBuffer = readResourceBinary(aResourceNumber, aNewFile, aNewFileDirectoryPointers,
        &loOldResourceLength);
    if (loOldResourceBuffer == NULL)
    {
        printf("Failed to read the resource number %d!\n", aResourceNumber);
        return false;
    }

    // get the converted font
    loNewResourceBuffer = getNewFontForOldFont(loOldResourceBuffer, loOldResourceLength, &loNewResourceLength);
    if (loNewResourceBuffer == NULL)
    {
        printf("Failed to get the converted Font for the resource number %d!\n", aResourceNumber);
        free(loOldResourceBuffer);
        return false;
    }
    else
    {
        printf("The converted font length is: %d bytes\n", loNewResourceLength);
    }

    // replace the old font by a new one
    if (replaceResourceInOpenedFile(aResourceName, aResourceNumber, loNewResourceBuffer,
            loNewResourceLength, aNewFile, aNewFileHeader, aNewFileDirectoryPointers, false) == false)
    {
        printf("Failed to replace the old font by a new one (resource number %d)!\n", aResourceNumber);
        free(loOldResourceBuffer);
        free(loNewResourceBuffer);
        return false;
    }

    free(loOldResourceBuffer);
    free(loNewResourceBuffer);
    return true;
}

/*
Allocate a big buffer for the new font
*/
unsigned char *allocateNewFontBuffer(unsigned int aOldFontHeaderSize, unsigned int *aNewFontBufferLength)
{
    int loAllocatedSize;
    unsigned char *loResult;
    // just some big value
    loAllocatedSize = 2 * (aOldFontHeaderSize + 5000);
    loResult = (unsigned char*)malloc(loAllocatedSize);
    if (loResult == NULL)
    {
        printf("Unable to allocate %d bytes for a new font buffer!\n", loAllocatedSize);
        return NULL;
    }
    else
    {
        *aNewFontBufferLength = loAllocatedSize;
        return loResult;
    }    
}

/*
Free old character definition table
*/
void freeOldCharacterDefinitionTable(unsigned char **aOldCharacterDefinitionTable)
{
    int i;
    if (aOldCharacterDefinitionTable == NULL)
    {
        printf("freeOldCharacterDefinitionTable(...) was called for NULL!\n");
        return;
    }    
    for(i = 0; i < MAX_CHARACTERS_IN_OLD_FONT; i++)
    {
        if (aOldCharacterDefinitionTable[i] != NULL)
        {
            free(aOldCharacterDefinitionTable[i]);
        }
    }
    free(aOldCharacterDefinitionTable);    
}

/*
Convert the old font to a new one
*/
unsigned char *getNewFontForOldFont(unsigned char *aOldResourceBuffer, unsigned int aOldResourceLength,
    int *aNewResourceLength)
{
    unsigned char *loNewFontBuffer = NULL;
    unsigned int loNewFontBufferLength = 0;
    int loNewFontBufferPointer = 0;
    struct OLD_FONT_HEADER loOldFontHeader;
    int loFontHeight;
    int loPos = 0;
    unsigned char **loOldCharacterDefinitions;
    int i;
    int loNewOffsetsStart;
    int loNumberOfCharacters;

    if (aOldResourceLength < sizeof(struct OLD_FONT_HEADER))
    {
        printf("The old font resource is too small: %d\n", aOldResourceLength);
        return NULL;
    }
    // read the font header
    memcpy(&loOldFontHeader, aOldResourceBuffer, sizeof(struct OLD_FONT_HEADER));
    loPos += sizeof(struct OLD_FONT_HEADER);
    // I am not sure about the header, so better to check for unexpected things
    if (loOldFontHeader.header4 != 0 || loOldFontHeader.header5 != 0 || loOldFontHeader.header6 != 0 ||
        loOldFontHeader.header7 != 0 || loOldFontHeader.char_count == 0 ||
        loOldFontHeader.char_count > MAX_CHARACTERS_IN_OLD_FONT || loOldFontHeader.char_height == 0)
    {
        printf("Unexpected content found in the header of the old font!\n");
        return NULL;
    }

    loFontHeight = loOldFontHeader.char_height;
    if (loFontHeight <= 0 || loFontHeight > MAX_FONT_HEIGHT)
    {
        printf("The font height is out of range: %d", loFontHeight);
        return NULL;
    }

    loNumberOfCharacters = loOldFontHeader.char_count;    

    printf("The font contains %d characters with height %d\n", loNumberOfCharacters, loFontHeight);
    
    loPos += MAX_CHARACTERS_IN_OLD_FONT;

    loOldCharacterDefinitions = (unsigned char **)malloc(MAX_CHARACTERS_IN_OLD_FONT * sizeof (unsigned char *));
    if (loOldCharacterDefinitions == NULL)
    {
        printf("Unable to allocate memory for the old characters definition table.\n");
        return NULL;
    }
    // set entries to NULL
    for(i = 0; i < MAX_CHARACTERS_IN_OLD_FONT; i++)
    {
        loOldCharacterDefinitions[i] = NULL;
    }

    // read the old characters
    for(i = 0; i < loNumberOfCharacters; i++)
    {
         if (readOldCharacterDefinition(&(loOldCharacterDefinitions[i]), loPos, i, aOldResourceBuffer,
                aOldResourceLength, loFontHeight) == false)
         {
             // error while reading an old character definition
             printf("Error while reading an old character definition number: %d\n", i);
             freeOldCharacterDefinitionTable(loOldCharacterDefinitions);
             return NULL;
         }
    }    
            
    // allocate the space for a new font
    loNewFontBuffer = allocateNewFontBuffer(aOldResourceLength, &loNewFontBufferLength);
    if (loNewFontBuffer == NULL)
    {
        printf("Error: failure to allocate a new font buffer!\n");
        freeOldCharacterDefinitionTable(loOldCharacterDefinitions);
        return NULL;
    }

    if (prepareNewFontHeader(loNumberOfCharacters, loFontHeight, loNewFontBuffer,
        loNewFontBufferLength, &loNewFontBufferPointer) == false)
    {
        printf("Unable to create the header for the new font!\n");
        freeOldCharacterDefinitionTable(loOldCharacterDefinitions);
        free(loNewFontBuffer);
        return NULL;
    }

    loNewOffsetsStart = loNewFontBufferPointer;
    
    // reserve the place for pointers to the new characters
    loNewFontBufferPointer += (loNumberOfCharacters * 4);

    for(i = 0; i < loNumberOfCharacters; i++)
    {
        if (convertOldAndStoreNewCharacter(loOldCharacterDefinitions[i], i, loFontHeight, loNewFontBuffer,
            loNewFontBufferLength, &loNewFontBufferPointer, loNewOffsetsStart) == false)
        {
            printf("Failure while converting the old character number: %d!\n", i);
            freeOldCharacterDefinitionTable(loOldCharacterDefinitions);
            free(loNewFontBuffer);
            return NULL;            
        }
    }

    // loNewFontBufferPointer points to the first not used byte in the new buffer (so it is in fact the length)
    *aNewResourceLength = loNewFontBufferPointer;
    printf("The newly converted font has size %d bytes.\n", (int)*aNewResourceLength);
    freeOldCharacterDefinitionTable(loOldCharacterDefinitions);
    return loNewFontBuffer;
}

/*
Read one old character definition
*/
int readOldCharacterDefinition(unsigned char **aResult, int aStartPos, int aChar, unsigned char *aOldResourceBuffer,
    unsigned int aOldResourceLength, int aHeight)
{
    unsigned int loPointerAddress;
    unsigned int loPointer;
    int loColumns;
    int loOldCharacterDefinitionSizeInBytes;

    loPointerAddress = 0x108 + aChar * 2;

    #ifdef FONT_CONVERSION_DEBUG
    printf("\n  The character number %d has its pointer on the address %d\n", aChar, loPointerAddress);
    #endif
    
    if (loPointerAddress + 2 > aOldResourceLength)
    {
        printf("The pointer address is outside of resource!\n");
        return false;
    }
    loPointer = aOldResourceBuffer[loPointerAddress] | (aOldResourceBuffer[loPointerAddress + 1]<<8);

    #ifdef FONT_CONVERSION_DEBUG
    printf("  The value of the pointer is: %d\n", loPointer);
    #endif
    
    if (loPointer + 2 > aOldResourceLength)
    {
        printf("The pointer points outside of resource!\n");
        return false;
    }
    // read the number of columns
    loColumns = aOldResourceBuffer[loPointer] | (aOldResourceBuffer[loPointer + 1]<<8);    
    loOldCharacterDefinitionSizeInBytes = 2 + (loColumns * aHeight);

    #ifdef FONT_CONVERSION_DEBUG
    printf("  The number of columns is: %d\n", loColumns);
    printf("  The old character definition size is: %d bytes.\n", loOldCharacterDefinitionSizeInBytes);
    #endif
    
    if (loPointer + loOldCharacterDefinitionSizeInBytes > aOldResourceLength)
    {
        printf("The character definition is outside of resource!\n");
        return false;
    }

    *aResult = (unsigned char *)malloc(loOldCharacterDefinitionSizeInBytes);
    if (*aResult == NULL)
    {
        printf("Unable to allocate %d bytes for the definition of the old character number: %d!\n", loOldCharacterDefinitionSizeInBytes, aChar);
        return false;
    }
    // copy the definition
    memcpy(*aResult, aOldResourceBuffer + loPointer, loOldCharacterDefinitionSizeInBytes);
   
    return true;
}

/*
Create header for the new font
*/
int prepareNewFontHeader(int aNumberOfCharacters, int aFontHeight, unsigned char *aNewFontBuffer,
        unsigned int aNewFontBufferLength, int *aNewFontBufferPointer)
{
    struct NEW_FONT_HEADER loNewFontHeader;
    if (sizeof(struct NEW_FONT_HEADER) > aNewFontBufferLength)
    {
        printf("The buffer is too small for a new font header!\n");
        return false;
    }
    loNewFontHeader.version1 = '2';
    loNewFontHeader.version2 = '.';
    loNewFontHeader.version3 = '\0';
    loNewFontHeader.version4 = '\0';
    loNewFontHeader.char_count = aNumberOfCharacters;
    loNewFontHeader.char_height = aFontHeight;
    loNewFontHeader.font_background = 0;
    memcpy(aNewFontBuffer, &loNewFontHeader, sizeof(struct NEW_FONT_HEADER));
    *aNewFontBufferPointer += sizeof(struct NEW_FONT_HEADER);
    return true;
}

/*
Convert old character ans store it as a new character
*/
int convertOldAndStoreNewCharacter(unsigned char *aOldCharacterDefinition, int aChar, int aFontHeight,
        unsigned char *aNewFontBuffer, unsigned int aNewFontBufferLength, int *aNewFontBufferPointer,
        int aNewOffsetsStart)
{
    int loOldColumns;
    unsigned int loOldCharacterDefinitionSizeInBytes;
    unsigned int loNewCharacterDefinitionSizeInBytes;
    unsigned int loPos;
    unsigned int loCharacterDefinitionStart = *aNewFontBufferPointer;
    unsigned int loNewColumns;
    unsigned int loOffsetPosition;
    
    if (aOldCharacterDefinition == NULL)
    {
        printf("The pointer to the converted character definition is NULL!\n");
        return false;
    }

    // in old font format, one byte is used for two pixels
    // loOldColumns is number of pixels in line / 2
    loOldColumns = aOldCharacterDefinition[0] | (aOldCharacterDefinition[ 1]<<8);    
    loOldCharacterDefinitionSizeInBytes = 2 + (loOldColumns * aFontHeight);

    // in new font format, one byte is used for two pixels
    // loNewColumns is number of pixels in line / 2
    loNewColumns = loOldColumns;
    loNewCharacterDefinitionSizeInBytes = 4 + (loNewColumns * aFontHeight);

    if (*aNewFontBufferPointer + loNewCharacterDefinitionSizeInBytes > aNewFontBufferLength)
    {
        printf("There is no space in buffer for a new character!\n");
        return false;
    }

    // store the new width
    memcpy(aNewFontBuffer + (*aNewFontBufferPointer), &loNewColumns, 4);
    (*aNewFontBufferPointer)+=4;

    loPos = 2;
    for(; loPos < loOldCharacterDefinitionSizeInBytes; loPos++)
    {
        aNewFontBuffer[*aNewFontBufferPointer] = aOldCharacterDefinition[loPos];
        (*aNewFontBufferPointer)++;                      
    }
    
    // set the pointer (offset) to the character
    loOffsetPosition = aNewOffsetsStart + (aChar * 4);
    if (loOffsetPosition + 4 > aNewFontBufferLength)
    {
        printf("The offset position of the new characters is outside of the buffer!\n");
        return false;
    }    
    memcpy(aNewFontBuffer + loOffsetPosition, &loCharacterDefinitionStart, 4);

    #ifdef FONT_CONVERSION_DEBUG
    printf("  Converting the character number %d\n", aChar);
    printf("    Character definition start: %d\n", (int)loCharacterDefinitionStart);
    printf("    Behind the character definition: %d\n", (int)(*aNewFontBufferPointer));
    printf("    Old columns: %d, old size: %d\n", loOldColumns, loOldCharacterDefinitionSizeInBytes);
    printf("    New columns: %d, new size: %d\n", loNewColumns, loNewCharacterDefinitionSizeInBytes);
    #endif
    
    return true;
    
}

/*
Patches the menu in EYE.RES from the EOB 3 so that it is usable in the AESOP/32
(changes the resource file)
*/
int patchEOB3MenuInOpenedFile(FILE *aNewFile, DIRPOINTER *aNewFileDirectoryPointers, struct RESGlobalHeader *aNewFileHeader)
{
    int loResourceNumber = 1374;
    char loResourceName[] = "menu";
    char loFoundResourceName[256];
    unsigned char *loResourceBuffer;
    int loFoundResourceLength;

    printf("Patching the resource number %d, name \"%s\" ...\n", loResourceNumber,
        loResourceName);
       
    if (getResourceName(loFoundResourceName, 1374) == NULL)
    {
        printf("The resource number %d (%s)was not found!\n", loResourceNumber, loResourceName);
        return false;
    }

    if (strcmp(loResourceName, loFoundResourceName) != 0)
    {
        printf("The resource number %d has name %s (the program expected %s)!\n", loResourceNumber, loFoundResourceName,
            loResourceName);
        return false;
    }

    loResourceBuffer = readResourceBinary(loResourceNumber, aNewFile, aNewFileDirectoryPointers,
        &loFoundResourceLength);
    if (loResourceBuffer == NULL)
    {
        printf("Failed to read the resource number %d!\n", loResourceNumber);
        return false;
    }
    /* patch (addr. inside the resource, old, new)
    00000345: 00 FF
    00000346: 00 FF
    00000348: FF 00
    00000349: 0F 1A
    */

    if (patchOneByte(loResourceBuffer, loFoundResourceLength, 0x345, 0x00, 0xff) == false)
    {
        free(loResourceBuffer);
        return false;
    }

    if (patchOneByte(loResourceBuffer, loFoundResourceLength, 0x346, 0x00, 0xff) == false)
    {
        free(loResourceBuffer);
        return false;
    }

    if (patchOneByte(loResourceBuffer, loFoundResourceLength, 0x348, 0xff, 0x00) == false)
    {
        free(loResourceBuffer);
        return false;
    }

    if (patchOneByte(loResourceBuffer, loFoundResourceLength, 0x349, 0x0f, 0x1a) == false)
    {
        free(loResourceBuffer);
        return false;
    }

    // replace the old resource by a new one
    if (replaceResourceInOpenedFile(loResourceName, loResourceNumber, loResourceBuffer, loFoundResourceLength, aNewFile, aNewFileHeader, aNewFileDirectoryPointers, false) == false)
    {
        printf("Failed to replace the old resource %s by a new one (resource number %d)!\n",
            loResourceName, loResourceNumber);
        free(loResourceBuffer);
        return false;
    }
    else
    {
        free(loResourceBuffer);
        return true;
    }                     
}

/*
Patch one byte in the provided binary
*/
int patchOneByte(unsigned char *aBinary, int aBinaryLength, int aAddress, unsigned char aOldValue,
    unsigned char aNewValue)
{
    if (aBinary == NULL)
    {
        printf("patchOneByte: aBinary is NULL!\n");
        return false;
    }
    if (aAddress < 0 || aAddress >= aBinaryLength)
    {
        printf("Attempt to patch the address %d which is outside of the provided binary!\n", aAddress);
        return false;
    }
    if (aBinary[aAddress] != aOldValue)
    {
        printf("The value %d was expected on the address %d, but %d was found!\n", (int)aOldValue, aAddress,
            (int)(aBinary[aAddress]));
        return false;
    }
    aBinary[aAddress] = aNewValue;
    printf("The address %d was succesfully patched to %d.\n", aAddress, (int)aNewValue);
    return true;
}
