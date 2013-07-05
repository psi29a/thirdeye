//����������������������������������������������������������������������������
//��                                                                        ��
//��  DICT.C                                                                ��
//��                                                                        ��
//��  General-purpose dictionary database class                             ��
//��                                                                        ��
//��  Version: 1.00 of 6-Mar-92 -- Initial version                          ��
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
#include <string.h>

#include "defs.hpp"
#include "system.hpp"
#include "dict.hpp"

/*************************************************************/
//
// Calculate hash key for string (private)
//
/*************************************************************/

static UWORD DICT_hash(DICT_class *DICT, BYTE *string) {
	UWORD c, h = 0, i = 0;

	if (string == NULL)
		return 0;

	while ((c = *(string + (i++))) != 0)
		h += c;

	return h % DICT->hash_size;
}

/*************************************************************/
//
// Create dictionary class instance, given size of hash table (or 1 to 
// store entries in linear list)
//
/*************************************************************/

DICT_class *DICT_construct(UWORD hash_size) {
	UWORD i;
	DICT_class *DICT;

	DICT = mem_alloc(sizeof(DICT_class));

	DICT->hash_size = hash_size;

	DICT->root = mem_alloc(hash_size * sizeof(DICT->root[0]));

	for (i = 0; i < hash_size; i++)
		DICT->root[i] = NULL;

	DICT->touched = 0;

	return DICT;
}

/*************************************************************/
//
// Destroy dictionary, freeing all definitions marked D_DEFHEAP
//
/*************************************************************/

void DICT_destroy(DICT_class *DICT) {
	DICT_entry *cur, *next;
	UWORD h;

	for (h = 0; h < DICT->hash_size; h++) {
		cur = DICT->root[h];

		while (cur != NULL) {
			mem_free(cur->tag);

			if ((cur->def != NULL) && (cur->attr & D_DEFHEAP))
				mem_free(cur->def);

			next = cur->next;
			mem_free(cur);
			cur = next;
		}
	}

	mem_free(DICT->root);
	mem_free(DICT);
}

/*************************************************************/
//
// Search dictionary for tag *name; return pointer to entry structure
// or NULL if not found
//
/*************************************************************/

DICT_entry *DICT_lookup(DICT_class *DICT, BYTE *name) {
	DICT_entry *cur;

	cur = DICT->root[DICT_hash(DICT, name)];

	while (cur != NULL) {
		if (!strcmp(name, cur->tag))
			return cur;

		cur = cur->next;
	}

	return NULL;
}

/*************************************************************/
//
// Remove entry tagged with name from dictionary
//
/*************************************************************/

void DICT_delete(DICT_class *DICT, BYTE *name) {
	DICT_entry *cur, *last;
	UWORD h;

	h = DICT_hash(DICT, name);
	cur = DICT->root[h];

	last = NULL;
	while (cur != NULL) {
		if (!strcmp(name, cur->tag)) {
			DICT->touched = 1;

			if (last != NULL)
				last->next = cur->next;
			else
				DICT->root[h] = cur->next;

			mem_free(cur->tag);

			if ((cur->def != NULL) && (cur->attr & D_DEFHEAP))
				mem_free(cur->def);

			mem_free(cur);
			break;
		}

		last = cur;
		cur = cur->next;
	}
}

/*************************************************************/
//
// Create new entry for name, returning pointer to entry structure
//
// Attributes:
//
//   D_DEFHEAP: Definition will be object on heap, and should be freed
//              automatically when entry deleted or dictionary destroyed
//
/*************************************************************/

DICT_entry *DICT_enter(DICT_class *DICT, BYTE *name, UWORD attributes) {
	DICT_entry *cur, *last;
	UWORD h;

	h = DICT_hash(DICT, name);
	cur = DICT->root[h];

	last = NULL;

	while (cur != NULL) {
		last = cur;
		cur = cur->next;
	}

	cur = mem_alloc(sizeof(DICT_entry));

	cur->next = NULL;

	cur->tag = str_alloc(name);

	cur->def = NULL;
	cur->attr = attributes;

	if (last != NULL)
		last->next = cur;
	else
		DICT->root[h] = cur;

	DICT->touched = 1;

	return cur;
}

