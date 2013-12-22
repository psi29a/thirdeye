#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "utils.hpp"
#include "damap.hpp"
#include "dvar.hpp"

#include "dasm.hpp"

BYTECODEPOINTER *bytecodeTable = NULL;

/*
 Read bytecode definition file
 */
BYTECODEPOINTER *readBytecodeDefinition(void) {
	FILE *loDefinitionFile;
	char loDefinitionFilePath[256];
	char loLine[256];
	int loBytecodeTableSize;

	//strcpy(loDefinitionFilePath, myHomeDirectory); #TODO: set to local directory, and eventually check in other locations
	strcat(loDefinitionFilePath, BYTECODE_DEFINITION_FILE);
	loDefinitionFile = fopen(loDefinitionFilePath, "r");
	if (loDefinitionFile == NULL) {
		printf("The attempt to open the bytecode definion file %s failed!\n",
				loDefinitionFilePath);
		return (NULL);
	}

	// allocate the field
	loBytecodeTableSize = MAX_BYTECODES * sizeof(BYTECODEPOINTER);
	bytecodeTable = (BYTECODEPOINTER *) malloc(loBytecodeTableSize);
	if (bytecodeTable == NULL) {
		printf(
				"Failure to allocate %d bytes for the bytecode definition table!",
				loBytecodeTableSize);
		fclose(loDefinitionFile);
		return (NULL);
	}
	// set everything to NULL
	for (unsigned int i = 0; i < MAX_BYTECODES; i++) {
		bytecodeTable[i] = NULL;
	}
	// read the definition file
	while (fgets(loLine, 256, loDefinitionFile) != NULL) {
		unsigned int i;
		int loResult;
		//printf("%s", loLine);
		// find the first non-empty character
		for (i = 0; i < strlen(loLine); i++) {
			if (loLine[i] != ' ' && loLine[i] != '\t') {
				break;
			}
		}
		if (i == strlen(loLine) || loLine[i] == ';') {
			// comment or an empty line, ignore
			continue;
		}
		loResult = processBytecodeDefinitionLine(loLine);
		if (loResult == false) {
			printf(
					"The analysis of the following line in the bytecode definition file failed:\n  %s\n",
					loLine);
			free(bytecodeTable); // well, this does not free everything, there can be some items already
			fclose(loDefinitionFile);
			bytecodeTable = NULL;
			return (NULL);
		}
	}
	return (bytecodeTable);
}

/*
 Process one line from the bytecode definition file
 */
int processBytecodeDefinitionLine(char *aLine) {
	char loLine[256];
	char *loTokens[MAX_TOKENS];
	char *loCommentStart;
	int i;
	int loBytecodeCode;
	char *loEndPtr;
	int loNumberOfParameters;
	BYTECODEPOINTER loBytecodeEntryPointer;
	int loEntryNumber;

	strncpy(loLine, aLine, 256 - 1);
	// set all tokens to NULL
	for (i = 0; i < MAX_TOKENS; i++) {
		loTokens[i] = NULL;
	}
	//printf("Processing line: %s", loLine);
	loCommentStart = strchr(loLine, ';');
	if (loCommentStart != NULL) {
		// ignore comment
		*loCommentStart = '\0';
	}

	loTokens[0] = strtok(loLine, ",");
	if (loTokens[0] == NULL) {
		printf("The line cannot be parsed: %s\n", loLine);
		return (false);
	}
	for (i = 1; i < MAX_TOKENS; i++) {
		loTokens[i] = strtok(NULL, ",");
		if (loTokens[i] == NULL) {
			break;
		}
	}
	/*
	 printf("\nTokens:\n");
	 for(i = 0; i < MAX_TOKENS; i++)
	 {
	 if (loTokens[i] != NULL)
	 {
	 printf("  Token: %s\n", loTokens[i]);
	 }
	 }
	 */

	loBytecodeCode = (int) strtol(loTokens[0], &loEndPtr, 16);
	if (*loEndPtr != '\0') {
		printf("The conversion of the hexadecimal number %s failed!\n",
				loTokens[0]);
		return (false);
	}
	//printf("loBytecodeCode: %d\n", loBytecodeCode);
	loNumberOfParameters = (int) strtol(loTokens[2], &loEndPtr, 10);
	if (*loEndPtr != '\0') {
		printf("The conversion of the decadic number %s failed!\n",
				loTokens[2]);
		return (false);
	}
	//printf("loNumberOfParameters: %d\n\n", loNumberOfParameters);
	if (loNumberOfParameters
			< 0|| loNumberOfParameters > PARAMETERS_HANDLED_BY_CODE) {
		printf("The nonesense number of parameters: %d\n",
				loNumberOfParameters);
		return (false);
	}
	loBytecodeEntryPointer = (BYTECODEPOINTER) malloc(sizeof(struct BYTECODE));
	if (loBytecodeEntryPointer == NULL) {
		printf("Failure to allocate the memory for a bytecode entry!\n");
		return (false);
	}

	loBytecodeEntryPointer->number = loBytecodeCode;
	loBytecodeEntryPointer->name = makeString(loTokens[1]);
	loBytecodeEntryPointer->paramCount = loNumberOfParameters;
	if (loNumberOfParameters
			== 0|| loNumberOfParameters == PARAMETERS_HANDLED_BY_CODE) {
	// allocate just one character (dummy)
		loBytecodeEntryPointer->paramString = (char*) malloc(sizeof(char));
	} else {
		loBytecodeEntryPointer->paramString = (char*) malloc(
				sizeof(char) * loNumberOfParameters + 1);
	}
	if (loBytecodeEntryPointer->paramString == NULL) {
		printf(
				"Failure to allocate the memory for a parameter string in a bytecode entry!\n");
		free(loBytecodeEntryPointer);
		return (false);
	}
	loBytecodeEntryPointer->paramString[0] = '\0';

	if (loNumberOfParameters == PARAMETERS_HANDLED_BY_CODE) {
		// special value, parameters of this bytecode will be handled by program
		loNumberOfParameters = 0;
	}

	// now read parameters
	for (i = 0; i < loNumberOfParameters; i++) {
		char *loType;
		loType = loTokens[3 + i];
		if (strcmpCS(loType, "byte") == 0) {
			// one byte parameter
			strcat(loBytecodeEntryPointer->paramString, "b");
		} else if (strcmpCS(loType, "word") == 0) {
			// two bytes parameter
			strcat(loBytecodeEntryPointer->paramString, "w");
		} else if (strcmpCS(loType, "long") == 0) {
			// four bytes parameter
			strcat(loBytecodeEntryPointer->paramString, "l");
		} else if (strcmpCS(loType, "import") == 0) {
			// two bytes reference to the import table
			strcat(loBytecodeEntryPointer->paramString, "i");
		} else if (strcmpCS(loType, "message") == 0) {
			// two bytes reference to the message table
			strcat(loBytecodeEntryPointer->paramString, "m");
		} else {
			printf("Unknown parameter type: %s!\n", loType);
			free(loBytecodeEntryPointer->paramString);
			free(loBytecodeEntryPointer);
			return (false);
		}
	}
	// explanation
	loBytecodeEntryPointer->explanation = makeString(
			loTokens[3 + loNumberOfParameters]);
	if (loBytecodeEntryPointer->explanation == NULL
			|| strlen(loBytecodeEntryPointer->explanation) == 0) {
		printf("The bytecode explanation is missing!\n");
		free(loBytecodeEntryPointer->paramString);
		free(loBytecodeEntryPointer);
		return (false);
	}

	//printf("\nCode: %d\n  Name: %s\n  ParamCount: %d\n  ParamString: %s\n  Explanation: %s\n",
	//    loBytecodeEntryPointer->number, loBytecodeEntryPointer->name, loBytecodeEntryPointer->paramCount,
	//    loBytecodeEntryPointer->paramString, loBytecodeEntryPointer->explanation);

	if (bytecodeTable[loBytecodeEntryPointer->number] != NULL) {
		printf("The bytecode entry %s is defined more than once!\n",
				loTokens[0]);
		free(loBytecodeEntryPointer->paramString);
		free(loBytecodeEntryPointer);
		return (false);
	}

	loEntryNumber = (int) (loBytecodeEntryPointer->number);
	bytecodeTable[loEntryNumber] = loBytecodeEntryPointer;
	return (true);
}

/*
 Disassemble the code resource
 */
