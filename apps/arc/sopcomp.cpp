//����������������������������������������������������������������������������
//��                                                                        ��
//��  SOPCOMP.C                                                             ��
//��                                                                        ��
//��  AESOP state-object program compiler class                             ��
//��                                                                        ��
//��  Version: 1.00 of 30-Apr-92 -- Initial version                         ��
//��                                                                        ��
//��  Project: Extensible State-Object Processor (AESOP/16)                 ��
//��   Author: John Miles                                                   ��
//��                                                                        ��
//��  C source compatible with IBM PC ANSI C/C++ implementations            ��
//��  Large memory model (16-bit DOS)                                       ��
//��                                                                        ��
//����������������������������������������������������������������������������
//��                                                                        ��
//��  Copyright (C) 1992 Miles Design, Inc.                                 ��
//��                                                                        ��
//��  Miles Design, Inc.                                                    ��
//��  10926 Jollyville #308                                                 ��
//��  Austin, TX 78759                                                      ��
//��  (512) 345-2642 / BBS (512) 454-9990 / FAX (512) 338-9630              ��
//��                                                                        ��
//����������������������������������������������������������������������������

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.hpp"
#include "system.hpp"
#include "arcmsg.hpp"
#include "dict.hpp"
#include "lexan.hpp"
#include "preproc.hpp"
#include "resfile.hpp"
#include "resource.hpp"
#include "rscomp.hpp"
#include "sopcomp.hpp"

BYTE *SOP_keywords[] = { KW_CASE, KW_DEFAULT, KW_MESSAGE, KW_PROCEDURE,
		KW_RETURN, KW_IF, KW_ELSE, KW_SWITCH, KW_WHILE, KW_DO, KW_FOR, KW_LOOP,
		KW_BREAK, KW_CONTINUE, KW_CLASS, KW_MEMBER, KW_EXTERN, KW_PUBLIC,
		KW_PRIVATE, KW_TABLE, KW_LONG, KW_WORD, KW_BYTE, KW_AND, KW_OR, KW_NOT,
		KW_STRING, KW_TRIGGER, NULL };

//
// Decimal tokens for keywords
//

enum {
	SOP_CASE,
	SOP_DEFAULT,
	SOP_MESSAGE,
	SOP_PROCEDURE,
	SOP_RETURN,
	SOP_IF,
	SOP_ELSE,
	SOP_SWITCH,
	SOP_WHILE,
	SOP_DO,
	SOP_FOR,
	SOP_LOOP,
	SOP_BREAK,
	SOP_CONTINUE,
	SOP_CLASS,
	SOP_MEMBER,
	SOP_EXTERN,
	SOP_PUBLIC,
	SOP_PRIVATE,
	SOP_TABLE,
	SOP_LONG,
	SOP_WORD,
	SOP_BYTE,
	SOP_AND,
	SOP_OR,
	SOP_NOT,
	SOP_STRING,
	SOP_TRIGGER
};

//
// Symbol names which share leading substrings must be defined in order
// of increasing length
//

BYTE *SOP_symbols[] = {
		",",     // single-char operators
		"?", ":", "|", "&", "^", "!", "+", "-", "*", "/", "%", "~", "@", "[",
		"]", "(", ")", ".", ";", "<", ">", "=", "{", "}",

		"^^",    // double-char operators
		"++", "--", "<=", ">=", "<<", ">>", "*=", "/=", "%=", "+=", "-=", "&=",
		"^=", "|=", "==", "!=",

		">>=",   // triple-char operators
		"<<=",

		NULL };

enum {
	SOP_COMMA,
	SOP_QUESTION,
	SOP_COLON,
	SOP_BIT_OR,
	SOP_BIT_AND,
	SOP_XOR,
	SOP_EXCLAMATION,
	SOP_PLUS,
	SOP_MINUS,
	SOP_MUL,
	SOP_DIV,
	SOP_MOD,
	SOP_TILDE,
	SOP_AT,
	SOP_L_SQUARE,
	SOP_R_SQUARE,
	SOP_L_PAREN,
	SOP_R_PAREN,
	SOP_PERIOD,
	SOP_SEMICOLON,
	SOP_LT,
	SOP_GT,
	SOP_SETEQ,
	SOP_L_CURLY,
	SOP_R_CURLY,

	SOP_EXP,
	SOP_INC,
	SOP_DEC,
	SOP_LE,
	SOP_GE,
	SOP_SHL,
	SOP_SHR,
	SOP_MULEQ,
	SOP_DIVEQ,
	SOP_MODEQ,
	SOP_PLUSEQ,
	SOP_MINUSEQ,
	SOP_ANDEQ,
	SOP_XOREQ,
	SOP_OREQ,
	SOP_E,
	SOP_NE,

	SOP_SHREQ,
	SOP_SHLEQ
};

enum {
	REF_LOAD, REF_STORE, REF_PUSH, REF_DUP
};

enum {
	OP_BRT,  // BRanch if True
	OP_BRF,  // BRanch if False
	OP_BRA,  // BRanch Always
	OP_CASE, // CASE selection
	OP_PUSH, // PUSH 0 onto stack
	OP_DUP,  // DUPlicate top of stack
	OP_NOT,  // Logical NOT (unary)
	OP_SETB, // SET Boolean value (unary)
	OP_NEG,  // NEGate (unary)
	OP_ADD,  // ADD (binary)
	OP_SUB,  // SUBtract (binary)
	OP_MUL,  // MULtiply (binary)
	OP_DIV,  // DIVide (binary)
	OP_MOD,  // MODulus (binary)
	OP_EXP,  // EXPonent (binary)
	OP_BAND, // Bitwise AND (binary)
	OP_BOR,  // Bitwise OR (binary)
	OP_XOR,  // Bitwise XOR (binary)
	OP_BNOT, // Bitwise NOT (unary)
	OP_SHL,  // SHift Left (binary)
	OP_SHR,  // SHift Right (binary)
	OP_LT,   // Less Than (binary)
	OP_LE,   // Less than or Equal (binary)
	OP_EQ,   // EQual (binary)
	OP_NE,   // Not Equal (binary)
	OP_GE,   // Greater than or Equal (binary)
	OP_GT,   // Greather Than (binary)
	OP_INC,  // INCrement (unary)
	OP_DEC,  // DECrement (unary)
	OP_SHTC, // Load SHorT Constant (0-256)
	OP_INTC, // Load INTeger Constant (256-64K)
	OP_LNGC, // Load LoNG Constant (64K-4G)
	OP_RCRS, // Reference Code ReSource
	OP_CALL, // CALL function
	OP_SEND, // SEND message
	OP_PASS, // PASS message to parent class
	OP_JSR,  // Jump to SubRoutine
	OP_RTS,  // ReTurn from Subroutine
	OP_AIM,  // Array Index Multiply
	OP_AIS,  // Array Index Shift
	OP_LTBA, // Load Table Byte Array
	OP_LTWA, // Load Table Word Array
	OP_LTDA, // Load Table Dword Array
	OP_LETA, // Load Effective Table Address
	OP_LAB,  // Load Auto Byte
	OP_LAW,  // Load Auto Word
	OP_LAD,  // Load Auto Dword
	OP_SAB,  // Store Auto Byte
	OP_SAW,  // Store Auto Word
	OP_SAD,  // Store Auto Dword
	OP_LABA, // Load Auto Byte Array
	OP_LAWA, // Load Auto Word Array
	OP_LADA, // Load Auto Dword Array
	OP_SABA, // Store Auto Byte Array
	OP_SAWA, // Store Auto Word Array
	OP_SADA, // Store Auto Dword Array
	OP_LEAA, // Load Effective Auto Address
	OP_LSB,  // Load Static Byte
	OP_LSW,  // Load Static Word
	OP_LSD,  // Load Static Dword
	OP_SSB,  // Store Static Byte
	OP_SSW,  // Store Static Word
	OP_SSD,  // Store Static Dword
	OP_LSBA, // Load Static Byte Array
	OP_LSWA, // Load Static Word Array
	OP_LSDA, // Load Static Dword Array
	OP_SSBA, // Store Static Byte Array
	OP_SSWA, // Store Static Word Array
	OP_SSDA, // Store Static Dword Array
	OP_LESA, // Load Effective Static Address
	OP_LXB,  // Load eXtern Byte
	OP_LXW,  // Load eXtern Word
	OP_LXD,  // Load eXtern Dword
	OP_SXB,  // Store eXtern Byte
	OP_SXW,  // Store eXtern Word
	OP_SXD,  // Store eXtern Dword
	OP_LXBA, // Load eXtern Byte Array
	OP_LXWA, // Load eXtern Word Array
	OP_LXDA, // Load eXtern Dword Array
	OP_SXBA, // Store eXtern Byte Array
	OP_SXWA, // Store eXtern Word Array
	OP_SXDA, // Store eXtern Dword Array
	OP_LEXA, // Load Effective eXtern Address
	OP_SXAS, // Set eXtern Array Source
	OP_LECA, // Load Effective Code Address
	OP_SOLE, // Selector for Object List Entry
	OP_END,  // END of handler
	OP_BRK   // BReaKpoint for debugging
};

typedef struct PVAL {
	void (*fn)(SOP_class *SOP, UWORD op, struct PVAL *pv);

	DICT_class *type;
	DICT_entry *val;

	WORD indexed;
	WORD source;
} PVAL;

void SOP_rvalue(SOP_class *SOP, PVAL *PV);
WORD SOP_check_lvalue(SOP_class *SOP, PVAL *PV);

void SOP_expression(SOP_class *SOP);
void SOP_expr_list(SOP_class *SOP, PVAL *PV);
void SOP_expr_assign(SOP_class *SOP, PVAL *PV);
void SOP_expr_cond(SOP_class *SOP, PVAL *PV);
void SOP_expr_or(SOP_class *SOP, PVAL *PV);
void SOP_expr_and(SOP_class *SOP, PVAL *PV);
void SOP_expr_bor(SOP_class *SOP, PVAL *PV);
void SOP_expr_xor(SOP_class *SOP, PVAL *PV);
void SOP_expr_band(SOP_class *SOP, PVAL *PV);
void SOP_expr_eq(SOP_class *SOP, PVAL *PV);
void SOP_expr_rel(SOP_class *SOP, PVAL *PV);
void SOP_expr_shift(SOP_class *SOP, PVAL *PV);
void SOP_expr_add(SOP_class *SOP, PVAL *PV);
void SOP_expr_mul(SOP_class *SOP, PVAL *PV);
void SOP_expr_exp(SOP_class *SOP, PVAL *PV);
void SOP_expr_unary(SOP_class *SOP, PVAL *PV);
void SOP_expr_postfix(SOP_class *SOP, PVAL *PV);
void SOP_expr_primary(SOP_class *SOP, PVAL *PV);

void SOP_compound_statement(SOP_class *SOP);
void SOP_statement(SOP_class *SOP);

#define SIZE_B 1           // Size of byte type
#define SIZE_W 2           // Size of word type
#define SIZE_L 4           // Size of long type
#define CON_D 0            // Static-declarative context
#define CON_H 1            // Message handler context
#define CON_P 2            // Procedure context
#define V_PRIVATE 0        // Private visibility
#define V_PUBLIC  1        // Public visibility (exported)
#define NTYP_MSG 0
#define NTYP_RES 1

#define TEMP_NAME "TEMP"   // Name of TEMP environment variable
/*************************************************************/
//
// Issue an error message of the form
// "Error <file line:> [text] [<current_lexeme>] [text]"
//
/*************************************************************/

void SOP_basic_error(SOP_class *SOP, BYTE *msg) {
	report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), msg,
			LEX_lexeme(SOP->LEX, LEX_CUR), NULL);
}

/*************************************************************/
//
// Emit a stack-machine opcode
//
/*************************************************************/

