//����������������������������������������������������������������������������
//��                                                                        ��
//��  SYSTEM.C                                                              ��
//��                                                                        ��
//��  Implementation-specific resource module for AESOP development         ��
//��                                                                        ��
//��  V1.00 of 26-Feb-92 -- Initial version                                 ��
//��  V1.01 of 15-Apr-93 -- str_alloc() null pointer bug fixed              ��
//��  V1.02 of 12-Sep-93 -- don't assume EXX hi-bits == 0 after intdos      ��
//��                                                                        ��
//��  Project: Extensible State-Object Processor (AESOP/16)                 ��
//��   Author: John Miles                                                   ��
//��                                                                        ��
//��  C source compatible with Borland C++ 3.0                              ��
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

//#include <conio.h>
#include <stdio.h>
#include <malloc.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/io.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

//#include "c:\tools\x32\include\x32.h"

#include "defs.hpp"
#include "arcmsg.hpp"
#include "system.hpp"

WORD system_err = 0;

ULONG checksum;
WORD errorlevel;
WORD verbosity;
WORD delete_policy;

BYTE str_buf[1024];
ULONG msgcnt[8];
BYTE *output_fn;

//
// 8-bit ASCII character type reference tables
//

BYTE is_namechar[256] =       // letters, digits, underscore
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				0, // 0-31
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
				1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
				0, // 32-63
				0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
				1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
				1, // 64-95
				0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
				1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
				0, // 96-127
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 128-159
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 160-191
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 192-223
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // 224-255
		};

BYTE is_digit[256] =          // digits only
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				0, // 0-31
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
				1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
				0, // 32-63
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 64-95
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 96-127
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 128-159
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 160-191
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 192-223
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // 224-255
		};

BYTE is_whitespace[256] =     // cr,lf,htab,vtab,ff,spc
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				0, // 0-31
				1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 32-63
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 64-95
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 96-127
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 128-159
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 160-191
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, // 192-223
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // 224-255
		};

//
//  Compiler-specific memory services
// 

void *farmalloc(ULONG size) {
	return malloc(size);
}

void farfree(void *block) {
	free(block);
}

//
//  Heap services
//

void mem_init(void) {   //TODO: no longer necessary, let malloc handle it
	checksum = 0L;
}

void mem_shutdown(void) {
	if (checksum != 0L) {
		report(E_FAULT, NULL, MSG_HER);
		abend();
	}
}

ULONG mem_avail(void) {
	return 0;   //farcoreleft();
}

void *mem_alloc(ULONG bytes) {
	ULONG *ptr;

	ptr = (ULONG*) farmalloc(bytes);

	if (ptr == NULL) {
		system_err = OUT_OF_MEMORY;
		report(E_FATAL, NULL, MSG_OOM);
	}

	checksum ^= (ULONG) *ptr;

	return ptr;
}

BYTE *str_alloc(BYTE *str) {
	BYTE *ptr;

	if (str != NULL) {
		ptr = (BYTE*) mem_alloc(strlen(str) + 1);
		strcpy(ptr, str);
	} else {
		ptr = (BYTE*) mem_alloc(1);
		*ptr = 0;
	}

	return ptr;
}

void mem_free(void *ptr)
{
   //checksum ^= (ULONG*) ptr;

   farfree(ptr);
}
//
//  DOS services
//
void set_system_error(WORD errno) {
	system_err = errno;
}

WORD get_system_error(void) {
	return system_err;
}

WORD clear_system_error(void) {
	WORD i;
	i = system_err;
	system_err = 0;
	return i;
}

/***************************************************/
void r_read(FILE *handle, void *buf, UWORD len) {
	UWORD nbytes;

	nbytes = (UWORD) fread(buf, 1, len, handle);

	if (nbytes != len)
		report(E_FATAL, NULL, MSG_RCR);
}

void r_write(FILE *handle, void *buf, UWORD len) {
	UWORD nbytes;

	nbytes = (UWORD) fwrite(buf, 1, len, handle);

	if (nbytes != len)
		report(E_FATAL, NULL, MSG_RCW);
}

/*************************************************************/
LONG file_size(BYTE *filename) {
	FILE *handle;
	ULONG len;

	handle = fopen(filename, O_RDONLY);
	if (handle == NULL) {
		system_err = FILE_NOT_FOUND;
		return -1L;
	}

	//len = filelength(handle);
	fseek(handle, 0, SEEK_END);
	len = ftell(handle);

	if (len == -1L)
		system_err = CANT_READ_FILE;

	fclose(handle);
	return len;
}

