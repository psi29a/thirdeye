//����������������������������������������������������������������������������
//��                                                                        ��
//��  RSCOMP.C                                                              ��
//��                                                                        ��
//��  Resource script parser, compiler, and maintenance facility            ��
//��                                                                        ��
//��  Version: 1.00 of 15-Apr-92 -- Initial version                         ��
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

#include <sys/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
#include "mapcomp.hpp"
#include "palcomp.hpp"

BYTE *manifest_constants[] = { "__TIMESTAMP__", "__AESOP__", NULL };

BYTE *manifest_defs[] = { "\"01-Jan-1980 00:00:00\"", "1.00", NULL };

BYTE *RS_keywords[] = { KW_ATTRIB, KW_SEQUENCE, KW_SAMPLE, KW_STRING, KW_SOURCE,
		KW_DOCUMENT, KW_MAP, KW_PALETTE, KW_FILE, KW_CODE, KW_FIXED,
		KW_MOVEABLE, KW_PRECIOUS, KW_DISCARD, KW_TEMPORARY, NULL };

enum {
	RS_ATTRIB,
	RS_SEQUENCE,
	RS_SAMPLE,
	RS_STRING,
	RS_SOURCE,
	RS_DOCUMENT,
	RS_MAP,
	RS_PALETTE,
	RS_FILE,
	RS_CODE,
	RS_FIXED,
	RS_MOVEABLE,
	RS_PRECIOUS,
	RS_DISCARDABLE,
	RS_TEMPORARY,
};

//
// Symbol names which share leading substrings must be defined in order
// of increasing length
//

BYTE *RS_symbols[] = { ",", "{", "}", ";", "-", NULL };

enum {
	RS_COMMA, RS_LCURL, RS_RCURL, RS_SEMICOLON, RS_MINUS
};

#define TEMP_NAME "TEMP"     // Name of TEMP environment variable
/*************************************************************/
//
// Verify that no declaration for resname has previously been
// compiled; delete resource name from "created by reference" 
// warning list
//
/*************************************************************/

WORD RS_check_name(RS_class *RS, BYTE *resname) {
	if (!(RS->flags & RS_NOUNIQUECHECK)) {
		if (DICT_lookup(RS->names, resname)) {
			report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_NIU, resname);
			return 0;
		}

		DICT_enter(RS->names, resname, D_DEFHEAP);
	}

	DICT_delete(RS->refcr, resname);

	return 1;
}

/*************************************************************/
//
// Update the RDEP (Resource DEPendency) directory entry for
// indirect resource resname  
//
// filelist normally contains a list of one or more files upon 
// which resname depends
//
// If the new filelist is the same as the old definition, leave the
// entry alone
//
/*************************************************************/

void RS_update_RDEP(RS_class *RS, BYTE *resname, DICT_class *filelist) {
	DICT_entry *cur;
	BYTE *string;

	string = DICT_build_tag_string(filelist);

	cur = DICT_lookup(RS->dict[RDEP], resname);

	if (cur != NULL)
		if (!strcasecmp((BYTE*)cur->def, string)) {
			mem_free(string);
			return;
		} else
			DICT_delete(RS->dict[RDEP], resname);

	cur = DICT_enter(RS->dict[RDEP], resname, D_DEFHEAP);

	cur->def = string;
}

/*************************************************************/
//
// Update the RDES (Resource DEScription) directory entry for
// indirect resource resname
//
// speclist normally contains a representation of all arguments
// associated with the resource's declaration
//
// If the new speclist is the same as the old definition, leave the
// entry alone
//
/*************************************************************/

void RS_update_RDES(RS_class *RS, BYTE *resname, BYTE *speclist) {
	DICT_entry *cur;

	cur = DICT_lookup(RS->dict[RDES], resname);

	if (cur != NULL)
		if (!strcasecmp((BYTE*)cur->def, speclist))
			return;
		else
			DICT_delete(RS->dict[RDES], resname);

	cur = DICT_enter(RS->dict[RDES], resname, D_DEFHEAP);

	cur->def = str_alloc(speclist);
}

