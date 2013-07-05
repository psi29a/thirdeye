//лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл
//лл                                                                        лл
//лл  PREPROC.C                                                             лл
//лл                                                                        лл
//лл  Preprocessor class for AESOP ARC compiler                             лл
//лл                                                                        лл
//лл  Version: 1.00 of 26-Feb-92 -- Initial version                         лл
//лл                                                                        лл
//лл  Project: Extensible State-Object Processor (AESOP/16)                 лл
//лл   Author: John Miles                                                   лл
//лл                                                                        лл
//лл  C source compatible with IBM PC ANSI C/C++ implementations            лл
//лл  Large memory model (16-bit DOS)                                       лл
//лл                                                                        лл
//лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл
//лл                                                                        лл
//лл  Copyright (C) 1992 Miles Design, Inc.                                 лл
//лл                                                                        лл
//лл  Miles Design, Inc.                                                    лл
//лл  10926 Jollyville #308                                                 лл
//лл  Austin, TX 78759                                                      лл
//лл  (512) 345-2642 / BBS (512) 454-9990 / FAX (512) 338-9630              лл
//лл                                                                        лл
//лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл

//#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.hpp"
#include "system.hpp"
#include "arcmsg.hpp"
#include "dict.hpp"
#include "preproc.hpp"

#define MAX_IN_LEN  512       // max. input file line length
#define MAX_OUT_LEN 4096      // max. i-file line length
#define IF_STK_SIZE 256       // max. nesting level for #ifdef conditionals
#define INC_NAME    "AINC"    // name of INCLUDE environment variable

BYTE *PD_keywords[] =
{
   PD_DEFINE,     
   PD_UNDEF,      
   PD_INCLUDE,    
   PD_DEPEND,    
   PD_IFDEF,      
   PD_IFNDEF,     
   PD_ELSE,       
   PD_ENDIF,
   PD_VERBOSE,
   PD_BRIEF
};

enum
{
   PP_DEFINE,     
   PP_UNDEF,      
   PP_INCLUDE,    
   PP_DEPEND,    
   PP_IFDEF,      
   PP_IFNDEF,     
   PP_ELSE,       
   PP_ENDIF,
   PP_VERBOSE,
   PP_BRIEF,
   NPDN
};

/***************************************************/
//
// Return pointer to static string containing original source file and
// line number information for current position
//
/***************************************************/

static BYTE *TF_line_info(TF_info *TF)                   
{
   static BYTE info[256];
   
   sprintf(info,TF->name);
   strcat(info," ");
   //ultoa(TF->line,&info[strlen(info)],10);
   sprintf(&info[strlen(info)],"%d",TF->line);
   strcat(info,": ");

   return info;
}

/***************************************************/
//
// Construct class to handle nested #ifdef conditionals
//
/***************************************************/

static IF_class *IF_construct(void)                       
{
   IF_class *IF;

   IF = mem_alloc(sizeof(IF_class));

   IF->stack = mem_alloc(IF_STK_SIZE * sizeof(int));

   IF->depth = -1;
   IF->condition = 1;

   return IF;
}

/***************************************************/
//
// Add a level of #ifdef nesting
//
/***************************************************/

static WORD IF_push(IF_class *IF)                          
{
   if (IF->depth == (IF_STK_SIZE-1))
      return 0;

   IF->stack[++IF->depth] = IF->condition;

   return 1;
}

/***************************************************/
//
// Handle #else directives by toggling #ifdef status
//
/***************************************************/

static WORD IF_else(IF_class *IF)                        
{
   if (IF->depth == -1)
      return 0;

   if (!IF->stack[IF->depth])
      IF->condition = 0;
   else
      IF->condition = !IF->condition;

   return 1;
}

/***************************************************/
//
// Handle #endif directives by popping a level from the #ifdef stack
//
/***************************************************/

static WORD IF_pop(IF_class *IF)                           
{
   if (IF->depth == -1)
      return 0;

   IF->condition = (IF->stack[IF->depth--]);

   return 1;
}

/***************************************************/
//
// Destroy the #ifdef handler class
//
/***************************************************/

static WORD IF_destroy(IF_class *IF)                       
{
   WORD i = IF->depth;

   mem_free(IF->stack);
   mem_free(IF);

   return (i == -1);
}

