//����������������������������������������������������������������������������
//��                                                                        ��
//��  RESOURCE.C                                                            ��
//��                                                                        ��
//��  Resource management functions for AESOP ARC compiler                  ��
//��                                                                        ��
//��  Version: 1.00 of 15-Apr-92 -- Initial version                         ��
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "defs.hpp"
#include "system.hpp"
#include "resfile.hpp"
#include "arcmsg.hpp"
#include "dict.hpp"
#include "resource.hpp"

#define BLK_SIZE 32768L

/***************************************************/
//
// Write resource to file, returning entry index or -1 if error
//
//     RF: Resource file class
// 
//  entry: Resource file entry number
// 
// source: Pointer to source data or descriptor
// 
//   RHDR: Pointer to resource.hppeader containing data size and attributes
// 
//   type: RTYP value (see RESFILE.H)
// 
/***************************************************/

ULONG RES_store_resource(RF_class *RF, ULONG entry, void *source,
		RF_entry_hdr *RHDR, UWORD type) {
	WORD file;
	UBYTE *ptr;
	ULONG len;
	WORD err;

	err = 0;

	r_write(RF->file, RHDR, sizeof(RF_entry_hdr));

	len = RHDR->data_size;
	if (!len)
		return entry;

	if (RHDR->data_attrib & DA_PLACEHOLDER)
		return entry;

	switch (type) {
	case RTYP_HOUSECLEAN:
		file = *(WORD *) source;
		ptr = (UBYTE*) mem_alloc(BLK_SIZE);

		while (len > BLK_SIZE) {
			r_read(file, ptr, (UWORD) BLK_SIZE);
			r_write(RF->file, ptr, (UWORD) BLK_SIZE);
			len -= BLK_SIZE;
		}
		r_read(file, ptr, (UWORD) len);
		r_write(RF->file, ptr, (UWORD) len);

		mem_free(ptr);
		break;

	case RTYP_DICTIONARY:
	case RTYP_RAW_MEM:
		ptr = (UBYTE*) source;

		while (len > BLK_SIZE) {
			r_write(RF->file, ptr, (UWORD) BLK_SIZE);
			len -= BLK_SIZE;
			ptr = (UBYTE*) add_ptr(ptr, BLK_SIZE);
		}
		r_write(RF->file, ptr, (UWORD) len);
		break;

	case RTYP_RAW_FILE:
		file = open((BYTE*)source, O_RDWR);
		ptr = (UBYTE*) mem_alloc(BLK_SIZE);

		while (len > BLK_SIZE) {
			r_read(file, ptr, (UWORD) BLK_SIZE);
			r_write(RF->file, ptr, (UWORD) BLK_SIZE);
			len -= BLK_SIZE;
		}
		r_read(file, ptr, (UWORD) len);
		r_write(RF->file, ptr, (UWORD) len);

		mem_free(ptr);
		close(file);
		break;
	}

	if (!err)
		return entry;
	else
		return (ULONG) -1;
}

/***************************************************/
//
// Read resource from file, returning FALSE if error
//
//     RF: Resource file class
// 
//  entry: Resource file entry number
// 
// source: Pointer to destination data or descriptor
// 
//   RHDR: Pointer to resource.hppeader structure to receive 
//         data size and attributes
// 
//   type: RTYP value (see RESFILE.H)
//         (Currently only RTYP_DICTIONARY is valid)
// 
/***************************************************/