/*************************************************************/
ULONG *read_file(BYTE *filename, void *dest) {
	WORD i;
	FILE *handle;
	ULONG len;
	ULONG *buf, *mem;

	len = file_size(filename);
	if (len == -1L) {
		system_err = FILE_NOT_FOUND;
		return NULL;
	}

	buf = mem = (dest == NULL) ? (ULONG*) mem_alloc(len) : (ULONG*) dest;

	handle = fopen(filename, "rb");
	if (handle == NULL) {
		free(mem);
		system_err = FILE_NOT_FOUND;
		return NULL;
	}

	while (len >= 4096L) {
		i = fread(buf, 1, 4096, handle);
		if (i != 4096) {
			free(mem);
			system_err = CANT_READ_FILE;
			return NULL;
		}
		len -= 4096L;
		buf = (ULONG*) add_ptr(buf, 4096L);
	}

	i = fread(buf, 1, (UWORD) len, handle);
	if (i != (UWORD) len) {
		free(mem);
		system_err = CANT_READ_FILE;
		return NULL;
	}

	fclose(handle);
	return mem;
}

/*************************************************************/
WORD write_file(BYTE *filename, void *buf, ULONG len) {
	WORD i;
	FILE *handle;

	handle = fopen(filename, "w+b");
	if (handle == NULL) {
		system_err = CANT_WRITE_FILE;
		return 0;
	}

	while (len >= 4096L) {
		i = fwrite(buf, 1, 4096, handle);
		if (i == -1) {
			system_err = CANT_WRITE_FILE;
			return 0;
		}
		if (i != 4096) {
			system_err = DISK_FULL;
			return 0;
		}
		len -= 4096L;
		buf = add_ptr(buf, 4096L);
	}

	i = fwrite(buf, 1, (UWORD) len, handle);
	if (i == -1) {
		system_err = CANT_WRITE_FILE;
		return 0;
	}
	if (i != (UWORD) len) {
		system_err = DISK_FULL;
		return 0;
	}

	fclose(handle);

	return 1;
}

/*************************************************************/
WORD append_file(BYTE *filename, void *buf, ULONG len) {
	WORD i;
	FILE *handle;

	handle = fopen(filename, "a+b");
	if (handle == NULL) {
		system_err = FILE_NOT_FOUND;
		return 0;
	}

	while (len >= 4096L) {
		i = fwrite(buf, 1, 4096, handle);
		if (i == -1) {
			system_err = CANT_WRITE_FILE;
			return 0;
		}
		if (i != 4096) {
			system_err = DISK_FULL;
			return 0;
		}
		len -= 4096L;
		buf = add_ptr(buf, 4096L);
	}

	i = fwrite(buf, 1, (UWORD) len, handle);
	if (i == -1) {
		system_err = CANT_WRITE_FILE;
		return 0;
	}
	if (i != (UWORD) len) {
		system_err = DISK_FULL;
		return 0;
	}

	fclose(handle);

	return 1;
}

/*************************************************************/
FILE *read_text_file(BYTE *filename) {
	static FILE *in;

	in = fopen(filename, "rt");
	if (in == NULL) {
		system_err = FILE_NOT_FOUND;
		return NULL;
	}

	return in;
}

/*************************************************************/
WORD read_text_line(FILE *in, UWORD maxlen, BYTE *string) {
	UWORD len;

	if (fgets(str_buf, sizeof(str_buf) - 1, in) == NULL) {
		if (ferror(in)) {
			clearerr(in);
			system_err = CANT_READ_FILE;
		} else
			system_err = EOF_REACHED;
		return 0;
	}

	len = strlen(str_buf);
	if ((len == (sizeof(str_buf) - 1)) || (len >= maxlen)) {
		system_err = LINE_TOO_LONG;
		return 0;
	}

	len = MIN(maxlen,len);
	strncpy(string, str_buf, len);
	string[len] = 0;

	return len;
}

/*************************************************************/
FILE *write_text_file(BYTE *filename) {
	static FILE *out;

	out = fopen(filename, "w+t");
	if (out == NULL) {
		system_err = CANT_WRITE_FILE;
		return NULL;
	}

	return out;
}

/*************************************************************/
WORD write_text_line(FILE *out, BYTE *string) {
	if (!strlen(string))
		return 1;

	if (fputs(string, out) == EOF) {
		system_err = CANT_WRITE_FILE;
		return 0;
	}

	if (ferror(out)) {
		clearerr(out);
		system_err = CANT_WRITE_FILE;
		return 0;
	}

	return 1;
}