/***************************************************/
//
// Fetch next line from current source file, reporting errors 
// where appropriate
//
// Returns: 0 if normal line fetched
//          1 if end-of-file reached
//          2 if line was truncated
//          3 if unspecified read error occurred
//
/***************************************************/

static WORD PP_fetch_line(PP_class *PP)                    
{
   ++PP->cur.line;

   if (!read_text_line(PP->cur.handle,MAX_IN_LEN,PP->inbuf))
      {
      switch (clear_system_error())
         {
         case EOF_REACHED:
            return 1;
         case LINE_TOO_LONG:
            report(E_ERROR,TF_line_info(&PP->cur),MSG_LTOOL);
            return 2;
         default:
            report(E_ERROR,TF_line_info(&PP->cur),MSG_CRF,PP->cur.name);
            close_text_file(PP->cur.handle);
            return 3;
         }
      }

   return 0;
}

/***************************************************/
//
// Create preprocessor class instance
//
//     in_fn: Top-level input filename
//
//    out_fn: Output (intermediate file, or i-file) filename
//
// init_macs: List of preinitialized macros (including manifest constants)
//
//   attribs: See PREPROC.H
//
/***************************************************/

PP_class *PP_construct(BYTE *in_fn, BYTE *out_fn, DICT_class *init_macs,
   UWORD attribs)
{
   PP_class *PP;

   PP = mem_alloc(sizeof(PP_class));

   PP->parent.name = in_fn;
   PP->cur.name = in_fn;
   PP->cur.line = 0;
   PP->depth = 1;
   PP->inbuf = mem_alloc(MAX_IN_LEN);
   PP->outbuf = mem_alloc(MAX_OUT_LEN);

   PP->tl_flag = attribs & PP_TXTLIT;              // 15: allow text literals
   PP->ws_flag = attribs & PP_KEEP_WS;             // 14: kill leading spaces

   PP->out.name = out_fn;
   PP->out.handle = write_text_file(PP->out.name);
   if (clear_system_error())
      report(E_FATAL,NULL,MSG_COT,PP->out.name);

   PP->IF = IF_construct();
   PP->MAC = DICT_construct(1024);

   DICT_copy(init_macs,PP->MAC);

   return PP;
}

/***************************************************/
//
// Destroy preprocessor class instance
//
/***************************************************/

void PP_destroy(PP_class *PP)
{
   if (!IF_destroy(PP->IF))
      report(E_ERROR,NULL,MSG_UEF);

   DICT_destroy(PP->MAC);

   close_text_file(PP->out.handle);

   mem_free(PP->outbuf);
   mem_free(PP->inbuf);
   mem_free(PP);
}

/***************************************************/
//
// Preprocess current input file; #include files handled via recursive
// calls to this function
//
/***************************************************/