UWORD RES_read_entry(RF_class *RF, ULONG entry, void *dest, RF_entry_hdr *RHDR,
		UWORD type) {
	static RF_entry_hdr header;
	ULONG len, hl;
	UWORD tl, dl, n;
	BYTE *tag;
	DICT_entry *cur;

	if (RF_flags(RF, entry) & (SA_UNUSED | SA_DELETED))
		return 0;

	header = *RF_header(RF, entry);
	if (RHDR != NULL)
		*RHDR = header;

	len = header.data_size;

	switch (type) {
	case RTYP_DICTIONARY:
		r_read(RF->file, &n, sizeof(UWORD));
		len -= ((ULONG) sizeof(UWORD) * 2L);

		hl = (ULONG) n * (ULONG) sizeof(ULONG);
		len -= hl;

		lseek(RF->file, hl, SEEK_CUR);

		while (len) {
			r_read(RF->file, &tl, sizeof(tl));
			len -= (ULONG) (sizeof(tl) + tl);

			if (!tl)
				continue;

			tag = (BYTE*) mem_alloc(tl);
			r_read(RF->file, tag, (UWORD) tl);

			cur = DICT_enter( (DICT_class*) dest, tag, D_DEFHEAP);
			mem_free(tag);

			r_read(RF->file, &dl, sizeof(dl));
			len -= (ULONG) (sizeof(dl) + dl);

			if (dl) {
				cur->def = mem_alloc((ULONG) dl);
				r_read(RF->file, cur->def, (UWORD) dl);
			} else
				cur->def = NULL;
		}
		break;

	default:
		return 0;
	}

	return 1;
}

/*************************************************************/
//
// Return storage timestamp for resource entry, or 0 if invalid or 
// created by reference only
//
/*************************************************************/

ULONG RES_storage_timestamp(RF_class *RF, ULONG entry) {
	RF_entry_hdr *RHDR;

	if (RF_flags(RF, entry) & (SA_DELETED | SA_UNUSED))
		return 0L;

	RHDR = RF_header(RF, entry);

	if (RHDR->data_attrib & DA_PLACEHOLDER)
		return 0L;

	return RHDR->timestamp;
}

/***************************************************/
//
// Save the contents of a dictionary (DICT_class instance) as a resource,
// returning entry index
//
// Create new entry for dictionary if entry == -1L
//
// Dictionary definitions are expected to be either NULL or pointers to
// zero-terminated ASCII strings
//
// Dictionary resource storage format:
//
//   struct header
//   {
//      UWORD hash_size;
//      ULONG chain[hash_size]; // offsets from start-of-header
//   }
//   
//   struct chain_entry
//   {
//      UWORD tag_len;          // (end of chain if 0)
//      UBYTE tag[tag_len];
//   
//      UWORD def_len;          // (no definition if 0)
//      UBYTE def[def_len];
//   }
// 
/***************************************************/

ULONG DICT_save(DICT_class *DICT, RF_class *RF, ULONG entry) {
	RF_entry_hdr RHDR;
	ULONG saved, total_len, nchains;
	UWORD i;
	DICT_entry *cur;
	DI_class *DI;
	BYTE *ptr, *base;
	ULONG *offset;

	nchains = 0L;
	for (i = 0; i < DICT->hash_size; i++)
		if (DICT->root[i] != NULL)
			++nchains;

	total_len = nchains * (ULONG) sizeof(UWORD);

	DI = DI_construct(DICT);

	total_len += ((ULONG) sizeof(UWORD)) + ((ULONG) sizeof(UWORD))
			+ ((ULONG) DICT->hash_size * (ULONG) sizeof(ULONG));

	while ((cur = DI_fetch(DI)) != NULL) {
		total_len += (ULONG) ((strlen(cur->tag) + 1) + (2 * sizeof(UWORD)));
		if (cur->def != NULL)
			total_len += (ULONG) (strlen((BYTE*)cur->def) + 1);
	}

	DI_destroy(DI);

	ptr = base = (BYTE*) mem_alloc(total_len);

	*(UWORD *) ptr = DICT->hash_size;
	ptr += sizeof(UWORD);

	offset = (ULONG *) ptr;
	ptr = (BYTE*) add_ptr(ptr, ((ULONG) sizeof(ULONG) * (ULONG) DICT->hash_size));

	for (i = 0; i < DICT->hash_size; i++) {
		cur = DICT->root[i];

		if (cur == NULL) {
			offset[i] = 0L;
			continue;
		}

		offset[i] = ptr_dif((ULONG*)ptr, (ULONG*)base);

		while (cur != NULL) {
			*(UWORD *) ptr = (strlen(cur->tag) + 1);
			ptr += sizeof(UWORD);

			strcpy(ptr, cur->tag);
			ptr += (strlen(cur->tag) + 1);

			if (cur->def == NULL) {
				*(UWORD *) ptr = 0;
				ptr += sizeof(UWORD);
			} else {
				*(UWORD *) ptr = (strlen((BYTE*)cur->def) + 1);
				ptr += sizeof(UWORD);

				strcpy(ptr, (BYTE*)cur->def);
				ptr += (strlen((BYTE*)cur->def) + 1);
			}

			cur = cur->next;
			ptr = (BYTE*) norm(ptr);
		}

		*(UWORD *) ptr = 0;
		ptr += sizeof(UWORD);
	}

	*(UWORD *) ptr = 0;
	ptr += sizeof(UWORD);

	RHDR.data_size = total_len;
	RHDR.data_attrib = DA_TEMPORARY;

	if (entry == (ULONG) -1)
		saved = RF_new_entry(RF, base, &RHDR, RTYP_DICTIONARY);
	else
		RF_write_entry(RF, saved = entry, base, &RHDR, RTYP_DICTIONARY);

	mem_free(base);

	return saved;
}

