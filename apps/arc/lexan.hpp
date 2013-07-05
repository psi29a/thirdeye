//
//  AESOP ARC lexical analyzer class
//
//  Requires class DICT
//

#ifndef LEXAN_H
#define LEXAN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   WORD  type;                      // private
   WORD  token;                     // private
   ULONG value;                     // private
   BYTE  *lexeme;                   // private
   BYTE  *line;                     // private
   WORD  new_line;                  // private
}
LEX_state;                          // analysis state description

typedef struct
{
   FILE  *handle;                   // private
   BYTE  *buf;                      // private
   BYTE  chr;                       // private
   BYTE  chrnxt;                    // private
   WORD  chrpnt;                    // private
   LEX_state state[2];              // private
   WORD  cur;                       // private
   DICT_class *keywords;            // private
   WORD  *symbol_key;               // private
   BYTE  **symbol_list;             // private
   DICT_class *file_list;           // private
   WORD  symbol_cnt;                // private
   UWORD flags;                     // private
}
LEX_class;                          // lexical analyzer main class


#define LEX_TXTLIT    0x8000        // allow text literals in [] brackets
#define LEX_LININFO   0x4000        // expect line # info in each input line

#define LEX_CUR       0             // select data for current token
#define LEX_NXT       1             // select data for next token

#define TTYP_KEYWORD  1             // keyword string
#define TTYP_NAME     2             // other name string
#define TTYP_SYMBOL   3             // symbol string
#define TTYP_STRLIT   4             // string literal ("")
#define TTYP_TXTLIT   5             // text literal ([])
#define TTYP_NUM      6             // numeric constant

#define TTYP_BAD     -3             // unrecognized/invalid symbol
#define TTYP_ERR     -2             // error reading input file
#define TTYP_EOF     -1             // end of input file reached

LEX_class *LEX_construct(UWORD init_flags, BYTE *filename, BYTE *keywords[],
   BYTE *symbols[], DICT_class *file_list);
void LEX_destroy(LEX_class *LEX);

void LEX_fetch(LEX_class *LEX);

WORD LEX_type(LEX_class *LEX, UWORD select);
WORD LEX_token(LEX_class *LEX, UWORD select);
ULONG LEX_value(LEX_class *LEX, UWORD select);
BYTE *LEX_lexeme(LEX_class *LEX, UWORD select);
BYTE *LEX_line(LEX_class *LEX, UWORD select);

void LEX_show(LEX_class *LEX, UWORD select);

WORD LEX_require(LEX_class *LEX, WORD type, WORD token, BYTE *expect);
WORD LEX_next_comma(LEX_class *LEX);
WORD LEX_next_constant(LEX_class *LEX);

#ifdef __cplusplus
}
#endif

#endif