UWORD SOP_emit_opcode(SOP_class *SOP, WORD op) {
	UWORD pc;
	static BYTE *ops[] =
			{ "BRT", "BRF", "BRA", "CASE", "PUSH", "DUP", "NOT", "SETB", "NEG",
					"ADD", "SUB", "MUL", "DIV", "MOD", "EXP", "BAND", "BOR",
					"XOR", "BNOT", "SHL", "SHR", "LT", "LE", "EQ", "NE", "GE",
					"GT", "INC", "DEC", "SHTC", "INTC", "LNGC", "RCRS", "CALL",
					"SEND", "PASS", "JSR", "RTS", "AIM", "AIS", "LTBA", "LTWA",
					"LTDA", "LETA", "LAB", "LAW", "LAD", "SAB", "SAW", "SAD",
					"LABA", "LAWA", "LADA", "SABA", "SAWA", "SADA", "LEAA",
					"LSB", "LSW", "LSD", "SSB", "SSW", "SSD", "LSBA", "LSWA",
					"LSDA", "SSBA", "SSWA", "SSDA", "LESA", "LXB", "LXW", "LXD",
					"SXB", "SXW", "SXD", "LXBA", "LXWA", "LXDA", "SXBA", "SXWA",
					"SXDA", "LEXA", "SXAS", "LECA", "SOLE", "END", "BRK" };

	if (verbose())
		printf("%s %.05u: %s\n", SOP->name, SOP->PC, ops[op]);

	*((UBYTE *) ((UBYTE *) SOP->CODE + (pc = SOP->PC))) = (UBYTE) op & 0xff;

	if (SOP->PC > MAX_CODE - 1 - sizeof(UBYTE))
		SOP_basic_error(SOP, MSG_OCS);

	SOP->PC += sizeof(UBYTE);

	return pc;
}

/*************************************************************/
//
// Emit an unsigned long constant value
//
/*************************************************************/

UWORD SOP_emit_long(SOP_class *SOP, ULONG val) {
	UWORD pc;

	if (verbose())
		printf("%s %.05u: %lu\n", SOP->name, SOP->PC, val);

	*((ULONG *) ((UBYTE *) SOP->CODE + (pc = SOP->PC))) = val;

	if (SOP->PC > MAX_CODE - 1 - sizeof(ULONG))
		SOP_basic_error(SOP, MSG_OCS);

	SOP->PC += sizeof(ULONG);

	return pc;
}

/*************************************************************/
//
// Emit an unsigned word constant value
//
/*************************************************************/

UWORD SOP_emit_word(SOP_class *SOP, ULONG val) {
	UWORD pc;

	if (verbose())
		printf("%s %.05u: %lu\n", SOP->name, SOP->PC, val);

	*((UWORD *) ((UBYTE *) SOP->CODE + (pc = SOP->PC))) = (UWORD) val & 0xffff;

	if (SOP->PC > MAX_CODE - 1 - sizeof(UWORD))
		SOP_basic_error(SOP, MSG_OCS);

	SOP->PC += sizeof(UWORD);

	return pc;
}

/*************************************************************/
//
// Emit an unsigned short constant value
//
/*************************************************************/

UWORD SOP_emit_byte(SOP_class *SOP, ULONG val) {
	UWORD pc;

	if (verbose())
		printf("%s %.05u: %lu\n", SOP->name, SOP->PC, val);

	*((UBYTE *) ((UBYTE *) SOP->CODE + (pc = SOP->PC))) = (UBYTE) val & 0xff;

	if (SOP->PC > MAX_CODE - 1 - sizeof(UBYTE))
		SOP_basic_error(SOP, MSG_OCS);

	SOP->PC += sizeof(UBYTE);

	return pc;
}

/*************************************************************/
//
// Emit optimal array indexing operation for a given dimension size
//
/*************************************************************/

void SOP_emit_array_index(SOP_class *SOP, ULONG dsize) {
	WORD log;

	if ((log = log2(dsize)) != -1)
		if (log == 0)
			SOP_emit_opcode(SOP, OP_ADD);
		else {
			SOP_emit_opcode(SOP, OP_AIS);
			SOP_emit_byte(SOP, log);
		}
	else {
		SOP_emit_opcode(SOP, OP_AIM);
		SOP_emit_word(SOP, dsize);
	}
}

/*************************************************************/
//
// Emit literal numeric constant value with shortest possible word
// size
//
/*************************************************************/

void SOP_emit_constant(SOP_class *SOP, ULONG val) {
	if (val < 256L) {
		SOP_emit_opcode(SOP, OP_SHTC);
		SOP_emit_byte(SOP, val);
	} else if (val < 65536L) {
		SOP_emit_opcode(SOP, OP_INTC);
		SOP_emit_word(SOP, val);
	} else {
		SOP_emit_opcode(SOP, OP_LNGC);
		SOP_emit_long(SOP, val);
	}
}

/*************************************************************/
//
// Boolean functions to test for presence of a given symbol, keyword, or
// class of tokens
//
/*************************************************************/

WORD SOP_next_symbol(SOP_class *SOP, WORD token) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	return (LEX_token(SOP->LEX, LEX_NXT) == token);
}

WORD SOP_next_keyword(SOP_class *SOP, WORD token) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_KEYWORD)
		return 0;

	return (LEX_token(SOP->LEX, LEX_NXT) == token);
}

WORD SOP_next_literal_constant(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) == TTYP_STRLIT)
		return 1;

	if (LEX_type(SOP->LEX, LEX_NXT) == TTYP_NUM)
		return 1;

	return (SOP_next_symbol(SOP, SOP_MINUS) || SOP_next_symbol(SOP, SOP_PLUS));
}

WORD SOP_next_mul(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_MUL:
	case SOP_DIV:
	case SOP_MOD:
		return 1;
	}

	return 0;
}

WORD SOP_next_add(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_PLUS:
	case SOP_MINUS:
		return 1;
	}

	return 0;
}

WORD SOP_next_shift(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_SHL:
	case SOP_SHR:
		return 1;
	}

	return 0;
}

WORD SOP_next_assign(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_SETEQ:
	case SOP_MULEQ:
	case SOP_DIVEQ:
	case SOP_MODEQ:
	case SOP_PLUSEQ:
	case SOP_MINUSEQ:
	case SOP_ANDEQ:
	case SOP_XOREQ:
	case SOP_OREQ:
	case SOP_SHREQ:
	case SOP_SHLEQ:
		return 1;
	}

	return 0;
}

WORD SOP_next_eq(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_E:
	case SOP_NE:
		return 1;
	}

	return 0;
}

WORD SOP_next_rel(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_LT:
	case SOP_GT:
	case SOP_LE:
	case SOP_GE:
		return 1;
	}

	return 0;
}

WORD SOP_next_unary(SOP_class *SOP) {
	if (SOP_next_keyword(SOP, SOP_NOT))
		return 1;

	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_MINUS:
	case SOP_PLUS:
	case SOP_EXCLAMATION:
	case SOP_TILDE:
	case SOP_AT:
	case SOP_INC:
	case SOP_DEC:
		return 1;
	}

	return 0;
}

WORD SOP_next_postfix(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_SYMBOL)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_L_PAREN:
	case SOP_L_SQUARE:
	case SOP_INC:
	case SOP_DEC:
	case SOP_PERIOD:
		return 1;
	}

	return 0;
}

WORD SOP_next_log_or(SOP_class *SOP) {
	return (SOP_next_keyword(SOP, SOP_OR));
}

WORD SOP_next_log_and(SOP_class *SOP) {
	return (SOP_next_keyword(SOP, SOP_AND));
}

WORD SOP_next_definition(SOP_class *SOP) {
	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_KEYWORD)
		return 0;

	switch (LEX_token(SOP->LEX, LEX_NXT)) {
	case SOP_CLASS:
		return 1;
	}

	return 0;
}

/*************************************************************/
//
// Look up current lexical name in ROED directory; warn if 
// resource was created by reference during another resource's
// compilation
//
/*************************************************************/

ULONG SOP_resource_name_entry(SOP_class *SOP) {
	ULONG val;

	val = RS_current_ROED_entry(SOP->RS, LEX_lexeme(SOP->LEX, LEX_CUR));

	if (val == -1L) {
		val = RS_get_ROED_entry(SOP->RS, LEX_lexeme(SOP->LEX, LEX_CUR));

		if (strcmp(LEX_lexeme(SOP->LEX, LEX_CUR), SOP->name))
			DICT_enter(SOP->RS->refcr, LEX_lexeme(SOP->LEX, LEX_CUR), D_DEFHEAP)->def =
					str_alloc(LEX_line(SOP->LEX, LEX_CUR));
	}

	return val;
}

/*************************************************************/
//
// Resolve references in local fixup chain
//
/*************************************************************/

void SOP_local_fixup(SOP_class *SOP, UWORD reference, UWORD value) {
	UWORD *ptr;

	while (reference) {
		ptr = (UWORD *) ((UBYTE *) SOP->CODE + reference);

		reference = *ptr;

		*ptr = value;
	}
}

/*************************************************************/
//
// Add symbol and value to export list, returning 0 if already 
// present
// 
/*************************************************************/

WORD SOP_export_symbol(SOP_class *SOP, ULONG symbol, ULONG value, BYTE type,
		ULONG asize) {
	CSS_class *def;
	BYTE *tag, *sym;
	WORD stat;

	if (type == 'M')
		sym = str(symbol);
	else
		sym = (BYTE *) symbol;

	def = CSS_construct(NULL);

	CSS_add_num(def, value);

	if (asize != 1)
		CSS_add_num(def, asize);

	tag = (BYTE*) mem_alloc(3L + (ULONG) strlen(sym));
	tag[0] = type;
	tag[1] = ':';
	tag[2] = 0;
	if (sym != NULL)
		strcat(tag, sym);

	if (DICT_lookup(SOP->EXPT, tag) == NULL) {
		DICT_enter(SOP->EXPT, tag, D_DEFHEAP)->def = CSS_string(def);
		stat = 1;
	} else {
		free(CSS_string(def));
		stat = 0;
	}

	free(tag);
	CSS_destroy(def);

	if (type == 'M')
		free(sym);

	return stat;
}

/*************************************************************/
//
// Add symbol of given type to import dictionary, returning      
// offset in external reference list
// 
/*************************************************************/

UWORD SOP_import_symbol(SOP_class *SOP, BYTE *sym, BYTE *class_type,
		BYTE type) {
	BYTE *tag, *def, *index;
	DICT_entry *entry;
	UWORD size;

	index = str(SOP->import_index);

	switch (type) {
	case 'C':
		size = sizeof(void *);
		def = str_alloc(index);
		break;

	case 'B':
	case 'W':
	case 'L':
		size = sizeof(UWORD);
		def = (BYTE*) mem_alloc(
				(ULONG) strlen(class_type) + (ULONG) strlen(index) + 4L);
		strcpy(def, index);
		strcat(def, ",");
		strcat(def, class_type);
		break;
	}

	tag = (BYTE*) mem_alloc(3L + (ULONG) strlen(sym));
	tag[0] = type;
	tag[1] = ':';
	tag[2] = 0;
	strcat(tag, sym);

	if ((entry = DICT_lookup(SOP->IMPT, tag)) == NULL) {
		entry = DICT_enter(SOP->IMPT, tag, D_DEFHEAP);

		entry->def = def;
		SOP->import_index += size;
	} else
		free(def);

	free(tag);
	free(index);

	return (UWORD) ascnum((BYTE*) entry->def, 10);
}

/*************************************************************/
//
// Warn if non-declarative statement lies outside message
// handler or procedure definition
//
/*************************************************************/

WORD SOP_check_access(SOP_class *SOP) {
	if (SOP->context == CON_D) {
		SOP_basic_error(SOP, MSG_URC);
		return 0;
	}

	return 1;
}

/*************************************************************/
//
// Warn if handler or procedure definition lies within handler
// or procedure definition
//
/*************************************************************/

WORD SOP_check_nest(SOP_class *SOP) {
	if (SOP->context != CON_D) {
		SOP_basic_error(SOP, MSG_CND);
		return 0;
	}

	return 1;
}

/*************************************************************/
//
// Warn if auto variable declaration lies outside handler
// or procedure definition
//
/*************************************************************/

WORD SOP_check_auto_nest(SOP_class *SOP) {
	if (SOP->context == CON_D) {
		SOP_basic_error(SOP, MSG_AVO);
		return 0;
	}

	return 1;
}

/*************************************************************/
//
// Warn if unused variables or values exist after compilation
// of a scope
//
/*************************************************************/

void SOP_show_usage(UWORD severity, DICT_class *DICT, BYTE *MSG) {
	DICT_entry *entry;
	DI_class *DI;

	DI = DI_construct(DICT);

	while ((entry = DI_fetch(DI)) != NULL)
		report(severity, (BYTE*) entry->def, MSG, (BYTE*) entry->tag);

	DI_destroy(DI);
}

/*************************************************************/
//
// Fetch next token, reporting error if not specified keyword
//
/*************************************************************/

void SOP_require_keyword(SOP_class *SOP, WORD token) {
	LEX_require(SOP->LEX, TTYP_KEYWORD, token, SOP_keywords[token]);
}

