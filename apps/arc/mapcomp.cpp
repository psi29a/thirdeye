//����������������������������������������������������������������������������
//��                                                                        ��
//��  MAPCOMP.C                                                             ��
//��                                                                        ��
//��  AESOP Cartesian map compiler class                                    ��
//��                                                                        ��
//��  Version: 1.00 of 30-Aug-92 -- Initial version                         ��
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
#include "mapcomp.hpp"
#include "arcmsg.hpp"

/*************************************************************/
//
// Report error during map compilation
//
/*************************************************************/

void MAP_error(RS_class *RS, BYTE *msg, BYTE *arg) {
	report(E_ERROR, LEX_line(RS->LEX, LEX_CUR), msg, arg);
}

/*************************************************************/
//
// Fetch unpacked scan line from .LBM file
//
/*************************************************************/

void LBM_fetch_line(UBYTE **ptr, UBYTE *buffer, UWORD width) {
	WORD x, i, color;
	UBYTE *body;
	BYTE token;

	body = (UBYTE*) norm(*ptr);

	x = 0;
	do {
		token = *body++;

		if (token == (BYTE) 0x80)
			continue;

		if (token >= 0) {
			for (i = 0; i <= token; i++)
				buffer[x++] = *body++;
		} else {
			color = *body++;

			for (i = 0; i <= -token; i++)
				buffer[x++] = color;
		}
	} while (x < width);

	*ptr = (UBYTE*) norm(body);
}

/*************************************************************/
//
// Create a new Cartesian map compiler class instance
//
// Expected IDR->speclist format:
//
// map "srcfile.lbm" { xsize,ysize,org_x,org_y,cell_x,cell_y }
//
// Note: Source .LBM file must be in 256-color DPaint IIe PBM ("new")
// format!
//
/*************************************************************/

MAP_class *MAP_construct(IDR_class *IDR) {
	MAP_class *MAP;
	BYTE *spec, *str, *tok;
	WORD i;

	MAP = (MAP_class*) mem_alloc(sizeof(MAP_class));

	MAP->IDR = IDR;

	spec = str = str_alloc(IDR->speclist);

	i = 0;
	while ((tok = strchr(str, ',')) != NULL) {
		str = tok + 1;

		if (i >= arysize(MAP->parms)) {
			MAP_error(IDR->RS, MSG_TMR, NULL);
			mem_free(MAP);
			mem_free(spec);
			return NULL;
		}

		MAP->parms[i++] = atoi(str);
	}

	mem_free(spec);

	if (i < arysize(MAP->parms)) {
		MAP_error(IDR->RS, MSG_MRS, NULL);
		mem_free(MAP);
		return NULL;
	}

	return MAP;
}

/*************************************************************/
//
// Destroy a Cartesian map compiler class instance
//
/*************************************************************/

void MAP_destroy(MAP_class *MAP) {
	mem_free(MAP);
}

/*************************************************************/
//
// Compile a Cartesian map from a 256-color DPaint IIe .LBM file
//
// See note on MAP_construct() above
//
/*************************************************************/

void MAP_compile(MAP_class *MAP) {
	RF_entry_hdr RHDR;
	ULONG flen;
	UBYTE *buffer;
	UBYTE *map;
	UBYTE *file;
	UBYTE *body;
	UBYTE *prop;
	UWORD width, height;
	UWORD x, y, ox, oy, dx, dy, sx, sy, m, i;

	file = (UBYTE *) read_file(MAP->IDR->fn, NULL);

	if (file == NULL) {
		MAP_error(MAP->IDR->RS, MSG_SNF, MAP->IDR->fn);
		return;
	}

	flen = file_size(MAP->IDR->fn);

	prop = (UBYTE*) IFF_property("BMHD", file, flen);
	if (prop == NULL) {
		MAP_error(MAP->IDR->RS, MSG_BFT, NULL);
		mem_free(file);
		return;
	}

	width = ((*(UBYTE *) prop) * 256) + (*(UBYTE *) (prop + 1));
	height = ((*(UBYTE *) (prop + 2)) * 256) + (*(UBYTE *) (prop + 3));

	body = (UBYTE*) IFF_property("BODY", file, flen);
	if (prop == NULL) {
		MAP_error(MAP->IDR->RS, MSG_BFT, NULL);
		mem_free(file);
		return;
	}

	ox = MAP->parms[MSP_ORGX];
	oy = MAP->parms[MSP_ORGY];
	dx = MAP->parms[MSP_CELLX];
	dy = MAP->parms[MSP_CELLY];

	sx = MAP->parms[MSP_XSIZE];
	sy = MAP->parms[MSP_YSIZE];

	buffer = (UBYTE*) mem_alloc(width);
	map = (UBYTE*) mem_alloc(sx * sy);

	m = 0;

	for (y = 0; y < height; y++) {
		LBM_fetch_line(&body, buffer, width);

		if (y == oy) {
			oy += dy;

			for (x = ox, i = 0; i < sx; x += dx, i++)
				map[m++] = buffer[x] - 1;

			if (!--sy)
				break;
		}
	}

	RHDR.data_attrib = MAP->IDR->attrib;
	RHDR.data_size = m;

	RF_write_entry(MAP->IDR->RS->RF, MAP->IDR->ord, map, &RHDR, RTYP_RAW_MEM);

	mem_free(map);
	mem_free(buffer);
	mem_free(file);
}
