//����������������������������������������������������������������������������
//��                                                                        ��
//��  LEXAN.C                                                               ��
//��                                                                        ��
//��  Lexical analyzer/tokenizer class for AESOP ARC compiler               ��
//��                                                                        ��
//��  Version: 1.00 of 10-Mar-92 -- Initial version                         ��
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "defs.hpp"
#include "system.hpp"
#include "dict.hpp"
#include "lexan.hpp"
#include "arcmsg.hpp"

#define MAX_IN_LEN 4096                // max. i-file line length
#define MAX_LININFO 512                // max. pathname+line # specifier len
#define EOF_CHAR 0x1a                  // ^Z = end-of-file marker
const BYTE hex_val[256] =              // values+1 of ASCII hex digits
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0, 0, 0,
				11, 12, 13, 14, 15, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 12, 13, 14, 15, 16,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0 };

/***************************************************/
//
// Translate C-style escape codes
//
/***************************************************/

static BYTE esc_xlat(BYTE ch) {
	switch (ch) {
	case 'a':
		return 7;
	case 'b':
		return 8;
	case 't':
		return 9;
	case 'n':
		return 10;
	case 'v':
		return 11;
	case 'f':
		return 12;
	case 'r':
		return 13;
	}

	return ch;
}

/***************************************************/
//
// Fetch next line from input file, stripping and preserving file/line
// information
//
// If source filename changes, record new filename in file_list dictionary
//
// Report new line "compiled"
//
/***************************************************/

static WORD LEX_readln(LEX_class *LEX) {
	WORD i, j, done;

	done = 0;
	do {
		if (!read_text_line(LEX->handle, MAX_IN_LEN, LEX->buf))
			return -1;

		if (LEX->buf[0] == '')
			switch (LEX->buf[1]) {
			case '<':
				set_verbosity(verbose() + 1);
				break;
			case '>':
				set_verbosity(verbose() - 1);
				break;
			}
		else
			done = 1;
	} while (!done);

	if (!(LEX->flags & LEX_LININFO))
		return 0;

	j = strlen(LEX->buf);

	if (LEX->file_list != NULL)
		for (i = 0; i < j; i++)
			if (LEX->buf[i] == ' ') {
				LEX->buf[i] = 0;
				if (DICT_lookup(LEX->file_list, LEX->buf) == NULL)
					DICT_enter(LEX->file_list, LEX->buf, 0);
				LEX->buf[i] = ' ';
				break;
			}

	for (i = 2; i < j; i++)
		if ((LEX->buf[i - 2] == ':') && (LEX->buf[i - 1] == ' '))
			break;

	if (i == j)
		return -1;

	LEX->buf[i - 2] = 0;

	LEX->state[0].new_line = 1;
	LEX->state[1].new_line = 1;

	report(E_NEWLINE, NULL, NULL);

	return i;
}

/***************************************************/
//
// Get next character from input file, fetching new lines when
// necessary; characters are buffered one level deep to allow for
// lookahead
//
/***************************************************/

static BYTE LEX_chrget(LEX_class *LEX) {
	LEX->chr = LEX->chrnxt;

	if (LEX->chr != EOF_CHAR) {
		if (!LEX->buf[LEX->chrpnt])
			LEX->chrpnt = LEX_readln(LEX);

		if (LEX->chrpnt == -1) {
			if (get_system_error() == EOF_REACHED)
				clear_system_error();

			LEX->chrnxt = EOF_CHAR;
		} else
			LEX->chrnxt = LEX->buf[LEX->chrpnt++];
	}

	return LEX->chr;
}

/***************************************************/
//
// Create lexical analyzer class instance
// 
//     flags: See lexan.hpp
//  
//  filename: Input file (normally preprocessor's output file)
// 
//  keywords: Array of strings to be recognized as valid keywords
//            (always delineated in input text by valid separator characters)
// 
//   symbols: Array of strings to be recognized as valid symbols
//            (possibly adjacent in input text; listed in order of increasing
//            length so that longest matching symbol may be recognized)
//
// file_list: Dictionary in which original source filenames are to be 
//            recorded during processing
//
/***************************************************/