/*************************************************************/
void close_text_file(FILE *text) {
	fclose(text);
}

/*************************************************************/
WORD verify_file(BYTE *filename) {
	/* TODO: this will not work in linux, requires dos.h
	 // http://www.linuxquestions.org/questions/programming-9/linux-equivalent-to-'union-regs'-in-dos-h-646334/
	 union REGS in,out;
	 struct SREGS segs;

	 in.x.eax = 0x4300;
	 in.x.edx = FP_OFF(filename);
	 segs.ds = segs.es = FP_SEG(filename);
	 intdosx(&in,&out,&segs);

	 return !out.x.cflag;       // return AX=0 if C=1
	 */
	return 0;
}

/*************************************************************/
void *IFF_property(BYTE *name, UBYTE *file, LONG flen) {
	LONG len;

	file += 12;

	do {
		while (*file == 0) {
			file++;
			flen--;
		}

		if (!strncasecmp((BYTE *) file, name, 4))
			return (file + 8);

		len = ((((ULONG) *(file + 4)) << 24) | (((ULONG) *(file + 5)) << 16)
				| (((ULONG) *(file + 6)) << 8) | (((ULONG) *(file + 7)))) + 8;

		file += len;
		flen -= len;
	} while (flen > 12);

	return NULL;
}

/*************************************************************/
//
// Generate name for new temporary file in specified DOS path
// (NULL to use current working directory)
//
/*************************************************************/

BYTE *temp_filename(BYTE *path) {
	BYTE *fn;
	BYTE *fp;
	UWORD n;

	fn = (BYTE*) mem_alloc(256L);

	n = 0;
	do {
		fp = fn;

		if (path != NULL) {
			strcpy(fp, path);
			strcat(fp, "\\");
			fp = &fn[strlen(fn)];
		} else
			fn[0] = NULL;

		sprintf(fp, "TEMPFILE.%.03u", n);
		n = (n + 1) % 1000;
	} while (verify_file(fn));

	return fn;
}

/*************************************************************/
void remove_tempfile(BYTE *filename) {
	if (delete_policy)
		unlink(filename);

	free((ULONG*) filename);
}

/*************************************************************/
void set_temp_deletion_policy(WORD dp) {
	delete_policy = dp;
}

/*************************************************************/
//
// AESOP timestamp format:
//
// 0..4    Second/2   [0..28]
// 5..10   Minute     [0..59]
// 11..15  Hour       [0..23]
// 16..20  Day        [1..31]
// 21..24  Month      [1..12]
// 25..31  Year-1980  [0..127]
//
BYTE *ASCII_time(ULONG timestamp) {
	ULONG sec, min, hr, day, mon, yr;

	static BYTE text[21];
	static BYTE *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
			"Aug", "Sep", "Oct", "Nov", "Dec" };

	sec = 2L * (timestamp & 0x1fL);
	timestamp >>= 5;

	min = timestamp & 0x3fL;
	timestamp >>= 6;

	hr = timestamp & 0x1fL;
	timestamp >>= 5;

	day = timestamp & 0x1fL;
	timestamp >>= 5;

	mon = (timestamp & 0x0fL) - 1L;
	timestamp >>= 4;

	yr = (timestamp & 0x7fL) + 1980L;

	sprintf(text, "%.02lu-%s-%.04lu %.02lu:%.02lu:%.02lu", day,
			months[(UWORD) mon], yr, hr, min, sec);

	return text;
}

ULONG current_time(void) {
	/* TODO: this will not work in linux, requires dos.h
	 union REGS in,out;
	 UWORD dtime,ddate;

	 in.h.ah = 0x2c;
	 intdos(&in,&out);

	 dtime = (out.h.dh >> 1) |
	 (out.h.cl << 5) |
	 (out.h.ch << 11);

	 in.h.ah = 0x2a;
	 intdos(&in,&out);

	 out.x.ecx &= 0xffff;

	 ddate = (out.h.dl) |
	 (out.h.dh << 5) |
	 ((out.x.ecx - 1980) << 9);

	 return (ULONG) dtime + ((ULONG) ddate << 16);
	 */
	return 0;
}

ULONG file_time(BYTE *filename) {
	/* TODO: this will not work in linux, requires dos.h
	 union REGS in,out;
	 FILE *handle;

	 handle = fopen(filename,O_RDONLY);

	 if (handle==NULL) return 0L;

	 in.x.eax = 0x5700;
	 in.x.ebx = handle;
	 intdos(&in,&out);

	 out.x.ecx &= 0xffff;
	 out.x.edx &= 0xffff;

	 fclose(handle);

	 return (ULONG) out.x.ecx + ((ULONG) out.x.edx << 16);
	 */
	return 0;
}