/*************************************************************/
//
// Return the latest timestamp of any file in DICT, or 0 (always 
// obsolete) if one or more files could not be found or if DICT
// contains the filename "$obsolete"
//
/*************************************************************/

ULONG RS_depend_time(RS_class *RS, DICT_class *DICT) {
	ULONG ts;
	BYTE *string;

	string = DICT_build_tag_string(DICT);

	ts = TS_latest_file_time(RS->TS, string);

	mem_free(string);

	return ts;
}

/*************************************************************/
//
// Return resname's index in the ROED directory, or -1 if not found
//
/*************************************************************/

ULONG RS_current_ROED_entry(RS_class *RS, BYTE *resname) {
	DICT_entry *cur;
	ULONG ord;

	cur = DICT_lookup(RS->dict[ROED], resname);

	if (cur != NULL) {
		ord = ascnum((BYTE*)cur->def, 10);

		return ord;
	}

	return -1L;
}

/*************************************************************/
//
// Return resname's index in the ROED directory
//
// If resname is not yet in ROED, insert it and report the creation 
// of a new resource entry
//
// Abandoned resource identifiers are never deleted; however, 4 billion
// unique identifiers exist.  No error checking is done to enforce
// this limit.
//
/*************************************************************/

ULONG RS_get_ROED_entry(RS_class *RS, BYTE *resname) {
	DICT_entry *cur;
	RF_entry_hdr RHDR;
	ULONG ord;

	cur = DICT_lookup(RS->dict[ROED], resname);

	if (cur != NULL)
		return ascnum((BYTE*)cur->def, 10);

	RHDR.data_attrib = DA_PLACEHOLDER;
	RHDR.data_size = 0L;

	ord = RF_new_entry(RS->RF, NULL, &RHDR, RTYP_RAW_MEM);

	DICT_enter(RS->dict[ROED], resname, D_DEFHEAP)->def = str(ord);

	report(E_RESNEW, NULL, NULL);

	return ord;
}

/*************************************************************/
//
// Return msgname's index in the MSGD directory  
//
// If msgname is not yet in MSGD, insert it
//
// Only 64K unique message identifiers are allowed; report an error if
// this limit is exceeded.  Note that abandoned message identifiers are
// never deleted.
//
/*************************************************************/

UWORD RS_get_MSGD_entry(RS_class *RS, BYTE *msgname) {
	DICT_entry *cur;
	DI_class *DI;
	ULONG msgnum;

	cur = DICT_lookup(RS->dict[MSGD], msgname);

	if (cur != NULL)
		return (UWORD) ascnum((BYTE*)cur->def, 10);

	msgnum = 0L;
	DI = DI_construct(RS->dict[MSGD]);
	while (DI_fetch(DI) != NULL)
		++msgnum;
	DI_destroy(DI);

	if (msgnum == 65536L)
		report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_TMM);

	DICT_enter(RS->dict[MSGD], msgname, D_DEFHEAP)->def = str(msgnum);

	return (UWORD) msgnum;
}

/*************************************************************/
//
// Return TRUE if next token is a resource attribute specifier keyword
//
/*************************************************************/

WORD RS_next_attribute_specifier(RS_class *RS) {
	WORD t;

	if (LEX_type(RS->LEX, LEX_NXT) == TTYP_KEYWORD) {
		t = LEX_token(RS->LEX, LEX_NXT);

		if ((t >= RS_FIXED) && (t <= RS_TEMPORARY))
			return 1;
	}

	return 0;
}

/*************************************************************/
//
// Return result of parsing attribute list; report appropriate errors
// or warnings if contradictory attribute combinations are specified
// (e.g., PRECIOUS,DISCARDABLE)
//
/*************************************************************/