/*************************************************************/
//
// Fetch next token, reporting error if not specified symbol
//
/*************************************************************/

void SOP_require_symbol(SOP_class *SOP, WORD token) {
	LEX_require(SOP->LEX, TTYP_SYMBOL, token, SOP_symbols[token]);
}

/*************************************************************/
//
// Fetch next literal constant, handling unary and binary +/- operators
//
/*************************************************************/

LONG SOP_fetch_literal_constant(SOP_class *SOP) {
	WORD t;
	LONG acc;
	ULONG sign;

	acc = 0L;
	sign = 1L;

	while (SOP_next_literal_constant(SOP)) {
		t = SOP_next_symbol(SOP, SOP_MINUS);
		LEX_fetch(SOP->LEX);

		if (t)
			sign = (sign == 1L) ? -1L : 1L;
		else
			switch (LEX_type(SOP->LEX, LEX_CUR)) {
			case TTYP_NUM:
				acc = acc + (sign * LEX_value(SOP->LEX, LEX_CUR));
				sign = 1L;
				break;

			case TTYP_STRLIT:
				acc = acc + (sign * SOP_resource_name_entry(SOP));
				break;
			}
	}

	return acc;
}

/*************************************************************/
//
// Emit code to handle variable references
//
// If external reference has no object handle, convert it to a
// static reference if possible
//
/*************************************************************/

void SOP_fn_var_reference(SOP_class *SOP, UWORD op, PVAL *PV) {
	DICT_entry *entry;
	UWORD i, read_op, write_op, vsize, index, ndims;
	CSS_class *CSS;

	if (PV->type == SOP->XTRN) {
		if (!PV->source)
			if (DICT_lookup(SOP->STAT, PV->val->tag) != NULL) {
				PV->type = SOP->STAT;
				PV->source = -1;
			} else
				SOP_basic_error(SOP, MSG_NCL);
	} else if (PV->source != -1)
		SOP_basic_error(SOP, MSG_IUD);

	entry = DICT_lookup(PV->type, PV->val->tag);

	CSS = CSS_construct((BYTE*) entry->def);

	index = (UWORD) CSS_fetch_num(CSS);
	ndims = (UWORD) CSS_fetch_num(CSS);

	if (ndims > 1)
		for (i = 0; i < (ndims - 1); i++)
			CSS_fetch_num(CSS);

	vsize = (UWORD) CSS_fetch_num(CSS);

	CSS_destroy(CSS);

	if (PV->type == SOP->XTRN)
		switch (vsize) {
		case SIZE_B:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LXBA : OP_LEXA;
				write_op = OP_SXBA;
			} else {
				read_op = OP_LXB;
				write_op = OP_SXB;
			}
			break;

		case SIZE_W:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LXWA : OP_LEXA;
				write_op = OP_SXWA;
			} else {
				read_op = OP_LXW;
				write_op = OP_SXW;
			}
			break;

		case SIZE_L:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LXDA : OP_LEXA;
				write_op = OP_SXDA;
			} else {
				read_op = OP_LXD;
				write_op = OP_SXD;
			}
			break;
		}
	else if (PV->type == SOP->STAT)
		switch (vsize) {
		case SIZE_B:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LSBA : OP_LESA;
				write_op = OP_SSBA;
			} else {
				read_op = OP_LSB;
				write_op = OP_SSB;
			}
			break;

		case SIZE_W:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LSWA : OP_LESA;
				write_op = OP_SSWA;
			} else {
				read_op = OP_LSW;
				write_op = OP_SSW;
			}
			break;

		case SIZE_L:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LSDA : OP_LESA;
				write_op = OP_SSDA;
			} else {
				read_op = OP_LSD;
				write_op = OP_SSD;
			}
			break;
		}
	else if (PV->type == SOP->TABL)
		switch (vsize) {
		case SIZE_B:
			read_op = (PV->indexed) ? OP_LTBA : OP_LETA;
			break;

		case SIZE_W:
			read_op = (PV->indexed) ? OP_LTWA : OP_LETA;
			break;

		case SIZE_L:
			read_op = (PV->indexed) ? OP_LTDA : OP_LETA;
			break;
		}
	else
		switch (vsize) {
		case SIZE_B:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LABA : OP_LEAA;
				write_op = OP_SABA;
			} else {
				read_op = OP_LAB;
				write_op = OP_SAB;
			}
			break;

		case SIZE_W:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LAWA : OP_LEAA;
				write_op = OP_SAWA;
			} else {
				read_op = OP_LAW;
				write_op = OP_SAW;
			}
			break;

		case SIZE_L:
			if (ndims) {
				read_op = (PV->indexed) ? OP_LADA : OP_LEAA;
				write_op = OP_SADA;
			} else {
				read_op = OP_LAD;
				write_op = OP_SAD;
			}
			break;
		}

	switch (op) {
	case REF_LOAD:
		if (PV->type == SOP->AUTO) {
			if ((DICT_lookup(SOP->ADUS, PV->val->tag) != NULL)
					&& (DICT_lookup(SOP->AVUS, PV->val->tag) == NULL))
				DICT_enter(SOP->APUS, PV->val->tag, D_DEFHEAP)->def = str_alloc(
						LEX_line(SOP->LEX, LEX_CUR));

			DICT_delete(SOP->ADUS, PV->val->tag);
			DICT_delete(SOP->AVUS, PV->val->tag);
		} else {
			DICT_delete(SOP->DUSE, PV->val->tag);
			DICT_delete(SOP->VUSE, PV->val->tag);
		}

		SOP_emit_opcode(SOP, read_op);
		SOP_emit_word(SOP, index);
		break;

	case REF_STORE:
		if (PV->type == SOP->TABL)
			report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_CMT, PV->val->tag);
		else if (PV->type == SOP->AUTO) {
			if (DICT_lookup(SOP->ADUS, PV->val->tag) != NULL) {
				DICT_delete(SOP->ADUS, PV->val->tag);
				DICT_enter(SOP->AVUS, PV->val->tag, D_DEFHEAP)->def = str_alloc(
						LEX_line(SOP->LEX, LEX_CUR));
			}

			if ((!strcmp(PV->val->tag, KW_THIS)))
				report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_IVA,
						PV->val->tag);
		} else if (DICT_lookup(SOP->DUSE, PV->val->tag) != NULL) {
			DICT_delete(SOP->DUSE, PV->val->tag);
			DICT_enter(SOP->VUSE, PV->val->tag, D_DEFHEAP)->def = str_alloc(
					LEX_line(SOP->LEX, LEX_CUR));
		}

		SOP_emit_opcode(SOP, write_op);
		SOP_emit_word(SOP, index);
		break;

	case REF_PUSH:
		if ((ndims) || (PV->type == SOP->XTRN))
			SOP_emit_opcode(SOP, OP_PUSH);
		return;

	case REF_DUP:
		if ((ndims) || (PV->type == SOP->XTRN))
			SOP_emit_opcode(SOP, OP_DUP);
		return;
	}
}

/*************************************************************/
//
// send_message ( expression , string-constant )
// send_message ( expression , string-constant , expression-list )
//
/*************************************************************/

void SOP_compile_send(SOP_class *SOP, PVAL *PV) {
	UWORD argcnt;
	UWORD msgnum;

	argcnt = 0;

	SOP_require_symbol(SOP, SOP_L_PAREN);

	SOP_expr_assign(SOP, PV);
	SOP_rvalue(SOP, PV);

	SOP_require_symbol(SOP, SOP_COMMA);

	LEX_fetch(SOP->LEX);

	if (LEX_type(SOP->LEX, LEX_CUR) != TTYP_STRLIT) {
		msgnum = -1U;
		SOP_basic_error(SOP, MSG_BMN);
	} else
		msgnum = RS_get_MSGD_entry(SOP->RS, LEX_lexeme(SOP->LEX, LEX_CUR));

	while (LEX_next_comma(SOP->LEX)) {
		++argcnt;

		SOP_emit_opcode(SOP, OP_PUSH);
		SOP_expr_assign(SOP, PV);
		SOP_rvalue(SOP, PV);
	}

	SOP_require_symbol(SOP, SOP_R_PAREN);

	SOP_emit_opcode(SOP, OP_SEND);
	SOP_emit_byte(SOP, argcnt);
	SOP_emit_word(SOP, msgnum);

	PV->fn = NULL;
}

/*************************************************************/
//
// pass_message ( opt_expression-list )
//
/*************************************************************/

void SOP_compile_pass(SOP_class *SOP, PVAL *PV) {
	UWORD argcnt;

	argcnt = 0;

	SOP_require_symbol(SOP, SOP_L_PAREN);

	if (!SOP_next_symbol(SOP, SOP_R_PAREN))
		do {
			++argcnt;

			SOP_emit_opcode(SOP, OP_PUSH);
			SOP_expr_assign(SOP, PV);
			SOP_rvalue(SOP, PV);
		} while (LEX_next_comma(SOP->LEX));

	SOP_require_symbol(SOP, SOP_R_PAREN);

	SOP_emit_opcode(SOP, OP_PASS);
	SOP_emit_byte(SOP, argcnt);

	PV->fn = NULL;
}

/*************************************************************/
//
// Add code resource to import list and emit reference opcode
//
/*************************************************************/

void SOP_fn_code_resource(SOP_class *SOP, UWORD op, PVAL *PV) {
	UWORD operand;

	op++;  // (avoid warning)

	operand = SOP_import_symbol(SOP, PV->val->tag, NULL, 'C');

	if ((!strcmp(PV->val->tag, "notify")) || (!strcmp(PV->val->tag, "cancel")))
		SOP->ntype = NTYP_MSG;
	else
		SOP->ntype = NTYP_RES;

	SOP_emit_opcode(SOP, OP_RCRS);
	SOP_emit_word(SOP, operand);
}

/*************************************************************/
//
// Emit code to call local procedure
//
/*************************************************************/

void SOP_fn_procedure(SOP_class *SOP, UWORD op, PVAL *PV) {
	ULONG operand;

	op++;  // (avoid warning)

	operand = ascnum((BYTE*) PV->val->def, 10);

	SOP_emit_opcode(SOP, OP_JSR);
	SOP_emit_word(SOP, (UWORD) operand);
}

/*************************************************************/
//
// Determine which namespace name belongs to, and select function to
// handle the reference
//
// If identifier is not valid in any namespace, report error and 
// create "dummy" long entry for identifier in auto variable space
// to suppress later error messages
//
/*************************************************************/

void SOP_name_reference(SOP_class *SOP, PVAL *PV, BYTE *name) {
	DICT_entry *entry;

	PV->indexed = 0;
	PV->source = -1;

	if ((entry = DICT_lookup(SOP->PROC, name)) != NULL) {
		PV->fn = SOP_fn_procedure;
		PV->val = entry;
		PV->type = SOP->PROC;
	} else if (!strcmp(name, FN_SEND)) {
		PV->fn = NULL;
		SOP_compile_send(SOP, PV);
	} else if (!strcmp(name, FN_PASS)) {
		PV->fn = NULL;
		SOP_compile_pass(SOP, PV);
	} else if ((entry = DICT_lookup(SOP->RS->dict[CRFD], name)) != NULL) {
		PV->fn = SOP_fn_code_resource;
		PV->val = entry;
		PV->type = SOP->RS->dict[CRFD];
	} else if ((entry = DICT_lookup(SOP->ARGV, name)) != NULL) {
		PV->fn = SOP_fn_var_reference;
		PV->val = entry;
		PV->type = SOP->ARGV;
	} else if ((entry = DICT_lookup(SOP->AUTO, name)) != NULL) {
		PV->fn = SOP_fn_var_reference;
		PV->val = entry;
		PV->type = SOP->AUTO;
	} else if ((entry = DICT_lookup(SOP->TABL, name)) != NULL) {
		PV->fn = SOP_fn_var_reference;
		PV->val = entry;
		PV->type = SOP->TABL;
	} else if ((entry = DICT_lookup(SOP->XTRN, name)) != NULL) {
		PV->fn = SOP_fn_var_reference;
		PV->val = entry;
		PV->type = SOP->XTRN;
		PV->source = 0;
	} else if ((entry = DICT_lookup(SOP->STAT, name)) != NULL) {
		PV->fn = SOP_fn_var_reference;
		PV->val = entry;
		PV->type = SOP->STAT;
	} else {
		report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_UID, name);

		entry = DICT_enter(SOP->AUTO, name, D_DEFHEAP);
		entry->def = str_alloc("4,0,4");

		PV->fn = SOP_fn_var_reference;
		PV->val = entry;
		PV->type = SOP->AUTO;
	}
}

