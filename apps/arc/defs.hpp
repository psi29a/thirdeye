//
//  AESOP common definitions for 32-bit DOS
//
//  (Used only by ARC)
//

#ifndef DEFS_H
#define DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned short UWORD;
typedef unsigned char UBYTE;
typedef unsigned int ULONG;
typedef short WORD;
typedef char BYTE;
typedef int LONG;

typedef UWORD HRES;           // run-time resource.hppandle
typedef UWORD HSTR;           // run-time len-prefixed string descriptor

#define MSG_CREATE  0         // predefined message tokens (sent by system)
#define MSG_DESTROY 1
#define MSG_RESTORE 2

#define MAX_G 16              // Maximum depth of "family trees"
typedef struct {
	HRES thunk;
} IHDR;                         // Instance header

typedef struct {
	UWORD MV_list;
	UWORD max_msg;
	UWORD SD_list;
	UWORD nprgs;
	UWORD isize;
	UWORD use_cnt;
} THDR;                         // Thunk header

typedef struct {
	UWORD static_size;
	ULONG imports;
	ULONG exports;
	ULONG parent;
} PRG_HDR;                      // SOP program header

typedef struct {
	UWORD auto_size;
} MHDR;                         // Message handler header

typedef struct {
	UWORD msg;
	UWORD handler;
	UWORD SD_offset;
} MV_entry;                     // Thunk message vector list entry

typedef struct {
	HRES SOP;
	ULONG exports;
	UWORD static_base;
	UWORD extern_base;
} SD_entry;                     // Thunk SOP descriptor list entry

typedef struct {
	UWORD ncolors;
	UWORD RGB;
	UWORD fade[11];
} PAL_HDR;                      // Palette resource.hppeader

typedef struct {
	UBYTE r;
	UBYTE g;
	UBYTE b;
} RGB;                          // VGA RGB triplet

#ifdef __cplusplus
}
#endif

#endif
