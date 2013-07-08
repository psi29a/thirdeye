//
// AESOP compiler system services
//

#ifndef SYSTEM_H
#define SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

// Static system data

extern BYTE is_whitespace[256];
extern BYTE is_digit[256];
extern BYTE is_namechar[256];

// Universal system error codes & types

#define E_NOTICE        0
#define E_WARN          1
#define E_ERROR         2
#define E_FATAL         3
#define E_FAULT         4
#define E_RESCOMP       5
#define E_RESNEW        6
#define E_NEWLINE       7

#define NO_ERROR        0
#define IO_ERROR        1
#define OUT_OF_MEMORY   2
#define FILE_NOT_FOUND  3
#define CANT_WRITE_FILE 4
#define CANT_READ_FILE  5
#define DISK_FULL       6

#define LINE_TOO_LONG   7
#define EOF_REACHED     8

// Misc. macros
#define arysize(x) (sizeof((x)) / sizeof((x)[0]))
#define MIN(a,b) \
({ __typeof__ (a) _a = (a); \
   __typeof__ (b) _b = (b); \
 _a < _b ? _a : _b; })
#define MAX(a,b) \
({ __typeof__ (a) _a = (a); \
   __typeof__ (b) _b = (b); \
 _a > _b ? _a : _b; })

// Memory heap management

void mem_init(void);
void mem_shutdown(void);
ULONG mem_avail(void);
void *mem_alloc(ULONG bytes);
BYTE *str_alloc(BYTE *string);
void mem_free(void *ptr);

// DOS

WORD get_system_error(void);
void set_system_error(WORD errno);
WORD clear_system_error(void);

void r_read(WORD handle, void *buf, UWORD len);
void r_write(WORD handle, void *buf, UWORD len);

BYTE *temp_filename(BYTE *path);
void remove_tempfile(BYTE *filename);
void set_temp_deletion_policy(WORD delete_policy);

WORD verify_file(BYTE *filename);
LONG file_size(BYTE *filename);
ULONG *read_file(BYTE *filename, void *dest);
BYTE *load_driver(BYTE *filename);
WORD write_file(BYTE *filename, void *src, ULONG len);
WORD append_file(BYTE *filename, void *src, ULONG len);

FILE *read_text_file(BYTE *filename);
WORD read_text_line(FILE *in, UWORD maxlen, BYTE *string);
FILE *write_text_file(BYTE *filename);
WORD write_text_line(FILE *out, BYTE *string);
void close_text_file(FILE *text);

void *IFF_property(BYTE *name, UBYTE *file, LONG flen);

BYTE *ASCII_time(ULONG timestamp);
ULONG current_time(void);
ULONG file_time(BYTE *filename);
WORD set_file_time(BYTE *filename, ULONG timestamp);

// Misc.

void * norm(void *farptr);
void * add_ptr(void *farptr, LONG offset);
LONG ptr_dif(void *sub2, void *sub1);

WORD log2(ULONG value);

ULONG ascnum(BYTE *string, UWORD base);
BYTE *str(ULONG value);
void set_verbosity(WORD v);
WORD verbose(void);
void set_errorlevel(WORD errlvl);
void set_output_filename(BYTE *filename);
ULONG error_message_count(void);
void report(UWORD errtype, BYTE *prefix, BYTE *msg, ...);
void summary(void);
void abend(void);

#ifdef __cplusplus
}
#endif

#endif