ULONG RS_parse_attribute_list(RS_class *RS) {
	ULONG attrib;
	WORD af, am, ap, ad, at;

	attrib = DA_TEMPORARY;
	af = am = ap = ad = at = 0;

	do {
		if (!RS_next_attribute_specifier(RS)) {
			report(E_ERROR, LEX_line(RS->LEX, LEX_NXT), MSG_MAS, NULL);
			return attrib;
		}

		LEX_fetch(RS->LEX);

		switch (LEX_token(RS->LEX, LEX_CUR)) {
		case RS_FIXED:
			if (af)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_RAT, NULL);
			else if (am)
				report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_CAT, NULL);
			else if (at)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_MTT,
						LEX_lexeme(RS->LEX, LEX_CUR), NULL);
			else {
				attrib |= DA_FIXED;
				af++;
			}
			break;

		case RS_MOVEABLE:
			if (am)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_RAT, NULL);
			else if (af)
				report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_CAT, NULL);
			else if (at)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_MTT,
						LEX_lexeme(RS->LEX, LEX_CUR), NULL);
			else {
				attrib &= (~DA_FIXED);
				am++;
			}
			break;

		case RS_PRECIOUS:
			if (ap)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_RAT, NULL);
			else if (ad)
				report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_CAT, NULL);
			else if (at)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_MTT,
						LEX_lexeme(RS->LEX, LEX_CUR), NULL);
			else {
				attrib |= DA_PRECIOUS;
				ap++;
			}
			break;

		case RS_DISCARDABLE:
			if (ad)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_RAT, NULL);
			else if (ap)
				report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_CAT, NULL);
			else if (at)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_MTT,
						LEX_lexeme(RS->LEX, LEX_CUR), NULL);
			else {
				attrib |= DA_DISCARDABLE;
				ad++;
			}
			break;

		case RS_TEMPORARY:
			if (at)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_RAT, NULL);
			else if (af || am || ap || ad)
				report(E_WARN, LEX_line(RS->LEX, LEX_CUR), MSG_MAT, NULL);
			else {
				attrib = DA_TEMPORARY;
				at++;
			}
			break;
		}
	} while (LEX_next_comma(RS->LEX));

	return attrib;
}

/*************************************************************/
//
// Create and return pointer to string representing a list of 
// numeric or string constants in input file
//
/*************************************************************/

static BYTE *RS_parse_constant_list_string(RS_class *RS) {
	DICT_class *cl;
	BYTE *string;
	BYTE buf[64];
	WORD neg;
	ULONG val;

	cl = DICT_construct(DC_LINEAR);

	do {
		neg = ((LEX_type(RS->LEX, LEX_NXT) == TTYP_SYMBOL)
				&& (LEX_token(RS->LEX, LEX_NXT) == RS_MINUS));

		if (neg)
			LEX_fetch(RS->LEX);

		if (!LEX_next_constant(RS->LEX)) {
			report(E_ERROR, LEX_line(RS->LEX, LEX_NXT), MSG_MSC, NULL);
			break;
		}

		switch (LEX_type(RS->LEX, LEX_CUR)) {
		case TTYP_TXTLIT:
		case TTYP_STRLIT:
			DICT_enter(cl, LEX_lexeme(RS->LEX, LEX_CUR), 0);
			break;

		case TTYP_NUM:
			val = LEX_value(RS->LEX, LEX_CUR);
			if (neg)
				val = -val;

			//DICT_enter(cl,atoi(val,buf,10),0);
			DICT_enter(cl, (BYTE*)sprintf(buf, "%d", val), 0);
			break;
		}
	} while (LEX_next_comma(RS->LEX));

	string = DICT_build_tag_string(cl);

	DICT_destroy(cl);

	return string;
}

/*************************************************************/
//
// Report syntax error in resource script
//
/*************************************************************/

void RS_syntax_error(RS_class *RS) {
	report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_SYN,
			LEX_lexeme(RS->LEX, LEX_CUR), NULL);
}

/*************************************************************/
//
// Report illegal use of reserved word in resource script
//
/*************************************************************/

void RS_reserved_word_error(RS_class *RS) {
	report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_RWU,
			LEX_lexeme(RS->LEX, LEX_CUR), NULL);
}

/*************************************************************/
//
// Parse an attrib statement with its accompanying attribute list
//
/*************************************************************/

static void RS_parse_attribute_specification(RS_class *RS) {
	WORD type;

	type = LEX_token(RS->LEX, LEX_NXT) - RS_SEQUENCE;

	if ((LEX_type(RS->LEX, LEX_NXT) != TTYP_KEYWORD) || (type >= RDAT_NTYPES)
			|| (type < 0)) {
		report(E_ERROR, LEX_line(RS->LEX, LEX_NXT), MSG_ERS, NULL);
		return;
	}

	LEX_fetch(RS->LEX);

	RS->attribs[type] = RS_parse_attribute_list(RS);
}

