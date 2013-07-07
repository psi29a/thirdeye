//����������������������������������������������������������������������������
//��                                                                        ��
//��  RESFILE.C                                                             ��
//��                                                                        ��
//��  Resource file class for AESOP ARC compiler                            ��
//��                                                                        ��
//��  Version: 1.00 of 27-Feb-92 -- Initial version                         ��
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
#include <errno.h>

#include "defs.hpp"
#include "system.hpp"
#include "resfile.hpp"
#include "dict.hpp"
#include "resource.hpp"
#include "arcmsg.hpp"

/***************************************************/
//
// Create resource file manager class instance
//
//   filename: Name of resource file (created automatically if not found)
//
// compacting: Determines message reported when new file created; should
//             always be 0 when called externally
//
/***************************************************/
RF_class *RF_construct(BYTE *filename, WORD compacting) {
	RF_class *RF;
	WORD i;
	OD_block *tmp;
	OD_link *link;
	OD_link **prev;
	ULONG next;

	RF = (RF_class*) mem_alloc(sizeof(RF_class));

	RF->filename = str_alloc(filename);

	while ((RF->file = open(filename, O_RDWR)) == -1) {
		if (compacting)
			report(E_NOTICE, NULL, MSG_CMR, filename);
		else
			report(E_NOTICE, NULL, MSG_CNR, filename);

		RF->hdr.file_size = sizeof(RF_file_hdr) + sizeof(OD_block);
		RF->hdr.lost_space = 0L;
		RF->hdr.FOB = sizeof(RF_file_hdr);
		RF->hdr.create_time = RF->hdr.modify_time = current_time();
		strcpy(RF->hdr.signature, RF_SIGNATURE);

		if (!write_file(filename, &RF->hdr, sizeof(RF_file_hdr)))
			report(E_FATAL, NULL, MSG_CWR, filename);

		tmp = (OD_block*) mem_alloc(sizeof(OD_block));
		tmp->next = 0L;
		for (i = 0; i < OD_SIZE; i++) {
			tmp->flags[i] = SA_UNUSED;
			tmp->index[i] = 0;
		}

		if (!append_file(filename, tmp, sizeof(OD_block)))
			report(E_FATAL, NULL, MSG_CWR, filename);

		mem_free(tmp);
	}

	r_read(RF->file, &RF->hdr, sizeof(RF_file_hdr));

	if (strcmp(RF->hdr.signature, RF_SIGNATURE))
		report(E_FATAL, NULL, MSG_OBF, filename);

	RF->touched = 0;

	next = RF->hdr.FOB;
	prev = &RF->root;
	while (next) {
		link = (OD_link*) mem_alloc(sizeof(OD_link));

		link->touched = 0;
		link->origin = lseek(RF->file, next, SEEK_SET);
		r_read(RF->file, &link->blk, sizeof(OD_block));

		next = link->blk.next;
		*prev = link;
		prev = &link->next;
	}
	*prev = NULL;

	return RF;
}

/***************************************************/
//
// Destroy resource file manager class
//
/***************************************************/

void RF_destroy(RF_class *RF, WORD compact_threshold) {
	OD_link *link, *next;
	WORD lost_percent;
	ULONG lost_space, entry, nentries;
	RF_class *new_class,*old;
	BYTE *RF_filename, *temp_fn;

	if (RF->touched) {
		lseek(RF->file, 0L, SEEK_SET);
		r_write(RF->file, &RF->hdr, sizeof(RF_file_hdr));
	}

	link = RF->root;
	while (link != NULL) {
		if (link->touched) {
			lseek(RF->file, link->origin, SEEK_SET);
			r_write(RF->file, &link->blk, sizeof(OD_block));
		}

		next = link->next;
		mem_free(link);
		link = next;
	}

	close(RF->file);

	lost_space = RF->hdr.lost_space;
	lost_percent = (WORD) ((lost_space * 100L) / RF->hdr.file_size);
	RF_filename = str_alloc(RF->filename);

	mem_free(RF->filename);
	mem_free(RF);

	if (lost_space && (lost_percent >= compact_threshold)) {
		rename(RF_filename, temp_fn = temp_filename(NULL));
		old = RF_construct(temp_fn, 0);

		new_class = RF_construct(RF_filename,1);

		new_class->hdr.modify_time = old->hdr.modify_time;
		new_class->hdr.create_time = old->hdr.create_time;
		new_class->touched = 1;

		nentries = RF_entry_count(old);
		for (entry = 0; entry < nentries; entry++) {
		RF_new_entry(new_class,&old->file,RF_header(old,entry),RTYP_HOUSECLEAN);
		RF_set_flags(new_class,entry,RF_flags(old,entry));
	}

	RF_destroy(new_class,0);
	RF_destroy(old, 100);
	remove_tempfile(temp_fn);
}

mem_free(RF_filename);
}