/*************************************************************/
//
// Compile a series of code resource function arguments 
//
/*************************************************************/

void SOP_compile_call(SOP_class *SOP, PVAL *PV) {
	UWORD argcnt;

	if (PV->type != SOP->RS->dict[CRFD])
		SOP_basic_error(SOP, MSG_IUA);

	argcnt = 0;

	SOP_rvalue(SOP, PV);
	if (!SOP_next_symbol(SOP, SOP_R_PAREN))
		do {
			++argcnt;

			SOP_emit_opcode(SOP, OP_PUSH);
			SOP_expr_assign(SOP, PV);
			SOP_rvalue(SOP, PV);
		} while (LEX_next_comma(SOP->LEX));

	SOP_require_symbol(SOP, SOP_R_PAREN);

	SOP_emit_opcode(SOP, OP_CALL);
	SOP_emit_byte(SOP, argcnt);

	SOP->ntype = NTYP_RES;

	PV->fn = NULL;
}

/*************************************************************/
//
// Compile a series of array-indexing expressions, attempting to
// generate the fastest possible code for a given case
//
/*************************************************************/

void SOP_compile_index(SOP_class *SOP, PVAL *PV) {
	ULONG ndims, dsize;
	CSS_class *CSS;
	DICT_entry *entry;

	if ((PV->type != SOP->AUTO) && (PV->type != SOP->ARGV)
			&& (PV->type != SOP->XTRN) && (PV->type != SOP->STAT)
			&& (PV->type != SOP->TABL)) {
		SOP_basic_error(SOP, MSG_IUB);
		return;
	}

	if (PV->type == SOP->XTRN)
		SOP_emit_opcode(SOP, OP_PUSH);

	entry = DICT_lookup(PV->type, PV->val->tag);

	CSS = CSS_construct((BYTE*) entry->def);
	CSS_fetch_num(CSS);

	ndims = CSS_fetch_num(CSS);

	if ((ndims == 0) || (ndims == -1L))
		SOP_basic_error(SOP, MSG_IUB);

	if (ndims == 1) {
		dsize = CSS_fetch_num(CSS);

		if (dsize == SIZE_B) {
			SOP_expression(SOP);
			SOP_require_symbol(SOP, SOP_R_SQUARE);
		} else {
			SOP_emit_opcode(SOP, OP_SHTC);
			SOP_emit_byte(SOP, 0L);

			SOP_emit_opcode(SOP, OP_PUSH);

			SOP_expression(SOP);
			SOP_require_symbol(SOP, SOP_R_SQUARE);

			SOP_emit_array_index(SOP, dsize);
		}
	} else {
		SOP_emit_opcode(SOP, OP_SHTC);
		SOP_emit_byte(SOP, 0L);

		while (ndims--) {
			SOP_emit_opcode(SOP, OP_PUSH);

			SOP_expression(SOP);
			SOP_require_symbol(SOP, SOP_R_SQUARE);

			if (ndims)
				if (SOP_next_symbol(SOP, SOP_L_SQUARE))
					LEX_fetch(SOP->LEX);
				else {
					report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_NED,
							PV->val->tag);
					break;
				}

			dsize = CSS_fetch_num(CSS);

			SOP_emit_array_index(SOP, dsize);
		}
	}

	while (SOP_next_symbol(SOP, SOP_L_SQUARE)) {
		report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_TMD, PV->val->tag);
		LEX_fetch(SOP->LEX);
		SOP_expression(SOP);
		SOP_require_symbol(SOP, SOP_R_SQUARE);
	}

	PV->indexed = 1;

	CSS_destroy(CSS);

	if (PV->type == SOP->XTRN)
		SOP_emit_opcode(SOP, OP_SXAS);
}

/*************************************************************/
//
// Turn a partial expression into an rvalue, if needed
//
/*************************************************************/

void SOP_rvalue(SOP_class *SOP, PVAL *PV) {
	if (PV->fn != NULL) {
		PV->fn(SOP, REF_LOAD, PV);
		PV->fn = NULL;
	}
}

/*************************************************************/
//
// Report error if expression does not designate a data object
//
/*************************************************************/

WORD SOP_check_lvalue(SOP_class *SOP, PVAL *PV) {
	if (PV->fn == NULL) {
		SOP_basic_error(SOP, MSG_LVR);
		return 0;
	}

	return 1;
}

/*************************************************************/
//
// Parse a complete expression, including any side-effects
//
/*************************************************************/

void SOP_expression(SOP_class *SOP) {
	PVAL PV;

	SOP_expr_list(SOP, &PV);
	SOP_rvalue(SOP, &PV);
}

/*************************************************************/
//
// expression:
//      assignment-expression
//      expression , assignment-expression
//
/*************************************************************/

void SOP_expr_list(SOP_class *SOP, PVAL *PV) {
	SOP_expr_assign(SOP, PV);

	while (LEX_next_comma(SOP->LEX)) {
		SOP_rvalue(SOP, PV);
		SOP_expr_list(SOP, PV);
		SOP_rvalue(SOP, PV);
	}
}

/*************************************************************/
//
// assignment-expression:
//      conditional-expression
//      unary-expression assignment-operator assignment-expression
//
/*************************************************************/

void SOP_expr_assign(SOP_class *SOP, PVAL *PV) {
	PVAL right_side;
	WORD tkn, op;

	SOP_expr_cond(SOP, PV);

	while (SOP_next_assign(SOP)) {
		LEX_fetch(SOP->LEX);
		if (!SOP_check_lvalue(SOP, PV))
			break;

		if ((tkn = LEX_token(SOP->LEX, LEX_CUR)) == SOP_SETEQ) {
			PV->fn(SOP, REF_PUSH, PV);
			SOP_expr_assign(SOP, &right_side);
			SOP_rvalue(SOP, &right_side);
			PV->fn(SOP, REF_STORE, PV);
			PV->fn = NULL;
			continue;
		}

		switch (tkn) {
		case SOP_MULEQ:
			op = OP_MUL;
			break;
		case SOP_DIVEQ:
			op = OP_DIV;
			break;
		case SOP_MODEQ:
			op = OP_MOD;
			break;
		case SOP_PLUSEQ:
			op = OP_ADD;
			break;
		case SOP_MINUSEQ:
			op = OP_SUB;
			break;
		case SOP_ANDEQ:
			op = OP_BAND;
			break;
		case SOP_XOREQ:
			op = OP_XOR;
			break;
		case SOP_OREQ:
			op = OP_BOR;
			break;
		case SOP_SHREQ:
			op = OP_SHR;
			break;
		case SOP_SHLEQ:
			op = OP_SHL;
			break;
		}

		PV->fn(SOP, REF_DUP, PV);
		PV->fn(SOP, REF_LOAD, PV);
		SOP_emit_opcode(SOP, OP_PUSH);
		SOP_expr_assign(SOP, &right_side);
		SOP_rvalue(SOP, &right_side);
		SOP_emit_opcode(SOP, op);
		PV->fn(SOP, REF_STORE, PV);
		PV->fn = NULL;
	}
}

/*************************************************************/
//
// conditional-expression:
//      logical-or-expression
//      logical-or-expression ? expression : conditional-expression
//
/*************************************************************/

void SOP_expr_cond(SOP_class *SOP, PVAL *PV) {
	UWORD falsed, end;

	SOP_expr_or(SOP, PV);

	while (SOP_next_symbol(SOP, SOP_QUESTION)) {
		LEX_fetch(SOP->LEX);

		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, OP_BRF);
		falsed = SOP_emit_word(SOP, 0);

		SOP_expr_list(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, OP_BRA);
		end = SOP_emit_word(SOP, 0);

		SOP_require_symbol(SOP, SOP_COLON);

		SOP_local_fixup(SOP, falsed, SOP->PC);

		SOP_expr_assign(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_local_fixup(SOP, end, SOP->PC);
	}
}

/*************************************************************/
//
// logical-or-expression:
//      logical-and-expression
//      logical-or-expression || / or logical-and-expression
//
/*************************************************************/

void SOP_expr_or(SOP_class *SOP, PVAL *PV) {
	UWORD end;

	SOP_expr_and(SOP, PV);

	end = 0;

	while (SOP_next_log_or(SOP)) {
		LEX_fetch(SOP->LEX);

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_BRT);
		end = SOP_emit_word(SOP, end);

		SOP_expr_and(SOP, PV);
		SOP_rvalue(SOP, PV);
	}

	if (end) {
		SOP_local_fixup(SOP, end, SOP->PC);
		SOP_emit_opcode(SOP, OP_SETB);
	}
}

/*************************************************************/
//
// logical-and-expression:
//      inclusive-or-expression
//      logical-and-expression && / and inclusive-or-expression
//
/*************************************************************/

void SOP_expr_and(SOP_class *SOP, PVAL *PV) {
	UWORD end;

	SOP_expr_bor(SOP, PV);

	end = 0;

	while (SOP_next_log_and(SOP)) {
		LEX_fetch(SOP->LEX);

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_BRF);
		end = SOP_emit_word(SOP, end);

		SOP_expr_bor(SOP, PV);
		SOP_rvalue(SOP, PV);
	}

	if (end) {
		SOP_local_fixup(SOP, end, SOP->PC);
		SOP_emit_opcode(SOP, OP_SETB);
	}
}

/*************************************************************/
//
// inclusive-or-expression:
//      exclusive-or-expression
//      inclusive-or-expression | exclusive-or-expression
//
/*************************************************************/

void SOP_expr_bor(SOP_class *SOP, PVAL *PV) {
	SOP_expr_xor(SOP, PV);

	while (SOP_next_symbol(SOP, SOP_BIT_OR)) {
		LEX_fetch(SOP->LEX);

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_xor(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, OP_BOR);
	}
}

/*************************************************************/
//
// exclusive-or-expression:
//      and-expression
//      exclusive-or-expression ^ and-expression
//
/*************************************************************/

void SOP_expr_xor(SOP_class *SOP, PVAL *PV) {
	SOP_expr_band(SOP, PV);

	while (SOP_next_symbol(SOP, SOP_XOR)) {
		LEX_fetch(SOP->LEX);

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_band(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, OP_XOR);
	}
}

/*************************************************************/
//
// and-expression:
//      equality-expression
//      and-expression & equality-expression
//
/*************************************************************/

void SOP_expr_band(SOP_class *SOP, PVAL *PV) {
	SOP_expr_eq(SOP, PV);

	while (SOP_next_symbol(SOP, SOP_BIT_AND)) {
		LEX_fetch(SOP->LEX);

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_eq(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, OP_BAND);
	}
}

/*************************************************************/
//
// equality-expression:
//         relational-expression
//         equality-expression == relational-expression
//         equality-expression != relational-expression
//
/*************************************************************/

void SOP_expr_eq(SOP_class *SOP, PVAL *PV) {
	WORD op;

	SOP_expr_rel(SOP, PV);

	while (SOP_next_eq(SOP)) {
		LEX_fetch(SOP->LEX);

		switch (LEX_token(SOP->LEX, LEX_CUR)) {
		case SOP_E:
			op = OP_EQ;
			break;
		case SOP_NE:
			op = OP_NE;
			break;
		}

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_rel(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, op);
	}
}

/*************************************************************/
//
// relational-expression:
//      shift-expression
//      relational-expression < shift-expression
//      relational-expression > shift-expression
//      relational-expression <= shift-expression
//      relational-expression >= shift-expression
//
/*************************************************************/

void SOP_expr_rel(SOP_class *SOP, PVAL *PV) {
	WORD op;

	SOP_expr_shift(SOP, PV);

	while (SOP_next_rel(SOP)) {
		LEX_fetch(SOP->LEX);

		switch (LEX_token(SOP->LEX, LEX_CUR)) {
		case SOP_LT:
			op = OP_LT;
			break;
		case SOP_GT:
			op = OP_GT;
			break;
		case SOP_LE:
			op = OP_LE;
			break;
		case SOP_GE:
			op = OP_GE;
			break;
		}

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_shift(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, op);
	}
}

/*************************************************************/
//
// shift-expression:
//      additive-expression
//      shift-expression << additive-expression
//      shift-expression >> additive-expression
//
/*************************************************************/

void SOP_expr_shift(SOP_class *SOP, PVAL *PV) {
	WORD op;

	SOP_expr_add(SOP, PV);

	while (SOP_next_shift(SOP)) {
		LEX_fetch(SOP->LEX);

		switch (LEX_token(SOP->LEX, LEX_CUR)) {
		case SOP_SHL:
			op = OP_SHL;
			break;
		case SOP_SHR:
			op = OP_SHR;
			break;
		}

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_add(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, op);
	}
}

