//
//  AESOP ARC preprocessor class
//
//  Requires class DICT
//

#ifndef PREPROC_H
#define PREPROC_H

#ifdef __cplusplus
extern "C" {
#endif

#define PP_TXTLIT   0x8000    // allow text literals in [] brackets
#define PP_KEEP_WS  0x4000    // preserve leading whitespace
typedef struct {
	BYTE *name;                // private
	FILE *handle;              // private
	ULONG line;                // private
} TF_info;                      // (text file information)

typedef struct {
	WORD *stack;               // private
	WORD depth;                // private
	WORD condition;            // private
} IF_class;                     // (conditional directive management class)

typedef struct {
	TF_info parent;            // private
	TF_info cur;               // private
	TF_info out;               // private
	DICT_class *MAC;           // private
	IF_class *IF;              // private
	BYTE *inbuf;               // private
	BYTE *outbuf;              // private
	WORD depth;                // private
	WORD tl_flag;              // private
	WORD ws_flag;              // private
} PP_class;                     // preprocessor main class

PP_class *PP_construct(BYTE *in_fn, BYTE *out_fn, DICT_class *init_macs,
		UWORD attribs);
void PP_process(PP_class *PP);
void PP_destroy(PP_class *PP);

#ifdef __cplusplus
}
#endif

#endif