/***************************************************/
//
// Load a set of dictionary tags and definitions from a given resource
// file entry into a DICT_class instance, and clear modified flag
//
// Dictionary definitions are expected to be either NULL or pointers to
// zero-terminated ASCII strings
//
/***************************************************/

void DICT_load(DICT_class *DICT, RF_class *RF, ULONG entry) {
	RES_read_entry(RF, entry, DICT, NULL, RTYP_DICTIONARY);

	DICT->touched = 0;
}

/***************************************************/
//
// Build and return a single string consisting of all tags in
// a given dictionary separated by commas
//
/***************************************************/

BYTE *DICT_build_tag_string(DICT_class *DICT) {
	ULONG len;
	BYTE *string;
	DI_class *DI;
	DICT_entry *cur;

	DI = DI_construct(DICT);
	len = 0;
	while ((cur = DI_fetch(DI)) != NULL)
		len += (ULONG) (strlen(cur->tag) + 1);
	DI_destroy(DI);

	string = (BYTE*) mem_alloc(len + 1L);
	string[0] = 0;

	DI = DI_construct(DICT);
	while ((cur = DI_fetch(DI)) != NULL) {
		strcat(string, cur->tag);
		strcat(string, ",");
	}
	DI_destroy(DI);

	len = strlen(string);
	if (len)
		string[(UWORD) len - 1] = 0;

	return string;
}

/*************************************************************/
//
// Create a DOS timestamp cache class instance
//
/*************************************************************/

TS_class *TS_construct(void) {
	TS_class *TS;

	TS = (TS_class*) mem_alloc(sizeof(TS_class));

	TS->cache = DICT_construct(256);

	return TS;
}

/*************************************************************/
//
// Destroy a DOS timestamp cache class instance
//
/*************************************************************/

void TS_destroy(TS_class *TS) {
	DICT_destroy(TS->cache);

	mem_free(TS);
}

/*************************************************************/
//
// Invalidate a DOS timestamp cache entry
//
/*************************************************************/

void TS_invalidate(TS_class *TS, BYTE *filename) {
	DICT_delete(TS->cache, filename);
}

/*************************************************************/
//
// Return the timestamp of a given DOS file, retrieved from cache
// without DOS call if possible
//
// Return timestamp of 0 (oldest possible) for filename "$obsolete"
//
/*************************************************************/

ULONG TS_file_time(TS_class *TS, BYTE *filename) {
	DICT_entry *cur;
	ULONG *tstamp;

	if (!strcasecmp(filename, "$obsolete"))
		return 0L;

	cur = DICT_lookup(TS->cache, filename);

	if (cur == NULL) {
		cur = DICT_enter(TS->cache, filename, D_DEFHEAP);

		tstamp = (ULONG*) mem_alloc(sizeof(ULONG));

		*tstamp = file_time(filename);
		cur->def = tstamp;
	}

	return *(ULONG *) cur->def;
}

/*************************************************************/
//
// Given a comma-separated list of filenames (cf. DICT_build_tag_string()
// above), return the timestamp of the most recently modified file
//
// If any file is missing or named "$obsolete", return 0 to indicate 
// obsolescence of entire fileset
//
/*************************************************************/