/*************************************************************/
//
// Parse code resource declaration list with appropriate error
// checking, creating dictionary of vector offsets
//
// If temporary dictionary does not match CRFD dictionary, replace
// CRFD entry set with temporary dictionary
//
/*************************************************************/

void RS_parse_code_resource_declaration(RS_class *RS) {
	DICT_class *new_construct;
	BYTE *name;
	WORD bad;
	ULONG link;

	if (!LEX_require(RS->LEX, TTYP_SYMBOL, RS_LCURL, RS_symbols[RS_LCURL]))
		return;

	new_construct = DICT_construct(64);
	bad = 0;
	link = 0L;

	DICT_enter(new_construct,FN_SEND,0);
	DICT_enter(new_construct,FN_PASS,0);

	do {
		if ((LEX_type(RS->LEX, LEX_NXT) == TTYP_SYMBOL)
				&& (LEX_token(RS->LEX, LEX_NXT) == RS_RCURL))
			break;

		LEX_fetch(RS->LEX);

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
			report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_CMI, NULL);
			bad = 1;
			break;

		case TTYP_NAME:
			name = LEX_lexeme(RS->LEX, LEX_CUR);

			if (DICT_lookup(new_construct,name) != NULL) {
				report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_CAD, name,
						NULL);
				bad = 1;
			} else {
				DICT_enter(new_construct,name,D_DEFHEAP)->def = str(link);
				link += CR_VECTOR_SIZE;
			}

			break;
		}
	} while (LEX_next_comma(RS->LEX));

	LEX_require(RS->LEX, TTYP_SYMBOL, RS_RCURL, RS_symbols[RS_RCURL]);
	LEX_require(RS->LEX, TTYP_SYMBOL, RS_SEMICOLON, RS_symbols[RS_SEMICOLON]);

	if ((!DICT_compare(new_construct,RS->dict[CRFD])) && (!bad)) {
		DICT_wipe(RS->dict[CRFD]);
	DICT_copy(new_construct,RS->dict[CRFD]);
}

DICT_destroy(new_construct);
}

/*************************************************************/
//
// Parse string resource definition
//
/*************************************************************/

void RS_string_resource(RS_class *RS) {
BYTE *name, *text, *lexeme;
WORD at_opt;
ULONG attrib, scr_time, res_time, ord;
RF_entry_hdr RHDR;

LEX_fetch(RS->LEX);

switch (LEX_type(RS->LEX, LEX_CUR)) {
default:
RS_syntax_error(RS);
break;

case TTYP_KEYWORD:
RS_reserved_word_error(RS);
break;

case TTYP_STRLIT:
case TTYP_NAME:
name = str_alloc(LEX_lexeme(RS->LEX, LEX_CUR));

RS_check_name(RS, name);

if ((at_opt = RS_next_attribute_specifier(RS)) != 0)
	attrib = RS_parse_attribute_list(RS);

LEX_fetch(RS->LEX);

switch (LEX_type(RS->LEX, LEX_CUR)) {
default:
	RS_syntax_error(RS);
	break;

case TTYP_TXTLIT:
case TTYP_STRLIT:
	lexeme = LEX_lexeme(RS->LEX, LEX_CUR);
	text = (BYTE*) mem_alloc((ULONG) strlen(lexeme) + 3L);
	strcpy(text, "S:");
	strcat(text, lexeme);

	scr_time = RS_depend_time(RS, RS->depend);
	ord = RS_get_ROED_entry(RS, name);
	res_time = RES_storage_timestamp(RS->RF, ord);

	if (res_time < scr_time) {
		RHDR.data_attrib = (at_opt) ? attrib : RS->attribs[RDAT_STR];
		RHDR.data_size = (ULONG) strlen(text) + 1L;

		RF_write_entry(RS->RF, ord, text, &RHDR, RTYP_RAW_MEM);

		report(E_RESCOMP, NULL, NULL);
	}

	mem_free(text);
	break;
}

mem_free(name);
break;
}
}

