//
//  AESOP ARC resource script compiler class
//
//  Requires classes PP,RF,DICT,LEXAN
//

#ifndef RSCOMP_H
#define RSCOMP_H

#ifdef __cplusplus
extern "C" {
#endif

#define DEF_C_THRESHOLD 25
#define RS_SUFFIX      ".SCR"
#define RES_SUFFIX     ".RES"

#define RS_REBUILD       0x0001 // Force creation of new resource file
#define RS_SHOWBRIEF     0x0002 // Show brief list of resource file contents
#define RS_SHOWVERBOSE   0x0004 // Show verbose list of resource file contents
#define RS_NOUNIQUECHECK 0x0008 // Do not verify unique resource names
#define RDAT_NTYPES  8          // # of resource-specifier types
#define RDAT_SEQ     0
#define RDAT_SAM     1
#define RDAT_STR     2
#define RDAT_SRC     3
#define RDAT_DOC     4
#define RDAT_FIL     5
#define RDAT_MAP     6
#define RDAT_PAL     7

#define CR_VECTOR_SIZE 4   // Size of code resource function address
#define NDICTS 5           // # of principal dictionaries
#define ROED 0             // Resource Ordinal Entry Directory
#define RDES 1             // Resource Description Directory
#define RDEP 2             // Resource Dependency Directory
#define CRFD 3             // Code Resource Function Directory
#define MSGD 4             // Message Name Directory
typedef struct {
	PP_class *PP;
	RF_class *RF;
	TS_class *TS;
	LEX_class *LEX;
	DICT_class *dict[NDICTS];
	DICT_class *refcr;
	DICT_class *predef;
	DICT_class *depend;
	DICT_class *names;
	ULONG RES_time;
	ULONG attribs[RDAT_NTYPES];
	BYTE *RES_fn;
	BYTE *SCR_fn;
	BYTE *tfile;
	UWORD flags;
	WORD c_threshold;
} RS_class;                  // Resource script description

typedef struct {
	WORD type;
	BYTE *name;
	BYTE *fn;
	BYTE *speclist;
	ULONG attrib;
	ULONG ord;
	RS_class *RS;
} IDR_class;                 // Indirect resource description

RS_class *RS_construct(BYTE *SCR_filename, BYTE *RES_filename,
		DICT_class *predef, WORD c_threshold, UWORD flags);
void RS_destroy(RS_class *RS);

void RS_compile(RS_class *RS);
void RS_update_RDEP(RS_class *RS, BYTE *resname, DICT_class *filelist);
ULONG RS_get_ROED_entry(RS_class *RS, BYTE *resname);
ULONG RS_current_ROED_entry(RS_class *RS, BYTE *resname);
UWORD RS_get_MSGD_entry(RS_class *RS, BYTE *msgname);
void RS_show_contents(RS_class *RS, UWORD verbose);

void RS_syntax_error(RS_class *RS);
void RS_reserved_word_error(RS_class *RS);
WORD RS_next_attribute_specifier(RS_class *RS);
ULONG RS_parse_attribute_list(RS_class *RS);
WORD RS_check_name(RS_class *RS, BYTE *resname);

void IDR_show(IDR_class *IDR);

#ifdef __cplusplus
}
#endif

#endif
