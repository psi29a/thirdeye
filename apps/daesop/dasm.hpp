///////////////////////////////////////////////////////////////////////////////
//
// DAESOP
// using code from AESOP engine and ReWiki website
// (c) Mirek Luza
// public domain software
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DASM_H
#define DASM_H

#include <stdio.h>

#include "tdefs.hpp"
#include "dict.hpp"
#include "utils.hpp"
#include "damap.hpp"
#include "dvar.hpp"

#define MAX_BYTECODES 256
#define BYTECODE_DEFINITION_FILE "abc_list.def"
#define MAX_TOKENS 20
#define PARAMETERS_HANDLED_BY_CODE 99

// important instructions needing a special handling
#define INSTRUCTION_BRT  0x00
#define INSTRUCTION_BRF  0x01
#define INSTRUCTION_BRA  0x02

#define INSTRUCTION_CASE 0x03

#define INSTRUCTION_SHTC 0x1D
#define INSTRUCTION_INTC 0x1E
#define INSTRUCTION_LNGC 0x1F

#define INSTRUCTION_RCRS 0x20

#define INSTRUCTION_JSR  0x24
#define INSTRUCTION_RTS  0x25

#define INSTRUCTION_LTBA 0x28
#define INSTRUCTION_LTWA 0x29
#define INSTRUCTION_LTDA 0x2A
#define INSTRUCTION_LETA 0x2B

#define INSTRUCTION_LAB  0x2C
#define INSTRUCTION_LAW  0x2D
#define INSTRUCTION_LAD  0x2E
#define INSTRUCTION_SAB  0x2F
#define INSTRUCTION_SAW  0x30
#define INSTRUCTION_SAD  0x31

#define INSTRUCTION_LABA 0x32
#define INSTRUCTION_LAWA 0x33
#define INSTRUCTION_LADA 0x34
#define INSTRUCTION_SABA 0x35
#define INSTRUCTION_SAWA 0x36
#define INSTRUCTION_SADA 0x37
#define INSTRUCTION_LEAA 0x38

#define INSTRUCTION_LSB  0x39
#define INSTRUCTION_LSW  0x3a
#define INSTRUCTION_LSD  0x3b
#define INSTRUCTION_SSB  0x3c
#define INSTRUCTION_SSW  0x3d
#define INSTRUCTION_SSD  0x3e

#define INSTRUCTION_LSBA 0x3f
#define INSTRUCTION_LSWA 0x40
#define INSTRUCTION_LSDA 0x41
#define INSTRUCTION_SSBA 0x42
#define INSTRUCTION_SSWA 0x43
#define INSTRUCTION_SSDA 0x44
#define INSTRUCTION_LESA 0x45

#define INSTRUCTION_LXB  0x46
#define INSTRUCTION_LXW  0x47
#define INSTRUCTION_LXD  0x48
#define INSTRUCTION_SXB  0x49
#define INSTRUCTION_SXW  0x4a
#define INSTRUCTION_SXD  0x4b

#define INSTRUCTION_LXBA 0x4c
#define INSTRUCTION_LXWA 0x4d
#define INSTRUCTION_LXDA 0x4e
#define INSTRUCTION_SXBA 0x4f
#define INSTRUCTION_SXWA 0x50
#define INSTRUCTION_SXDA 0x51
#define INSTRUCTION_LEXA 0x52

#define INSTRUCTION_LECA 0x54
#define INSTRUCTION_END  0x56

// meta instructions
#define META_DISASSEMBLER_VERSION            ".DISASSEMBLER_VERSION  "
#define META_OBJECT_NAME                     ".OBJECT_NAME  "
#define META_PARENT_RESOURCE_NAME            ".PARENT_RESOURCE_NAME  "
#define META_MESSAGE_HANDLER_NAME            ".MESSAGE_HANDLER_NAME  "
#define META_LOCAL_VARIABLES_SIZE            ".LOCAL_VARIABLE_SIZE  "
#define META_ORIGINAL_PARENT_RESOURCE_NUMBER ".ORIGINAL_PARENT_RESOURCE_NUMBER"
#define META_ORIGINAL_CODE_RESOURCE_SIZE     ".ORIGINAL_CODE_RESOURCE_SIZE    "
#define META_ORIGINAL_CODE_RESOURCE_NUMBER   ".ORIGINAL_CODE_RESOURCE_NUMBER  "
#define META_ORIGINAL_IMPORT_RESOURCE_SIZE   ".ORIGINAL_IMPORT_RESOURCE_SIZE  "
#define META_ORIGINAL_IMPORT_RESOURCE_NUMBER ".ORIGINAL_IMPORT_RESOURCE_NUMBER"
#define META_ORIGINAL_EXPORT_RESOURCE_SIZE   ".ORIGINAL_EXPORT_RESOURCE_SIZE  "
#define META_ORIGINAL_EXPORT_RESOURCE_NUMBER ".ORIGINAL_EXPORT_RESOURCE_NUMBER"
#define META_ORIGINAL_STATIC_SIZE            ".ORIGINAL_STATIC_SIZE           "
#define META_PROCEDURE_NAME                  ".PROCEDURE  "
#define META_IMPORTED_RUNTIME_FUNCTION       ".IMPORTED_RUNTIME_FUNCTION  "
#define META_IMPORTED_VARIABLE               ".IMPORTED_VARIABLE  "
#define META_IMPORTED_ARRAY                  ".IMPORTED_ARRAY     "
#define META_PUBLIC_STATIC_ARRAY             ".PUBLIC_STATIC_ARRAY      "
#define META_PUBLIC_STATIC_VARIABLE          ".PUBLIC_STATIC_VARIABLE   "
#define META_PRIVATE_STATIC_ARRAY            ".PRIVATE_STATIC_ARRAY     "
#define META_PRIVATE_STATIC_VARIABLE         ".PRIVATE_STATIC_VARIABLE  "
#define META_TABLE_BYTE                      ".META_TABLE_BYTE  "
#define META_TABLE_WORD                      ".META_TABLE_WORD  "
#define META_TABLE_LONG                      ".META_TABLE_LONG  "
#define META_PARAMETER_ARRAY                 ".META_PARAMETER_ARRAY"
#define META_PARAMETER_VARIABLE              ".META_PARAMETER_VARIABLE"
#define META_LOCAL_ARRAY                     ".META_LOCAL_ARRAY"
#define META_LOCAL_VARIABLE                  ".META_LOCAL_VARIABLE"
#define META_DISASSEMBLER_END                ".DISASSEMBLER_END               "