void disassembleCodeResource(int aCodeResourceNumber, unsigned char *aResource,
		int aLength, IMPORTENTRYPOINTER *aFullImportResourceDictionary,
		int aImportResourceSize,
		EXPORTENTRYPOINTER *aFullExportResourceDictionary,
		int aExportResourceSize, FILE *aOutputFile, FILE *aResFile,
		DIRPOINTER *aDirectoryPointers) {
	int i;
	int loDisassemblyStart;
	char *loObjectName = (char *) "unknown_code_object";
	struct SOP_script_header loScriptHeader;
	RESINFOPOINTER *loResourcesInfoTable;

	printf("Starting disassembly...\n");

	if (aFullExportResourceDictionary == NULL) {
		printf(
				"The disassembly cannot be done because the export resource is not available!\n");
		return;
	}

	if (bytecodeTable == NULL) {
		bytecodeTable = readBytecodeDefinition();
	}
	if (bytecodeTable == NULL) {
		char *loError = (char *) "The bytecode definion could not be read!";
		fprintf(aOutputFile, "%s\n", loError);
		printf("%s\n", loError);
		return;
	}

	// create code map
	createCodeMap(aLength);

	// go through the all export entries and mark them as disassembly starts in the code map
	for (i = 0; aFullExportResourceDictionary[i] != NULL; i++) {
		EXPORTENTRYPOINTER loCurrentExportEntry =
				aFullExportResourceDictionary[i];
		if (loCurrentExportEntry->exportType == EXPORT_ENTRY_OBJECT_NAME) {
			// object name, just store and nothing else
			loObjectName = loCurrentExportEntry->objectName;
			continue;
		} else if (loCurrentExportEntry->exportType
				== EXPORT_ENTRY_MESSAGE_HANDLER) {
			// it is a message handler
			int loMessageHandlerStart =
					loCurrentExportEntry->messageHandlerStart;
			if (loMessageHandlerStart != -1) {
				// there is a valid start address
				// mark it in the code map address as a disasembly start
				printf("Message handler \"%s\" start address: %d\n",
						loCurrentExportEntry->messageHandlerName,
						loMessageHandlerStart);
				setCodeMapAddress(loMessageHandlerStart,
						MAP_MESSAGE_HANDLER_NOT_DONE);
			}
		}
	}

	// now went through the code map so many times till there are addresses where the disassembly can start
	printf("Starting the first disassembly pass for the resource: %s ...\n",
			loObjectName);
	// initialize static variable list (exported variables are public static variables)
	initializeStaticVariableList(aFullExportResourceDictionary);
	// initialize the list of constant tables
	initializeConstantTableList();
	// initialize the table of local variable references
	initializeLocalVariableReferencesList();
	// loop through the disassembled parts of the code
	while ((loDisassemblyStart = findFirstAddressForDisassembling()) != -1) {
		// just set the code map properly, the real disassembly will be done later
		if (makeFirstDisassemblyPass(aResource, aLength, loDisassemblyStart,
				aFullImportResourceDictionary) == false) {
			// error
			break;
		}
	}

	// process constant table list (modifies the code map accordingly)
	processConstantTableList();
	// sort the list of local references
	sortLocalVariableReferencesList();
	//displayLocalVariableReferencesList();

	printf("The first disassembly pass finished.\n");

	// SOP script header
	memcpy(&loScriptHeader, aResource, sizeof(struct SOP_script_header));
	// meta information
	fprintf(aOutputFile, "\n%s %d\n", META_DISASSEMBLER_VERSION,
			DISASSEMBLER_VERSION);
	fprintf(aOutputFile, ";\n");
	fprintf(aOutputFile, ";main data\n");
	fprintf(aOutputFile, "%s \"%s\"\n", META_OBJECT_NAME, loObjectName);
	if (loScriptHeader.parent != 0xffffffff) {
		// there is a parent
		char loParentName[256];
		if (getResourceName(loParentName, loScriptHeader.parent) == NULL) {
			strcpy(loParentName, "__unknown_parent__");
		}
		fprintf(aOutputFile, "%s \"%s\"\n", META_PARENT_RESOURCE_NAME,
				loParentName);
	}
	fprintf(aOutputFile, ";\n");
	fprintf(aOutputFile, ";other original settings\n");
	fprintf(aOutputFile, "%s #%08x ;%ld\n",
			META_ORIGINAL_PARENT_RESOURCE_NUMBER, loScriptHeader.parent,
			(long) loScriptHeader.parent);
	fprintf(aOutputFile, "%s %d\n", META_ORIGINAL_CODE_RESOURCE_SIZE, aLength);
	fprintf(aOutputFile, "%s #%04x     ;%d\n",
			META_ORIGINAL_CODE_RESOURCE_NUMBER, (int) aCodeResourceNumber,
			(int) aCodeResourceNumber);
	fprintf(aOutputFile, "%s %d\n", META_ORIGINAL_IMPORT_RESOURCE_SIZE,
			aImportResourceSize);
	fprintf(aOutputFile, "%s #%04x     ;%d\n",
			META_ORIGINAL_IMPORT_RESOURCE_NUMBER,
			(int) loScriptHeader.import_resource,
			(int) loScriptHeader.import_resource);
	fprintf(aOutputFile, "%s %d\n", META_ORIGINAL_EXPORT_RESOURCE_SIZE,
			aExportResourceSize);
	fprintf(aOutputFile, "%s #%04x     ;%d\n",
			META_ORIGINAL_EXPORT_RESOURCE_NUMBER,
			(int) loScriptHeader.export_resource,
			(int) loScriptHeader.export_resource);
	fprintf(aOutputFile, "%s %d\n", META_ORIGINAL_STATIC_SIZE,
			(int) loScriptHeader.static_size);

	fprintf(aOutputFile, ";\n");
	// write out the imported references
	if (writeExternalReferencesInfo(aFullImportResourceDictionary, aOutputFile)
			== false) {
		fprintf(aOutputFile,
				";failure while generating the list of the external references (imports)!\n");
	}
	fprintf(aOutputFile, ";\n");

	// write out the imported references
	if (writeExportedVariablesInfo(aOutputFile) == false) {
		fprintf(aOutputFile,
				";failure while generating the list of the static variables (private and public)!\n");
	}
	fprintf(aOutputFile, ";\n");

	// generate the resource information table (it is used for generating comments about referred resources in the disassembly)
	loResourcesInfoTable = getResourcesInformationTable(aResFile,
			aDirectoryPointers, true);
	if (loResourcesInfoTable == NULL) {
		// unable to get the resource information table
		const char *loError =
				"Error: unable to get the resource information table!";
		printf("%s\n", loError);
		fprintf(aOutputFile, "%s\n", loError);
		return;
	}

	printf(
			"Starting the second disassembly pass for the code resource: %s ...\n",
			loObjectName);

	// second pass (writes output)
	makeSecondDisassemblyPass(aResource, aLength, aFullImportResourceDictionary,
			aFullExportResourceDictionary, aOutputFile, aResFile,
			aDirectoryPointers, loResourcesInfoTable);
	fprintf(aOutputFile, "%s\n\n", META_DISASSEMBLER_END);

	//displayCodeTableMap(aOutputFile);

	printf("The second disassembly pass finished.\n");
	printf("Ending disassembly.\n");
}

/*
 Make first disassembly pass
 */
int makeFirstDisassemblyPass(unsigned char *aResource, int aLength,
		int aStartAddress, IMPORTENTRYPOINTER *aFullImportResourceDictionary) {
	int loCurrentAddress;
	int loCodeMapValue;

	loCurrentAddress = aStartAddress;
	loCodeMapValue = getCodeMapAddressValue(loCurrentAddress);
	if (loCodeMapValue == MAP_MESSAGE_HANDLER_NOT_DONE) {
		// the first two bytes of a message handler indicate the size of local variables
		setCodeMapAddress(loCurrentAddress++, MAP_MESSAGE_HANDLER_PROCESSED);
		setCodeMapAddress(loCurrentAddress++, MAP_SPECIAL);
	} else if (loCodeMapValue == MAP_PROCEDURE_START_NOT_DONE) {
		// the first two bytes of a message handler indicate the size of local variables
		setCodeMapAddress(loCurrentAddress++, MAP_PROCEDURE_START_PROCESSED);
		setCodeMapAddress(loCurrentAddress++, MAP_SPECIAL);
	}
	for (;;) {
		int loInstructionLength;
		int loInstructionEndsDisassembly;
		int i;
		if (shouldBeAddressDisassembled(loCurrentAddress) != true) {
			// time to end;
			break;
		}
		// get the instruction length
		loInstructionLength = getInstructionLength(aResource, aLength,
				loCurrentAddress);
		if (loInstructionLength == -1) {
			// error, mark it in the code map and end
			printf("Setting the MAP_ERROR for the code map address: %d\n",
					loCurrentAddress);
			setCodeMapAddress(loCurrentAddress, MAP_ERROR);
			return (false);
		}
		// targets for jump instructions, CASE, LECA etc.
		setTargetsForTheInstruction(aResource, aLength, loCurrentAddress);

		// add the variable to a variable list, mark arrays etc.
		handleVariableAndArrayRelatedInstructions(aResource, aLength,
				loCurrentAddress, aFullImportResourceDictionary);

		// mark the instruction bytes as disassembled
		for (i = loCurrentAddress; i < loCurrentAddress + loInstructionLength;
				i++) {
			markAddressAsDisassembled(i);
		}
		// find out whether the instruction ends the disassembly (END or uncond. jump)
		loInstructionEndsDisassembly = endsDisassembly(aResource, aLength,
				loCurrentAddress);
		if (loInstructionEndsDisassembly == true) {
			// ends this disassembly (end or unconditional jump)
			break;
		}
		// move behind the instruction
		loCurrentAddress += loInstructionLength;

	}
	return (true);
}