/***************************************************/
//
// Return number of in-use entries in resource file
//
/***************************************************/

ULONG RF_entry_count(RF_class *RF) {
ULONG cnt;
WORD i;
OD_link *link;

cnt = 0L;
link = RF->root;

while (link->next != NULL) {
	cnt += OD_SIZE;
	link = link->next;
}

for (i = 0; i < OD_SIZE; i++)
	if (!(link->blk.flags[i] & SA_UNUSED))
		cnt++;

return cnt;
}

/***************************************************/
//
// Return index at which next entry will be created in file
//
/***************************************************/

ULONG RF_next_entry(RF_class *RF) {
ULONG entry;
WORD i;
OD_link *link;

entry = 0L;
link = RF->root;

while (link != NULL) {
	for (i = 0; i < OD_SIZE; i++) {
		if (link->blk.flags[i] & (SA_UNUSED | SA_DELETED))
			return entry;

		entry++;
	}

	link = link->next;
}

return entry;
}

/***************************************************/
//
// Create new entry in file, returning index
//
// Requires type, header, and pointer to source data or descriptor
//
/***************************************************/

ULONG RF_new_entry(RF_class *RF, void *source, RF_entry_hdr *RHDR, UWORD type) {
ULONG entry;
WORD i, j, found;
OD_link *link, *newlink, *next;

found = 0;
entry = 0L;                   // set entry = index of next available entry
next = RF->root;              //      link = pointer to block in chain
do                            //         i = entry within block
{
	link = next;

	for (i = 0; i < OD_SIZE; i++) {
		j = link->blk.flags[i];

		if (j & SA_UNUSED) {
			found = 1;
			break;
		}

		if ((j & SA_DELETED) && (type != RTYP_HOUSECLEAN))
			return RF_write_entry(RF, entry, source, RHDR, type);

		entry++;
	}
	next = link->next;
} while ((next != NULL) && (!found));

if (!found)                   // at end of directory; must create new link
{
	newlink = (OD_link*) mem_alloc(sizeof(OD_link));

	link->next = newlink;
	link->blk.next = RF->hdr.file_size;
	link->touched = 1;

	newlink->next = NULL;
	newlink->blk.next = 0L;

	for (i = 0; i < OD_SIZE; i++) {
		newlink->blk.flags[i] = SA_UNUSED;
		newlink->blk.index[i] = 0;
	}

	newlink->origin = lseek(RF->file, 0L, SEEK_END);
	r_write(RF->file, &newlink->blk, sizeof(OD_block));
	RF->hdr.file_size += sizeof(OD_block);
	RF->touched = 1;

	link = newlink;
	i = 0;
}

link->blk.index[i] = RF->hdr.file_size;
link->blk.flags[i] &= (~SA_UNUSED);
link->touched = 1;

return RF_write_entry(RF, entry, source, RHDR, type);
}

/***************************************************/
//
// Replace existing entry in file, returning entry index if successful
// or -1 otherwise
//
/***************************************************/

ULONG RF_write_entry(RF_class *RF, ULONG entry, void *source,
	RF_entry_hdr *RHDR, UWORD type) {
RF_entry_hdr cur, dummy;
ULONG blknum;
UWORD i;
OD_link *link;

blknum = (entry / (ULONG) OD_SIZE);      // blknum = directory block #
i = (UWORD) (entry % (ULONG) OD_SIZE);   // i = entry # within block

link = RF->root;
while (blknum--) {
	link = link->next;
	if (link == NULL)
		return (ULONG) -1L;
}
if (link->blk.flags[i] & SA_UNUSED)
	return (ULONG) -1L;

RF->touched = 1;

if (RHDR == NULL) {
	dummy.data_size = 0L;
	dummy.data_attrib = DA_TEMPORARY;
	RHDR = &dummy;
}

if (type != RTYP_HOUSECLEAN) {
	if (link->blk.flags[i] & SA_DELETED) {
		link->blk.flags[i] &= (~SA_DELETED);
		link->touched = 1;
	}
	RHDR->timestamp = RF->hdr.modify_time = current_time();
}

lseek(RF->file, link->blk.index[i], SEEK_SET);

if (link->blk.index[i] == RF->hdr.file_size) {
	RF->hdr.file_size += (sizeof(RF_entry_hdr) + RHDR->data_size);
	return RES_store_resource(RF, entry, source, RHDR, type);
}

r_read(RF->file, &cur, sizeof(RF_entry_hdr));

if (RHDR->data_size > cur.data_size) {
	RF->hdr.lost_space += (cur.data_size + sizeof(RF_entry_hdr));
	link->blk.index[i] = RF->hdr.file_size;
	link->touched = 1;
	lseek(RF->file, 0L, SEEK_END);
	RF->hdr.file_size += (sizeof(RF_entry_hdr) + RHDR->data_size);
} else {
	RF->hdr.lost_space += (cur.data_size - RHDR->data_size);
	lseek(RF->file, link->blk.index[i], SEEK_SET);
}

return RES_store_resource(RF, entry, source, RHDR, type);
}

