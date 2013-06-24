///////////////////////////////////////////////////////////////////////////////
//
// DAESOP
// using code from AESOP engine and ReWiki website
// (c) Mirek Luza
// public domain software
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DAMAP_H
#define DAMAP_H

#include <stdio.h>

#include "tdefs.hpp"
#include "utils.hpp"

// constants from the disassembly map (they must be < 127)
// data
#define MAP_DATA_BYTE        0
#define MAP_DATA_WORD        1
#define MAP_DATA_LONG        2
#define MAP_DATA_STRING      3
#define MAP_DATA_CONTINUE    4
#define MAP_BYTE_TABLE_START 5
#define MAP_WORD_TABLE_START 6
#define MAP_LONG_TABLE_START 7
// message handler entries
#define MAP_MESSAGE_HANDLER_NOT_DONE    10
#define MAP_MESSAGE_HANDLER_PROCESSED   11
// code
#define MAP_CODE_START_NOT_DONE  20
#define MAP_CODE_START_PROCESSED 21
#define MAP_CODE_PROCESSED       22
// procedures
#define MAP_PROCEDURE_START_NOT_DONE  30
#define MAP_PROCEDURE_START_PROCESSED 31
// special data
#define MAP_SPECIAL 40
// error
#define MAP_ERROR 50

// the constant for specifying/testing "label" bit
#define LABEL_MASK 128
// constant to get normal values
#define VALUE_MASK 127

int createCodeMap(int aLength);
int setCodeMapAddress(int aAddress, unsigned char aValue);
int getCodeMapAddressValue(int aAddress);
int setLabelForAddress(int aAddress);
int hasAddressLabel(int aAddress);
int findFirstAddressForDisassembling(void);
int shouldBeAddressDisassembled(int aAddress);
int markAddressAsDisassembled(int aAddress);
int setJumpTarget(int aAddress);
int setProcedureStart(int aAddress);
void displayCodeTableMap(FILE *aOutputFile);
void fixCodeTableForConstantTables(void);
int getNumberOfTableElementsInRange(int aTableType, int aStart, int aBehindEnd);
int fillTheMapInCodeTable(int aTableType, int aStart, int aNumberOfElements);

#endif