LEX_class *LEX_construct(UWORD flags, BYTE *filename, BYTE *keywords[],
		BYTE *symbols[], DICT_class *file_list) {
	LEX_class *LEX;
	WORD i;
	DICT_entry *e;

	LEX = (LEX_class*) mem_alloc(sizeof(LEX_class));

	LEX->flags = flags;
	LEX->file_list = file_list;

	LEX->handle = read_text_file(filename);
	if (clear_system_error())
		report(E_FATAL, NULL, MSG_COT, filename);

	LEX->buf = (BYTE*) mem_alloc(MAX_IN_LEN);

	LEX->state[0].lexeme = (BYTE*) mem_alloc(MAX_IN_LEN);
	LEX->state[1].lexeme = (BYTE*)mem_alloc(MAX_IN_LEN);

	if (LEX->flags & LEX_LININFO) {
		LEX->state[0].line = (BYTE*) mem_alloc(MAX_LININFO);
		LEX->state[1].line = (BYTE*) mem_alloc(MAX_LININFO);
	} else {
		LEX->state[0].line = NULL;
		LEX->state[1].line = NULL;
	}

	LEX->state[0].new_line = 0;
	LEX->state[1].new_line = 0;

	LEX->chrpnt = LEX_readln(LEX);
	LEX->chrnxt = (LEX->chrpnt == -1) ? EOF_CHAR : LEX->buf[LEX->chrpnt++];

	LEX->keywords = DICT_construct(256);

	for (i = 0; keywords[i] != NULL; i++) {
		e = DICT_enter(LEX->keywords, keywords[i], D_DEFHEAP);
		e->def = mem_alloc(sizeof(int));
		*(WORD *) e->def = i;
	}

	LEX->symbol_list = symbols;
	LEX->symbol_key = (WORD*) mem_alloc(256L * sizeof(int));

	for (i = 0; i < 256; i++)
		LEX->symbol_key[i] = -1;

	for (i = 0; symbols[i] != NULL; i++)
		if ((strlen(symbols[i]) == 1) && (LEX->symbol_key[symbols[i][0]] == -1))
			LEX->symbol_key[symbols[i][0]] = i;
		else
			LEX->symbol_key[symbols[i][0]] = -2;

	LEX->symbol_cnt = i;

	LEX->cur = 0;
	LEX_fetch(LEX);

	return LEX;
}

/***************************************************/
// 
// Destroy lexical analyzer class instance
//
/***************************************************/

void LEX_destroy(LEX_class *LEX) {
	close_text_file(LEX->handle);
	DICT_destroy(LEX->keywords);

	mem_free(LEX->symbol_key);
	mem_free(LEX->buf);

	mem_free(LEX->state[0].lexeme);
	mem_free(LEX->state[1].lexeme);

	if (LEX->flags & LEX_LININFO) {
		mem_free(LEX->state[0].line);
		mem_free(LEX->state[1].line);
	}

	mem_free(LEX);
}

/***************************************************/
//
// Return current or next token
//
/***************************************************/

WORD LEX_token(LEX_class *LEX, UWORD select) {
	return LEX->state[LEX->cur ^ select].token;
}

/***************************************************/
//
// Return lexical type of current or next token
//
/***************************************************/

WORD LEX_type(LEX_class *LEX, UWORD select) {
	return LEX->state[LEX->cur ^ select].type;
}

/***************************************************/
//
// Return lexical value of current or next token
//
/***************************************************/

ULONG LEX_value(LEX_class *LEX, UWORD select) {
	return LEX->state[LEX->cur ^ select].value;
}

/***************************************************/
//
// Return lexeme of current or next token
//
/***************************************************/

BYTE *LEX_lexeme(LEX_class *LEX, UWORD select) {
	return LEX->state[LEX->cur ^ select].lexeme;
}

/***************************************************/
//
// Return original source line number of current or next token
//
/***************************************************/

BYTE *LEX_line(LEX_class *LEX, UWORD select) {
	return LEX->state[LEX->cur ^ select].line;
}

/***************************************************/
//
// Dump line #, type, and value of current or next token
// (for diagnostic purposes)
//
/***************************************************/