/*
 Gets the end of the current instruction
 */
int getInstructionLength(unsigned char *aResource, int aResourceLength,
		int aCurrentAddress) {
	int i;
	int loInstruction;
	int loParamCount;
	int loLength = 1;  // alwayst at least 1
	loInstruction = (unsigned char) (aResource[aCurrentAddress]);
	if (loInstruction > MAX_BYTECODES || bytecodeTable[loInstruction] == NULL) {
		printf(
				"getInstructionLength(): there is no instruction with the code %x on the address %d!\n",
				loInstruction, aCurrentAddress);
		return (-1);
	}
	loParamCount = bytecodeTable[loInstruction]->paramCount;
	if (loParamCount == PARAMETERS_HANDLED_BY_CODE) {
		if (loInstruction == INSTRUCTION_CASE) {
			// CASE parameters are handled here by a special code
			long loCaseOptions;
			int loParamRes;
			int loParameterAddress = aCurrentAddress + 1;
			// read the number of options in case (word parameter)
			loParamRes = getParameterAsNumber(aResource, aResourceLength, 'w',
					&loCaseOptions, &loParameterAddress);
			if (loParamRes == false) {
				// error
				return (-1);
			}
			// instruction code + loCaseOptions_length + (loCaseOptions * (value_length + label_length)) + default_label_length
			loLength = loLength + 2 + (loCaseOptions * (4 + 2)) + 2;
			if (aCurrentAddress + loLength > aResourceLength) {
				printf(
						"The CASE instruction parameters are outside of the code resource!\n");
				return (-1);
			}
			return (loLength);
		} else {
			// looks like nosense
			printf(
					"The parameter handling in code is not supported for the instruction %s on the address %d!\n",
					bytecodeTable[loInstruction]->name, aCurrentAddress);
			return (-1);
		}
	}
	// read parameters
	for (i = 0; i < loParamCount; i++) {
		char loParameterType = bytecodeTable[loInstruction]->paramString[i];
		if (loParameterType == 'b') {
			loLength++;
		} else if (loParameterType == 'w' || loParameterType == 'i'
				|| loParameterType == 'm') {
			loLength += 2;
		} else if (loParameterType == 'l') {
			loLength += 4;
		} else {
			printf("Unknown parameter type %c of the instruction %s!\n",
					loParameterType, bytecodeTable[loInstruction]->name);
			return (-1);
		}
	}
	if (aCurrentAddress + loLength > aResourceLength) {
		printf(
				"The instruction parameters are outside of the code resource!\n");
		return (-1);
	}
	return (loLength);
}

/*
 Set targets for the instruction (jumps, CASE, LECA)
 */