/***************************************************/
//
// Dump contents of resource file; should be called only 
// immediately before closing file
//
/***************************************************/

void RS_show_contents(RS_class *RS, UWORD verbose) {
ULONG entry, nentries;
RF_entry_hdr *RHDR;
BYTE movatr[4], mematr[4];
DI_class *DI;
DICT_entry *rent;
BYTE *name;

nentries = RF_entry_count(RS->RF);

printf("\n");

printf(MSG_RS_FIL, RS->RF->filename);
printf(MSG_RS_FOR, RS->RF->hdr.signature);
if (verbose) {
printf(MSG_RS_SIZ, RS->RF->hdr.file_size);
printf(MSG_RS_USE, RS->RF->hdr.file_size - RS->RF->hdr.lost_space,
		((RS->RF->hdr.lost_space * 100L) / RS->RF->hdr.file_size));
printf(MSG_RS_NEN, nentries);
printf(MSG_RS_CTS, ASCII_time(RS->RF->hdr.create_time));
printf(MSG_RS_MTS, ASCII_time(RS->RF->hdr.modify_time));
}

if (nentries)
printf("\n");

for (entry = 0L; entry < nentries; entry++)
if (RF_flags(RS->RF, entry) & SA_DELETED)
	printf(MSG_RS_DEL, entry);
else {
	RHDR = RF_header(RS->RF, entry);
	if (RHDR == NULL)
		report(E_ERROR, NULL, (BYTE*)MSG_BRE, entry);
	else {
		if (RHDR->data_attrib & DA_FIXED)
			strcpy(movatr, MSG_RS_FIX);
		else
			strcpy(movatr, MSG_RS_MOV);

		if (RHDR->data_attrib & DA_PRECIOUS)
			strcpy(mematr, MSG_RS_PRE);
		else if (RHDR->data_attrib & DA_DISCARDABLE)
			strcpy(mematr, MSG_RS_DIS);
		else
			strcpy(mematr, MSG_RS_TMP);

		if (verbose) {
			name = str_alloc((BYTE*)MSG_RS_INV);

			DI = DI_construct(RS->dict[ROED]);
			while ((rent = DI_fetch(DI)) != NULL) {
				if (ascnum((BYTE*)rent->def, 10) == entry) {
					mem_free(name);
					name = str_alloc(rent->tag);
					break;
				}
			}
			DI_destroy(DI);

			if (strlen(name) > 29)
				name[29] = 0;

			if (RHDR->data_attrib & DA_PLACEHOLDER)
				printf(MSG_RS_PLA, entry,
						RF_index(RS->RF, entry) + sizeof(RF_entry_hdr),
						ASCII_time(RHDR->timestamp), name);
			else
				printf(MSG_RS_VER, entry,
						RF_index(RS->RF, entry) + sizeof(RF_entry_hdr), movatr,
						mematr, RHDR->data_size, ASCII_time(RHDR->timestamp),
						name);

			mem_free(name);
		} else
			printf(MSG_RS_BRI, entry,
					RF_index(RS->RF, entry) + sizeof(RF_entry_hdr), movatr,
					mematr, RHDR->data_size);
	}
}
}

/*************************************************************/
//
// Create an indirect resource compiler class instance
//
// (Indirect resources are resources defined by data external to the 
// resource script, such as files, documents, or sequences.  SOP programs
// are also indirect resources, but are handled separately.)
//
/*************************************************************/

