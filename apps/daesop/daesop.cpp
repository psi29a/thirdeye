///////////////////////////////////////////////////////////////////////////////
//
// DAESOP
// using code from AESOP engine and ReWiki website
// (c) Mirek Luza and Bret Curtis
// public domain software
//
// WARNING: The structure alignment must be set to 1 for this program to work!
//          RES files depend on the structure alignment - EOB 3 uses 1.
//          Remember to set this in compiler options.
//
///////////////////////////////////////////////////////////////////////////////


#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

#include "daesop.hpp"
#include "dblocks.hpp"
//#include "dict.h"
//#include "utils.h"
//#include "rentry.h"
//#include "dasm.h"
//#include "convert.h"

char myResName[256];
struct RESGlobalHeader myHeader;
char myHomeDirectory[256];


/*************************************************************/
int main(int argc, char *argv[])
{
    char loFunctionOption[256];
    int loFunction = NOTHING;
    int i;
    DIRPOINTER loDirectoryPointers[MAX_DIRECTORIES];
    FILE *loResFile;
    int loResult;
    int loNumberOfRequiredParameters;

    // store the home directory
    strcpy(myHomeDirectory, argv[0]);
    for(i = strlen(myHomeDirectory) - 1; i >= 0; i--)
    {
        if (myHomeDirectory[i] == '/' || myHomeDirectory[i] == '\\')
        {
            myHomeDirectory[i + 1] = '\0';
            break;
        }
    }

    // empty table
    for(i = 0; i < MAX_DIRECTORIES; i++)
    {
        loDirectoryPointers[i] = NULL;
    }

    printf("AESOP decompiler version: %1.2f\n", VERSION);

    if (argc < 3)
    {
        syntaxInformation();
        return 1;
    }

    // decide what to do
    strcpy(loFunctionOption, argv[1]);

    if (strcmp(loFunctionOption,"/i") == 0)
    {
        loFunction = GET_INFORMATION;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/ir") == 0)
    {
        loFunction = GET_RESOURCES_INFORMATION;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/ioff") == 0)
    {
        loFunction = GET_OFFSET_INFORMATION;
        loNumberOfRequiredParameters = 2;
    }
    else if (strcmp(loFunctionOption,"/j") == 0)
    {
        loFunction = GET_RESOURCE_INFORMATION_BY_NAME;
        loNumberOfRequiredParameters = 2;
    }
    else if (strcmp(loFunctionOption,"/k") == 0)
    {
        loFunction = GET_RESOURCE_INFORMATION_BY_NUMBER;
        loNumberOfRequiredParameters = 2;
    }
    else if (strcmp(loFunctionOption,"/e") == 0)
    {
        loFunction = GET_RESOURCE_BY_NAME;
        loNumberOfRequiredParameters = 2;
    }
    else if (strcmp(loFunctionOption,"/eh") == 0)
    {
        loFunction = GET_RESOURCE_WITH_HEADER_BY_NAME;
        loNumberOfRequiredParameters = 2;
    }
    else if (strcmp(loFunctionOption,"/x") == 0)
    {
        loFunction = GET_RESOURCE_BY_NUMBER;
        loNumberOfRequiredParameters = 2;
    }
    else if (strcmp(loFunctionOption,"/xh") == 0)
    {
        loFunction = GET_RESOURCE_WITH_HEADER_BY_NUMBER;
        loNumberOfRequiredParameters = 2;
    }
    else if (strcmp(loFunctionOption,"/test_old_bitmaps") == 0)
    {
        loFunction = TEST_OLD_BITMAPS;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/r") == 0)
    {
        loFunction = REPLACE_BY_RESOURCE;
        loNumberOfRequiredParameters = 3;
    }
    else if (strcmp(loFunctionOption,"/rh") == 0)
    {
        loFunction = REPLACE_BY_RESOURCE_WITH_HEADER;
        loNumberOfRequiredParameters = 3;
    }
    else if (strcmp(loFunctionOption,"/create_tbl") == 0)
    {
        loFunction = CREATE_TBL_FILE;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/cob") == 0)
    {
        loFunction = CONVERT_OLD_BITMAPS;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/convert_old_bitmaps") == 0)
    {
        loFunction = CONVERT_OLD_BITMAPS_IGNORE_ERRORS;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/cof") == 0)
    {
        loFunction = CONVERT_OLD_FONTS;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/convert_old_fonts") == 0)
    {
        loFunction = CONVERT_OLD_FONTS_IGNORE_ERRORS;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/eob3menupatch") == 0)
    {
        loFunction = PATCH_EOB3_MENU;
        loNumberOfRequiredParameters = 1;
    }
    else if (strcmp(loFunctionOption,"/eob3conv") == 0)
    {
        loFunction = CONVERT_EOB3_TO_AESOP32;
        loNumberOfRequiredParameters = 1;
    }
    else
    {
        syntaxInformation();
        return 1;
    }

   if (argc != loNumberOfRequiredParameters + 3)
   {
        syntaxInformation();
        return 1;
   }

    // res name
    strcpy(myResName,argv[2]);
    for (i = 0; i <strlen(myResName); i++)
    {
        if (myResName[i] == '.')
        {
            myResName[i] = 0;
            break;
        }
   }
   strcat(myResName,".RES");


   // Begin processing
   printf("Processing AESOP resource file: %s ...\n", myResName);

   loResFile = openAESOPResourceAndSetToFirstDirectoryBlock(myResName, "rb", &myHeader);
   if (loResFile == NULL)
   {
       return 2;
   }

   loResult = readDirectoryBlocks(loResFile, loDirectoryPointers);
   if (loResult == false)
   {
       printf("The reading of directory blocks failed!\n");
       fclose(loResFile);
       return 4;
   }

   loResult = false;
   if (loFunction == GET_INFORMATION)
   {
       //loResult = getInformation(loResFile, loDirectoryPointers, argv[3]);
   }
   else if (loFunction == GET_RESOURCES_INFORMATION)
   {
       //loResult = getResourcesInformation(loResFile, loDirectoryPointers, argv[3]);
   }
   else if (loFunction == GET_OFFSET_INFORMATION)
   {
       //loResult = getOffsetInformation(loResFile, loDirectoryPointers, argv[3], argv[4]);
   }
   else if (loFunction == GET_RESOURCE_INFORMATION_BY_NAME ||
          loFunction == GET_RESOURCE_INFORMATION_BY_NUMBER)
   {
       //loResult = getResourceInformation(loResFile, loDirectoryPointers, loFunction, argv[3], argv[4]);
   }
   else if (loFunction == GET_RESOURCE_BY_NAME ||
          loFunction == GET_RESOURCE_WITH_HEADER_BY_NAME ||
          loFunction == GET_RESOURCE_BY_NUMBER ||
          loFunction == GET_RESOURCE_WITH_HEADER_BY_NUMBER)
   {
       //loResult = getResource(loResFile, loDirectoryPointers, loFunction, argv[3], argv[4]);
   }
   else if (loFunction == TEST_OLD_BITMAPS)
   {
       //loResult = testOldBitmaps(loResFile, loDirectoryPointers, argv[3]);
   }
   else if (loFunction == REPLACE_BY_RESOURCE)
   {
       char *loResNumber = argv[3];
       char *loAddedFile = argv[4];
       char *loNewName = argv[5];
       //loResult = replaceResourceByResourceFromFile(loResFile, loDirectoryPointers, loResNumber, loAddedFile, loNewName, false);
   }
   else if (loFunction == REPLACE_BY_RESOURCE_WITH_HEADER)
   {
       char *loResNumber = argv[3];
       char *loAddedFile = argv[4];
       char *loNewName = argv[5];
       //loResult = replaceResourceByResourceFromFile(loResFile, loDirectoryPointers, loResNumber, loAddedFile, loNewName, true);
   }
   else if (loFunction == CREATE_TBL_FILE)
   {
       //loResult = createTblFile(loResFile, loDirectoryPointers, argv[3]);
   }
   else if (loFunction == CONVERT_OLD_BITMAPS)
   {
       // convert bitmaps, end on any error
       //loResult = convertOldBitmaps(loResFile, loDirectoryPointers, argv[3], false);
   }
   else if (loFunction == CONVERT_OLD_BITMAPS_IGNORE_ERRORS)
   {
       // convert bitmaps, skip bitmaps with errors
       //loResult = convertOldBitmaps(loResFile, loDirectoryPointers, argv[3], true);
   }
   else if (loFunction == CONVERT_OLD_FONTS)
   {
       // convert fonts, end on any error
       //loResult = convertOldFonts(loResFile, loDirectoryPointers, argv[3], false);
   }
   else if (loFunction == CONVERT_OLD_FONTS_IGNORE_ERRORS)
   {
       // convert fonts, skip fonts with errors
       //loResult = convertOldFonts(loResFile, loDirectoryPointers, argv[3], true);
   }
   else if (loFunction == PATCH_EOB3_MENU)
   {
       // patch EOB 3 menu code
       //loResult = patchEOB3Menu(loResFile, loDirectoryPointers, argv[3]);
   }
   else if (loFunction == CONVERT_EOB3_TO_AESOP32)
   {
       // convert EOB 3
       //loResult = convertEOB3toAESOP32(loResFile, loDirectoryPointers, argv[3]);
   }
   fclose(loResFile);
   if (loResult == false)
   {
       printf("Finished with an error!\n");
   }
   else
   {
       printf("Finished OK.\n");
   }

   return 0;
}

/*
Syntax information
*/
void syntaxInformation(void)
{
    printf("        DAESOP COMMAND LINE PARAMETERS\n");
    printf("        Information:\n");
    printf("        daesop /i <res_file> <output_text_file>\n");
    printf("        daesop /ir <res_file> <output_text_file>\n");
    printf("        daesop /j <res_file> <resource_name> <output_text_file>\n");
    printf("        daesop /k <res_file> <resource_number> <output_text_file>\n");
    printf("        daesop /ioff <res_file> <offset> <output_text_file>\n");
    printf("        daesop /test_old_bitmaps <res_file> <output_text_file>\n");
    printf("        Extraction:\n");
    printf("        daesop /e <res_file> <resource_name> <output_binary_file>\n");
    printf("        daesop /eh <res_file> <resource_name> <output_binary_file>\n");
    printf("        daesop /x <res_file> <resource_number> <output_binary_file>\n");
    printf("        daesop /xh <res_file> <resource_number> <output_binary_file>\n");
    printf("        Conversion:\n");
    printf("        daesop /r <res_file> <resource_number> <new_resource_binary> <output_res_file>\n");
    printf("        daesop /rh <res_file> <resource_number> <new_resource_binary> <output_res_file>\n");
    printf("        daesop /eob3conv <res_file> <output_res_file>\n");
    printf("        daesop /create_tbl <res_file> <output_tbl_file>\n");
    printf("        daesop /cob <res_file> <output_res_file>\n");
    printf("        daesop /convert_old_bitmaps <res_file> <output_res_file>\n");
    printf("        daesop /cof <res_file> <output_res_file>\n");
    printf("        daesop /convert_old_fonts <res_file> <output_res_file>\n");
    printf("        daesop /eob3menupatch <res_file> <output_res_file>\n");
}