WORD set_file_time(BYTE *filename, ULONG timestamp) {
	/* TODO: this will not work in linux, requires dos.h
	 union REGS in,out;
	 FILE *handle;

	 handle = fopen(filename,O_APPEND | O_RDWR);

	 if (handle==NULL)
	 {
	 system_err = FILE_NOT_FOUND;
	 return 0;
	 }

	 in.x.eax = 0x5701;
	 in.x.ebx = handle;
	 in.x.ecx = (UWORD) (timestamp & 0x0000ffffL);
	 in.x.edx = (UWORD) (timestamp >> 16);
	 intdos(&in,&out);

	 fclose(handle);

	 return !out.x.cflag;
	 */
	return 0;
}

/*************************************************************/
//
// Misc. services
//
WORD log2(ULONG value) {
	WORD n;
	ULONG pwr;

	for (pwr = 1, n = 0; n < 32; n++) {
		if (value == pwr)
			return n;

		pwr <<= 1;
	}

	return -1;
}

/*************************************************************/
ULONG ascnum(BYTE *string, UWORD base) {
	UWORD i, j;
	ULONG total = 0L;

	for (i = 0; i < strlen(string); i++) {
		for (j = 0; j < base; j++)
			if (toupper(string[i]) == "0123456789ABCDEF"[j]) {
				total = (total * (ULONG) base) + (ULONG) j;
				break;
			}
		if (j == base)
			return total;
	}

	return total;
}

/*************************************************************/
BYTE *str(ULONG value) {
	BYTE temp[64];

	//ultoa(value,temp,10);
	sprintf(temp, "%d", value);
	//snprintf(target_string, size_of_target_string_in_bytes, "%d", source_int);

	return str_alloc(temp);
}

/*************************************************************/
void set_verbosity(WORD v) {
	verbosity = v;
}

/*************************************************************/
WORD verbose(void) {
	return verbosity;
}

/*************************************************************/
void set_errorlevel(WORD errlvl) {
	UWORD i;

	for (i = 0; i < 8; i++)
		msgcnt[i] = 0L;

	output_fn = NULL;

	errorlevel = errlvl;
}

/*************************************************************/
void set_output_filename(BYTE *filename) {
	output_fn = filename;
}

/*************************************************************/
void abend(void) {
	fcloseall();

	if (output_fn != NULL)
		unlink(output_fn);

	exit(1);
}

/*************************************************************/
void report(UWORD errtype, BYTE *prefix, BYTE *msg, ...) {
	va_list argptr;
	static BYTE *errtypes[] = { MSG_NOTICE, MSG_WARNING, MSG_ERROR, MSG_FATAL,
			MSG_FAULT };
	++msgcnt[errtype];

	if (errtype >= arysize(errtypes))
		return;

	if (errtype < errorlevel)
		return;

	printf(errtypes[errtype]);
	if (prefix != NULL)
		printf(" %s", prefix);
	else
		printf(": ");

	va_start(argptr, msg);
	vprintf(msg, argptr);
	va_end(argptr);
	printf("\n");

	if ((errtype == E_FATAL) || (errtype == E_FAULT))
		abend();
}

/*************************************************************/
ULONG error_message_count(void) {
	return msgcnt[E_ERROR] + msgcnt[E_FATAL] + msgcnt[E_FAULT];
}

/*************************************************************/
static BYTE *summarize(ULONG num) {
	static BYTE buf[64];

	if (!num)
		strcpy(buf, MSG_NON);
	else
		sprintf(buf, "%d", num);
	//ultoa(num,buf,10);

	return buf;
}

/*************************************************************/
void summary(void) {
	printf("\n");
	printf(MSG_CP_SML, msgcnt[E_NEWLINE]);
	printf(MSG_CP_RCO, msgcnt[E_RESCOMP]);
	printf(MSG_CP_RCC, msgcnt[E_RESNEW]);
	printf(MSG_CP_SME, summarize(error_message_count()));
	printf(MSG_CP_SMW, summarize(msgcnt[E_WARN]));
}

/*************************************************************/
void * norm(void *farptr) {
	return farptr;
}

void * add_ptr(ULONG *farptr, LONG offset) {
	return (void *) (((ULONG*) farptr) + offset);
}

LONG ptr_dif(ULONG *sub2, ULONG *sub1) {
	return (LONG) ((ULONG*) sub2 - (ULONG*) sub1);
}