/***************************************************/
//
// Delete an entry from the resource file
//
// (Not currently used by AESOP)
//
/***************************************************/

void RF_delete_entry(RF_class *RF, ULONG entry) {
RF_entry_hdr RHDR;
ULONG blknum;
UWORD i;
OD_link *link;

blknum = (entry / (ULONG) OD_SIZE);      // blknum = directory block #
i = (UWORD) (entry % (ULONG) OD_SIZE);   // i = entry # within block

link = RF->root;
while (blknum--) {
	link = link->next;
	if (link == NULL)
		return;
}

if (link->blk.flags[i] & (SA_UNUSED | SA_DELETED))
	return;

lseek(RF->file, link->blk.index[i], SEEK_SET);
r_read(RF->file, &RHDR, sizeof(RF_entry_hdr));

link->blk.flags[i] |= SA_DELETED;
link->touched = 1;

RF->hdr.lost_space += RHDR.data_size;
RF->touched = 1;

RHDR.data_size = 0L;

lseek(RF->file, link->blk.index[i], SEEK_SET);
r_write(RF->file, &RHDR, sizeof(RF_entry_hdr));
}

/***************************************************/
//
// Return pointer to static entry header for specified resource
//
/***************************************************/

RF_entry_hdr *RF_header(RF_class *RF, ULONG entry) {
static RF_entry_hdr RHDR;
ULONG blknum;
UWORD i;
OD_link *link;

blknum = (entry / (ULONG) OD_SIZE);      // blknum = directory block #
i = (UWORD) (entry % (ULONG) OD_SIZE);   // i = entry # within block

link = RF->root;
while (blknum--) {
	link = link->next;
	if (link == NULL)
		return NULL;
}
if (link->blk.flags[i] & SA_UNUSED)
	return NULL;

lseek(RF->file, link->blk.index[i], SEEK_SET);
r_read(RF->file, &RHDR, sizeof(RF_entry_hdr));

return &RHDR;
}

/***************************************************/
//
// Return storage attribute flags (see RESFILE.H) for specified resource 
//
/***************************************************/

UWORD RF_flags(RF_class *RF, ULONG entry) {
ULONG blknum;
UWORD i;
OD_link *link;

blknum = (entry / (ULONG) OD_SIZE);      // blknum = directory block #
i = (UWORD) (entry % (ULONG) OD_SIZE);   // i = entry # within block

link = RF->root;
while (blknum--) {
	link = link->next;
	if (link == NULL)
		return SA_UNUSED;
}

return (link->blk.flags[i]);
}

/***************************************************/
//
// Return offset of resource in file
//
/***************************************************/

ULONG RF_index(RF_class *RF, ULONG entry) {
ULONG blknum;
UWORD i;
OD_link *link;

blknum = (entry / (ULONG) OD_SIZE);      // blknum = directory block #
i = (UWORD) (entry % (ULONG) OD_SIZE);   // i = entry # within block

link = RF->root;
while (blknum--) {
	link = link->next;
	if (link == NULL)
		return 0L;
}

return (link->blk.index[i]);
}

/***************************************************/
//
// Set storage attribute flags (see RESFILE.H) for specified resource 
//
/***************************************************/

void RF_set_flags(RF_class *RF, ULONG entry, UWORD flags) {
ULONG blknum;
UWORD i;
OD_link *link;

blknum = (entry / (ULONG) OD_SIZE);      // blknum = directory block #
i = (UWORD) (entry % (ULONG) OD_SIZE);   // i = entry # within block

link = RF->root;
while (blknum--) {
	link = link->next;
	if (link == NULL)
		return;
}

if (link->blk.flags[i] & SA_UNUSED)
	return;

link->blk.flags[i] = flags;
link->touched = 1;
}