/*************************************************************/
//
// additive-expression:
//      multiplicative-expression
//      additive-expression + multiplicative-expression
//      additive-expression - multiplicative-expression
//
/*************************************************************/

void SOP_expr_add(SOP_class *SOP, PVAL *PV) {
	WORD op;

	SOP_expr_mul(SOP, PV);

	while (SOP_next_add(SOP)) {
		LEX_fetch(SOP->LEX);

		switch (LEX_token(SOP->LEX, LEX_CUR)) {
		case SOP_PLUS:
			op = OP_ADD;
			break;
		case SOP_MINUS:
			op = OP_SUB;
			break;
		}

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_mul(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, op);
	}
}

/*************************************************************/
//
// multiplicative-expression:
//      exponential-expression
//      multiplicative-expression * exponential-expression
//      multiplicative-expression / exponential-expression
//      multiplicative-expression % exponential-expression
//
/*************************************************************/

void SOP_expr_mul(SOP_class *SOP, PVAL *PV) {
	WORD op;

	SOP_expr_exp(SOP, PV);

	while (SOP_next_mul(SOP)) {
		LEX_fetch(SOP->LEX);

		switch (LEX_token(SOP->LEX, LEX_CUR)) {
		case SOP_MUL:
			op = OP_MUL;
			break;
		case SOP_DIV:
			op = OP_DIV;
			break;
		case SOP_MOD:
			op = OP_MOD;
			break;
		}

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_exp(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, op);
	}
}

/*************************************************************/
//
// exponential-expression:
//      unary-expression
//      exponential-expression ^^ unary-expression
//
/*************************************************************/

void SOP_expr_exp(SOP_class *SOP, PVAL *PV) {
	SOP_expr_unary(SOP, PV);

	while (SOP_next_symbol(SOP, SOP_EXP)) {
		LEX_fetch(SOP->LEX);

		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_PUSH);

		SOP_expr_unary(SOP, PV);
		SOP_rvalue(SOP, PV);

		SOP_emit_opcode(SOP, OP_EXP);
	}
}

/*************************************************************/
//
// unary-expression:
//         postfix-expression
//         ++ unary-expression
//         -- unary-expression
//         unary-operator unary-expression
//
/*************************************************************/

void SOP_expr_unary(SOP_class *SOP, PVAL *PV) {
	WORD tkn;

	if (!SOP_next_unary(SOP)) {
		SOP_expr_postfix(SOP, PV);
		return;
	}

	LEX_fetch(SOP->LEX);
	tkn = LEX_token(SOP->LEX, LEX_CUR);

	if ((LEX_type(SOP->LEX, LEX_CUR) == TTYP_KEYWORD) && (tkn == SOP_NOT))
		tkn = SOP_EXCLAMATION;

	SOP_expr_postfix(SOP, PV);

	switch (tkn) {
	case SOP_MINUS:
		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_NEG);
		break;

	case SOP_EXCLAMATION:
		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_NOT);
		break;

	case SOP_TILDE:
		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_BNOT);
		break;

	case SOP_AT:
		SOP_rvalue(SOP, PV);
		SOP_emit_opcode(SOP, OP_SOLE);
		break;

	case SOP_INC:
		if (!SOP_check_lvalue(SOP, PV))
			break;

		PV->fn(SOP, REF_DUP, PV);
		PV->fn(SOP, REF_LOAD, PV);
		SOP_emit_opcode(SOP, OP_INC);
		PV->fn(SOP, REF_STORE, PV);
		PV->fn = NULL;
		break;

	case SOP_DEC:
		if (!SOP_check_lvalue(SOP, PV))
			break;

		PV->fn(SOP, REF_DUP, PV);
		PV->fn(SOP, REF_LOAD, PV);
		SOP_emit_opcode(SOP, OP_DEC);
		PV->fn(SOP, REF_STORE, PV);
		PV->fn = NULL;
		break;
	}
}

/*************************************************************/
//
// postfix-expression:
//         primary-expression
//         postfix-expression [ expression ]
//         postfix-expression ( opt_expression-list )
//         postfix-expression . name
//         postfix-expression ++
//         postfix-expression --
//
/*************************************************************/

void SOP_expr_postfix(SOP_class *SOP, PVAL *PV) {
	SOP_expr_primary(SOP, PV);

	while (SOP_next_postfix(SOP)) {
		LEX_fetch(SOP->LEX);

		switch (LEX_token(SOP->LEX, LEX_CUR)) {
		case SOP_INC:
			if (!SOP_check_lvalue(SOP, PV))
				break;

			PV->fn(SOP, REF_DUP, PV);
			PV->fn(SOP, REF_LOAD, PV);
			SOP_emit_opcode(SOP, OP_INC);
			PV->fn(SOP, REF_STORE, PV);
			SOP_emit_opcode(SOP, OP_DEC);
			PV->fn = NULL;
			break;

		case SOP_DEC:
			if (!SOP_check_lvalue(SOP, PV))
				break;

			PV->fn(SOP, REF_DUP, PV);
			PV->fn(SOP, REF_LOAD, PV);
			SOP_emit_opcode(SOP, OP_DEC);
			PV->fn(SOP, REF_STORE, PV);
			SOP_emit_opcode(SOP, OP_INC);
			PV->fn = NULL;
			break;

		case SOP_L_SQUARE:
			SOP_compile_index(SOP, PV);
			break;

		case SOP_L_PAREN:
			SOP_compile_call(SOP, PV);
			break;

		case SOP_PERIOD:
			SOP_rvalue(SOP, PV);

			SOP_expr_primary(SOP, PV);
			PV->source = 1;
			break;
		}
	}
}

/*************************************************************/
//
// primary-expression:
//      ( opt_expression )
//      string string-constant
//      constant
//      var-name
//
/*************************************************************/

void SOP_expr_primary(SOP_class *SOP, PVAL *PV) {
	ULONG val;
	UWORD i, end;
	BYTE *str;

	switch (LEX_type(SOP->LEX, LEX_NXT)) {
	case TTYP_SYMBOL:
		switch (LEX_token(SOP->LEX, LEX_NXT)) {
		case SOP_L_PAREN:
			LEX_fetch(SOP->LEX);

			if (!SOP_next_symbol(SOP, SOP_R_PAREN))
				SOP_expr_list(SOP, PV);
			else {
				LEX_fetch(SOP->LEX);
				SOP_expr_assign(SOP, PV);
				return;
			}

			SOP_require_symbol(SOP, SOP_R_PAREN);
			break;

		default:
			SOP_basic_error(SOP, MSG_EPE);
			PV->fn = NULL;
		}
		break;

	case TTYP_KEYWORD:
		switch (LEX_token(SOP->LEX, LEX_NXT)) {
		case SOP_STRING:
			LEX_fetch(SOP->LEX);

			LEX_fetch(SOP->LEX);
			if (LEX_type(SOP->LEX, LEX_CUR) != TTYP_STRLIT) {
				SOP_basic_error(SOP, MSG_ELS);
				break;
			}

			SOP_emit_opcode(SOP, OP_LECA);
			SOP_emit_word(SOP, SOP->PC + 5);
			SOP_emit_opcode(SOP, OP_BRA);
			end = SOP_emit_word(SOP, 0);

			str = LEX_lexeme(SOP->LEX, LEX_CUR);

			for (i = 0; i < strlen(str); i++)
				SOP_emit_byte(SOP, str[i]);
			SOP_emit_byte(SOP, 0);

			SOP_local_fixup(SOP, end, SOP->PC);
			break;

		default:
			SOP_basic_error(SOP, MSG_EPE);
			PV->fn = NULL;
		}
		break;

	case TTYP_NUM:
		LEX_fetch(SOP->LEX);
		val = LEX_value(SOP->LEX, LEX_CUR);

		SOP_emit_constant(SOP, val);

		PV->fn = NULL;
		break;

	case TTYP_STRLIT:
		LEX_fetch(SOP->LEX);

		if (SOP->ntype == NTYP_MSG) {
			val = RS_get_MSGD_entry(SOP->RS, LEX_lexeme(SOP->LEX, LEX_CUR));
			SOP->ntype = NTYP_RES;
		} else
			val = SOP_resource_name_entry(SOP);

		SOP_emit_constant(SOP, val);

		PV->fn = NULL;
		break;

	case TTYP_NAME:
		LEX_fetch(SOP->LEX);
		SOP_name_reference(SOP, PV, LEX_lexeme(SOP->LEX, LEX_CUR));
		break;

	default:
		SOP_basic_error(SOP, MSG_EPE);
		PV->fn = NULL;
		break;
	}
}

/*************************************************************/
//
// variable-declaration:
//     opt_scope-specifier type-specifier init-declarator-list
//
// scope-specifier:
//     public
//     private
//     extern
//     table
//
// type-specifier:
//     long
//     word
//     byte
//
// init-declarator-list:
//     init-declarator
//     init-declarator-list , init-declarator
//
// init-declarator:
//     declarator
//     declarator = { initializer-list }
//
// initializer-list:
//     integer-constant
//     initializer-list , integer-constant
//
// declarator:
//     identifier opt_dimension-list
//     string-constant identifier opt_dimension-list
//
// dimension-list:
//     dimension-specifier
//     dimension-list dimension-specifier
//
// dimension-specifier:
//     [ integer-constant ]
//
/*************************************************************/

UWORD SOP_var_declaration(SOP_class *SOP, UWORD vsize, DICT_class *scope,
		BYTE vtype, UWORD offset, UWORD visibility) {
	BYTE *name;
	BYTE *class_type;
	DICT_entry *entry;
	CSS_class *CSS;
	UWORD i, ndims, dims[MAX_DIMS];
	ULONG ninit, tsize, dsize, asize, val;

	do {
		class_type = str_alloc("");
		if (LEX_type(SOP->LEX, LEX_NXT) == TTYP_STRLIT) {
			LEX_fetch(SOP->LEX);

			if (scope != SOP->XTRN)
				SOP_basic_error(SOP, MSG_BCN);
			else {
				free(class_type);
				class_type = str(SOP_resource_name_entry(SOP));
			}
		}

		LEX_fetch(SOP->LEX);

		name = str_alloc(LEX_lexeme(SOP->LEX, LEX_CUR));

		if ((scope == SOP->XTRN) && (!strlen(class_type)))
			SOP_basic_error(SOP, MSG_MCS);

		if (LEX_type(SOP->LEX, LEX_CUR) == TTYP_KEYWORD)
			SOP_basic_error(SOP, MSG_RWU);

		else if (LEX_type(SOP->LEX, LEX_CUR) != TTYP_NAME)
			SOP_basic_error(SOP, MSG_SYN);

		else if (DICT_lookup(SOP->RS->dict[CRFD], name) != NULL)
			SOP_basic_error(SOP, MSG_ICU);

		else if (DICT_lookup(scope, name) != NULL)
			SOP_basic_error(SOP, MSG_VIU);

		else {
			entry = DICT_enter(scope, name, D_DEFHEAP);

			ndims = 0;
			asize = 1L;
			while (SOP_next_symbol(SOP, SOP_L_SQUARE)) {
				LEX_fetch(SOP->LEX);

				if (!SOP_next_literal_constant(SOP)) {
					LEX_fetch(SOP->LEX);
					SOP_basic_error(SOP, MSG_ELC);
				} else {
					dsize = SOP_fetch_literal_constant(SOP);
					if (ndims == MAX_DIMS)
						report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_TMD,
								name);
					else {
						dims[ndims++] = (UWORD) dsize;
						asize *= dsize;
					}
				}

				SOP_require_symbol(SOP, SOP_R_SQUARE);
			}

			tsize = (ULONG) vsize * asize;

			if (tsize > 65535L)
				report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_ASX);

			CSS = CSS_construct(NULL);

			if (scope == SOP->AUTO) {
				offset += (UWORD) tsize;
				CSS_add_num(CSS, offset);
			} else if (scope == SOP->ARGV) {
				CSS_add_num(CSS, offset);
				offset -= (UWORD) tsize;
			} else if (scope == SOP->XTRN) {
				offset = SOP_import_symbol(SOP, name, class_type, vtype);

				CSS_add_num(CSS, offset);
			} else {
				if (visibility == V_PUBLIC)
					SOP_export_symbol(SOP, (ULONG) atoi(name), (ULONG) offset, vtype, asize);

				CSS_add_num(CSS, offset);
				offset += (UWORD) tsize;
			}

			CSS_add_num(CSS, ndims);

			if (ndims > 1)
				for (i = 0; i < (ndims - 1); i++) {
					tsize /= (ULONG) dims[i];

					CSS_add_num(CSS, tsize);
				}

			CSS_add_num(CSS, vsize);

			entry->def = CSS_string(CSS);

			CSS_destroy(CSS);
		}

		ninit = 0;

		if (SOP_next_symbol(SOP, SOP_SETEQ)) {
			LEX_fetch(SOP->LEX);

			if (scope != SOP->TABL)
				SOP_basic_error(SOP, MSG_CIA);

			SOP_require_symbol(SOP, SOP_L_CURLY);

			do {
				if (SOP_next_symbol(SOP, SOP_R_CURLY))
					break;

				if (!SOP_next_literal_constant(SOP)) {
					LEX_fetch(SOP->LEX);
					SOP_basic_error(SOP, MSG_ELC);
				} else {
					++ninit;

					val = SOP_fetch_literal_constant(SOP);

					switch (vsize) {
					case SIZE_B:
						SOP_emit_byte(SOP, val);
						break;
					case SIZE_W:
						SOP_emit_word(SOP, val);
						break;
					case SIZE_L:
						SOP_emit_long(SOP, val);
						break;
					}
				}
			} while (LEX_next_comma(SOP->LEX));

			SOP_require_symbol(SOP, SOP_R_CURLY);
		}

		if (scope == SOP->TABL)
			if (SOP->context != CON_D)
				SOP_basic_error(SOP, MSG_BPT);
			else if (!ndims)
				SOP_basic_error(SOP, MSG_CIA);
			else if (offset != SOP->PC)
				report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_TIN, name,
						asize, ninit);

		if (scope == SOP->AUTO)
			DICT_enter(SOP->ADUS, name, D_DEFHEAP)->def = str_alloc(
					LEX_line(SOP->LEX, LEX_CUR));
		else if ((scope != SOP->ARGV) && (scope != SOP->XTRN)
				&& (visibility != V_PUBLIC))
			DICT_enter(SOP->DUSE, name, D_DEFHEAP)->def = str_alloc(
					LEX_line(SOP->LEX, LEX_CUR));

		free(name);
		free(class_type);
	} while (LEX_next_comma(SOP->LEX));

	return offset;
}