IDR_class *IDR_construct(RS_class *RS) {
BYTE *rtype;
WORD i, bad;
IDR_class *IDR;
BYTE *rspec;

IDR = (IDR_class*) mem_alloc(sizeof(IDR_class));

IDR->RS = RS;

rtype = str_alloc(LEX_lexeme(RS->LEX, LEX_CUR));

IDR->type = LEX_token(RS->LEX, LEX_CUR);

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
case TTYP_NAME:
IDR->name = str_alloc(LEX_lexeme(RS->LEX, LEX_CUR));

RS_check_name(RS, IDR->name);

if (RS_next_attribute_specifier(RS))
	IDR->attrib = RS_parse_attribute_list(RS);
else
	IDR->attrib = RS->attribs[IDR->type - RS_SEQUENCE];

LEX_require(RS->LEX, TTYP_SYMBOL, RS_LCURL, RS_symbols[RS_LCURL]);

rspec = RS_parse_constant_list_string(RS);

IDR->speclist = (BYTE*) mem_alloc(strlen(rtype) + strlen(rspec) + 4);
strcpy(IDR->speclist, rtype);
strcat(IDR->speclist, ":");
strcat(IDR->speclist, rspec);

for (i = 0; i < strlen(rspec); i++)
	if (rspec[i] == ',')
		rspec[i] = 0;

IDR->fn = str_alloc(rspec);

mem_free(rspec);

LEX_require(RS->LEX, TTYP_SYMBOL, RS_RCURL, RS_symbols[RS_RCURL]);
}

mem_free(rtype);

if (bad) {
mem_free(IDR);
return NULL;
}

return IDR;
}

/*************************************************************/
//
// Display the name and attributes of an indirect resource
// (for diagnostic purposes)
//
/*************************************************************/

void IDR_show(IDR_class *IDR) {
printf("IDR \"%s\" [%s] [%s]", IDR->name, IDR->fn, IDR->speclist);
printf(" (attr %X)", IDR->attrib);
printf("\n");
}

/*************************************************************/
//
// Return 0 if indirect resource image in resource file is up to date
//
// An indirect resource is obsolete and must be recompiled if:
//
// 1) Its resource file entry is a PLACEHOLDER, i.e. created by reference
//    rather than by declaration; or
//
// 2) The resource's data attributes have changed; or
//
// 3) The resource's file dependencies have not yet been entered in RDEP; or
//
// 4) Any file upon which the resource depends is missing or 
//    named "$obsolete"; or
//
// 5) Any file upon which the resource depends has been modified since the
//    resource was last compiled; or
//
// 6) Any part of the resource's definition string has changed
//
/*************************************************************/

WORD IDR_test(IDR_class *IDR) {
ULONG ftime;
RF_entry_hdr *RHDR;
DICT_entry *cur;

IDR->ord = RS_get_ROED_entry(IDR->RS, IDR->name);

RHDR = RF_header(IDR->RS->RF, IDR->ord);
if (RHDR->data_attrib & DA_PLACEHOLDER)
return 1;

if (RHDR->data_attrib != IDR->attrib)
return 1;

cur = DICT_lookup(IDR->RS->dict[RDEP], IDR->name);

if ((cur == NULL) || (cur->def == NULL))
return 1;

ftime = TS_latest_file_time(IDR->RS->TS, (BYTE*)cur->def);

if (!ftime)
return 1;

if (RES_storage_timestamp(IDR->RS->RF, IDR->ord) < ftime)
return 1;

cur = DICT_lookup(IDR->RS->dict[RDES], IDR->name);

if ((cur == NULL) || (cur->def == NULL))
return 1;

if (strcasecmp((BYTE*)cur->def, IDR->speclist))
return 1;

return 0;
}

/*************************************************************/
//
// Compile an indirect resource
//
// Verify that the source file exists, extract the requested resource
// from the file, report the compilation of the resource, and update
// the Resource DEPendency and Resource DEScriptor dictionaries
//
/*************************************************************/

void IDR_compile(IDR_class *IDR) {
DICT_class *depend;
MAP_class *MAP;
PAL_class *PAL;
RF_entry_hdr RHDR;

if (!verify_file(IDR->fn)) {
DICT_delete(IDR->RS->dict[RDEP], IDR->name);
report(E_ERROR, LEX_line(IDR->RS->LEX, LEX_CUR), MSG_SNF, IDR->fn, NULL);
return;
}

depend = DICT_construct(4);

switch (IDR->type) {
case RS_SEQUENCE:    // (temporarily same as RS_FILE)
case RS_SAMPLE:

case RS_FILE:
RHDR.data_attrib = IDR->attrib;
RHDR.data_size = file_size(IDR->fn);

RF_write_entry(IDR->RS->RF, IDR->ord, IDR->fn, &RHDR, RTYP_RAW_FILE);

DICT_enter(depend, IDR->fn, 0);
break;

case RS_MAP:
MAP = MAP_construct(IDR);
if (MAP == NULL)
	break;

MAP_compile(MAP);
MAP_destroy(MAP);

DICT_enter(depend, IDR->fn, 0);
break;

case RS_PALETTE:
PAL = PAL_construct(IDR);
if (PAL == NULL)
	break;

PAL_compile(PAL);
PAL_destroy(PAL);

DICT_enter(depend, IDR->fn, 0);
break;
}

report(E_RESCOMP, NULL, NULL);

RS_update_RDEP(IDR->RS, IDR->name, depend);
RS_update_RDES(IDR->RS, IDR->name, IDR->speclist);

DICT_destroy(depend);
}