ULONG TS_latest_file_time(TS_class *TS, BYTE *filelist) {
	ULONG latest;
	ULONG timestamp;
	BYTE *str;
	UWORD beg, end, last;

	if (filelist == NULL)
		return 0L;

	if (!strlen(filelist))
		return 0L;

	str = str_alloc(filelist);

	latest = 0L;

	beg = last = 0;
	while (!last) {
		end = beg;
		while (str[end] && (str[end] != ','))
			end++;

		if (str[end] == ',')
			str[end] = 0;
		else
			last = 1;

		timestamp = TS_file_time(TS, &str[beg]);

		if (timestamp == 0L) {
			latest = 0L;
			break;
		}

		if (timestamp > latest)
			latest = timestamp;

		beg = end + 1;
	}

	mem_free(str);
	return latest;
}

/***************************************************/
//
// Create comma-separated string (CSS) class instance based on string
// (Note: string must remain valid until CSS instance destroyed)
//
// If string == NULL, create new empty string
//
// Initialize iteration pointer to beginning of string
//
/***************************************************/

CSS_class *CSS_construct(BYTE *string) {
	CSS_class *CSS;

	CSS = (CSS_class*) mem_alloc(sizeof(CSS_class));

	if (string == NULL)
		CSS->string = CSS->cur = str_alloc("");
	else
		CSS->string = CSS->cur = string;

	return CSS;
}

/***************************************************/
//
// Destroy comma-separated string class instance
//
/***************************************************/

void CSS_destroy(CSS_class *CSS) {
	mem_free(CSS);
}

/***************************************************/
//
// Return next substring from comma-separated string set,
// or NULL if at end of source string
//
// Returned string must be freed with mem_free() when no longer
// needed
//
/***************************************************/

BYTE *CSS_fetch_string(CSS_class *CSS) {
	BYTE save, *ptr;

	ptr = CSS->cur;

	if (*ptr == 0)
		return NULL;

	while ((*CSS->cur != ',') && (*CSS->cur))
		CSS->cur++;

	save = *CSS->cur;
	*CSS->cur = 0;

	ptr = str_alloc(ptr);

	if (save) {
		*CSS->cur = save;
		CSS->cur++;
	}

	return ptr;
}

/***************************************************/
//
// Return next substring from comma-separated string set as
// unsigned long integer
//
/***************************************************/

ULONG CSS_fetch_num(CSS_class *CSS) {
	BYTE save, *ptr;
	ULONG num;

	ptr = CSS->cur;

	if (*ptr == 0)
		return (ULONG) -1L;

	while ((*CSS->cur != ',') && (*CSS->cur))
		CSS->cur++;

	save = *CSS->cur;
	*CSS->cur = 0;

	num = ascnum(ptr, 10);

	if (save) {
		*CSS->cur = save;
		CSS->cur++;
	}

	return num;
}

/***************************************************/
//
// Reset CSS iteration pointer to beginning of source string
//
/***************************************************/

void CSS_rewind(CSS_class *CSS) {
	CSS->cur = CSS->string;
}

/***************************************************/
//
// Append new substring to end of source string
//
/***************************************************/

void CSS_add_string(CSS_class *CSS, BYTE *string) {
	BYTE *ptr;

	ptr = (BYTE*) mem_alloc((ULONG) strlen(CSS->string) + (ULONG) strlen(string) + 4L);

	strcpy(ptr, CSS->string);

	if (strlen(ptr))
		strcat(ptr, ",");

	strcat(ptr, string);

	mem_free(CSS->string);
	CSS->string = ptr;
}

/***************************************************/
//
// Append ASCII string representation of unsigned long integer 
// to comma-separated string set
//
/***************************************************/

void CSS_add_num(CSS_class *CSS, ULONG num) {
	BYTE *ptr;

	ptr = str(num);
	CSS_add_string(CSS, ptr);
	mem_free(ptr);
}

/***************************************************/
//
// Return current source string
//
/***************************************************/

BYTE *CSS_string(CSS_class *CSS) {
	return CSS->string;
}