/*************************************************************/
//
// member string-constant ;
//
/*************************************************************/

void SOP_member_statement(SOP_class *SOP) {
	LEX_fetch(SOP->LEX);

	if (LEX_type(SOP->LEX, LEX_CUR) != TTYP_STRLIT)
		SOP_basic_error(SOP, MSG_ELS);
	else {
		SOP->parent = SOP_resource_name_entry(SOP);

		if (!SOP_export_symbol(SOP, atoi("PARENT"), (ULONG) SOP->parent,
				(BYTE) 'N', 1))
			SOP_basic_error(SOP, MSG_NMI);
	}

	SOP_require_symbol(SOP, SOP_SEMICOLON);
}

/*************************************************************/
//
// Add a level to the "break" target stack
//
/*************************************************************/

UWORD *SOP_add_break(SOP_class *SOP, UWORD label) {
	UWORD *old;

	old = SOP->bsp;

	SOP->bsp = (UWORD*) add_ptr(SOP->bsp, (ULONG) sizeof(UWORD));

	if (ptr_dif((ULONG*) SOP->bsp, (ULONG*) &SOP->b_stack[MAX_NEST]) < 0L)
		*SOP->bsp = label;
	else
		report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_TML);

	return old;
}

/*************************************************************/
//
// Add a level to the "continue" target stack
//
/*************************************************************/

UWORD *SOP_add_continue(SOP_class *SOP, UWORD label) {
	UWORD *old;

	old = SOP->csp;

	SOP->csp = (UWORD*) add_ptr(SOP->csp, (ULONG) sizeof(UWORD));

	if (ptr_dif((ULONG*) SOP->csp, (ULONG*) &SOP->c_stack[MAX_NEST]) < 0L)
		*SOP->csp = label;
	else
		report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_TML);

	return old;
}

/*************************************************************/
//
// Remove a level from the "break" target stack
//
/*************************************************************/

UWORD SOP_remove_break(SOP_class *SOP, UWORD *old, UWORD label) {
	if (ptr_dif((ULONG*) SOP->bsp, (ULONG*) old) > 0L) {
		label = *SOP->bsp;

		SOP->bsp = (UWORD*) add_ptr(SOP->bsp, -(ULONG) sizeof(UWORD));
	}

	return label;
}

/*************************************************************/
//
// Remove a level from the "continue" target stack
//
/*************************************************************/

void SOP_remove_continue(SOP_class *SOP, UWORD *old) {
	SOP->csp = old;
}

/*************************************************************/
//
// continue opt_( integer-constant ) ;
//
/*************************************************************/

void SOP_continue_statement(SOP_class *SOP) {
	LONG lvl;
	LONG rq_lvl;
	UWORD *ptr;

	lvl = ptr_dif((ULONG*) SOP->csp, (ULONG*) SOP->c_stack);

	rq_lvl = 0L * (ULONG) sizeof(UWORD);

	if (SOP_next_symbol(SOP, SOP_L_PAREN)) {
		LEX_fetch(SOP->LEX);

		if (SOP_next_literal_constant(SOP))
			rq_lvl = (SOP_fetch_literal_constant(SOP) - 1L)
					* (ULONG) sizeof(UWORD);
		else {
			LEX_fetch(SOP->LEX);
			SOP_basic_error(SOP, MSG_ELC);
		}

		SOP_require_symbol(SOP, SOP_R_PAREN);
	}

	if ((LONG) rq_lvl >= 0L)
		if (lvl >= rq_lvl) {
			ptr = (UWORD*) add_ptr(SOP->csp, -rq_lvl);

			SOP_emit_opcode(SOP, OP_BRA);
			SOP_emit_word(SOP, *ptr);
		} else
			SOP_basic_error(SOP, MSG_GBC);

	SOP_require_symbol(SOP, SOP_SEMICOLON);
}

/*************************************************************/
//
// break opt_( integer-constant ) ;
//
/*************************************************************/

void SOP_break_statement(SOP_class *SOP) {
	LONG lvl;
	LONG rq_lvl;
	UWORD *ptr;

	lvl = ptr_dif((ULONG*) SOP->bsp, (ULONG*) SOP->b_stack);

	rq_lvl = 0L * (ULONG) sizeof(UWORD);

	if (SOP_next_symbol(SOP, SOP_L_PAREN)) {
		LEX_fetch(SOP->LEX);

		if (SOP_next_literal_constant(SOP))
			rq_lvl = (SOP_fetch_literal_constant(SOP) - 1L)
					* (ULONG) sizeof(UWORD);
		else {
			LEX_fetch(SOP->LEX);
			SOP_basic_error(SOP, MSG_ELC);
		}

		SOP_require_symbol(SOP, SOP_R_PAREN);
	}

	if ((LONG) rq_lvl >= 0L)
		if (lvl >= rq_lvl) {
			ptr = (UWORD*) add_ptr(SOP->bsp, -rq_lvl);

			SOP_emit_opcode(SOP, OP_BRA);
			*ptr = SOP_emit_word(SOP, *ptr);
		} else
			SOP_basic_error(SOP, MSG_GBC);

	SOP_require_symbol(SOP, SOP_SEMICOLON);
}

/*************************************************************/
//
// for ( opt_expression ; opt_expression ; opt_expression ) statement
//
/*************************************************************/

void SOP_for_statement(SOP_class *SOP) {
	UWORD next, end, update, body;
	UWORD *brk, *con;

	SOP_require_symbol(SOP, SOP_L_PAREN);

	if (!SOP_next_symbol(SOP, SOP_SEMICOLON))
		SOP_expression(SOP);

	SOP_require_symbol(SOP, SOP_SEMICOLON);

	next = SOP->PC;

	if (!SOP_next_symbol(SOP, SOP_SEMICOLON))
		SOP_expression(SOP);
	else {
		SOP_emit_opcode(SOP, OP_SHTC);
		SOP_emit_byte(SOP, 1);
	}

	SOP_require_symbol(SOP, SOP_SEMICOLON);

	SOP_emit_opcode(SOP, OP_BRT);
	body = SOP_emit_word(SOP, 0);

	SOP_emit_opcode(SOP, OP_BRA);
	end = SOP_emit_word(SOP, 0);

	update = SOP->PC;

	if (!SOP_next_symbol(SOP, SOP_R_PAREN))
		SOP_expression(SOP);

	SOP_require_symbol(SOP, SOP_R_PAREN);

	SOP_emit_opcode(SOP, OP_BRA);
	SOP_emit_word(SOP, next);

	SOP_local_fixup(SOP, body, SOP->PC);

	brk = SOP_add_break(SOP, end);
	con = SOP_add_continue(SOP, update);
	SOP_statement(SOP);
	end = SOP_remove_break(SOP, brk, end);
	SOP_remove_continue(SOP, con);

	SOP_emit_opcode(SOP, OP_BRA);
	SOP_emit_word(SOP, update);

	SOP_local_fixup(SOP, end, SOP->PC);
}

/*************************************************************/
//
// while ( expression ) statement
//
/*************************************************************/

void SOP_while_statement(SOP_class *SOP) {
	UWORD next, end;
	UWORD *brk, *con;

	next = SOP->PC;

	SOP_require_symbol(SOP, SOP_L_PAREN);
	SOP_expression(SOP);
	SOP_require_symbol(SOP, SOP_R_PAREN);

	SOP_emit_opcode(SOP, OP_BRF);
	end = SOP_emit_word(SOP, 0);

	brk = SOP_add_break(SOP, end);
	con = SOP_add_continue(SOP, next);
	SOP_statement(SOP);
	end = SOP_remove_break(SOP, brk, end);
	SOP_remove_continue(SOP, con);

	SOP_emit_opcode(SOP, OP_BRA);
	SOP_emit_word(SOP, next);

	SOP_local_fixup(SOP, end, SOP->PC);
}

/*************************************************************/
//
// do statement while ( expression ) ;
//
/*************************************************************/

void SOP_do_while_statement(SOP_class *SOP) {
	UWORD next, end;
	UWORD *brk, *con;

	next = SOP->PC;

	brk = SOP_add_break(SOP, 0);
	con = SOP_add_continue(SOP, next);
	SOP_statement(SOP);
	end = SOP_remove_break(SOP, brk, 0);
	SOP_remove_continue(SOP, con);

	SOP_require_keyword(SOP, SOP_WHILE);

	SOP_require_symbol(SOP, SOP_L_PAREN);
	SOP_expression(SOP);
	SOP_require_symbol(SOP, SOP_R_PAREN);

	SOP_require_symbol(SOP, SOP_SEMICOLON);

	SOP_emit_opcode(SOP, OP_BRT);
	SOP_emit_word(SOP, next);

	SOP_local_fixup(SOP, end, SOP->PC);
}

/*************************************************************/
//
// loop statement
//
/*************************************************************/

void SOP_loop_statement(SOP_class *SOP) {
	UWORD next, end;
	UWORD *brk, *con;

	next = SOP->PC;

	brk = SOP_add_break(SOP, 0);
	con = SOP_add_continue(SOP, next);
	SOP_statement(SOP);
	end = SOP_remove_break(SOP, brk, 0);
	SOP_remove_continue(SOP, con);

	SOP_emit_opcode(SOP, OP_BRA);
	SOP_emit_word(SOP, next);

	SOP_local_fixup(SOP, end, SOP->PC);
}

/*************************************************************/
//
// if ( expression ) statement
// if ( expression ) statement else statement
//
/*************************************************************/

void SOP_if_statement(SOP_class *SOP) {
	UWORD next, end;

	SOP_require_symbol(SOP, SOP_L_PAREN);
	SOP_expression(SOP);
	SOP_require_symbol(SOP, SOP_R_PAREN);

	SOP_emit_opcode(SOP, OP_BRF);
	next = SOP_emit_word(SOP, 0);

	SOP_statement(SOP);

	if (!SOP_next_keyword(SOP, SOP_ELSE))
		SOP_local_fixup(SOP, next, SOP->PC);
	else {
		LEX_fetch(SOP->LEX);

		SOP_emit_opcode(SOP, OP_BRA);
		end = SOP_emit_word(SOP, 0);

		SOP_local_fixup(SOP, next, SOP->PC);

		SOP_statement(SOP);

		SOP_local_fixup(SOP, end, SOP->PC);
	}
}