/*************************************************************/
//
// Destroy an indirect resource compiler class instance
//
/*************************************************************/

void IDR_destroy(IDR_class *IDR) {
mem_free(IDR->name);
mem_free(IDR->fn);
mem_free(IDR->speclist);

mem_free(IDR);
}

/*************************************************************/
//
// Create a resource script compiler class instance
//
// SCR_filename: Resource script (input) filename
//
// RES_filename: Resource file (output) filename
//
//       predef: Dictionary containing predefined macros
//
//  c_threshold: Percentage of "wasted" resource file space at which
//               housecleaning will be initiated
//
//        flags: See RSCOMP.H
//
/*************************************************************/

RS_class *RS_construct(BYTE *SCR_filename, BYTE *RES_filename,
DICT_class *predef, WORD c_threshold, UWORD flags) {
RS_class *RS;
UWORD i;

strncpy(&manifest_defs[0][1], ASCII_time(current_time()), 20);

if (flags & RS_REBUILD)
unlink(RES_filename);

RS = (RS_class*) mem_alloc(sizeof(RS_class));

RS->RES_fn = str_alloc(RES_filename);
RS->SCR_fn = str_alloc(SCR_filename);

RS->depend = DICT_construct(4);
RS->names = DICT_construct(64);
RS->refcr = DICT_construct(64);
RS->predef = DICT_construct(DC_LINEAR);

RS->TS = TS_construct();

RS->flags = flags;
RS->c_threshold = c_threshold;

RS->RF = RF_construct(RES_filename, 0);

RS->RES_time = TS_file_time(RS->TS, RES_filename);

DICT_copy(predef, RS->predef);

for (i = 0; manifest_constants[i] != NULL; i++) {
if (DICT_lookup(RS->predef, manifest_constants[i])) {
	report(E_NOTICE, NULL, MSG_RDF, manifest_constants[i]);
	DICT_delete(RS->predef, manifest_constants[i]);
}

DICT_enter(RS->predef, manifest_constants[i], 0)->def = manifest_defs[i];
}

for (i = 0; i < NDICTS; i++) {
RS->dict[i] = DICT_construct(512);

if (RF_flags(RS->RF, i) & SA_UNUSED)
	DICT_save(RS->dict[i], RS->RF, (ULONG) -1);
else
	DICT_load(RS->dict[i], RS->RF, i);
}

RS_get_MSGD_entry(RS, MN_CREATE);
RS_get_MSGD_entry(RS, MN_DESTROY);
RS_get_MSGD_entry(RS, MN_RESTORE);

return RS;
}

/*************************************************************/
//
// Destroy a resource script compiler class instance; issue
// warnings about any resources which were created by reference but
// never actually compiled
//
/*************************************************************/

void RS_destroy(RS_class *RS) {
UWORD i;
DICT_entry *entry;
DI_class *DI;

DI = DI_construct(RS->refcr);

while ((entry = DI_fetch(DI)) != NULL)
report(E_WARN, (BYTE*)entry->def, (BYTE*)MSG_PMR, entry->tag);

DI_destroy(DI);

for (i = 0; i < NDICTS; i++)
DICT_destroy(RS->dict[i]);

RF_destroy(RS->RF, RS->c_threshold);

TS_destroy(RS->TS);

DICT_destroy(RS->predef);
DICT_destroy(RS->refcr);
DICT_destroy(RS->names);
DICT_destroy(RS->depend);

mem_free(RS->RES_fn);
mem_free(RS->SCR_fn);
mem_free(RS);
}