void LEX_show(LEX_class *LEX, UWORD select) {
	printf("%s: ", LEX_line(LEX, select));

	switch (LEX_type(LEX, select)) {
	case TTYP_KEYWORD:
		printf("Keyword [%s] token %d\n", LEX_lexeme(LEX, select),
				LEX_token(LEX, select));
		break;
	case TTYP_NAME:
		printf("   Name [%s]\n", LEX_lexeme(LEX, select));
		break;
	case TTYP_SYMBOL:
		printf(" Symbol [%s] token %d\n", LEX_lexeme(LEX, select),
				LEX_token(LEX, select));
		break;
	case TTYP_STRLIT:
		printf(" String [%s]\n", LEX_lexeme(LEX, select));
		break;
	case TTYP_TXTLIT:
		printf("   Text [%s]\n", LEX_lexeme(LEX, select));
		break;
	case TTYP_NUM:
		printf(" Number %lu (%lX)\n", LEX_value(LEX, select),
				LEX_value(LEX, select));
		break;
	case TTYP_EOF:
		printf("*** EOF ***\n");
		break;
	case TTYP_BAD:
		printf("Bad sym [%s]\n", LEX_lexeme(LEX, select));
		break;
	default:
		printf("*** Error %d ***\n", LEX_type(LEX, select));
		break;
	}
}

/***************************************************/
//
// Fetch next token from input file
//
// Tokens are buffered one level deep to allow lookahead
//
/***************************************************/

void LEX_fetch(LEX_class *LEX) {
	BYTE cur, init;
	DICT_entry *ptr;
	WORD i, j, k, p, found, t, done;
	ULONG m, n, base, ndigits;
	LEX_state *LST;

	LST = &LEX->state[LEX->cur];        // get pointer to "current" state
	LEX->cur ^= 1;                      // toggle state for next fetch

	while (is_whitespace[LEX->chrnxt]) {
		LEX_chrget(LEX);

		if (clear_system_error()) {
			LST->line[0] = 0;
			LST->type = TTYP_ERR;
			return;
		}
	}

	if (LST->new_line)                  // update file/line indicator for
	{                                // current state
		LST->new_line = 0;
		strncpy(LST->line, LEX->buf, MAX_LININFO - 4);
		LST->line[MAX_LININFO - 4] = 0;
		strcat(LST->line, ": ");
	}

	init = LEX->chrnxt;                 // get initial character of lexeme

	if (init == '\"')                   // determine lexeme's token type by
			{                                // examining initial character
		t = TTYP_STRLIT;
		LEX_chrget(LEX);
	} else if (init == '\'') {
		t = TTYP_NUM;
		LEX_chrget(LEX);
	} else if ((init == '[') && (LEX->flags & LEX_TXTLIT)) {
		t = TTYP_TXTLIT;
		LEX_chrget(LEX);
	} else if (is_digit[init])
		t = TTYP_NUM;
	else if (is_namechar[init] && (!is_digit[init]))
		t = TTYP_NAME;
	else if (LEX->symbol_key[init] != -1)
		t = TTYP_SYMBOL;
	else if (init == EOF_CHAR) {
		LST->lexeme[0] = 0;
		LST->type = TTYP_EOF;
		return;
	} else {
		LST->lexeme[0] = LEX_chrget(LEX);
		LST->lexeme[1] = 0;
		LST->type = TTYP_BAD;
		return;
	}

	switch (t)                                      // process each token type
	{
	case TTYP_NAME:
		for (i = 0; is_namechar[LEX->chrnxt]; i++)
			LST->lexeme[i] = LEX_chrget(LEX);

		LST->lexeme[i] = 0;

		if ((ptr = DICT_lookup(LEX->keywords, LST->lexeme)) != NULL) {
			t = TTYP_KEYWORD;
			LST->token = *(WORD *) ptr->def;
		}
		break;

	case TTYP_STRLIT:                            // fetch string or text
	case TTYP_TXTLIT:                            // literal
		i = 0;
		j = (init == '[') ? ']' : '"';
		do {
			while ((cur = LEX_chrget(LEX)) != j) {
				if (cur == EOF_CHAR)
					break;

				if (cur == '\\')                    // translate escape codes
					cur = esc_xlat(LEX_chrget(LEX));

				LST->lexeme[i++] = cur;
			}

			done = 1;
			if (j == '"') {
				while (is_whitespace[LEX->chrnxt])
					LEX_chrget(LEX);

				if (LEX->chrnxt == '"') {
					LEX_chrget(LEX);                 // concatenate adjacent
					done = 0;                          // string literals
				}
			}
		} while (!done);

		LST->lexeme[i] = 0;
		break;

	case TTYP_SYMBOL:
		LST->lexeme[0] = init;
		LST->lexeme[1] = 0;
		i = 1;

		if (LEX->symbol_key[init] >= 0)           // do fast lookup for
				{                                      // single-BYTE symbols
			LST->token = LEX->symbol_key[init];
			LEX_chrget(LEX);
			break;
		}
		// else find longest
		do                                        // matching symbol string
			for (j = found = 0; j < LEX->symbol_cnt; j++)
				if (!strncmp(LEX->symbol_list[j], LST->lexeme, i)) {
					LEX_chrget(LEX);
					LST->lexeme[i++] = LEX->chrnxt;
					LST->lexeme[i] = 0;
					k = j;
					found = 1;
					break;
				} while (found);

		LST->token = k;
		LST->lexeme[i - 1] = 0;
		break;

	case TTYP_NUM:
		p = 0;
		base = 10L;
		n = 0L;
		ndigits = 0L;

		switch (init) {
		case '\'':                             // get character constant
			n = 0L;                             // in original byte order
			while (LEX->chrnxt != '\'') {
				n <<= 8;
				m = LEX_chrget(LEX) & 0xffL;

				if (m == EOF_CHAR)
					break;

				if (m == '\\')
					m = esc_xlat(LEX_chrget(LEX)) & 0xffL;

				LST->lexeme[p++] = m;
				n += m;
			}
			LEX_chrget(LEX);
			break;

		default:                               // get numeric constant
			do                                  // in 80x86 byte order
			{
				while ((i = hex_val[j = LEX->chrnxt] - 1) != -1) {
					if ((!is_digit[j]) && (base != 16L))
						break;

					if (i >= base)
						break;

					n = (n * base) + (ULONG) i;

					++ndigits;

					LST->lexeme[p++] = LEX_chrget(LEX);
				}

				done = 1;
				if (((j == 'x') || (j == 'X')) && (base == 10L) && (n == 0L)) {
					base = 16L;
					ndigits = 0L;
					LST->lexeme[p++] = LEX_chrget(LEX);
					done = 0;
				} else if (((j == 'b') || (j == 'B')) && (base == 10L)
						&& (n == 0L)) {
					base = 2L;
					ndigits = 0L;
					LST->lexeme[p++] = LEX_chrget(LEX);
					done = 0;
				}

				if (LEX->chrnxt == EOF_CHAR)
					break;
			} while (!done);
			break;
		}

		LST->value = n;
		LST->lexeme[p] = 0;

		if ((base == 2L) && (ndigits > 4L) && (ndigits % 4L))
			report(E_WARN, LST->line, MSG_NBC, LST->lexeme, ndigits);

		break;
	}

	LST->type = t;

	return;
}