/*************************************************************/
//
// switch ( expression ) statement
//
/*************************************************************/

void SOP_switch_statement(SOP_class *SOP) {
	UWORD test, end, ncases, case_cnt;
	UWORD *brk;
	UWORD def;
	DICT_class *old;
	DICT_entry *entry;
	DI_class *DI;

	SOP_require_symbol(SOP, SOP_L_PAREN);
	SOP_expression(SOP);
	SOP_require_symbol(SOP, SOP_R_PAREN);

	SOP_emit_opcode(SOP, OP_BRA);
	test = SOP_emit_word(SOP, 0);

	old = SOP->CASE;
	SOP->CASE = DICT_construct(DC_LINEAR);

	brk = SOP_add_break(SOP, 0);
	SOP_statement(SOP);
	end = SOP_remove_break(SOP, brk, 0);

	SOP_emit_opcode(SOP, OP_BRA);
	end = SOP_emit_word(SOP, end);

	SOP_local_fixup(SOP, test, SOP->PC);

	SOP_emit_opcode(SOP, OP_CASE);
	ncases = SOP_emit_word(SOP, 0);

	case_cnt = 0;
	def = 0;

	DI = DI_construct(SOP->CASE);
	while ((entry = DI_fetch(DI)) != NULL)
		if (!strcmp(entry->tag, KW_DEFAULT))
			def = (UWORD) ascnum((BYTE*) entry->def, 10);
		else {
			SOP_emit_long(SOP, ascnum((BYTE*) entry->tag, 10));
			SOP_emit_word(SOP, (UWORD) ascnum((BYTE*) entry->def, 10));
			++case_cnt;
		}
	DI_destroy(DI);

	if (def)
		SOP_emit_word(SOP, def);
	else
		end = SOP_emit_word(SOP, end);

	SOP_local_fixup(SOP, ncases, case_cnt);

	SOP_local_fixup(SOP, end, SOP->PC);

	DICT_destroy(SOP->CASE);
	SOP->CASE = old;
}

/*************************************************************/
//
// case constant-list : statement
//
/*************************************************************/

void SOP_case_statement(SOP_class *SOP) {
	BYTE *case_val;

	if (SOP->CASE == NULL)
		SOP_basic_error(SOP, MSG_OSW);

	do
		if (!SOP_next_literal_constant(SOP)) {
			LEX_fetch(SOP->LEX);
			SOP_basic_error(SOP, MSG_ELC);
		} else {
			case_val = str(SOP_fetch_literal_constant(SOP));

			if (SOP->CASE != NULL)
				if (DICT_lookup(SOP->CASE, case_val))
					SOP_basic_error(SOP, MSG_RDC);
				else
					DICT_enter(SOP->CASE, case_val, D_DEFHEAP)->def = str(
							SOP->PC);

			free(case_val);
		} while (LEX_next_comma(SOP->LEX));

	SOP_require_symbol(SOP, SOP_COLON);
}

/*************************************************************/
//
// default : statement
//
/*************************************************************/

void SOP_default_statement(SOP_class *SOP) {
	if (SOP->CASE == NULL)
		SOP_basic_error(SOP, MSG_OSW);
	else if (DICT_lookup(SOP->CASE, KW_DEFAULT))
		SOP_basic_error(SOP, MSG_RDC);
	else
		DICT_enter(SOP->CASE, KW_DEFAULT, D_DEFHEAP)->def = str(SOP->PC);

	SOP_require_symbol(SOP, SOP_COLON);
}

/*************************************************************/
//
// message string-constant compound-statement
// message string-constant , identifier-list compound-statement
//
/*************************************************************/

void SOP_message_statement(SOP_class *SOP) {
	UWORD msgnum;
	UWORD old_context;
	MHDR *header;

	old_context = SOP->context;
	SOP->context = CON_H;

	LEX_fetch(SOP->LEX);

	if (LEX_type(SOP->LEX, LEX_CUR) != TTYP_STRLIT) {
		msgnum = -1U;
		SOP_basic_error(SOP, MSG_BMN);
	} else
		msgnum = RS_get_MSGD_entry(SOP->RS, LEX_lexeme(SOP->LEX, LEX_CUR));

	if (!SOP_export_symbol(SOP, msgnum, SOP->PC, 'M', 1))
		SOP_basic_error(SOP, MSG_RMN);

	DICT_wipe(SOP->ARGV);
	if (LEX_next_comma(SOP->LEX))
		SOP_var_declaration(SOP, SIZE_L, SOP->ARGV, 'L', 0, V_PRIVATE);

	DICT_wipe(SOP->ADUS);
	DICT_wipe(SOP->AVUS);
	DICT_wipe(SOP->APUS);
	DICT_wipe(SOP->AUTO);
	DICT_enter(SOP->AUTO, KW_THIS, 0)->def = (BYTE*) "2,0,2";
	SOP->auto_index = 2;

	header = (MHDR *) (((UBYTE *) SOP->CODE) + SOP->PC);
	SOP->PC += sizeof(MHDR);

	SOP_require_symbol(SOP, SOP_L_CURLY);
	SOP_compound_statement(SOP);
	SOP_emit_opcode(SOP, OP_END);

	SOP_show_usage(E_NOTICE, SOP->ADUS, MSG_DNU);
	SOP_show_usage(E_NOTICE, SOP->AVUS, MSG_VNU);
	SOP_show_usage(E_WARN, SOP->APUS, MSG_PPU);

	header->auto_size = SOP->auto_index;

	SOP->context = old_context;
}

/*************************************************************/
//
// procedure identifier compound-statement
//
/*************************************************************/

void SOP_procedure_statement(SOP_class *SOP) {
	BYTE *name;
	UWORD old_context;
	MHDR *header;

	old_context = SOP->context;
	SOP->context = CON_P;

	LEX_fetch(SOP->LEX);

	name = LEX_lexeme(SOP->LEX, LEX_CUR);

	if (LEX_type(SOP->LEX, LEX_CUR) != TTYP_NAME)
		SOP_basic_error(SOP, MSG_BPN);
	else if (DICT_lookup(SOP->PROC, name) != NULL)
		SOP_basic_error(SOP, MSG_RPN);
	else
		DICT_enter(SOP->PROC, name, D_DEFHEAP)->def = str(SOP->PC);

	DICT_wipe(SOP->ADUS);
	DICT_wipe(SOP->AVUS);
	DICT_wipe(SOP->APUS);
	DICT_wipe(SOP->AUTO);
	DICT_enter(SOP->AUTO, KW_THIS, 0)->def = (BYTE*) "2,0,2";
	SOP->auto_index = 2;

	header = (MHDR *) (((UBYTE *) SOP->CODE) + SOP->PC);
	SOP->PC += sizeof(MHDR);

	SOP_require_symbol(SOP, SOP_L_CURLY);
	SOP_compound_statement(SOP);
	SOP_emit_opcode(SOP, OP_RTS);

	SOP_show_usage(E_NOTICE, SOP->ADUS, MSG_DNU);
	SOP_show_usage(E_NOTICE, SOP->AVUS, MSG_VNU);
	SOP_show_usage(E_WARN, SOP->APUS, MSG_PPU);

	header->auto_size = SOP->auto_index;

	SOP->context = old_context;
}

/*************************************************************/
//
// return opt_expression ;
//
/*************************************************************/

void SOP_return_statement(SOP_class *SOP) {
	if (!SOP_next_symbol(SOP, SOP_SEMICOLON))
		SOP_expression(SOP);

	if (SOP->context != CON_P)
		SOP_emit_opcode(SOP, OP_END);
	else
		SOP_emit_opcode(SOP, OP_RTS);

	SOP_require_symbol(SOP, SOP_SEMICOLON);
}

/*************************************************************/
//
// trigger ;
//
/*************************************************************/

void SOP_trigger_statement(SOP_class *SOP) {
	SOP_emit_opcode(SOP, OP_BRK);

	SOP_require_symbol(SOP, SOP_SEMICOLON);
#if 0
	asm {int 3;}
#endif
}

/*************************************************************/
//
// statement:
//      labeled-statement
//      expression-statement
//      compound-statement
//      selection-statement
//      iteration-statement
//      branch-statement
//      definition-statement
//      declaration-statement
//
/*************************************************************/

