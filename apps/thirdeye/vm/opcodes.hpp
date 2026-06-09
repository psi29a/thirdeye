/*
 * opcodes.hpp
 *
 * AESOP SOP bytecode opcode set (AESOP/16 & AESOP/32).
 *
 * The 88 opcodes 0x00..0x57 are 1:1 with:
 *   - files/bytecode.def                         (param-count / param-types table)
 *   - eob3_research/runtime/RT.ASM "op_dispatch" (the original dispatch vector)
 * Anything >= 0x58 is an invalid opcode (RT.ASM traps it via "invalid_opcode").
 *
 * Param-type legend (matches bytecode.def): b=byte w=word l=long
 * i=import-table ref (word) m=message-table ref (word). CASE is variable-length
 * and decoded specially.
 */

#ifndef THIRDEYE_VM_OPCODES_HPP
#define THIRDEYE_VM_OPCODES_HPP

#include <cstdint>

namespace VM {

enum class Op : uint8_t {
	BRT  = 0x00, // BRanch if True            (w target)
	BRF  = 0x01, // BRanch if False           (w target)
	BRA  = 0x02, // BRanch Always             (w target)
	CASE = 0x03, // CASE selection            (special: word count, count*(long val,word target), word default)
	PUSH = 0x04, // PUSH 0 onto stack         (parameter delimiter for CALL/SEND)
	DUP  = 0x05, // DUPlicate top of stack
	NOT  = 0x06, // Logical NOT (unary)
	SETB = 0x07, // SET Boolean value (unary)
	NEG  = 0x08, // NEGate (unary)
	ADD  = 0x09, // ADD (binary)
	SUB  = 0x0A, // SUBtract (binary)
	MUL  = 0x0B, // MULtiply (binary)
	DIV  = 0x0C, // DIVide (binary, signed)
	MOD  = 0x0D, // MODulus (binary, signed)
	EXP  = 0x0E, // EXPonent (binary)
	BAND = 0x0F, // Bitwise AND (binary)
	BOR  = 0x10, // Bitwise OR (binary)
	XOR  = 0x11, // Bitwise XOR (binary)
	BNOT = 0x12, // Bitwise NOT (unary)
	SHL  = 0x13, // SHift Left (binary)
	SHR  = 0x14, // SHift Right (binary)
	LT   = 0x15, // Less Than (binary)
	LE   = 0x16, // Less than or Equal (binary)
	EQ   = 0x17, // EQual (binary)
	NE   = 0x18, // Not Equal (binary)
	GE   = 0x19, // Greater than or Equal (binary)
	GT   = 0x1A, // Greater Than (binary)
	INC  = 0x1B, // INCrement (unary)
	DEC  = 0x1C, // DECrement (unary)
	SHTC = 0x1D, // Load SHorT Constant       (b)
	INTC = 0x1E, // Load INTeger Constant     (w)
	LNGC = 0x1F, // Load LoNG Constant        (l)
	RCRS = 0x20, // Reference Code ReSource address (i)   -- needs link/extern table
	CALL = 0x21, // CALL runtime function     (b argcount) -- needs runtime-fn library
	SEND = 0x22, // SEND message              (b argcount, m message) -- needs object system
	PASS = 0x23, // PASS message to parent    (b argcount) -- needs object system
	JSR  = 0x24, // Jump to SubRoutine        (w target)
	RTS  = 0x25, // ReTurn from Subroutine
	AIM  = 0x26, // Array Index Multiply      (w)
	AIS  = 0x27, // Array Index Shift         (b)
	LTBA = 0x28, // Load Table Byte Array     (w)
	LTWA = 0x29, // Load Table Word Array     (w)
	LTDA = 0x2A, // Load Table Dword Array    (w)
	LETA = 0x2B, // Load Effective Table Address (w)
	LAB  = 0x2C, // Load Auto Byte            (w offset)
	LAW  = 0x2D, // Load Auto Word            (w offset)
	LAD  = 0x2E, // Load Auto Dword           (w offset)
	SAB  = 0x2F, // Store Auto Byte           (w offset)
	SAW  = 0x30, // Store Auto Word           (w offset)
	SAD  = 0x31, // Store Auto Dword          (w offset)
	LABA = 0x32, // Load Auto Byte Array      (w offset)
	LAWA = 0x33, // Load Auto Word Array      (w offset)
	LADA = 0x34, // Load Auto Dword Array     (w offset)
	SABA = 0x35, // Store Auto Byte Array     (w offset)
	SAWA = 0x36, // Store Auto Word Array     (w offset)
	SADA = 0x37, // Store Auto Dword Array    (w offset)
	LEAA = 0x38, // Load Effective Auto Address (w offset)
	LSB  = 0x39, // Load Static Byte          (w offset)
	LSW  = 0x3A, // Load Static Word          (w offset)
	LSD  = 0x3B, // Load Static Dword         (w offset)
	SSB  = 0x3C, // Store Static Byte         (w offset)
	SSW  = 0x3D, // Store Static Word         (w offset)
	SSD  = 0x3E, // Store Static Dword        (w offset)
	LSBA = 0x3F, // Load Static Byte Array    (w offset)
	LSWA = 0x40, // Load Static Word Array    (w offset)
	LSDA = 0x41, // Load Static Dword Array   (w offset)
	SSBA = 0x42, // Store Static Byte Array   (w offset)
	SSWA = 0x43, // Store Static Word Array   (w offset)
	SSDA = 0x44, // Store Static Dword Array  (w offset)
	LESA = 0x45, // Load Effective Static Address (w offset)
	LXB  = 0x46, // Load eXtern Byte          (i)
	LXW  = 0x47, // Load eXtern Word          (i)
	LXD  = 0x48, // Load eXtern Dword         (i)
	SXB  = 0x49, // Store eXtern Byte         (i)
	SXW  = 0x4A, // Store eXtern Word         (i)
	SXD  = 0x4B, // Store eXtern Dword        (i)
	LXBA = 0x4C, // Load eXtern Byte Array    (i)
	LXWA = 0x4D, // Load eXtern Word Array    (i)
	LXDA = 0x4E, // Load eXtern Dword Array   (i)
	SXBA = 0x4F, // Store eXtern Byte Array   (i)
	SXWA = 0x50, // Store eXtern Word Array   (i)
	SXDA = 0x51, // Store eXtern Dword Array  (i)
	LEXA = 0x52, // Load Effective eXtern Address (i)
	SXAS = 0x53, // Set eXtern Array Source
	LECA = 0x54, // Load Effective Code Address (w) -- address of inline string/data in bytecode
	SOLE = 0x55, // Selector for Object List Entry
	END  = 0x56, // END of handler (also used as return)
	BRK  = 0x57, // BReaKpoint for debugging
};

inline constexpr uint8_t kMaxOpcode = 0x57; // anything above this is invalid

// Mnemonic for an opcode (matches bytecode.def). Returns "?" if out of range.
inline const char* opName(Op op) {
	switch (op) {
	case Op::BRT: return "BRT"; case Op::BRF: return "BRF"; case Op::BRA: return "BRA";
	case Op::CASE: return "CASE"; case Op::PUSH: return "PUSH"; case Op::DUP: return "DUP";
	case Op::NOT: return "NOT"; case Op::SETB: return "SETB"; case Op::NEG: return "NEG";
	case Op::ADD: return "ADD"; case Op::SUB: return "SUB"; case Op::MUL: return "MUL";
	case Op::DIV: return "DIV"; case Op::MOD: return "MOD"; case Op::EXP: return "EXP";
	case Op::BAND: return "BAND"; case Op::BOR: return "BOR"; case Op::XOR: return "XOR";
	case Op::BNOT: return "BNOT"; case Op::SHL: return "SHL"; case Op::SHR: return "SHR";
	case Op::LT: return "LT"; case Op::LE: return "LE"; case Op::EQ: return "EQ";
	case Op::NE: return "NE"; case Op::GE: return "GE"; case Op::GT: return "GT";
	case Op::INC: return "INC"; case Op::DEC: return "DEC"; case Op::SHTC: return "SHTC";
	case Op::INTC: return "INTC"; case Op::LNGC: return "LNGC"; case Op::RCRS: return "RCRS";
	case Op::CALL: return "CALL"; case Op::SEND: return "SEND"; case Op::PASS: return "PASS";
	case Op::JSR: return "JSR"; case Op::RTS: return "RTS"; case Op::AIM: return "AIM";
	case Op::AIS: return "AIS"; case Op::LTBA: return "LTBA"; case Op::LTWA: return "LTWA";
	case Op::LTDA: return "LTDA"; case Op::LETA: return "LETA"; case Op::LAB: return "LAB";
	case Op::LAW: return "LAW"; case Op::LAD: return "LAD"; case Op::SAB: return "SAB";
	case Op::SAW: return "SAW"; case Op::SAD: return "SAD"; case Op::LABA: return "LABA";
	case Op::LAWA: return "LAWA"; case Op::LADA: return "LADA"; case Op::SABA: return "SABA";
	case Op::SAWA: return "SAWA"; case Op::SADA: return "SADA"; case Op::LEAA: return "LEAA";
	case Op::LSB: return "LSB"; case Op::LSW: return "LSW"; case Op::LSD: return "LSD";
	case Op::SSB: return "SSB"; case Op::SSW: return "SSW"; case Op::SSD: return "SSD";
	case Op::LSBA: return "LSBA"; case Op::LSWA: return "LSWA"; case Op::LSDA: return "LSDA";
	case Op::SSBA: return "SSBA"; case Op::SSWA: return "SSWA"; case Op::SSDA: return "SSDA";
	case Op::LESA: return "LESA"; case Op::LXB: return "LXB"; case Op::LXW: return "LXW";
	case Op::LXD: return "LXD"; case Op::SXB: return "SXB"; case Op::SXW: return "SXW";
	case Op::SXD: return "SXD"; case Op::LXBA: return "LXBA"; case Op::LXWA: return "LXWA";
	case Op::LXDA: return "LXDA"; case Op::SXBA: return "SXBA"; case Op::SXWA: return "SXWA";
	case Op::SXDA: return "SXDA"; case Op::LEXA: return "LEXA"; case Op::SXAS: return "SXAS";
	case Op::LECA: return "LECA"; case Op::SOLE: return "SOLE"; case Op::END: return "END";
	case Op::BRK: return "BRK";
	}
	return "?";
}

// Short human-readable description of what an opcode does (for trace output).
inline const char* opDesc(Op op) {
	switch (op) {
	case Op::BRT:  return "branch if top is true";
	case Op::BRF:  return "branch if top is false";
	case Op::BRA:  return "branch always";
	case Op::CASE: return "jump table on top-of-stack";
	case Op::PUSH: return "push 0 (reserve a stack slot)";
	case Op::DUP:  return "duplicate top of stack";
	case Op::NOT:  return "logical NOT of top";
	case Op::SETB: return "coerce top to boolean 0/1";
	case Op::NEG:  return "negate top";
	case Op::ADD:  return "add: second + top";
	case Op::SUB:  return "subtract: second - top";
	case Op::MUL:  return "multiply: second * top";
	case Op::DIV:  return "divide: second / top";
	case Op::MOD:  return "modulo: second % top";
	case Op::EXP:  return "power: second ^ top";
	case Op::BAND: return "bitwise AND";
	case Op::BOR:  return "bitwise OR";
	case Op::XOR:  return "bitwise XOR";
	case Op::BNOT: return "bitwise NOT of top";
	case Op::SHL:  return "shift left: second << top";
	case Op::SHR:  return "shift right: second >> top";
	case Op::LT:   return "compare: second < top";
	case Op::LE:   return "compare: second <= top";
	case Op::EQ:   return "compare: second == top";
	case Op::NE:   return "compare: second != top";
	case Op::GE:   return "compare: second >= top";
	case Op::GT:   return "compare: second > top";
	case Op::INC:  return "increment top";
	case Op::DEC:  return "decrement top";
	case Op::SHTC: return "load byte constant";
	case Op::INTC: return "load word constant";
	case Op::LNGC: return "load long constant";
	case Op::RCRS: return "reference runtime function";
	case Op::CALL: return "call runtime function";
	case Op::SEND: return "send message to object";
	case Op::PASS: return "pass message to parent class";
	case Op::JSR:  return "call subroutine";
	case Op::RTS:  return "return from subroutine";
	case Op::AIM:  return "array index *= width";
	case Op::AIS:  return "array index <<= shift";
	case Op::LTBA: return "load table byte[index]";
	case Op::LTWA: return "load table word[index]";
	case Op::LTDA: return "load table dword[index]";
	case Op::LETA: return "table element address";
	case Op::LAB:  return "load auto byte (local/param)";
	case Op::LAW:  return "load auto word (local/param)";
	case Op::LAD:  return "load auto dword (local/param)";
	case Op::SAB:  return "store auto byte (local/param)";
	case Op::SAW:  return "store auto word (local/param)";
	case Op::SAD:  return "store auto dword (local/param)";
	case Op::LABA: return "load auto byte[index]";
	case Op::LAWA: return "load auto word[index]";
	case Op::LADA: return "load auto dword[index]";
	case Op::SABA: return "store auto byte[index]";
	case Op::SAWA: return "store auto word[index]";
	case Op::SADA: return "store auto dword[index]";
	case Op::LEAA: return "auto variable address";
	case Op::LSB:  return "load static byte (object state)";
	case Op::LSW:  return "load static word (object state)";
	case Op::LSD:  return "load static dword (object state)";
	case Op::SSB:  return "store static byte (object state)";
	case Op::SSW:  return "store static word (object state)";
	case Op::SSD:  return "store static dword (object state)";
	case Op::LSBA: return "load static byte[index]";
	case Op::LSWA: return "load static word[index]";
	case Op::LSDA: return "load static dword[index]";
	case Op::SSBA: return "store static byte[index]";
	case Op::SSWA: return "store static word[index]";
	case Op::SSDA: return "store static dword[index]";
	case Op::LESA: return "static variable address";
	case Op::LXB:  return "load extern byte (cross-object)";
	case Op::LXW:  return "load extern word (cross-object)";
	case Op::LXD:  return "load extern dword (cross-object)";
	case Op::SXB:  return "store extern byte (cross-object)";
	case Op::SXW:  return "store extern word (cross-object)";
	case Op::SXD:  return "store extern dword (cross-object)";
	case Op::LXBA: return "load extern byte[index]";
	case Op::LXWA: return "load extern word[index]";
	case Op::LXDA: return "load extern dword[index]";
	case Op::SXBA: return "store extern byte[index]";
	case Op::SXWA: return "store extern word[index]";
	case Op::SXDA: return "store extern dword[index]";
	case Op::LEXA: return "extern variable address";
	case Op::SXAS: return "set extern array source";
	case Op::LECA: return "address of inline code data (e.g. string)";
	case Op::SOLE: return "select object-list entry";
	case Op::END:  return "end of handler / return";
	case Op::BRK:  return "debug breakpoint";
	}
	return "";
}

} // namespace VM

#endif // THIRDEYE_VM_OPCODES_HPP