// disassembler version
#define DISASSEMBLER_VERSION 4

// formatting of the output
#define NUMBER_OF_HEX_CODES_IN_DISSASSEMBLY 6
#define CASE_ENTRY                          "CASE_ENTRY"
#define LABEL_PREFIX                        "LBL_"
#define CASE_DEFAULT                        "CASE_DEFAULT"
#define INSTRUCTION_FORMAT_STRING           "%-6s  "
#define PROCEDURE_PREFIX                    "PROCEDURE_"

// auto variables in this range are local variables
#define LOCAL_VARIABLES_START_INDEX          3
#define LOCAL_VARIABLES_END_INDEX            32767

struct BYTECODE
{
    int number;
    char *name;
    int paramCount;
    char *paramString;
    char *explanation;    
};


struct SOP_script_header
{
  UWORD static_size;       // probably size of class variables/constants (??)
  ULONG import_resource;  // the number of the corresponding import resource
  ULONG export_resource;   // the number of the corresponding export resource
  ULONG parent;            // the number of parent object (ffffffff if none) 
};


typedef struct BYTECODE *BYTECODEPOINTER;

BYTECODEPOINTER *readBytecodeDefinition(void);
int processBytecodeDefinitionLine(char *aLine);
void disassembleCodeResource(int aCodeResourceNumber, unsigned char *aResource, int aLength,
    IMPORTENTRYPOINTER *aFullImportResourceDictionary, int aImportResourceSize,
    EXPORTENTRYPOINTER *aFullExportResourceDictionary, int aExportResourceSize,
    FILE *aOutputFile, FILE *aResFile, DIRPOINTER *aDirectoryPointers);
int makeFirstDisassemblyPass(unsigned char *aResource, int aLength, int aStartAddress, IMPORTENTRYPOINTER *aFullExportResourceDictionary);
int getInstructionLength(unsigned char *aResource, int aResourceLength, int aCurrentAddress);
void setTargetsForTheInstruction(unsigned char *aResource, int aLength, int aOriginalAddress);
int endsDisassembly(unsigned char *aResource, int aLength, int aCurrentAddress);
int getParameterAsNumber(unsigned char *aResource, int aLength, char aParameterType, long *aParameterValue, int *aCurrentAddress);
char *getRuntimeCodeFunctionName(char *aResult, long aFunctionNumber, IMPORTENTRYPOINTER *aFullImportResourceDictionary);
void makeSecondDisassemblyPass(unsigned char *aResource, int aLength, IMPORTENTRYPOINTER *aFullImportResourceDictionary,
        EXPORTENTRYPOINTER *aFullExportResourceDictionary, FILE *aOutputFile, FILE *aResFile,
        DIRPOINTER *aDirectoryPointers, RESINFOPOINTER *aResourcesInfoTable);
char *getMessageHandlerNameForAddress(int aEntryPointAddress, EXPORTENTRYPOINTER *aFullExportResourceDictionary);
void getDBByteString(char *aResult, unsigned char *aResource, int aResourceLength, int aAddress);
void getDWWordString(char *aResult, unsigned char *aResource, int aResourceLength, int aAddress);
void getDLLongString(char *aResult, unsigned char *aResource, int aResourceLength, int aAddress);
int writeOneInstruction(unsigned char *aResource, int aLength, int *loCurrentAddress, IMPORTENTRYPOINTER *aFullImportResourceDictionary,
        FILE *aOutputFile, FILE *aResFile, DIRPOINTER *aDirectoryPointers, RESINFOPOINTER *aResourcesInfoTable);
void getHexCodes(char *aString, unsigned char *aResource, int aBufferLength, int aStartAddress, int aSize);
void writeLabel(int aAddress, FILE *aOutputFile);
int getParameterString(char *aString, unsigned char aParameterType, long aParameterValue,
    int aAddParameterComment, int aAddCharactersInTheComment);
void handleVariableAndArrayRelatedInstructions(unsigned char *aResource, int aLength, int aOriginalAddress,
        IMPORTENTRYPOINTER *aFullImportResourceDictionary);
char getUppercaseVariableType(int aInstruction);   

// CASE instruction handling
void writeCaseHeader(unsigned char *aResource, int aLength, int aStartAddress, long aCaseOptions, FILE *aOutputFile);
void writeCaseEntry(unsigned char *aResource, int aLength, int aStartAddress, long aValue, int aTarget, FILE *aOutputFile);
void writeCaseDefault(unsigned char *aResource, int aLength, int aStartAddress, int aTarget, FILE *aOutputFile);

// parameters, local variables
int getAddressBehindTheFunction(int aStartAddress, int aLength);

// comment about the referred resource
void writeInfoAboutReferredResourceIfAvailable(long aParameterValue, RESINFOPOINTER *aResourcesInfoTable,
    FILE *aOutputFile);
   
#endif

