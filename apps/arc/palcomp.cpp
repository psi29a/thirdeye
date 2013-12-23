//����������������������������������������������������������������������������
//��                                                                        ��
//��  PALCOMP.C                                                             ��
//��                                                                        ��
//��  AESOP palette compiler class                                          ��
//��                                                                        ��
//��  Version: 1.00 of 6-Oct-92 -- Initial version                          ��
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
#include "dict.hpp"
#include "preproc.hpp"
#include "resfile.hpp"
#include "resource.hpp"
#include "lexan.hpp"
#include "rscomp.hpp"
#include "palcomp.hpp"
#include "arcmsg.hpp"

typedef struct {
	UBYTE red;                      // supports up to 24-bit color
	UBYTE green;
	UBYTE blue;
} rgb;

typedef struct {
	UWORD word_size;                     // # of bits per R/G/B element
	UWORD array_size;                    // # of RGB values in palette
	rgb *colors;                         // points to rgb[array_size] list
} pal;

/*************************************************************/
//
// Find closest palette color index
//
/*************************************************************/

UWORD remap_RGB(rgb *old, pal *new_pal)
{
	UWORD dif,d,i,n,best;
	UBYTE old_r,old_g,old_b;

	old_r = old->red;
	old_g = old->green;
	old_b = old->blue;

	dif = 65535U;

	n = new_pal->array_size;

	for (i=0;i<n;i++)
	{
		d = abs(old_r - new_pal->colors[i].red) +
		abs(old_g - new_pal->colors[i].green) +
		abs(old_b - new_pal->colors[i].blue);

		if (d <= dif)
		{
			best = i;
			dif = d;
		}
	}

	return (best);
}

/*************************************************************/
//
// Report error during compilation
//
/*************************************************************/

void PAL_error(RS_class *RS, BYTE *msg, BYTE *arg) {
	report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), msg, arg);
}

/*************************************************************/
//
// Create a new palette compiler class instance
//
// Expected IDR->speclist format:
//
// palette "srcfile.lbm" { range_beg,range_end,fade_levels }
//
/*************************************************************/

PAL_class *PAL_construct(IDR_class *IDR) {
	PAL_class *PAL;
	BYTE *spec, *str, *tok;
	WORD i;

	PAL = (PAL_class*) mem_alloc(sizeof(PAL_class));

	PAL->IDR = IDR;

	spec = str = str_alloc(IDR->speclist);

	i = 0;
	while ((tok = strchr(str, ',')) != NULL) {
		str = tok + 1;

		if (i >= arysize(PAL->parms)) {
			PAL_error(IDR->RS, MSG_TMR, NULL);
			mem_free(PAL);
			mem_free(spec);
			return (NULL);
		}

		PAL->parms[i++] = atoi(str);
	}

	mem_free(spec);

	if (i < arysize(PAL->parms)) {
		PAL_error(IDR->RS, MSG_MRS, NULL);
		mem_free(PAL);
		return (NULL);
	}

	return (PAL);
}

/*************************************************************/
//
// Destroy a palette compiler class instance
//
/*************************************************************/

void PAL_destroy(PAL_class *PAL) {
	mem_free(PAL);
}

/*************************************************************/
//
// Compile a palette from a 256-color .LBM file
//
// Create 11 "fade tables" for palette, ranging from 0 to 100% in
// 10% increments,  Each fade table contains indexes of colors which
// are n% as bright as the original palette's colors
//
/*************************************************************/

void PAL_compile(PAL_class *PAL) {
	RF_entry_hdr RHDR;
	ULONG flen, dsize;
	UBYTE *file;
	WORD rngbeg, rngend, ncolors, i, j;
	PAL_HDR *phdr;
	RGB dim, *array, *cmap;
	BYTE *fade[11];
	void *pr;
	pal orig;

	file = (UBYTE *) read_file(PAL->IDR->fn, NULL);

	if (file == NULL) {
		PAL_error(PAL->IDR->RS, MSG_SNF, PAL->IDR->fn);
		return;
	}

	flen = file_size(PAL->IDR->fn);

	cmap = (RGB*) IFF_property("CMAP", file, flen);
	if (cmap == NULL) {
		PAL_error(PAL->IDR->RS, MSG_BFT, NULL);
		mem_free(file);
		return;
	}

	rngbeg = PAL->parms[PAL_RNGBEG];
	rngend = PAL->parms[PAL_RNGEND];
	ncolors = rngend - rngbeg + 1;

	dsize = sizeof(PAL_HDR) + (ncolors * sizeof(RGB)) + (11 * ncolors);

	pr = mem_alloc(dsize);

	phdr = (PAL_HDR*) pr;
	array = (RGB*) add_ptr(phdr, sizeof(PAL_HDR));

	for (i = 0; i < ncolors; i++) {
		array[i] = cmap[i + rngbeg];

		array[i].r >>= 2;
		array[i].g >>= 2;
		array[i].b >>= 2;
	}

	orig.word_size = 6;
	orig.array_size = ncolors;
	orig.colors = (rgb *) array;

	fade[0] = (BYTE*) add_ptr(array, ncolors * sizeof(RGB));
	phdr->fade[0] = sizeof(PAL_HDR) + (ncolors * sizeof(RGB));

	for (i = 1; i < 11; i++) {
		fade[i] = (BYTE*) add_ptr(fade[i - 1], ncolors);
		phdr->fade[i] = phdr->fade[i - 1] + ncolors;
	}

	phdr->ncolors = ncolors;
	phdr->RGB = sizeof(PAL_HDR);

	for (i = 0; i < 11; i++)
		for (j = 0; j < ncolors; j++) {
			dim = array[j];
			dim.r = (dim.r * i) / 10;
			dim.g = (dim.g * i) / 10;
			dim.b = (dim.b * i) / 10;

			fade[i][j] = remap_RGB((rgb *) &dim, &orig);
		}

	RHDR.data_attrib = PAL->IDR->attrib;
	RHDR.data_size = dsize;

	RF_write_entry(PAL->IDR->RS->RF, PAL->IDR->ord, pr, &RHDR, RTYP_RAW_MEM);

	mem_free(pr);
	mem_free(file);
}
