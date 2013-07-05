//
//  AESOP state-object program compiler class
//
//  Requires classes PP,RF,DICT,LEXAN,RS
//

#ifndef SOPCOMP_H
#define SOPCOMP_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NEST 16        // Maximum loop nesting count
#define MAX_DIMS 16        // Maximum # of array dimensions
#define MAX_CODE 32768U    // Maximum code object size (up to 65536; reduce
// to save memory)
typedef struct {
	RS_class *RS;
	BYTE *fn;
	BYTE *name;
	ULONG attrib;
	BYTE *tfile;
	LEX_class *LEX;
	DICT_class *depend;
	DICT_class *EXPT;
	DICT_class *IMPT;
	DICT_class *AUTO;
	DICT_class *ARGV;
	DICT_class *STAT;
	DICT_class *TABL;
	DICT_class *XTRN;
	DICT_class *PROC;
	DICT_class *CASE;
	DICT_class *DUSE;
	DICT_class *VUSE;
	DICT_class *ADUS;
	DICT_class *AVUS;
	DICT_class *APUS;
	ULONG parent;
	void *CODE;
	UWORD PC;
	UWORD import_index;
	UWORD auto_index;
	UWORD static_index;
	UWORD EOF_reached;
	UWORD b_stack[MAX_NEST];
	UWORD c_stack[MAX_NEST];
	UWORD *bsp;
	UWORD *csp;
	UWORD context;
	UWORD ntype;
} SOP_class;

SOP_class *SOP_construct(RS_class *RS, ULONG def_attribs);
void SOP_destroy(SOP_class *SOP);

WORD SOP_test(SOP_class *SOP);
void SOP_compile(SOP_class *SOP);

#ifdef __cplusplus
}
#endif

#endif
