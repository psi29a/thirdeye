//
//  AESOP ARC resource library class
//

#ifndef RF_H
#define RF_H

#ifdef __cplusplus
extern "C" {
#endif

#define OD_SIZE 128                // # of entries/ordinal directory block
                         
#define RTYP_HOUSECLEAN  0         // Resource type/source equates
#define RTYP_RAW_MEM     1            
#define RTYP_RAW_FILE    2
#define RTYP_DICTIONARY  3

#define DA_FIXED       0x00000001L // Resource attribute equates
#define DA_MOVEABLE    0x00000000L
#define DA_PRECIOUS    0x00000020L
#define DA_DISCARDABLE 0x00000010L
#define DA_TEMPORARY   0x00000000L
#define DA_PLACEHOLDER 0x10000000L

#define SA_UNUSED      0x01        // Storage attribute flag equates
#define SA_DELETED     0x02

#define RF_SIGNATURE  "AESOP/16 V1.00"

typedef struct
{
   BYTE  signature[16];
   ULONG file_size;
   ULONG lost_space;
   ULONG FOB;
   ULONG create_time;
   ULONG modify_time;
}
RF_file_hdr;

typedef struct
{
   ULONG timestamp;           // public
   ULONG data_attrib;         // public
   ULONG data_size;           // public
}
RF_entry_hdr;

typedef struct OD_block
{
   ULONG next;
   UBYTE flags[OD_SIZE];
   ULONG index[OD_SIZE];
}
OD_block;

typedef struct OD_link
{
   struct OD_link *next;
   WORD touched;
   ULONG origin;
   OD_block blk;
}
OD_link;

typedef struct
{
   BYTE *filename;
   WORD file;
   WORD touched;
   RF_file_hdr hdr;
   OD_link *root;
}
RF_class;

RF_class *RF_construct(BYTE *filename, WORD compacting);
void RF_destroy(RF_class *RF, WORD compact_threshold);

ULONG RF_entry_count(RF_class *RF);

ULONG RF_next_entry(RF_class *RF);
ULONG RF_new_entry(RF_class *RF, void *source, RF_entry_hdr *RHDR,
   UWORD type);
ULONG RF_write_entry(RF_class *RF, ULONG entry, void *source,
   RF_entry_hdr *RHDR, UWORD type);
void RF_delete_entry(RF_class *RF, ULONG entry);

ULONG RF_index(RF_class *RF, ULONG entry);
UWORD RF_flags(RF_class *RF, ULONG entry);
void RF_set_flags(RF_class *RF, ULONG entry, UWORD flags);
RF_entry_hdr *RF_header(RF_class *RF, ULONG entry);

#ifdef __cplusplus
}
#endif

#endif