void SOP_statement(SOP_class *SOP) {
	switch (LEX_type(SOP->LEX, LEX_NXT)) {
	case TTYP_KEYWORD:
		LEX_fetch(SOP->LEX);

		switch (LEX_token(SOP->LEX, LEX_CUR)) {
		case SOP_LONG:
			SOP_check_auto_nest(SOP);

			SOP->auto_index = SOP_var_declaration(SOP, SIZE_L, SOP->AUTO, 'L',
					SOP->auto_index, V_PRIVATE);

			SOP_require_symbol(SOP, SOP_SEMICOLON);
			break;

		case SOP_WORD:
			SOP_check_auto_nest(SOP);

			SOP->auto_index = SOP_var_declaration(SOP, SIZE_W, SOP->AUTO, 'W',
					SOP->auto_index, V_PRIVATE);

			SOP_require_symbol(SOP, SOP_SEMICOLON);
			break;

		case SOP_BYTE:
			SOP_check_auto_nest(SOP);

			SOP->auto_index = SOP_var_declaration(SOP, SIZE_B, SOP->AUTO, 'B',
					SOP->auto_index, V_PRIVATE);

			SOP_require_symbol(SOP, SOP_SEMICOLON);
			break;

		case SOP_PRIVATE:
			LEX_fetch(SOP->LEX);
			switch (LEX_token(SOP->LEX, LEX_CUR)) {
			case SOP_LONG:
				SOP->static_index = SOP_var_declaration(SOP, SIZE_L, SOP->STAT,
						'L', SOP->static_index, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			case SOP_WORD:
				SOP->static_index = SOP_var_declaration(SOP, SIZE_W, SOP->STAT,
						'W', SOP->static_index, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			case SOP_BYTE:
				SOP->static_index = SOP_var_declaration(SOP, SIZE_B, SOP->STAT,
						'B', SOP->static_index, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			default:
				SOP_basic_error(SOP, MSG_SYN);
			}
			break;

		case SOP_PUBLIC:
			LEX_fetch(SOP->LEX);
			switch (LEX_token(SOP->LEX, LEX_CUR)) {
			case SOP_LONG:
				SOP->static_index = SOP_var_declaration(SOP, SIZE_L, SOP->STAT,
						'L', SOP->static_index, V_PUBLIC);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			case SOP_WORD:
				SOP->static_index = SOP_var_declaration(SOP, SIZE_W, SOP->STAT,
						'W', SOP->static_index, V_PUBLIC);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			case SOP_BYTE:
				SOP->static_index = SOP_var_declaration(SOP, SIZE_B, SOP->STAT,
						'B', SOP->static_index, V_PUBLIC);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			default:
				SOP_basic_error(SOP, MSG_SYN);
			}
			break;

		case SOP_EXTERN:
			LEX_fetch(SOP->LEX);
			switch (LEX_token(SOP->LEX, LEX_CUR)) {
			case SOP_LONG:
				SOP_var_declaration(SOP, SIZE_L, SOP->XTRN, 'L', 0, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			case SOP_WORD:
				SOP_var_declaration(SOP, SIZE_W, SOP->XTRN, 'W', 0, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			case SOP_BYTE:
				SOP_var_declaration(SOP, SIZE_B, SOP->XTRN, 'B', 0, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			default:
				SOP_basic_error(SOP, MSG_SYN);
			}
			break;

		case SOP_TABLE:
			LEX_fetch(SOP->LEX);
			switch (LEX_token(SOP->LEX, LEX_CUR)) {
			case SOP_LONG:
				SOP->PC = SOP_var_declaration(SOP, SIZE_L, SOP->TABL, 'L',
						SOP->PC, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			case SOP_WORD:
				SOP->PC = SOP_var_declaration(SOP, SIZE_W, SOP->TABL, 'W',
						SOP->PC, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			case SOP_BYTE:
				SOP->PC = SOP_var_declaration(SOP, SIZE_B, SOP->TABL, 'B',
						SOP->PC, V_PRIVATE);

				SOP_require_symbol(SOP, SOP_SEMICOLON);
				break;

			default:
				SOP_basic_error(SOP, MSG_SYN);
			}
			break;

		case SOP_MESSAGE:
			SOP_check_nest(SOP);
			SOP_message_statement(SOP);
			break;

		case SOP_PROCEDURE:
			SOP_check_nest(SOP);
			SOP_procedure_statement(SOP);
			break;

		case SOP_MEMBER:
			SOP_member_statement(SOP);
			break;

		default:
			SOP_check_access(SOP);

			switch (LEX_token(SOP->LEX, LEX_CUR)) {
			case SOP_CONTINUE:
				SOP_continue_statement(SOP);
				break;

			case SOP_BREAK:
				SOP_break_statement(SOP);
				break;

			case SOP_FOR:
				SOP_for_statement(SOP);
				break;

			case SOP_WHILE:
				SOP_while_statement(SOP);
				break;

			case SOP_DO:
				SOP_do_while_statement(SOP);
				break;

			case SOP_LOOP:
				SOP_loop_statement(SOP);
				break;

			case SOP_IF:
				SOP_if_statement(SOP);
				break;

			case SOP_SWITCH:
				SOP_switch_statement(SOP);
				break;

			case SOP_CASE:
				SOP_case_statement(SOP);
				break;

			case SOP_DEFAULT:
				SOP_default_statement(SOP);
				break;

			case SOP_RETURN:
				SOP_return_statement(SOP);
				break;

			case SOP_TRIGGER:
				SOP_trigger_statement(SOP);
				break;
			}
			break;
		}
		break;

	case TTYP_SYMBOL:
		SOP_check_access(SOP);

		switch (LEX_token(SOP->LEX, LEX_NXT)) {
		case SOP_L_CURLY:
			LEX_fetch(SOP->LEX);
			SOP_compound_statement(SOP);
			break;

		case SOP_SEMICOLON:
			LEX_fetch(SOP->LEX);
			break;

		default:
			SOP_expression(SOP);
			SOP_require_symbol(SOP, SOP_SEMICOLON);
			break;
		}
		break;

	default:
		SOP_check_access(SOP);

		SOP_expression(SOP);
		SOP_require_symbol(SOP, SOP_SEMICOLON);
	}
}

/*************************************************************/
//
// compound-statement:
//     { opt_statement-list }
//
/*************************************************************/

void SOP_compound_statement(SOP_class *SOP) {
	while (!SOP_next_symbol(SOP, SOP_R_CURLY)) {
		if (LEX_type(SOP->LEX, LEX_NXT) == TTYP_EOF) {
			if (!SOP->EOF_reached) {
				report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_UEO);
				SOP->EOF_reached = 1;
			}
			return;
		}

		SOP_statement(SOP);
	}

	LEX_fetch(SOP->LEX);
}

/*************************************************************/
//
// Write compiled SOP code to resource file
//
/*************************************************************/

void SOP_write_code(SOP_class *SOP, void *ptr, ULONG len) {
	RF_entry_hdr RHDR;
	ULONG ord;

	ord = RS_get_ROED_entry(SOP->RS, SOP->name);

	RHDR.data_attrib = SOP->attrib;
	RHDR.data_size = len;

	RF_write_entry(SOP->RS->RF, ord, ptr, &RHDR, RTYP_RAW_MEM);
	report(E_RESCOMP, NULL, NULL);
}

/*************************************************************/
//
// Write import or export dictionary to resource file, returning
// ordinal entry # of dictionary
//
/*************************************************************/

ULONG SOP_write_dict(SOP_class *SOP, BYTE *suffix, DICT_class *DICT) {
	BYTE *name;
	ULONG ord;

	name = (BYTE*) mem_alloc(strlen(SOP->name) + strlen(suffix) + 1L);
	strcpy(name, SOP->name);
	strcat(name, suffix);

	ord = RS_get_ROED_entry(SOP->RS, name);

	DICT_save(DICT, SOP->RS->RF, ord);
	report(E_RESCOMP, NULL, NULL);

	free(name);

	return ord;
}

/*************************************************************/
//
// Return TRUE if SOP program's resource attributes have changed since 
// last compilation
//
/*************************************************************/

WORD SOP_new_source_attrib(SOP_class *SOP) {
	WORD new_source;
	DICT_entry *cur;

	new_source = 0;
	cur = DICT_lookup(SOP->RS->dict[RDES], SOP->fn);

	if (cur == NULL)
		new_source = 1;
	else if (ascnum((BYTE*) cur->def, 10) != SOP->attrib) {
		DICT_delete(SOP->RS->dict[RDES], SOP->fn);
		new_source = 1;
	}

	if (new_source)
		DICT_enter(SOP->RS->dict[RDES], SOP->fn, D_DEFHEAP)->def = str(
				SOP->attrib);

	return new_source;
}

/*************************************************************/
//
// Create a SOP compiler class instance (special case of IDR class in 
// RSCOMP.C)
//
/*************************************************************/

SOP_class *SOP_construct(RS_class *RS, ULONG def_attribs) {
	SOP_class *SOP;
	WORD bad;

	SOP = (SOP_class*) mem_alloc(sizeof(SOP_class));

	SOP->RS = RS;

	LEX_fetch(RS->LEX);

	bad = 0;
	switch (LEX_type(RS->LEX, LEX_CUR)) {
	default:
		RS_syntax_error(RS);
		bad = 1;
		break;

	case TTYP_KEYWORD:
		RS_reserved_word_error(RS);
		bad = 1;
		break;

	case TTYP_STRLIT:
		SOP->fn = str_alloc(LEX_lexeme(RS->LEX, LEX_CUR));

		if (RS_next_attribute_specifier(RS))
			SOP->attrib = RS_parse_attribute_list(RS);
		else
			SOP->attrib = def_attribs;
		break;
	}

	if (bad) {
		free(SOP);
		return NULL;
	}

	return SOP;
}

/*************************************************************/
//
// Destroy a SOP compiler class instance
//
/*************************************************************/

void SOP_destroy(SOP_class *SOP) {
	free(SOP->fn);

	free(SOP);
}

/*************************************************************/
//
// Return 0 if compiled SOP program image in resource file is up to date
//
// A compiled SOP program is obsolete and must be recompiled if:
//
// 1) Its source file dependencies have not yet been entered in RDEP; or
//
// 2) Any file upon which the resource depends is missing or 
//    named "$obsolete"; or
//
// 3) Any file upon which the resource depends has been modified since the
//    resource file was last modified; or
//
// 4) The resource's data attributes have changed
//
/*************************************************************/

WORD SOP_test(SOP_class *SOP) {
	WORD new_source;
	ULONG ftime;
	DICT_entry *cur;

	new_source = SOP_new_source_attrib(SOP);

	cur = DICT_lookup(SOP->RS->dict[RDEP], SOP->fn);

	if ((cur == NULL) || (cur->def == NULL))
		return 1;

	ftime = TS_latest_file_time(SOP->RS->TS, (BYTE*) cur->def);

	if (!ftime)
		return 1;

	if (SOP->RS->RES_time < ftime)
		return 1;

	return new_source;
}

/*************************************************************/
//
// Compile one or more SOP programs, via the following steps:
//    
//  1) Verify that the source file exists
//    
//  2) Preprocess the SOP source file and any of its #include files,
//     generating a single intermediate file (i-file)
//    
//  3) Lexically analyze the i-file, partitioning it logically into
//     individual SOP class and object programs
//    
//  4) Begin compiling the SOP by initializing its CODE, IMPT, and EXPT
//     data spaces and other working data structures
//    
//  5) Fetch the name and type of the SOP program
//    
//  6) Call the compound statement handler to compile the program
//    
//  7) Update the SOP program header (variable space needed, etc.)
//
//  8) If no errors occurred while compiling the program, write its CODE,
//     IMPT, and EXPT blocks to the resource file; otherwise, leave the
//     last-compiled version of the program intact
//  
//  9) Clean up various data structures used during compilation and branch to 
//     step 4) above if another SOP program definition follows
//
// 10) If any errors occurred during compilation, add the "$obsolete" meta-
//     filename to the source file's dependency list
//
// 11) Clean up, delete the i-file, and return
//
/*************************************************************/

void SOP_compile(SOP_class *SOP) {
	PP_class *PP;
	DICT_class *depend;
	ULONG impt, expt, init_err;

	init_err = error_message_count();

	SOP_new_source_attrib(SOP);

	if (!verify_file(SOP->fn)) {
		report(E_ERROR, LEX_line(SOP->RS->LEX, LEX_CUR), MSG_SNF, SOP->fn,
				NULL);
		return;
	}

	SOP->tfile = temp_filename(getenv(TEMP_NAME));
	PP = PP_construct(SOP->fn, SOP->tfile, SOP->RS->predef, 0);
	PP_process(PP);
	PP_destroy(PP);

	depend = DICT_construct(4);

	SOP->LEX = LEX_construct(LEX_LININFO, SOP->tfile, SOP_keywords, SOP_symbols,
			depend);

	SOP->EOF_reached = 0;

	while (SOP_next_definition(SOP)) {
		SOP->parent = -1L;
		SOP->import_index = 0;
		SOP->static_index = 0;

		SOP->CODE = mem_alloc(MAX_CODE);
		SOP->PC = sizeof(PRG_HDR);

		SOP->IMPT = DICT_construct(DC_LINEAR);
		SOP->EXPT = DICT_construct(DC_LINEAR);
		SOP->AUTO = DICT_construct(16);
		SOP->ARGV = DICT_construct(16);
		SOP->STAT = DICT_construct(16);
		SOP->PROC = DICT_construct(16);
		SOP->XTRN = DICT_construct(16);
		SOP->TABL = DICT_construct(16);
		SOP->DUSE = DICT_construct(64);
		SOP->VUSE = DICT_construct(64);
		SOP->ADUS = DICT_construct(64);
		SOP->AVUS = DICT_construct(64);
		SOP->APUS = DICT_construct(64);

		SOP->bsp = (UWORD*) add_ptr(SOP->b_stack, -(ULONG) sizeof(UWORD));
		SOP->csp = (UWORD*) add_ptr(SOP->c_stack, -(ULONG) sizeof(UWORD));

		SOP->CASE = NULL;

		SOP->context = CON_D;
		SOP->ntype = NTYP_RES;

		LEX_fetch(SOP->LEX);

		LEX_fetch(SOP->LEX);
		if (LEX_type(SOP->LEX, LEX_CUR) != TTYP_STRLIT)
			report(E_ERROR, LEX_line(SOP->LEX, LEX_CUR), MSG_NML);
		SOP->name = str_alloc(LEX_lexeme(SOP->LEX, LEX_CUR));
		RS_check_name(SOP->RS, SOP->name);

		DICT_enter(SOP->EXPT, "N:OBJECT", 0)->def = SOP->name;

		SOP_require_symbol(SOP, SOP_L_CURLY);
		SOP_compound_statement(SOP);

		SOP_show_usage(E_NOTICE, SOP->DUSE, MSG_DNU);
		SOP_show_usage(E_NOTICE, SOP->VUSE, MSG_VNU);

		if (error_message_count() == init_err) {
			impt = SOP_write_dict(SOP, ".IMPT", SOP->IMPT);
			expt = SOP_write_dict(SOP, ".EXPT", SOP->EXPT);

			((PRG_HDR *) SOP->CODE)->static_size = SOP->static_index;
			((PRG_HDR *) SOP->CODE)->imports = impt;
			((PRG_HDR *) SOP->CODE)->exports = expt;
			((PRG_HDR *) SOP->CODE)->parent = SOP->parent;

			SOP_write_code(SOP, SOP->CODE, SOP->PC);
		}

		free(SOP->name);

		DICT_destroy(SOP->APUS);
		DICT_destroy(SOP->AVUS);
		DICT_destroy(SOP->ADUS);
		DICT_destroy(SOP->VUSE);
		DICT_destroy(SOP->DUSE);
		DICT_destroy(SOP->TABL);
		DICT_destroy(SOP->XTRN);
		DICT_destroy(SOP->PROC);
		DICT_destroy(SOP->STAT);
		DICT_destroy(SOP->ARGV);
		DICT_destroy(SOP->AUTO);
		DICT_destroy(SOP->EXPT);
		DICT_destroy(SOP->IMPT);

		free(SOP->CODE);
	}

	if (LEX_type(SOP->LEX, LEX_NXT) != TTYP_EOF)
		report(E_ERROR, LEX_line(SOP->LEX, LEX_NXT), MSG_EDS);

	if (error_message_count() != init_err)
		DICT_enter(depend, "$obsolete", 0);

	RS_update_RDEP(SOP->RS, SOP->fn, depend);

	LEX_destroy(SOP->LEX);
	DICT_destroy(depend);
	remove_tempfile(SOP->tfile);
}