/*************************************************************/
//
// Return TRUE if any entries have been added to or deleted from 
// dictionary
//
/*************************************************************/

WORD DICT_touched(DICT_class *DICT) {
	return DICT->touched;
}

/*************************************************************/
//
// Dump all tags and definitions for diagnostic purposes
//
/*************************************************************/

void DICT_show(DICT_class *DICT) {
	DI_class *DI;
	DICT_entry *cur;

	DI = DI_construct(DICT);

	while ((cur = DI_fetch(DI)) != NULL)
		printf("[%s] = [%s]\n", cur->tag, (char*) cur->def);

	DI_destroy(DI);
}

/*************************************************************/
//
// Copy all entries and tags from src to dest dictionary
//
/*************************************************************/

void DICT_copy(DICT_class *src, DICT_class *dest) {
	DI_class *DI;
	DICT_entry *entry;

	DI = DI_construct(src);

	while ((entry = DI_fetch(DI)) != NULL)
		DICT_enter(dest, entry->tag, D_DEFHEAP)->def = str_alloc(entry->def);

	DI_destroy(DI);
}

/*************************************************************/
//
// Delete all entries from dictionary (without destroying class instance) 
//
/*************************************************************/

void DICT_wipe(DICT_class *DICT) {
	DI_class *DI;
	DICT_entry *entry;

	DI = DI_construct(DICT);

	while ((entry = DI_fetch(DI)) != NULL)
		DICT_delete(DICT, entry->tag);

	DI_destroy(DI);
}

/*************************************************************/
//
// Compare all entries in d1 and d2, returning TRUE if all entries
// match
//
/*************************************************************/

WORD DICT_compare(DICT_class *d1, DICT_class *d2) {
	DI_class *DI1, *DI2;
	DICT_entry *e1, *e2;
	WORD failed, result;

	failed = 0;

	DI1 = DI_construct(d1);
	DI2 = DI_construct(d2);

	while ((e1 = DI_fetch(DI1)) != NULL) {
		if (DI_fetch(DI2) == NULL) {
			failed = 1;
			break;
		}

		e2 = DICT_lookup(d2, e1->tag);
		if (e2 == NULL) {
			failed = 1;
			break;
		}

		if ((e1->def == NULL) || (e2->def == NULL)) {
			if (e1->def != e2->def) {
				failed = 1;
				break;
			}
		} else if (strcmp(e1->def, e2->def)) {
			failed = 1;
			break;
		}
	}

	result = (DI_fetch(DI2) == NULL);

	DI_destroy(DI1);
	DI_destroy(DI2);

	if (failed)
		return 0;
	else
		return result;
}

/*************************************************************/
//
// Construct dictionary iterator class, given dictionary to scan
//
// "Rewind" iterator to first entry
//
/*************************************************************/

DI_class *DI_construct(DICT_class *DICT) {
	DI_class *DI;

	DI = mem_alloc(sizeof(DI_class));

	DI->base = DICT;
	DI->cur = NULL;

	for (DI->bucket = 0; DI->bucket < DICT->hash_size; DI->bucket++)
		if ((DI->cur = DICT->root[DI->bucket]) != NULL)
			break;

	return DI;
}

/***************************************************/
//
// Destroy dictionary iterator class
//
/***************************************************/

void DI_destroy(DI_class *DI) {
	mem_free(DI);
}

/***************************************************/
//
// Fetch next entry from iterated dictionary
//
// If dictionary was constructed with hash size of 1 (DC_LINEAR), entries
// will be returned from DI_fetch() in their original order of creation;
// otherwise, entries will be returned in order of increasing hash key 
// values
//
/***************************************************/

DICT_entry *DI_fetch(DI_class *DI) {
	DICT_entry *cur;

	cur = DI->cur;
	if (cur == NULL)
		return NULL;

	DI->cur = DI->cur->next;

	while (DI->cur == NULL) {
		if (++DI->bucket == DI->base->hash_size)
			break;

		DI->cur = DI->base->root[DI->bucket];
	}

	return cur;
}