/*************************************************************/
//
// Compile resource script, via the following steps:
//
// 1) If no resource dependencies have been modified since the last
//    compilation, including the resource script itself, issue "up to date"
//    notice and return
//
// 2) Preprocess the resource script and any of its #include files,
//    generating a single intermediate file (i-file)
//
// 3) Lexically analyze the i-file, parsing attribute declarations,
//    resource definitions, and other constructs
//
// 4) Check each resource for obsolescence, compiling where necessary
//
// 5) If any errors occurred during processing or compilation, mark the
//    resource script itself "obsolete" so that test 1) above will fail
//    on the next invocation
//
// 6) Update any dictionary resources which were modified during compilation
//
// 7) Clean up, delete i-file, and write current time to resource file 
//    timestamp
//
/*************************************************************/

void RS_compile(RS_class *RS) {
UWORD i;
UWORD cr_defined;
PP_class *PP;
DI_class *DI;
IDR_class *IDR;
SOP_class *SOP;
ULONG n, latest_comp;
DICT_entry *cur;

DI = DI_construct(RS->dict[RDEP]);
latest_comp = 0L;
while ((cur = DI_fetch(DI)) != NULL) {
n = TS_latest_file_time(RS->TS, (BYTE*)cur->def);
if (n == 0L) {
	latest_comp = n;
	break;
}
if (n > latest_comp)
	latest_comp = n;
}
DI_destroy(DI);

if ((latest_comp) && (RS->RES_time >= latest_comp)) {
report(E_NOTICE, NULL, MSG_UTD, RS->RES_fn, NULL);
return;
}

for (i = 0; i < RDAT_NTYPES; i++)
RS->attribs[i] = DA_MOVEABLE | DA_DISCARDABLE;

cr_defined = 0;

RS->tfile = temp_filename(getenv(TEMP_NAME));
PP = PP_construct(RS->SCR_fn, RS->tfile, RS->predef, PP_TXTLIT);
PP_process(PP);
PP_destroy(PP);

RS->LEX = LEX_construct(LEX_LININFO | LEX_TXTLIT, RS->tfile, RS_keywords,
	RS_symbols, RS->depend);

while (LEX_type(RS->LEX, LEX_NXT) != TTYP_EOF) {
LEX_fetch(RS->LEX);

switch (LEX_type(RS->LEX, LEX_CUR)) {
case TTYP_KEYWORD:
	switch (LEX_token(RS->LEX, LEX_CUR)) {
	case RS_ATTRIB:
		RS_parse_attribute_specification(RS);
		break;

	case RS_CODE:
		if (cr_defined)
			report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), MSG_MDT, NULL);
		else {
			RS_parse_code_resource_declaration(RS);
			cr_defined = 1;
		}
		break;

	case RS_STRING:
		RS_string_resource(RS);
		break;

	case RS_SEQUENCE:
	case RS_SAMPLE:
	case RS_FILE:
	case RS_MAP:
	case RS_PALETTE:
		IDR = IDR_construct(RS);
		if (IDR == NULL)
			break;

		if (IDR_test(IDR))
			IDR_compile(IDR);

		IDR_destroy(IDR);
		break;

	case RS_SOURCE:
		SOP = SOP_construct(RS, RS->attribs[RS_SOURCE - RS_SEQUENCE]);
		if (SOP == NULL)
			break;

		if (SOP_test(SOP))
			SOP_compile(SOP);

		SOP_destroy(SOP);
		break;

	default:
		RS_syntax_error(RS);
	}
	break;

default:
	RS_syntax_error(RS);
}
}

if (error_message_count())
DICT_enter(RS->depend, "$obsolete", 0);

RS_update_RDEP(RS, "$", RS->depend);

for (i = 0; i < NDICTS; i++)
if (DICT_touched(RS->dict[i]))
	DICT_save(RS->dict[i], RS->RF, i);

LEX_destroy(RS->LEX);
remove_tempfile(RS->tfile);

set_file_time(RS->RES_fn, current_time());
}
