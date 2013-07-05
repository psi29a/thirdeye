//
//  General-purpose dictionary class definitions
//

#ifndef DICT_H
#define DICT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct entry {
	void *def;                 // public
	BYTE *tag;                 // public

	UWORD attr;                // private
	struct entry *next;        // private
} DICT_entry;                   // dictionary entry object

typedef struct {
	DICT_entry **root;         // private
	UWORD hash_size;           // private
	WORD touched;              // private
} DICT_class;                   // dictionary base class

typedef struct {
	DICT_class *base;          // private
	UWORD bucket;              // private
	DICT_entry *cur;           // private
} DI_class;                     // serial dictionary reader (iterator) class

#define D_DEFHEAP  0x8000     // entry attribute: definition object on heap
#define DC_LINEAR  1          // hash size = 1 to store as single linear array
DICT_class *DICT_construct(UWORD hash_size);
void DICT_destroy(DICT_class *DICT);

DICT_entry *DICT_lookup(DICT_class *DICT, BYTE *name);
void DICT_delete(DICT_class *DICT, BYTE *name);
DICT_entry *DICT_enter(DICT_class *DICT, BYTE *name, UWORD attributes);
WORD DICT_touched(DICT_class *DICT);
void DICT_show(DICT_class *DICT);
void DICT_copy(DICT_class *src, DICT_class *dest);
void DICT_wipe(DICT_class *DICT);
WORD DICT_compare(DICT_class *d1, DICT_class *d2);

DI_class *DI_construct(DICT_class *DICT);
void DI_destroy(DI_class *DI);
DICT_entry *DI_fetch(DI_class *DI);

#ifdef __cplusplus
}
#endif

#endif