void setTargetsForTheInstruction(unsigned char *aResource, int aLength,
		int aOriginalAddress) {
	int loInstruction;
	long loTarget;
	int loParamRes;
	long loCaseOptions;
	int i;
	int loCurrentAddress;

	loCurrentAddress = aOriginalAddress;
	loInstruction = (unsigned char) (aResource[loCurrentAddress]);
	if (loInstruction > MAX_BYTECODES || bytecodeTable[loInstruction] == NULL) {
		printf(
				"setTargetsForTheInstruction(): there is no instruction with the code %x on the address %d!\n",
				loInstruction, loCurrentAddress);
		return;
	}
	loCurrentAddress++;
	switch (loInstruction) {
	case INSTRUCTION_LECA:
		// just set a label for LECA
		// read the word parameter representing the target
		loParamRes = getParameterAsNumber(aResource, aLength, 'w', &loTarget,
				&loCurrentAddress);
		if (loParamRes == false) {
			printf(
					"Unable to read a target for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
			return;
		}
		if (setLabelForAddress((int) loTarget) == false) {
			printf(
					"Unable to set a target for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
		}
		break;
	case INSTRUCTION_BRT:
	case INSTRUCTION_BRF:
	case INSTRUCTION_BRA:
		// read the word parameter representing the target
		loParamRes = getParameterAsNumber(aResource, aLength, 'w', &loTarget,
				&loCurrentAddress);
		if (loParamRes == false) {
			printf(
					"Unable to read a jump target for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
			return;
		}
		if (setJumpTarget((int) loTarget) == false) {
			printf(
					"Unable to set a jump target for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
		}
		break;
	case INSTRUCTION_JSR:
		// read the word parameter representing the target
		loParamRes = getParameterAsNumber(aResource, aLength, 'w', &loTarget,
				&loCurrentAddress);
		if (loParamRes == false) {
			printf(
					"Unable to read a procedure address for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
			return;
		}
		if (setProcedureStart((int) loTarget) == false) {
			printf(
					"Unable to set a procedure target for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
		}
		break;
	case INSTRUCTION_CASE:
		// CASE needs a special hadling
		// read the number of options in case (word parameter)
		loParamRes = getParameterAsNumber(aResource, aLength, 'w',
				&loCaseOptions, &loCurrentAddress);
		if (loParamRes == false) {
			// error
			printf(
					"Unable to read the number of options for the CASE instruction at the address %d\n",
					aOriginalAddress);
			return;
		}
		// go through all case options and mark their targets
		for (i = 0; i < loCaseOptions; i++) {
			loCurrentAddress += 4; // ignore the value, we need only jump target
			loParamRes = getParameterAsNumber(aResource, aLength, 'w',
					&loTarget, &loCurrentAddress);
			if (loParamRes == false) {
				printf(
						"Unable to read a case option target for the CASE instruction at the address %d!\n",
						aOriginalAddress);
				return;
			}
			if (setJumpTarget((int) loTarget) == false) {
				printf(
						"Unable to set a case option target for the CASE instruction at the address %d!\n",
						aOriginalAddress);
			}
		}
		// now read the default target
		loParamRes = getParameterAsNumber(aResource, aLength, 'w', &loTarget,
				&loCurrentAddress);
		if (loParamRes == false) {
			printf(
					"Unable to read a default target for the CASE instruction at the address %d!\n",
					aOriginalAddress);
			return;
		}
		if (setJumpTarget((int) loTarget) == false) {
			printf(
					"Unable to set a default target for the CASE instruction at the address %d!\n",
					aOriginalAddress);
		}
		break;
	default:
		break;
	}
}

/*
 Informs whether the instruction ends disassembly (END or unconditional jump or RTS)
 */
int endsDisassembly(unsigned char *aResource, int aLength,
		int aCurrentAddress) {
	int loInstruction;

	loInstruction = (unsigned char) (aResource[aCurrentAddress]);
	if (loInstruction > MAX_BYTECODES || bytecodeTable[loInstruction] == NULL) {
		printf(
				"endsDisassembly(): there is no instruction with the code %x on the address %d!\n",
				loInstruction, aCurrentAddress);
		return (true);
	}
	if (loInstruction == INSTRUCTION_BRA || loInstruction == INSTRUCTION_END
			|| loInstruction == INSTRUCTION_RTS) {
		return (true);
	} else {
		return (false);
	}
}

/*
 Gets parameter as number
 */
int getParameterAsNumber(unsigned char *aResource, int aLength,
		char aParameterType, long *aParameterValue, int *aCurrentAddress) {
	int loAddressBehind = *aCurrentAddress;
	if (aParameterType == 'b') {
		unsigned char loValue = 0;
		loAddressBehind += 1;
		if (loAddressBehind >= aLength) {
			printf(
					"The current address got out of the code resource when reading a parameter!\n");
			return (false);
		}
		memcpy(&loValue, aResource + *aCurrentAddress, 1);
		*aParameterValue = loValue;
	} else if (aParameterType == 'w' || aParameterType == 'i'
			|| aParameterType == 'm') {
		unsigned short loValue = 0;
		loAddressBehind += 2;
		if (loAddressBehind >= aLength) {
			printf(
					"The current address got out of the code resource when reading a parameter!\n");
			return (false);
		}
		memcpy(&loValue, aResource + *aCurrentAddress, 2);
		*aParameterValue = loValue;
	} else if (aParameterType == 'l') {
		unsigned int loValue = 0;
		loAddressBehind += 4;
		if (loAddressBehind >= aLength) {
			printf(
					"The current address got out of the code resource when reading a parameter!\n");
			return (false);
		}
		memcpy(&loValue, aResource + *aCurrentAddress, 4);
		*aParameterValue = loValue;
	} else {
		printf("Unknown parameter type %c\n", aParameterType);
		return (false);
	}
	(*aCurrentAddress) = loAddressBehind;
	return (true);
}

/*
 Get the runtime code function name
 */
char *getRuntimeCodeFunctionName(char *aResult, long aFunctionNumber,
		IMPORTENTRYPOINTER *aFullImportResourceDictionary) {
	int i;
	if (aFullImportResourceDictionary == NULL) {
		printf("The import resource dictionary is NULL!\n!");
		return (NULL);
	}
	for (i = 0; aFullImportResourceDictionary[i] != NULL; i++) {
		if (aFullImportResourceDictionary[i]->importType
				== IMPORT_ENTRY_RUNTIME_FUNCTION) {
			if (aFunctionNumber
					== aFullImportResourceDictionary[i]->runtimeFunctionNumber) {
				strcpy(aResult,
						aFullImportResourceDictionary[i]->firstOriginal);
				return (aResult);
			}
		}
	}
	printf("Unknown runtime function number: %d!\n", (int) aFunctionNumber);
	return (NULL);
}

/*
 Write the disassembly
 */
void makeSecondDisassemblyPass(unsigned char *aResource, int aLength,
		IMPORTENTRYPOINTER *aFullImportResourceDictionary,
		EXPORTENTRYPOINTER *aFullExportResourceDictionary, FILE *aOutputFile,
		FILE *aResFile, DIRPOINTER *aDirectoryPointers,
		RESINFOPOINTER *aResourcesInfoTable) {
	int loCurrentAddress;
	//displayCodeTableMap(aOutputFile);
	// start after the special resources
	for (loCurrentAddress = 14; loCurrentAddress < aLength;) {
		int loCodeMapValue = getCodeMapAddressValue(loCurrentAddress);
		if (loCodeMapValue == MAP_MESSAGE_HANDLER_PROCESSED) {
			unsigned short loLocalVariablesSize;
			char *loMessageHandlerName;
			int loEndAddress;
			loMessageHandlerName = getMessageHandlerNameForAddress(
					loCurrentAddress, aFullExportResourceDictionary);
			fprintf(aOutputFile, ";\n");
			fprintf(aOutputFile, "%s \"%s\"\n", META_MESSAGE_HANDLER_NAME,
					loMessageHandlerName);
			// first two bytes of a message handler are local variables
			memcpy(&loLocalVariablesSize, aResource + loCurrentAddress, 2);
			fprintf(aOutputFile,
					"%s %d    ;includes 2 bytes for THIS pointer\n",
					META_LOCAL_VARIABLES_SIZE, loLocalVariablesSize);
			loEndAddress = getAddressBehindTheFunction(loCurrentAddress,
					aLength);
			// parameter declarations
			writeParameters(aOutputFile, loCurrentAddress, loEndAddress);
			// local variable declarations
			writeLocalVariables(aOutputFile, loCurrentAddress, loEndAddress);
			loCurrentAddress += 2; // set the address at the first instruction
		} else if (loCodeMapValue == MAP_PROCEDURE_START_PROCESSED) {
			unsigned short loLocalVariablesSize;
			char loProcedureName[256];
			int loEndAddress;
			sprintf(loProcedureName, "%s%d", PROCEDURE_PREFIX,
					(int) loCurrentAddress);
			fprintf(aOutputFile, ";\n");
			fprintf(aOutputFile, "%s \"%s\"\n", META_PROCEDURE_NAME,
					loProcedureName);
			// first two bytes of a procedure are local variables
			memcpy(&loLocalVariablesSize, aResource + loCurrentAddress, 2);
			fprintf(aOutputFile,
					"%s %d    ;includes 2 bytes for THIS pointer\n",
					META_LOCAL_VARIABLES_SIZE, loLocalVariablesSize);
			loEndAddress = getAddressBehindTheFunction(loCurrentAddress,
					aLength);
			// parameter declarations
			writeParameters(aOutputFile, loCurrentAddress, loEndAddress);
			// local variable declarations
			writeLocalVariables(aOutputFile, loCurrentAddress, loEndAddress);
			loCurrentAddress += 2; // set the address at the first instruction
		} else if (loCodeMapValue == MAP_DATA_BYTE) {
			char loDBString[256];
			if (hasAddressLabel(loCurrentAddress) == true) {
				writeLabel(loCurrentAddress, aOutputFile);
			}
			getDBByteString(loDBString, aResource, aLength, loCurrentAddress);
			fprintf(aOutputFile, "%s\n", loDBString);
			loCurrentAddress++;
		} else if (loCodeMapValue == MAP_DATA_WORD) {
			char loDBString[256];
			if (hasAddressLabel(loCurrentAddress) == true) {
				writeLabel(loCurrentAddress, aOutputFile);
			}
			getDWWordString(loDBString, aResource, aLength, loCurrentAddress);
			fprintf(aOutputFile, "%s\n", loDBString);
			loCurrentAddress += 2;
		} else if (loCodeMapValue == MAP_DATA_LONG) {
			char loDBString[256];
			if (hasAddressLabel(loCurrentAddress) == true) {
				writeLabel(loCurrentAddress, aOutputFile);
			}
			getDLLongString(loDBString, aResource, aLength, loCurrentAddress);
			fprintf(aOutputFile, "%s\n", loDBString);
			loCurrentAddress += 4;
		} else if (loCodeMapValue == MAP_CODE_START_PROCESSED
				|| loCodeMapValue == MAP_CODE_PROCESSED) {
			if (writeOneInstruction(aResource, aLength, &loCurrentAddress,
					aFullImportResourceDictionary, aOutputFile, aResFile,
					aDirectoryPointers, aResourcesInfoTable) == false) {
				// error
				fprintf(aOutputFile,
						"writeOneInstruction() returned an error!\n");
				return;
			}
		} else if (loCodeMapValue == MAP_BYTE_TABLE_START) {
			char loTableName[256];
			char loDBString[256];
			fprintf(aOutputFile, ";byte table start\n");
			if (getConstantTableName(loTableName, loCurrentAddress) == NULL) {
				strcpy(loTableName, "__unknown_byte_table__");
			}
			fprintf(aOutputFile, "%s \"%s\" ;%d\n", META_TABLE_BYTE,
					loTableName, loCurrentAddress);
			getDBByteString(loDBString, aResource, aLength, loCurrentAddress);
			fprintf(aOutputFile, "%s\n", loDBString);
			loCurrentAddress++;
		} else if (loCodeMapValue == MAP_WORD_TABLE_START) {
			char loTableName[256];
			char loDBString[256];
			fprintf(aOutputFile, ";word table start\n");
			if (getConstantTableName(loTableName, loCurrentAddress) == NULL) {
				strcpy(loTableName, "__unknown_word_table__");
			}
			fprintf(aOutputFile, "%s \"%s\" ;%d\n", META_TABLE_WORD,
					loTableName, loCurrentAddress);
			getDWWordString(loDBString, aResource, aLength, loCurrentAddress);
			fprintf(aOutputFile, "%s\n", loDBString);
			loCurrentAddress += 2;
		} else if (loCodeMapValue == MAP_LONG_TABLE_START) {
			char loTableName[256];
			char loDBString[256];
			fprintf(aOutputFile, ";long table start\n");
			if (getConstantTableName(loTableName, loCurrentAddress) == NULL) {
				strcpy(loTableName, "__unknown_long_table__");
			}
			fprintf(aOutputFile, "%s \"%s\" ;%d\n", META_TABLE_LONG,
					loTableName, loCurrentAddress);
			getDLLongString(loDBString, aResource, aLength, loCurrentAddress);
			fprintf(aOutputFile, "%s\n", loDBString);
			loCurrentAddress += 4;
		} else if (loCodeMapValue == MAP_ERROR) {
			fprintf(aOutputFile,
					"Error in the code table on the address %d with the value %d (%02x hexadecimal)!\n",
					loCurrentAddress,
					(int) (unsigned char) aResource[loCurrentAddress],
					(int) (unsigned char) aResource[loCurrentAddress]);
			fprintf(aOutputFile, "See the errors in console for details.\n");
			// error
			return;
		} else {
			char loError[256];
			sprintf(loError,
					"Unimplemented code map value %d on the address %d\n",
					loCodeMapValue, loCurrentAddress);
			printf("%s\n", loError);
			fprintf(aOutputFile, "%s\n", loError);
			return;
		}
	}
}

/*
 Get the message handler name for the message handler starting on the specified address
 */
char *getMessageHandlerNameForAddress(int aEntryPointAddress,
		EXPORTENTRYPOINTER *aFullExportResourceDictionary) {
	int i;
	for (i = 0; aFullExportResourceDictionary[i] != NULL; i++) {
		EXPORTENTRYPOINTER loCurrentExportEntry =
				aFullExportResourceDictionary[i];
		if (loCurrentExportEntry->exportType == EXPORT_ENTRY_MESSAGE_HANDLER) {
			// message handler entry
			if (aEntryPointAddress
					== loCurrentExportEntry->messageHandlerStart) {
				// entry points agree
				return (loCurrentExportEntry->messageHandlerName);
			}
		}
	}
	return ((char *) "error_name_not_found");
}

/*
 Returns DB <value> string
 */
void getDBByteString(char *aResult, unsigned char *aResource, int aLength,
		int aAddress) {
	char loResultLine[256];
	char loTmp[256];
	char loHexaCodes[256];
	unsigned char loValue;

	if (aAddress < 0 || aAddress >= aLength) {
		strcpy(aResult,
				"getDBBytestring() is called for the address outside of the code resource!\n");
		return;
	}

	// read the value
	loValue = aResource[aAddress];
	// get the hex codes
	getHexCodes(loHexaCodes, aResource, aLength, aAddress, 1);
	strcpy(loResultLine, loHexaCodes);
	// instruction
	sprintf(loTmp, INSTRUCTION_FORMAT_STRING, "DB");
	strcat(loResultLine, loTmp);
	getParameterString(loTmp, 'b', loValue, true, true);
	strcat(loResultLine, loTmp);
	strcpy(aResult, loResultLine);
}

/*
 Returns DW <value> string
 */
void getDWWordString(char *aResult, unsigned char *aResource, int aLength,
		int aAddress) {
	char loResultLine[256];
	char loTmp[256];
	char loHexaCodes[256];
	unsigned char loValue;

	if (aAddress < 0 || aAddress >= aLength) {
		strcpy(aResult,
				"getDWWordstring() is called for the address outside of the code resource!\n");
		return;
	}

	// read the value
	loValue = aResource[aAddress];
	// get the hex codes
	getHexCodes(loHexaCodes, aResource, aLength, aAddress, 2);
	strcpy(loResultLine, loHexaCodes);
	// instruction
	sprintf(loTmp, INSTRUCTION_FORMAT_STRING, "DW");
	strcat(loResultLine, loTmp);
	getParameterString(loTmp, 'w', loValue, true, true);
	strcat(loResultLine, loTmp);
	strcpy(aResult, loResultLine);
}

/*
 Returns DL <value> string
 */
void getDLLongString(char *aResult, unsigned char *aResource, int aLength,
		int aAddress) {
	char loResultLine[256];
	char loTmp[256];
	char loHexaCodes[256];
	unsigned char loValue;

	if (aAddress < 0 || aAddress >= aLength) {
		strcpy(aResult,
				"getDLLongstring() is called for the address outside of the code resource!\n");
		return;
	}

	// read the value
	loValue = aResource[aAddress];
	// get the hex codes
	getHexCodes(loHexaCodes, aResource, aLength, aAddress, 4);
	strcpy(loResultLine, loHexaCodes);
	// instruction
	sprintf(loTmp, INSTRUCTION_FORMAT_STRING, "DL");
	strcat(loResultLine, loTmp);
	getParameterString(loTmp, 'l', loValue, true, true);
	strcat(loResultLine, loTmp);
	strcpy(aResult, loResultLine);
}

/*
 Write one disassembled instruction
 */
int writeOneInstruction(unsigned char *aResource, int aLength,
		int *loCurrentAddress,
		IMPORTENTRYPOINTER *aFullImportResourceDictionary, FILE *aOutputFile,
		FILE *aResFile, DIRPOINTER *aDirectoryPointers,
		RESINFOPOINTER *aResourcesInfoTable) {
	int loParamCount;
	int i;
	int loInstructionStartAddress;
	char loInstruction[256];
	char loHexaCodes[256];
	char loResultLine[256];
	char loTmp[256];
	int loCurrentInstruction;

	loInstructionStartAddress = *loCurrentAddress;
	loCurrentInstruction = (unsigned char) (aResource[*loCurrentAddress]);
	if (loCurrentInstruction > MAX_BYTECODES
			|| bytecodeTable[loCurrentInstruction] == NULL) {
		char loError[256];
		sprintf(loError,
				"writeOneInstruction(): there is no instruction with the code %x on the address %d!\n",
				loCurrentInstruction, *loCurrentAddress);
		printf("%s\n", loError);
		fprintf(aOutputFile, "%s\n", loError);
		return (false);
	}

	// move to the parameters
	(*loCurrentAddress)++;
	// instruction name
	sprintf(loTmp, INSTRUCTION_FORMAT_STRING,
			bytecodeTable[loCurrentInstruction]->name);
	strcpy(loInstruction, loTmp);

	// parameters
	loParamCount = bytecodeTable[loCurrentInstruction]->paramCount;
	if (loParamCount == PARAMETERS_HANDLED_BY_CODE) {
		long loCaseOptions;
		long loTarget;
		long loValue;
		int loParamRes;

		if (loCurrentInstruction != INSTRUCTION_CASE) {
			// looks like nonsense
			char loError[256];
			sprintf(loError,
					"The parameter handling in code is not supported for the instruction %s on the address %d!",
					bytecodeTable[loCurrentInstruction]->name,
					loInstructionStartAddress);
			printf("%s\n", loError);
			fprintf(aOutputFile, "%s\n", loError);
			return (false);
		}
		fprintf(aOutputFile, ";CASE instruction start\n");

		// read the number of options in case (word parameter)
		loParamRes = getParameterAsNumber(aResource, aLength, 'w',
				&loCaseOptions, loCurrentAddress);
		if (loParamRes == false) {
			// error
			printf(
					"Unable to read the number of options for the CASE instruction at the address %d\n",
					loInstructionStartAddress);
			return (false);
		}
		// write to the output file
		writeCaseHeader(aResource, aLength, loInstructionStartAddress,
				loCaseOptions, aOutputFile);

		// go through all case options
		for (i = 0; i < loCaseOptions; i++) {
			int loCaseEntryStartAddress = *loCurrentAddress;
			loParamRes = getParameterAsNumber(aResource, aLength, 'l', &loValue,
					loCurrentAddress);
			if (loParamRes == false) {
				printf(
						"Unable to read a case option value for the CASE instruction at the address %d!\n",
						loInstructionStartAddress);
				return (false);
			}
			loParamRes = getParameterAsNumber(aResource, aLength, 'w',
					&loTarget, loCurrentAddress);
			if (loParamRes == false) {
				printf(
						"Unable to read a case option target for the CASE instruction at the address %d!\n",
						loInstructionStartAddress);
				return (false);
			}
			// write to the the output files
			writeCaseEntry(aResource, aLength, loCaseEntryStartAddress, loValue,
					loTarget, aOutputFile);
		}

		// default label
		if (getParameterAsNumber(aResource, aLength, 'w', &loTarget,
				loCurrentAddress) == false) {
			printf(
					"Unable to read a default target for the CASE instruction at the address %d!\n",
					loInstructionStartAddress);
			return (false);
		}
		// write to the output file
		writeCaseDefault(aResource, aLength, *loCurrentAddress - 2, loTarget,
				aOutputFile);
		fprintf(aOutputFile, ";CASE instruction end\n");
	} else {
		// read parameters
		for (i = 0; i < loParamCount; i++) {
			char loParameterType =
					bytecodeTable[loCurrentInstruction]->paramString[i];
			long loParameterValue;
			int loIsLastParameter = (i == (loParamCount - 1)) ? true : false;

			if (getParameterAsNumber(aResource, aLength, loParameterType,
					&loParameterValue, loCurrentAddress) == false) {
				// error
				return (false);
			}
			if (i > 0) {
				strcat(loInstruction, ", ");
			}
			if (loParameterType == 'i') {
				// the parameter refers import resource
				if (loCurrentInstruction == INSTRUCTION_RCRS) {
					char loRuntimeCodeResourceName[256];
					if (getRuntimeCodeFunctionName(loRuntimeCodeResourceName,
							loParameterValue,
							aFullImportResourceDictionary) == NULL) {
						strcpy(loRuntimeCodeResourceName,
								"__unknown_runtime_function__");
					}
					sprintf(loTmp, "\"%s\"", loRuntimeCodeResourceName);
					strcat(loInstruction, loTmp);
					/*
					 if (loIsLastParameter == true)
					 {
					 // it is last parameter, add the number as a comment
					 sprintf(loTmp, "  ;runtime_function_number: %d", loParameterValue);
					 strcat(loInstruction, loTmp);
					 }
					 */
				} else if (loCurrentInstruction == INSTRUCTION_LXB
						|| loCurrentInstruction == INSTRUCTION_LXW
						|| loCurrentInstruction == INSTRUCTION_LXD
						|| loCurrentInstruction == INSTRUCTION_SXB
						|| loCurrentInstruction == INSTRUCTION_SXW
						|| loCurrentInstruction == INSTRUCTION_SXD
						|| loCurrentInstruction == INSTRUCTION_LEXA) // Warning: just assume that LEXA refers a normal variable (not array)
				{
					char loVariableName[256];
					if (getExternalVariableNameForVariableNumber(loVariableName,
							loParameterValue,
							aFullImportResourceDictionary) == NULL) {
						strcpy(loVariableName, "__unknown_external_variable__");
					}
					sprintf(loTmp, "\"%s\"", loVariableName);
					strcat(loInstruction, loTmp);
					/*
					 if (loIsLastParameter == true)
					 {
					 // it is last parameter, add the number as a comment
					 sprintf(loTmp, "  ;external_variable_number: %d", loParameterValue);
					 strcat(loInstruction, loTmp);
					 }
					 */

				} else if (loCurrentInstruction == INSTRUCTION_LXBA
						|| loCurrentInstruction == INSTRUCTION_LXWA
						|| loCurrentInstruction == INSTRUCTION_LXDA
						|| loCurrentInstruction == INSTRUCTION_SXBA
						|| loCurrentInstruction == INSTRUCTION_SXWA
						|| loCurrentInstruction == INSTRUCTION_SXDA) {
					char loArrayName[256];
					if (getExternalVariableNameForVariableNumber(loArrayName,
							loParameterValue,
							aFullImportResourceDictionary) == NULL) {
						strcpy(loArrayName, "__unknown_external_array__");
					}
					sprintf(loTmp, "\"%s\"", loArrayName);
					strcat(loInstruction, loTmp);
				} else {
					printf(
							"The parameter \"import\" is not supported for the instruction %d\n",
							loCurrentInstruction);
					return (false);
				}
			} else if (loParameterType == 'm') {
				// the parameter refers import resource
				char loMessageName[256];
				char loMessageID[256];
				sprintf(loMessageID, "M:%d", (int) loParameterValue);
				if (getMessageName(loMessageName, loMessageID, aResFile,
						aDirectoryPointers) == NULL) {
					strcpy(loMessageName, "__unknown_message__");
				}
				sprintf(loTmp, "\"%s\"", loMessageName);
				strcat(loInstruction, loTmp);
				if (i == loParamCount - 1) {
					// it is last parameter, add the number as a comment
					sprintf(loTmp, "  ;message_number: %ld", loParameterValue);
					strcat(loInstruction, loTmp);
				}
			} else {
				if ((loCurrentInstruction == INSTRUCTION_BRT
						|| loCurrentInstruction == INSTRUCTION_BRF
						|| loCurrentInstruction == INSTRUCTION_BRA
						|| loCurrentInstruction == INSTRUCTION_LECA)
						&& loParamCount == 1) {
					// show label for BRT/BRF/BRA/LECA
					sprintf(loTmp, "%s%d ;%d", LABEL_PREFIX,
							(int) loParameterValue, (int) loParameterValue);
					strcat(loInstruction, loTmp);

				} else if (loCurrentInstruction == INSTRUCTION_LSB
						|| loCurrentInstruction == INSTRUCTION_LSW
						|| loCurrentInstruction == INSTRUCTION_LSD
						|| loCurrentInstruction == INSTRUCTION_SSB
						|| loCurrentInstruction == INSTRUCTION_SSW
						|| loCurrentInstruction == INSTRUCTION_SSD
						|| loCurrentInstruction == INSTRUCTION_LSBA
						|| loCurrentInstruction == INSTRUCTION_LSWA
						|| loCurrentInstruction == INSTRUCTION_LSDA
						|| loCurrentInstruction == INSTRUCTION_SSBA
						|| loCurrentInstruction == INSTRUCTION_SSWA
						|| loCurrentInstruction == INSTRUCTION_SSDA
						|| loCurrentInstruction == INSTRUCTION_LESA) {
					// instructions working with static variables (both private or public)
					char loStaticVariableName[256];
					if (getStaticVariableNameForIndex(loStaticVariableName,
							loParameterValue) == NULL) {
						printf(
								"Failure to get the name of the static variable %ld from the static variable table!\n",
								loParameterValue);
						strcpy(loStaticVariableName,
								"__unknown_static_variable__");
					}
					sprintf(loTmp, "\"%s\" ;%d", loStaticVariableName,
							(int) loParameterValue);
					strcat(loInstruction, loTmp);
				} else if (loCurrentInstruction == INSTRUCTION_LAB
						|| loCurrentInstruction == INSTRUCTION_LAW
						|| loCurrentInstruction == INSTRUCTION_LAD
						|| loCurrentInstruction == INSTRUCTION_SAB
						|| loCurrentInstruction == INSTRUCTION_SAW
						|| loCurrentInstruction == INSTRUCTION_SAD
						|| loCurrentInstruction == INSTRUCTION_LABA
						|| loCurrentInstruction == INSTRUCTION_LAWA
						|| loCurrentInstruction == INSTRUCTION_LADA
						|| loCurrentInstruction == INSTRUCTION_SABA
						|| loCurrentInstruction == INSTRUCTION_SAWA
						|| loCurrentInstruction == INSTRUCTION_SADA
						|| loCurrentInstruction == INSTRUCTION_LEAA) {
					// instructions working with local (auto) variables
					char loAutoVariableName[256];
					getAutoVariableNameForIndex(loAutoVariableName,
							loCurrentInstruction, loParameterValue);
					sprintf(loTmp, "\"%s\" ;%d", loAutoVariableName,
							(int) loParameterValue);
					strcat(loInstruction, loTmp);
				} else if (loCurrentInstruction == INSTRUCTION_LTBA
						|| loCurrentInstruction == INSTRUCTION_LTWA
						|| loCurrentInstruction == INSTRUCTION_LTDA
						|| loCurrentInstruction == INSTRUCTION_LETA) {
					// instructions working with constant tables
					char loConstantTableName[256];
					if (getConstantTableName(loConstantTableName,
							loParameterValue) == NULL) {
						printf(
								"Failure to get the name of the constant table %ld!\n",
								loParameterValue);
						strcpy(loConstantTableName,
								"__unknown_constant_table__");
					}
					sprintf(loTmp, "\"%s\" ;%d", loConstantTableName,
							(int) loParameterValue);
					strcat(loInstruction, loTmp);
				} else if (loCurrentInstruction == INSTRUCTION_JSR) {
					// show procedure name for JSR
					sprintf(loTmp, "\"%s%d\" ;%d", PROCEDURE_PREFIX,
							(int) loParameterValue, (int) loParameterValue);
					strcat(loInstruction, loTmp);

				} else {

					// it is a number
					if (loCurrentInstruction == INSTRUCTION_SHTC
							|| loCurrentInstruction == INSTRUCTION_INTC
							|| loCurrentInstruction == INSTRUCTION_LNGC) {
						// may be the name can referer a resource, so add a comment
						writeInfoAboutReferredResourceIfAvailable(
								loParameterValue, aResourcesInfoTable,
								aOutputFile);
					}
					if (getParameterString(loTmp, loParameterType,
							loParameterValue, loIsLastParameter, true)
							!= true) {
						printf(
								"Failure to get the parameter value for the parameter type %c, value %ld for the instruction %d on the address %d!\n",
								(unsigned char) loParameterType,
								(long) loParameterValue,
								(int) loCurrentInstruction,
								(int) loInstructionStartAddress);
						return (false);
					}
					strcat(loInstruction, loTmp);
				}
			}
		}

		// get the hex codes
		getHexCodes(loHexaCodes, aResource, aLength, loInstructionStartAddress,
				*loCurrentAddress - loInstructionStartAddress);
		// add label if needed
		if (hasAddressLabel(loInstructionStartAddress) == true) {
			writeLabel(loInstructionStartAddress, aOutputFile);
		}
		// complete the line
		strcpy(loResultLine, loHexaCodes);
		strcat(loResultLine, loInstruction);
		fprintf(aOutputFile, "%s\n", loResultLine);
		if (loCurrentInstruction == INSTRUCTION_RTS) {
			// add one empty comment line behind the procedure body
			fprintf(aOutputFile, ";\n");
		}
	}
	return (true);
}

/*
 Creates the hex codes shown at the beginning of the disassembled line (address and hex values)
 */
void getHexCodes(char *aString, unsigned char *aResource, int aBufferLength,
		int aStartAddress, int aSize) {
	char loTmp[256];
	int i;
	if (aStartAddress + aSize > aBufferLength) {
		// error
		strcpy(aString,
				"getHexCodes() called on addresses outside of the code resource!\n");
		return;
	}

	sprintf(loTmp, "%6d (%06x): ", aStartAddress, aStartAddress);
	strcpy(aString, loTmp);
	for (i = aStartAddress; i < aStartAddress + aSize; i++) {
		sprintf(loTmp, "%02x ", (int) aResource[i]);
		strcat(aString, loTmp);
	}

	// add some spaces (instructions can have different length)
	for (i = 0; i < NUMBER_OF_HEX_CODES_IN_DISSASSEMBLY - aSize; i++) {
		strcat(aString, "   ");
	}
}

/*
 Writes CASE instruction header during disassembly
 */
void writeCaseHeader(unsigned char *aResource, int aLength, int aStartAddress,
		long aCaseOptions, FILE *aOutputFile) {
	char loHexCodes[256];
	char loTmp[256];
	char loInstruction[256];
	char loResultLine[256];
	if (hasAddressLabel(aStartAddress) == true) {
		writeLabel(aStartAddress, aOutputFile);
	}
	// hex codes
	getHexCodes(loHexCodes, aResource, aLength, aStartAddress, 3);
	// instruction
	sprintf(loTmp, INSTRUCTION_FORMAT_STRING,
			bytecodeTable[INSTRUCTION_CASE]->name);
	strcpy(loInstruction, loTmp);
	getParameterString(loTmp, 'w', aCaseOptions, true, false);
	strcat(loInstruction, loTmp);
	// result
	strcpy(loResultLine, loHexCodes);
	strcat(loResultLine, loInstruction);
	fprintf(aOutputFile, "%s\n", loResultLine);
}

/*
 Writes CASE instruction entry during disassembly
 */
void writeCaseEntry(unsigned char *aResource, int aLength, int aStartAddress,
		long aValue, int aTarget, FILE *aOutputFile) {
	char loHexCodes[256];
	char loTmp[256];
	char loInstruction[256];
	char loResultLine[256];
	// hex codes
	getHexCodes(loHexCodes, aResource, aLength, aStartAddress, 6);
	// instruction
	sprintf(loTmp, INSTRUCTION_FORMAT_STRING, CASE_ENTRY);
	strcpy(loInstruction, loTmp);
	sprintf(loTmp, "#%08lx, %s%d     ;%ld, %d", aValue, LABEL_PREFIX, aTarget,
			aValue, aTarget);
	strcat(loInstruction, loTmp);
	// result
	strcpy(loResultLine, loHexCodes);
	strcat(loResultLine, loInstruction);
	fprintf(aOutputFile, "%s\n", loResultLine);
}

/*
 Writes CASE instruction default during disassembly
 */
void writeCaseDefault(unsigned char *aResource, int aLength, int aStartAddress,
		int aTarget, FILE *aOutputFile) {
	char loHexCodes[256];
	char loTmp[256];
	char loInstruction[256];
	char loResultLine[256];
	// hex codes
	getHexCodes(loHexCodes, aResource, aLength, aStartAddress, 2);
	// instruction
	sprintf(loTmp, INSTRUCTION_FORMAT_STRING, CASE_DEFAULT);
	strcpy(loInstruction, loTmp);
	sprintf(loTmp, "%s%d     ;%d", LABEL_PREFIX, aTarget, aTarget);
	strcat(loInstruction, loTmp);
	// result
	strcpy(loResultLine, loHexCodes);
	strcat(loResultLine, loInstruction);
	fprintf(aOutputFile, "%s\n", loResultLine);
}

/*
 Write label
 */
void writeLabel(int aAddress, FILE *aOutputFile) {
	fprintf(aOutputFile, "%s%d:\n", LABEL_PREFIX, aAddress);
}

/*
 Get the parameter as the string
 */
int getParameterString(char *aString, unsigned char aParameterType,
		long aParameterValue, int aAddParameterComment,
		int aAddCharactersInTheComment) {
	char loTmp[256];
	int loSize;
	if (aParameterType == 'b') {
		loSize = 1;
		sprintf(loTmp, "#%02lx", aParameterValue);
		sprintf(aString, "%-8s", loTmp);
	} else if (aParameterType == 'w') {
		loSize = 2;
		sprintf(loTmp, "#%04lx", aParameterValue);
		sprintf(aString, "%-8s", loTmp);
	} else if (aParameterType == 'l') {
		loSize = 4;
		sprintf(loTmp, "#%08lx", aParameterValue);
		sprintf(aString, "%-8s", loTmp);
	} else {
		printf("Unknown parameter type: %c\n", aParameterType);
		sprintf(aString, "%ld", aParameterValue);
		return (false);
	}

	if (aAddParameterComment == true) {
		// ok, add a comment
		char loTmp[256];
		int i;
		unsigned char loArray[4];
		int loPrintableCharacters = true;
		if (loSize == 1) {
			unsigned char loChar = (unsigned char) aParameterValue;
			memcpy(loArray, &loChar, 1);
			sprintf(loTmp, "     ;%u", (unsigned int) aParameterValue);
			strcat(aString, loTmp);
		}
		if (loSize == 2) {
			unsigned short loWord = (unsigned short) aParameterValue;
			memcpy(loArray, &loWord, 2);
			sprintf(loTmp, "     ;%u", (unsigned int) aParameterValue);
			strcat(aString, loTmp);
		}
		if (loSize == 4) {
			unsigned int loLong = (unsigned int) aParameterValue;
			memcpy(loArray, &loLong, 4);
			sprintf(loTmp, "     ;%u", (unsigned int) aParameterValue);
			strcat(aString, loTmp);
		}
		for (i = 0; i < loSize; i++) {
			if (loArray[i] < 32) {
				// at least one unprintable character
				loPrintableCharacters = false;
				break;
			}
		}
		if (loPrintableCharacters == true
				&& aAddCharactersInTheComment == true) {
			// add the printable characters in ""
			strcat(aString, ", \"");
			for (i = 0; i < loSize; i++) {
				sprintf(loTmp, "%c", loArray[i]);
				strcat(aString, loTmp);
			}
			strcat(aString, "\"");
		}

	}
	return (true);
}

/*
 Handle the instructions refering to variables
 (store info, mark as array if needed etc)
 */
void handleVariableAndArrayRelatedInstructions(unsigned char *aResource,
		int aLength, int aOriginalAddress,
		IMPORTENTRYPOINTER *aFullImportResourceDictionary) {
	int loInstruction;
	long loParameter;
	int loParamRes;
	int loCurrentAddress;
	char loVariableType;

	loCurrentAddress = aOriginalAddress;
	loInstruction = (unsigned char) (aResource[loCurrentAddress]);
	if (loInstruction > MAX_BYTECODES || bytecodeTable[loInstruction] == NULL) {
		printf(
				"markParameterAsArrayIfNeeded(): there is no instruction with the code %x on the address %d!\n",
				loInstruction, loCurrentAddress);
		return;
	}
	loCurrentAddress++;
	switch (loInstruction) {
	case INSTRUCTION_LTBA:
	case INSTRUCTION_LTWA:
	case INSTRUCTION_LTDA:
	case INSTRUCTION_LETA:

	case INSTRUCTION_LAB:
	case INSTRUCTION_LAW:
	case INSTRUCTION_LAD:
	case INSTRUCTION_SAB:
	case INSTRUCTION_SAW:
	case INSTRUCTION_SAD:

	case INSTRUCTION_LABA:
	case INSTRUCTION_LAWA:
	case INSTRUCTION_LADA:
	case INSTRUCTION_SABA:
	case INSTRUCTION_SAWA:
	case INSTRUCTION_SADA:
	case INSTRUCTION_LEAA:

	case INSTRUCTION_LSB:
	case INSTRUCTION_LSW:
	case INSTRUCTION_LSD:
	case INSTRUCTION_SSB:
	case INSTRUCTION_SSW:
	case INSTRUCTION_SSD:

	case INSTRUCTION_LSBA:
	case INSTRUCTION_LSWA:
	case INSTRUCTION_LSDA:
	case INSTRUCTION_SSBA:
	case INSTRUCTION_SSWA:
	case INSTRUCTION_SSDA:
	case INSTRUCTION_LESA:

	case INSTRUCTION_LXB:
	case INSTRUCTION_LXW:
	case INSTRUCTION_LXD:
	case INSTRUCTION_SXB:
	case INSTRUCTION_SXW:
	case INSTRUCTION_SXD:

	case INSTRUCTION_LXBA:
	case INSTRUCTION_LXWA:
	case INSTRUCTION_LXDA:
	case INSTRUCTION_SXBA:
	case INSTRUCTION_SXWA:
	case INSTRUCTION_SXDA:
	case INSTRUCTION_LEXA:
		// read the word parameter representing the parameter
		loParamRes = getParameterAsNumber(aResource, aLength, 'w', &loParameter,
				&loCurrentAddress);
		if (loParamRes == false) {
			printf(
					"Unable to read a parameter for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
			return;
		}
		break;
	default:
		// uninsteresting instruction
		return;
	}

	// do something with it
	switch (loInstruction) {
	case INSTRUCTION_LTBA:
	case INSTRUCTION_LTWA:
	case INSTRUCTION_LTDA:
	case INSTRUCTION_LETA:
		loVariableType = getUppercaseVariableType(loInstruction);
		if (addConstantTableEntryIfNotExisting((int) loParameter,
				loVariableType) == false) {
			printf(
					"Unable to add an entry into the table of constant tables for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
		}
		break;

	case INSTRUCTION_LAB:
	case INSTRUCTION_LAW:
	case INSTRUCTION_LAD:
	case INSTRUCTION_SAB:
	case INSTRUCTION_SAW:
	case INSTRUCTION_SAD:
		if (addAutoVariableReference(aOriginalAddress, loInstruction,
				(int) loParameter, false) == false) {
			printf(
					"Unable to add an entry into the table local variables for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
		}
		break;
	case INSTRUCTION_LABA:
	case INSTRUCTION_LAWA:
	case INSTRUCTION_LADA:
	case INSTRUCTION_SABA:
	case INSTRUCTION_SAWA:
	case INSTRUCTION_SADA:
	case INSTRUCTION_LEAA: // this is not correct, we do not know whether it is an array or not for LEAA
		if (addAutoVariableReference(aOriginalAddress, loInstruction,
				(int) loParameter, true) == false) {
			printf(
					"Unable to add an entry into the table local variables for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
		}
		break;

	case INSTRUCTION_LSB:
	case INSTRUCTION_LSW:
	case INSTRUCTION_LSD:
	case INSTRUCTION_SSB:
	case INSTRUCTION_SSW:
	case INSTRUCTION_SSD:
		loVariableType = getUppercaseVariableType(loInstruction);
		if (addPrivateStaticVariableIfNotExisting((int) loParameter, false,
				loVariableType) == false) {
			printf(
					"Unable to add an entry into the static variable table for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
		}
		break;

	case INSTRUCTION_LSBA:
	case INSTRUCTION_LSWA:
	case INSTRUCTION_LSDA:
	case INSTRUCTION_SSBA:
	case INSTRUCTION_SSWA:
	case INSTRUCTION_SSDA:
	case INSTRUCTION_LESA: // this is not correct, we do not know whether is is an array or not for LESA
		loVariableType = getUppercaseVariableType(loInstruction);
		if (addPrivateStaticVariableIfNotExisting((int) loParameter, true,
				loVariableType) == false) {
			printf(
					"Unable to add an entry into the static variable table for the instruction %d at the address %d!\n",
					loInstruction, aOriginalAddress);
		}
		break;

	case INSTRUCTION_LXBA:
	case INSTRUCTION_LXWA:
	case INSTRUCTION_LXDA:
	case INSTRUCTION_SXBA:
	case INSTRUCTION_SXWA:
	case INSTRUCTION_SXDA:
		if (changeImportEntryVariableToArray(loParameter,
				aFullImportResourceDictionary) == false) {
			printf(
					"Unable to mark a parameter for the instruction %d at the address %d as array in the import dictionary!\n",
					loInstruction, aOriginalAddress);
			return;
		}
		break;

	default:
		// nothing
		break;
	}
}

/*
 Gets the upper case letter telling the variable type
 */
char getUppercaseVariableType(int aInstruction) {
	char loResult;
	switch (aInstruction) {
	case INSTRUCTION_LTBA:

	case INSTRUCTION_LSB:
	case INSTRUCTION_SSB:
	case INSTRUCTION_LSBA:
	case INSTRUCTION_SSBA:

	case INSTRUCTION_LXB:
	case INSTRUCTION_SXB:

	case INSTRUCTION_LXBA:
	case INSTRUCTION_SXBA:
		loResult = 'B';
		break;

	case INSTRUCTION_LTWA:

	case INSTRUCTION_LSW:
	case INSTRUCTION_SSW:
	case INSTRUCTION_LSWA:
	case INSTRUCTION_SSWA:

	case INSTRUCTION_LXW:
	case INSTRUCTION_SXW:

	case INSTRUCTION_LXWA:
	case INSTRUCTION_SXWA:
		loResult = 'W';
		break;

	case INSTRUCTION_LTDA:

	case INSTRUCTION_LSD:
	case INSTRUCTION_SSD:
	case INSTRUCTION_LSDA:
	case INSTRUCTION_SSDA:

	case INSTRUCTION_LXD:
	case INSTRUCTION_SXD:

	case INSTRUCTION_LXDA:
	case INSTRUCTION_SXDA:
		loResult = 'L';
		break;
	case INSTRUCTION_LETA:
	case INSTRUCTION_LESA:
	case INSTRUCTION_LEXA:
		// we do not know the operand size here
		loResult = '?';
		break;

	default:
		loResult = '?';
		printf(
				"getUppercaseVariableType(): unknown variable type for the instruction %d\n",
				aInstruction);
		break;

	}
	return (loResult);
}

/*
 Get the address which is safely behind the current message handler/procedure
 */
int getAddressBehindTheFunction(int aStartAddress, int aLength) {
	int i;
	aStartAddress += 2; // local variable size
	for (i = aStartAddress; i < aLength; i++) {
		int loCodeMapValue = getCodeMapAddressValue(i);
		if (loCodeMapValue == MAP_MESSAGE_HANDLER_PROCESSED
				|| loCodeMapValue == MAP_PROCEDURE_START_PROCESSED) {
			// next message handler/procedure starts, time to end
			break;
		}
	}
	// here the i points either to the next message handler/procedure or behind the end of the resource
	return (i);
}

/*
 Add info about the referred resource if available
 */
void writeInfoAboutReferredResourceIfAvailable(long aParameterValue,
		RESINFOPOINTER *aResourcesInfoTable, FILE *aOutputFile) {
	int i, j;
	if (aResourcesInfoTable == NULL) {
		printf(
				"writeInfoAboutReferredResourceIfAvailable(...): aResourcesInfoTable is NULL!\n");
	}
	if (aParameterValue < NUMBER_OF_SPECIAL_TABLES) {
		// a special AESOP table cannot be ever referred in the bytecode
		return;
	}
	for (i = 0; aResourcesInfoTable[i] != NULL; i++) {
		if (i == aParameterValue) {
			// resource found
			char loTypeString[256];
			int loResourceType = aResourcesInfoTable[i]->resourceType;

			if (loResourceType == RESOURCE_TYPE_IMPORT
					|| loResourceType == RESOURCE_TYPE_EXPORT) {
				// IMPORT/EXPORT resources are connected with the corresponding CODE resources,
				// they cannot be referred on their own from the bytecode on their own
				return;
			}

			getResourceTypeString(loTypeString, loResourceType);
			fprintf(aOutputFile, "%7s%s \"%s\" (%s)\n", " ",
					";possible reference to the resource ",
					aResourcesInfoTable[i]->name, loTypeString);
			if (aResourcesInfoTable[i]->resourceType == RESOURCE_TYPE_STRING
					&& aResourcesInfoTable[i]->stringValue != NULL) {
				// write string value
				char *loStringValue = aResourcesInfoTable[i]->stringValue;
				int loStringValueLength = strlen(loStringValue);
				fprintf(aOutputFile, "%7s%s ", " ", ";value: ");
				fprintf(aOutputFile, "\"");
				for (j = 0; j < loStringValueLength; j++) {
					switch (loStringValue[j]) {
					case '\n':
						fprintf(aOutputFile, "%s", "\\n");
						break;
					case '\r':
						fprintf(aOutputFile, "%s", "\\r");
						break;
					case '\t':
						fprintf(aOutputFile, "%s", "\\t"); //TODO: %s was original t, check this
						break;
					case '\"':
						fprintf(aOutputFile, "%s", "\\\"");
						break;
					case '\\':
						fprintf(aOutputFile, "%s", "\\\\");
						break;
					default:
						fprintf(aOutputFile, "%c", loStringValue[j]);
						break;
					}
				}
				fprintf(aOutputFile, "\"\n");
			}
			return;
		}
	}
	// there is no resource with this number
	return;
}