/*************************************************************/
//
// Fetch next token, reporting error if type and token do not match
// expected value
//
/*************************************************************/

WORD LEX_require(LEX_class *LEX, WORD type, WORD token, BYTE *expect) {
	WORD OK = 1;

	if ((LEX_type(LEX, LEX_NXT) != type) || (LEX_token(LEX, LEX_NXT) != token))
		OK = 0;

	if (!OK)
		report(E_ERROR, LEX_line(LEX, LEX_NXT), MSG_EXP, expect,
				LEX_lexeme(LEX, LEX_NXT), NULL);

	LEX_fetch(LEX);

	return OK;
}

/*************************************************************/
//
// If next token is comma symbol, fetch it and return TRUE, else 
// return FALSE
//
/*************************************************************/

WORD LEX_next_comma(LEX_class *LEX) {
	if ((LEX_type(LEX, LEX_NXT) == TTYP_SYMBOL)
			&& (LEX_lexeme(LEX, LEX_NXT)[0] == ',')
			&& (LEX_lexeme(LEX, LEX_NXT)[1] == 0)) {
		LEX_fetch(LEX);
		return 1;
	}

	return 0;
}

/*************************************************************/
//
// If next token is literal constant, fetch it and return TRUE, else 
// return FALSE
//
/*************************************************************/

WORD LEX_next_constant(LEX_class *LEX) {
	WORD type;

	type = LEX_type(LEX, LEX_NXT);

	if ((type == TTYP_STRLIT) || (type == TTYP_TXTLIT) || (type == TTYP_NUM)) {
		LEX_fetch(LEX);
		return 1;
	}

	return 0;
}
