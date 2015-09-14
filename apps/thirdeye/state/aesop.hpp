/*
 * aesop.hpp
 *
 *  Created on: Aug 19, 2015
 *      Author: bcurtis
 */

#include <stdint.h>
#include <vector>
#include <map>
#include <memory>
#include <stack>

#include <boost/filesystem.hpp>

#include "sop.hpp"
#include "../resources/res.hpp"

#ifndef AESOP_HPP
#define AESOP_HPP

namespace STATE {

#define INDEX_CREATE    0   /* this is the index of the 'create' function in the 'start' SOP

/* SOP OPERANDS */
#define OP_BRT  0x00    /* 00   BRT   word            BRanch if True   */
#define OP_BRF  0x01    /* 01   BRF   word            BRanch if False  */
#define OP_BRA  0x02    /* 02   BRA   word            BRanch Always    */
#define OP_CASE 0x03    /* 03   CASE  (see below)     CASE selection   */
#define OP_PUSH 0x04    /* 04   PUSH  -               PUSH 0 onto stack (used as a parameter delimiter for CALL, SEND) */
#define OP_DUP  0x05    /* 05   DUP   -               DUPlicate top of stack    */
#define OP_NOT  0x06    /* 06   NOT   -               Logical NOT (unary)       */
#define OP_SETB 0x07    /* 07   SETB  -               SET Boolean value (unary) */
#define OP_NEG  0x08    /* 08   NEG   -               NEGate (unary)    */
#define OP_ADD  0x09    /* 09   ADD   -               ADD (binary)      */
#define OP_SUB  0x0A    /* 0A   SUB   -               SUBtract (binary) */
#define OP_MUL  0x0B    /* 0B   MUL   -               MULtiply (binary) */
#define OP_DIV  0x0C    /* 0C   DIV   -               DIVide (binary)   */
#define OP_MOD  0x0D    /* 0D   MOD   -               MODulus (binary)  */
#define OP_EXP  0x0E    /* 0E   EXP   -               EXPonent (binary) */
#define OP_BAND 0x0F    /* 0F   BAND  -               Bitwise AND (binary)      */
#define OP_BOR  0x10    /* 10   BOR   -               Bitwise OR (binary)       */
#define OP_XOR  0x11    /* 11   XOR   -               Bitwise XOR (binary)      */
#define OP_BNOT 0x12    /* 12   BNOT  -               Bitwise NOT (unary)       */
#define OP_SHL  0x13    /* 13   SHL   -               SHift Left (binary)       */
#define OP_SHR  0x14    /* 14   SHR   -               SHift Right (binary)      */
#define OP_LT   0x15    /* 15   LT    -               Less Than (binary)        */
#define OP_LE   0x16    /* 16   LE    -               Less than or Equal (binary)   */
#define OP_EQ   0x17    /* 17   EQ    -               EQual (binary)    */
#define OP_NE   0x18    /* 18   NE    -               Not Equal (binary)        */
#define OP_GE   0x19    /* 19   GE    -               Greater than or Equal (binary)    */
#define OP_GT   0x1A    /* 1A   GT    -               Greather Than (binary)    */
#define OP_INC  0x1B    /* 1B   INC   -               INCrement (unary) */
#define OP_DEC  0x1C    /* 1C   DEC   -               DECrement (unary) */
#define OP_SHTC 0x1D    /* 1D   SHTC  byte            Load SHorT Constant (0...256) */
#define OP_INTC 0x1E    /* 1E   INTC  word            Load INTeger Constant (256...64K) */
#define OP_LNGC 0x1F    /* 1F   LNGC  long            Load LoNG Constant (64K...4G) */
#define OP_RCRS 0x20    /* 20   RCRS  word            Reference Code ReSource address (auxiliary for CALL)  */
#define OP_CALL 0x21    /* 21   CALL  byte            CALL code resource handler  (call of the runtime function)    */
#define OP_SEND 0x22    /* 22   SEND  byte, word      SEND message (sending message to another object - it in fact calls an object method)  */
#define OP_PASS 0x23    /* 23   PASS  byte            PASS message to parent class */
#define OP_JSR  0x24    /* 24   JSR   word            Jump to SubRoutine        */
#define OP_RTS  0x25    /* 25   RTS   -               ReTurn from Subroutine    */
#define OP_AIM  0x26    /* 26   AIM   word            Array Index Multiply      */
#define OP_AIS  0x27    /* 27   AIS   byte            Array Index Shift         */
#define OP_LTBA 0x28    /* 28   LTBA  word            Load Table Byte Array     */
#define OP_LTWA 0x29    /* 29   LTWA  word            Load Table Word Array     */
#define OP_LTDA 0x2A    /* 2A   LTDA  word            Load Table Dword Array    */
#define OP_LETA 0x2B    /* 2B   LETA  word            Load Effective Table Address  */
#define OP_LAB  0x2C    /* 2C   LAB   word            Load Auto Byte ("auto" works with parameters/local variables) */
#define OP_LAW  0x2D    /* 2D   LAW   word            Load Auto Word    */
#define OP_LAD  0x2E    /* 2E   LAD   word            Load Auto Dword   */
#define OP_SAB  0x2F    /* 2F   SAB   word            Store Auto Byte   */
#define OP_SAW  0x30    /* 30   SAW   word            Store Auto Word   */
#define OP_SAD  0x31    /* 31   SAD   word            Store Auto Dword  */
#define OP_LABA 0x32    /* 32   LABA  word            Load Auto Byte Array      */
#define OP_LAWA 0x33    /* 33   LAWA  word            Load Auto Word Array      */
#define OP_LADA 0x34    /* 34   LADA  word            Load Auto Dword Array     */
#define OP_SABA 0x35    /* 35   SABA  word            Store Auto Byte Array     */
#define OP_SAWA 0x36    /* 36   SAWA  word            Store Auto Word Array     */
#define OP_SADA 0x37    /* 37   SADA  word            Store Auto Dword Array    */
#define OP_LEAA 0x38    /* 38   LEAA  word            Load Effective Auto Address   */
#define OP_LSB  0x39    /* 39   LSB   word            Load Static Byte  */
#define OP_LSW  0x3A    /* 3A   LSW   word            Load Static Word  */
#define OP_LSD  0x3B    /* 3B   LSD   word            Load Static Dword */
#define OP_SSB  0x3C    /* 3C   SSB   word            Store Static Byte */
#define OP_SSW  0x3D    /* 3D   SSW   word            Store Static Word */
#define OP_SSD  0x3E    /* 3E   SSD   word            Store Static Dword        */
#define OP_LSBA 0x3F    /* 3F   LSBA  word            Load Static Byte Array    */
#define OP_LSWA 0x40    /* 40   LSWA  word            Load Static Word Array    */
#define OP_LSDA 0x41    /* 41   LSDA  word            Load Static Dword Array   */
#define OP_SSBA 0x42    /* 42   SSBA  word            Store Static Byte Array   */
#define OP_SSWA 0x43    /* 43   SSWA  word            Store Static Word Array   */
#define OP_SSDA 0x44    /* 44   SSDA  word            Store Static Dword Array  */
#define OP_LESA 0x45    /* 45   LESA  word            Load Effective Static Address */
#define OP_LXB  0x46    /* 46   LXB   word            Load eXtern Byte  */
#define OP_LXW  0x47    /* 47   LXW   word            Load eXtern Word  */
#define OP_LXD  0x48    /* 48   LXD   word            Load eXtern Dword */
#define OP_SXB  0x49    /* 49   SXB   word            Store eXtern Byte */
#define OP_SXW  0x4A    /* 4A   SXW   word            Store eXtern Word */
#define OP_SXD  0x4B    /* 4B   SXD   word            Store eXtern Dword        */
#define OP_LXBA 0x4C    /* 4C   LXBA  word            Load eXtern Byte Array    */
#define OP_LXWA 0x4D    /* 4D   LXWA  word            Load eXtern Word Array    */
#define OP_LXDA 0x4E    /* 4E   LXDA  word            Load eXtern Dword Array   */
#define OP_SXBA 0x4F    /* 4F   SXBA  word            Store eXtern Byte Array   */
#define OP_SXWA 0x50    /* 50   SXWA  word            Store eXtern Word Array   */
#define OP_SXDA 0x51    /* 51   SXDA  word            Store eXtern Dword Array  */
#define OP_LEXA 0x52    /* 52   LEXA  word            Load Effective eXtern Address */
#define OP_SXAS 0x53    /* 53   SXAS  -               Set eXtern Array Source   */
#define OP_LECA 0x54    /* 54   LECA  word            Load Effective Code Address  (used to address direct string in bytecode)  */
#define OP_SOLE 0x55    /* 55   SOLE  -               Selector for Object List Entry    */
#define OP_END  0x56    /* 56   END   -               END of handler (end of handler, used also as return)  */
#define OP_BRK  0x57    /* 57   BRK   -               BReaKpoint for debugging  */


#if defined(__GNUC__)
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#elif defined(_MSC_VER )
#define PACK(__Declaration__) __pragma(pack(push,1)) __Declaration__ __pragma(pack(pop))
#else
#error "Unknown platform!"
#endif


class Aesop
{
    RESOURCES::Resource &mRes;
    boost::filesystem::path resPath;
    std::map<uint16_t, std::shared_ptr<SOP>> mSOP;
    uint16_t mCurrentSOP;
    std::stack<std::vector<uint8_t>> mStack;
    std::vector<uint8_t> mStaticVariable;
    std::vector<uint8_t> mLocalVariable;

private:
    void do_BRA();
    void do_BRT();
    void do_BRF();
    void do_CASE();
    void do_CALL();
    void do_PUSH();
    void do_RCRS();

public:
    Aesop(RESOURCES::Resource &resource);
    virtual ~Aesop();
    void run();
};


}

#endif /* AESOP_HPP */
