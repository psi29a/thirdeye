//����������������������������������������������������������������������������
//��                                                                        ��
//��  ARC.C                                                                 ��
//��                                                                        ��
//��  AESOP Resource Compiler main program shell                            ��
//��                                                                        ��
//��  V1.00 of 26-Feb-92 -- Initial version                                 ��
//��  V1.01 of 15-Apr-93 -- V1.01 version identification                    ��
//��  V1.02 of 12-Sep-93 -- V1.02 version identification                    ��
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
#include <ctype.h>

#include "defs.hpp"
#include "system.hpp"
#include "arcmsg.hpp"
#include "dict.hpp"
#include "resfile.hpp"
#include "resource.hpp"
#include "preproc.hpp"
#include "lexan.hpp"
#include "rscomp.hpp"

const BYTE VERSION[] = "1.02";

/*************************************************************/
//
// Show CLI usage notes
//
/*************************************************************/

void show_syntax(void) {
	printf(MSG_SYN_1);
	printf(MSG_SYN_2);
	printf(MSG_SYN_3);
	printf(MSG_SYN_4);
	printf(MSG_SYN_5);
	printf(MSG_SYN_6);
	printf(MSG_SYN_7);
	printf(MSG_SYN_8);
	printf(MSG_SYN_9);
	printf(MSG_SYN_10);
	printf(MSG_SYN_11);
	printf(MSG_SYN_12);
	printf(MSG_SYN_13);
	printf(MSG_SYN_14);

	abend();
}

/*************************************************************/
void main(int argc, BYTE *argv[]) {
	static BYTE SCR_filename[256];
	static BYTE RES_filename[256];
	RS_class *RS;
	DICT_class *predef;
	WORD c_threshold;
	LONG n;
	UWORD flags;
	UWORD i, j;

	setbuf(stdout, NULL);

	set_errorlevel(E_NOTICE);
	set_verbosity(0);
	set_temp_deletion_policy(1);

	printf(MSG_BANNER"\n", VERSION);
	mem_init();

	predef = DICT_construct(DC_LINEAR);

	SCR_filename[0] = 0;
	RES_filename[0] = 0;
	c_threshold = DEF_C_THRESHOLD;
	flags = 0;

	if ((argc < 2) || (argv[1][1] == '?')) {
		show_syntax();
		exit(1);
	}

	atexit(summary);

	for (i = 1; i < argc; i++)
		if ((argv[i][0] != '/') && (argv[i][0] != '-'))
			if (strlen(SCR_filename))
				report(E_FATAL, NULL, MSG_ICO);
			else
				strcpy(SCR_filename, argv[i]);
		else if (!strcasecmp(&argv[i][1], "n"))
			flags |= RS_REBUILD;

		else if (!strcasecmp(&argv[i][1], "w0"))
			set_errorlevel(E_NOTICE);

		else if (!strcasecmp(&argv[i][1], "w1"))
			set_errorlevel(E_WARN);

		else if (!strcasecmp(&argv[i][1], "w2"))
			set_errorlevel(E_ERROR);

		else if (!strcasecmp(&argv[i][1], "x"))
			flags |= RS_NOUNIQUECHECK;

		else if (!strcasecmp(&argv[i][1], "v"))
			set_verbosity(1);

		else if (!strcasecmp(&argv[i][1], "sv"))
			flags |= RS_SHOWVERBOSE;

		else if (!strcasecmp(&argv[i][1], "sb"))
			flags |= RS_SHOWBRIEF;

		else if (!strcasecmp(&argv[i][1], "kp"))
			set_temp_deletion_policy(0);

		else if (tolower(argv[i][1]) == 'o')
			strcpy(RES_filename, &argv[i][2]);

		else if (tolower(argv[i][1]) == 'c')
			if ((n = ascnum(&argv[i][2], 10)) == -1L)
				report(E_ERROR, NULL, MSG_IVC);
			else
				c_threshold = (WORD) n;

		else if (tolower(argv[i][1]) == 'd')
			if (!argv[i][2])
				report(E_ERROR, NULL, MSG_BCM);
			else {
				for (j = 3; j < strlen(argv[i]); j++)
					if (argv[i][j] == '=') {
						argv[i][j] = 0;
						j++;
						break;
					}
				if (DICT_lookup(predef, &argv[i][2])) {
					report(E_NOTICE, NULL, MSG_RDF, &argv[i][2]);
					DICT_delete(predef, &argv[i][2]);
				}
				DICT_enter(predef, &argv[i][2], D_DEFHEAP)->def = str_alloc(
						&argv[i][j]);
			}

		else
			report(E_FATAL, NULL, MSG_ICO);

	if (!strlen(SCR_filename))
		report(E_FATAL, NULL, MSG_NFS);

	for (i = 0; i < strlen(SCR_filename); i++)
		if (SCR_filename[i] == '.') {
			SCR_filename[i] = 0;
			break;
		}

	if (!strlen(RES_filename)) {
		strcpy(RES_filename, SCR_filename);
		strcat(RES_filename, RES_SUFFIX);
		for (i = 0; RES_filename[i]; i++) {
			RES_filename[i] = tolower(RES_filename[i]);
		}
	}

	strcat(SCR_filename, RS_SUFFIX);
	for (i = 0; SCR_filename[i]; i++) {
		SCR_filename[i] = tolower(SCR_filename[i]);
	}

	set_output_filename(RES_filename);

	RS = RS_construct(SCR_filename, RES_filename, predef, c_threshold, flags);
	RS_compile(RS);

	if (flags & (RS_SHOWBRIEF | RS_SHOWVERBOSE))
		RS_show_contents(RS, flags & RS_SHOWVERBOSE);

	RS_destroy(RS);

	DICT_destroy(predef);

	mem_shutdown();
	exit(error_message_count() != 0);
}