void PP_process(PP_class *PP)
{
   PP_class include;

   BYTE *inbuf,*outbuf;
   UBYTE  chr,chrnxt;
   WORD stat,done;
   WORD mac_out;
   WORD qpsl,qpcc,qptl;
   WORD com_lin,com_blk,chr_cnt;
   WORD i,j,k,m,nt,llen,out,org_out;
   WORD name_beg,name_end,dir_state,dir_type;
   WORD inc_out;
   WORD depend;
   WORD esc;
   BYTE *str,*strend;
   BYTE *text;
   BYTE strsave;
   DICT_entry *cur_mac,*entry;

   inbuf = PP->inbuf; outbuf = PP->outbuf;         // (aliased for speed)

   strcpy(outbuf,PP->cur.name);                    // write dummy "line 0" to
   strcat(outbuf," 0: \n");                        // output file to ensure
   write_text_line(PP->out.handle,outbuf);         // autodependency tracking

   PP->cur.handle = read_text_file(PP->cur.name);
   if (clear_system_error())
      {
      if (PP->depth == 1)
         {
         fcloseall();
         unlink(PP->out.name);
         report(E_FATAL,NULL,MSG_FNF,PP->cur.name);
         }
      else
         {
         report(E_ERROR,TF_line_info(&PP->parent),MSG_UOI,PP->cur.name);
         return;
         }
      }

   if (PP->depth == 1)
      printf("%s\n",PP->cur.name);

   com_blk = 0;                 // comment flag for block comments (/*...*/)

   done = 0;
   while (!done)
      {
      stat = PP_fetch_line(PP);
      if (stat == 1) break;
      else if (stat == 2) continue;
      else if (stat == 3) return;

      strcpy(outbuf,TF_line_info(&PP->cur));
      out = strlen(outbuf);     // output text pointer
      org_out = out;            // copy of initial output text pointer
      chr_cnt = 0;              // # of non-whitespace chars written to line
      qpsl = 0;                 // quote parity for string literals ("")
      qpcc = 0;                 // quote parity for character constants ('')
      qptl = 0;                 // quote parity for text literals ([])
      com_lin = 0;              // comment flag for line comments (//, etc.)
      esc = 0;                  // # of chars to insert after \ escape code
      dir_state = 0;            // 0=!dir, else parsing: 1=#-dir 2=arg1 3=arg2
      inc_out = -1;             // pointer to #include filespec
      depend = 0;               // #depend directive flag
      name_beg = -1;            // initial identifier character index
      name_end = -1;            // initial end-of-identifier index
      cur_mac = NULL;           // pointer to macro currently being #defined

      llen = strlen(inbuf);

      nt = (inbuf[llen-1] == '\n') ? 1:0;

      for (i=0;i<llen-nt;i++)
         {
         chr = inbuf[i]; chrnxt = inbuf[i+1];

         if ((!qpsl) && (!qpcc) && (!qptl) && (!esc)) 
            {                                      // maintain comment flags
            if ((!com_blk) && (!com_lin))
               if ((chr == '/') && (chrnxt == '/'))
                  {
                  i=i+1;
                  com_lin = 1;
                  }
               else if (chr == '`')
                  com_lin = 1;

            if (com_lin) continue;

            if ((chr == '*') && (chrnxt == '/'))
               {
               if (!com_blk)
                  report(E_ERROR,TF_line_info(&PP->cur),MSG_UBC,PP->cur.name);
               else
                  --com_blk;
               i=i+1;
               continue;
               }
            else if ((chr == '/') && (chrnxt == '*'))
               {
               com_blk++;
               i=i+1;
               }

            if (com_blk) continue;
            }

         if ((out > (MAX_OUT_LEN - (MAX_OUT_LEN / 10))))
            {
            report(E_ERROR,TF_line_info(&PP->cur),MSG_OLL);
            break;                                 // break out if < 10% of
            }                                      // output line space left

         if (esc) --esc;

         if ((chr == '\\') && (!esc))              // concatenate lines
            {                                      // and handle escape codes
            for (j=i+1;j<llen;j++)
               if (!is_whitespace[inbuf[j]]) break;

            if (j==llen)
               {
               if (name_beg != -1)
                  report(E_ERROR,TF_line_info(&PP->cur),MSG_CCF);

               stat = PP_fetch_line(PP);
               if (stat == 1)
                  {
                  report(E_ERROR,TF_line_info(&PP->cur),MSG_CCL,PP->cur.name);
                  return;
                  }
               else if (stat == 2) break;
               else if (stat == 3) return;
  
               llen = strlen(inbuf);
               i = -1;
               continue;
               }
            else
               if (!(qpcc || qpsl || qptl))
                  report(E_ERROR,TF_line_info(&PP->cur),MSG_CCE);
               else
                  esc = 2;
            }
                                 
         if (!esc)                                 // maintain quote parity
            if ((chr == '\'') && (!(qpsl || qptl)))
               qpcc ^= chr;
            else if ((chr == '"') && (!(qptl || qpcc)))
               qpsl ^= chr;
            else if ((chr == '[') && (!(qpcc || qpsl)) && (PP->tl_flag))
               qptl = 1;
            else if ((chr == ']') && (!(qpcc || qpsl)) && (PP->tl_flag))
               if (!qptl)
                  report(E_ERROR,TF_line_info(&PP->cur),MSG_UBB,PP->cur.name);
               else
                  qptl = 0;

         if (qpcc || qpsl || qptl || esc)          // copy literal text &
            {                                      // continue
            ++chr_cnt;
            outbuf[out++] = chr;
            continue;
            }

         if ((chr == '#') && (!dir_state))         // flag beginning of PP
            {                                      // directive
            if (chr_cnt)
               report(E_ERROR,TF_line_info(&PP->cur),MSG_MPD);
            dir_state = 1;
            continue;
            }

         if ((name_beg == -1) && (is_namechar[chr] && (!is_digit[chr])))
            name_beg = i;

         if ((name_beg >= 0) && (!is_namechar[chrnxt]) && (chrnxt != '\\'))
            name_end = i;

         if ((name_beg == -1) && (dir_state==0))   // output BYTE if not part
            {                                      // of identifier or PP dir
            chr_cnt += (!is_whitespace[chr]);
            outbuf[out++] = chr;                   
            }
         
         if (name_end == -1) continue;             // (not end of identifier)

         str = &inbuf[name_beg];                   // - at end of identifier -
         strend = &inbuf[name_end+1];              // make temporary string
         strsave = *strend;
         *strend = 0;

         switch (dir_state)
            {
            case 0:                                // state 0: parsing name
               if ((entry = DICT_lookup(PP->MAC,str)) == NULL)
                  text = str;
               else
                  text = entry->def;

               outbuf[out] = 0;                    // output identifier (or
               strcat(outbuf,text);                // expanded macro) 
               out+=strlen(text);

               for (j=0;j<strlen(text);j++)
                  chr_cnt += (!is_whitespace[text[j]]);
               break;

            case 1:                                // state 1: expecting
               for (j=0;j<NPDN;j++)                // directive name lexeme
                  if (!strcmp(PD_keywords[j],str)) break;

               switch (j)
                  {
                  case PP_DEFINE:
                  case PP_UNDEF:
                     if (!PP->IF->condition)           
                        dir_state = -1;
                     else
                        {
                        dir_type = j;                 
                        dir_state = 2;
                        }
                     break;

                  case PP_INCLUDE:
                     if (!PP->IF->condition)           
                        dir_state = -1;
                     else
                        {                          
                        inc_out = out;             
                        dir_state = 0;
                        }
                     break;

                  case PP_DEPEND:
                     if (!PP->IF->condition)
                        dir_state = -1;
                     else
                        {
                        depend = 1;
                        inc_out = out;
                        dir_state = 0;
                        }
                     break;

                  case PP_IFDEF:
                  case PP_IFNDEF:
                     dir_state = -1;
                     if (!IF_push(PP->IF))
                        report(E_ERROR,TF_line_info(&PP->cur),MSG_NTD);
                     else
                        if (PP->IF->condition)
                           {
                           dir_type = j;
                           dir_state = 2;
                           }
                     break;

                  case PP_ELSE:
                     if (!IF_else(PP->IF))
                        report(E_ERROR,TF_line_info(&PP->cur),MSG_UPE);
                     dir_state = -1;
                     break;

                  case PP_ENDIF:
                     if (!IF_pop(PP->IF))
                        report(E_ERROR,TF_line_info(&PP->cur),MSG_UPI);
                     dir_state = -1;
                     break;

                  case PP_VERBOSE:
                     write_text_line(PP->out.handle,"<\n");
                     dir_state = -1;
                     break;

                  case PP_BRIEF:
                     write_text_line(PP->out.handle,">\n");
                     dir_state = -1;
                     break;

                  default:                         // unknown directive, skip it
                     report(E_WARN,TF_line_info(&PP->cur),MSG_UPD,str);
                     dir_state = -1;
                     break;
                  }
               break;

            case 2:                                // state 2: expecting
               switch (dir_type)                   // 1st directive argument
                  {
                  case PP_DEFINE:                  // #define: create macro
                     if (DICT_lookup(PP->MAC,str) != NULL)
                        {
                        report(E_NOTICE,TF_line_info(&PP->cur),MSG_RDF,str);
                        DICT_delete(PP->MAC,str);
                        }
                     cur_mac = DICT_enter(PP->MAC,str,D_DEFHEAP);
                     mac_out = out;
                     dir_state = 0;                // set up to parse macro's
                     break;                        // expansion as text string

                  case PP_UNDEF:                   // #undef: delete macro
                     if (DICT_lookup(PP->MAC,str) == NULL)
                        report(E_NOTICE,TF_line_info(&PP->cur),MSG_MND,str);
                     else
                        DICT_delete(PP->MAC,str);
                     dir_state = -1;
                     break;

                  case PP_IFDEF:                   // #ifdef
                     PP->IF->condition = (DICT_lookup(PP->MAC,str) != NULL);
                     dir_state = -1;
                     break;

                  case PP_IFNDEF:                 // #ifndef
                     PP->IF->condition = (DICT_lookup(PP->MAC,str) == NULL);
                     dir_state = -1;
                     break;
                  }
               break;
            }     

         name_beg = -1;                            // reset name flags & 
         name_end = -1;                            // restore next character
         *strend = strsave;

         if (dir_state == -1)                      // skip unused PP dir text
            break;
         }                                         // (next i)

      if (!PP->IF->condition) continue;

      outbuf[out] = 0;                             // terminate output string

      if (dir_state == 2)                          // check argument presence
         report(E_ERROR,TF_line_info(&PP->cur),MSG_MIA);

      if (qpcc || qpsl || qptl)                    // check quote parity
         report(E_ERROR,TF_line_info(&PP->cur),MSG_UCC);

      if (inc_out != -1)                           // #include or #depend
         {                                         // requested
         for (j=out-1;j>=inc_out;j--)              
            if ((outbuf[j] == '\"') || (outbuf[j]=='>'))
               {
               outbuf[j] = 0;                      // kill trailing delimiter
               break;
               }
         
         while (is_whitespace[outbuf[inc_out]])    // kill leading spaces
            inc_out++;

         j=0;                                      // (assume "" spec type)
         if (outbuf[inc_out] == '<')               // kill leading non-name
            {                                      // char
            j = 1;
            inc_out++;
            }
         else if (outbuf[inc_out] == '"')
            inc_out++;

         if (inc_out == out)
            {
            report(E_ERROR,TF_line_info(&PP->cur),MSG_MIF);
            continue;
            }

         strcpy(inbuf,&outbuf[inc_out]);           // build pathname

         if (j && (getenv(INC_NAME) != NULL))      // use inbuf for workspace
            {
            strcpy(inbuf,getenv(INC_NAME));
            for (k=0;k<strlen(inbuf);k++)
               if (inbuf[k]==';') inbuf[k]=0;
            strcat(inbuf,"\\");
            strcat(inbuf,&outbuf[inc_out]);
            if (!verify_file(inbuf))
               strcpy(inbuf,&outbuf[inc_out]);
            }

         if (depend)                               // register #depend file...
            {                                      // write dummy "line 0" to
            strcat(inbuf," 0: \n");                // output file to force
            write_text_line(PP->out.handle,inbuf); // autodependency tracking
            }
         else
            {
            str = str_alloc(inbuf);                // process #include file
            include = *PP;                         // via recursive call
            include.parent = PP->cur;                 
            ++include.depth;
            include.cur.name = str;
            include.cur.line = 0;
            PP_process(&include);
            mem_free(str);
            }

         continue;
         }

      if (cur_mac != NULL)                         
         {                                         // #defining macro
         while (is_whitespace[outbuf[mac_out]]) mac_out++;
         for (j=out-1;j>=mac_out;j--)
            if (is_whitespace[outbuf[j]])          // kill leading/trailing
               outbuf[j] = 0;                      // whitespace
            else
               break;
         cur_mac->def = str_alloc(&outbuf[mac_out]);
         continue;
         }

      if (chr_cnt)                                 // write line and
         {                                         // descriptor to output
         for (j=strlen(outbuf);j;j--)              // file
            if (!is_whitespace[outbuf[j-1]])
               {                                   // kill trailing whitespace
               outbuf[j] = 0;                      
               break;
               }
      
         if (!PP->ws_flag)
            {
            i = strlen(outbuf);                       
            for (j=org_out;j<i;j++)
               if (!is_whitespace[outbuf[j]])      // find first non-space
                  break;

            for (m=org_out;j<i;m++,j++)            // remove leading spaces
               outbuf[m] = outbuf[j];
            outbuf[m] = 0;
            }

         strcat(outbuf,"\n");                      
         write_text_line(PP->out.handle,outbuf);
         }
      }

   if (com_blk)
      report(E_ERROR,NULL,MSG_OCO);

   close_text_file(PP->cur.handle);
}
