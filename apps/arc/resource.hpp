//
//  AESOP ARC resource management class
//
//  Requires classes RF, DICT
//

#ifndef RESOURCE_H
#define RESOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	DICT_class *cache;         // private
} TS_class;                     // DOS timestamp cache class

typedef struct {
	BYTE *string;              // private
	BYTE *cur;                 // private
} CSS_class;                    // comma-separated string class

TS_class *TS_construct(void);
void TS_invalidate(TS_class *TS, BYTE *filename);
ULONG TS_file_time(TS_class *TS, BYTE *filename);
ULONG TS_latest_file_time(TS_class *TS, BYTE *filelist);
void TS_destroy(TS_class *TS);

ULONG RES_store_resource(RF_class *RF, ULONG entry, void *source,
		RF_entry_hdr *RHDR, UWORD type);

UWORD RES_read_resource(RF_class *RF, ULONG entry, void *dest,
		RF_entry_hdr *RHDR, UWORD type);

ULONG RES_storage_timestamp(RF_class *RF, ULONG entry);

ULONG DICT_save(DICT_class *DICT, RF_class *RF, ULONG entry);
void DICT_load(DICT_class *DICT, RF_class *RF, ULONG entry);

BYTE *DICT_build_tag_string(DICT_class *DICT);

CSS_class *CSS_construct(BYTE *string);
void CSS_destroy(CSS_class *CSS);
BYTE *CSS_fetch_string(CSS_class *CSS);
ULONG CSS_fetch_num(CSS_class *CSS);
void CSS_rewind(CSS_class *CSS);
void CSS_add_string(CSS_class *CSS, BYTE *string);
void CSS_add_num(CSS_class *CSS, ULONG num);
BYTE *CSS_string(CSS_class *CSS);

#ifdef __cplusplus
}
#endif

#endif
