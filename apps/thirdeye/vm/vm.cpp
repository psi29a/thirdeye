/*
 * vm.cpp
 *
 * AESOP SOP bytecode interpreter. See vm.hpp for the execution model and the
 * reference (eob3_research/runtime/RT.ASM, proc RT_execute + op_dispatch).
 */

#include "vm.hpp"

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace {
// Two-digit hex like "0x20" for an opcode byte.
std::string hexByte(uint8_t b) {
	char buf[5];
	std::snprintf(buf, sizeof(buf), "0x%02X", b);
	return buf;
}
}

namespace VM {

Interpreter::Interpreter(std::vector<uint8_t> code) : mCode(std::move(code)) {
	// Parse the 14-byte PHDR if present (little-endian). Code begins at offset 14.
	if (mCode.size() >= 14) {
		std::memcpy(&mHeader.static_size, &mCode[0], 2);
		std::memcpy(&mHeader.import_resource, &mCode[2], 4);
		std::memcpy(&mHeader.export_resource, &mCode[6], 4);
		std::memcpy(&mHeader.parent, &mCode[10], 4);
	}
	mStk.assign(kStackBytes, 0);
}

// --- bytecode fetch ---------------------------------------------------------

uint8_t Interpreter::fetch8() {
	if (mPC >= mCode.size())
		throw VmError("PC ran past end of code resource");
	return mCode[mPC++];
}

uint16_t Interpreter::fetch16() {
	uint16_t v = static_cast<uint16_t>(fetch8());
	v |= static_cast<uint16_t>(fetch8()) << 8;
	return v;
}

uint32_t Interpreter::fetch32() {
	uint32_t v = fetch16();
	v |= static_cast<uint32_t>(fetch16()) << 16;
	return v;
}

// --- byte-stack access (little-endian) --------------------------------------

Value Interpreter::readStk(uint32_t off) const {
	uint32_t v;
	std::memcpy(&v, &mStk[off], 4);
	return static_cast<Value>(v);
}

void Interpreter::writeStk(uint32_t off, Value v) {
	uint32_t u = static_cast<uint32_t>(v);
	std::memcpy(&mStk[off], &u, 4);
}

// --- frame entry / public execute -------------------------------------------

void Interpreter::enterFrame(uint32_t handlerOffset, uint16_t thisIndex) {
	mFptr = mSp;                                  // frame base = current SP
	mStk[mFptr - kOffThis] = thisIndex & 0xFF;    // THIS at fptr-2 (16-bit)
	mStk[mFptr - kOffThis + 1] = (thisIndex >> 8) & 0xFF;

	mPC = handlerOffset;
	uint16_t autoSize = fetch16();                // MHDR.auto_size (includes THIS)
	mSp -= autoSize;                              // reserve auto space
	mSp -= kValueSize;                           // first free operand slot
	// mPC now points just past the 2-byte MHDR, at the first instruction.
}

Value Interpreter::execute(uint32_t handlerOffset, uint16_t thisIndex) {
	mSp = kStackBytes;                            // top of (empty) stack
	enterFrame(handlerOffset, thisIndex);
	return run();
}

// --- dispatch loop ----------------------------------------------------------

Value Interpreter::run() {
	for (;;) {
		uint32_t opPC = mPC;
		uint8_t raw = fetch8();
		if (raw > kMaxOpcode)
			throw VmError("invalid opcode " + hexByte(raw) +
			              " (>0x57) at PC=" + std::to_string(opPC) +
			              " -- corrupt code or bad PC");
		Op op = static_cast<Op>(raw);

		if (mTrace)
			std::cout << "  [vm] " << std::setw(5) << opPC << "  "
			          << hexByte(raw) << " " << opName(op) << std::endl;

		switch (op) {
		// --- branches (absolute 16-bit target into the resource) ---
		case Op::BRT: { uint16_t t = fetch16(); if (popVal() != 0) mPC = t; break; }
		case Op::BRF: { uint16_t t = fetch16(); if (popVal() == 0) mPC = t; break; }
		case Op::BRA: { mPC = fetch16(); break; }
		case Op::CASE: {
			Value sel = topVal();              // CASE keeps selector? asm reads [edi], pops via branch
			uint16_t n = fetch16();
			uint16_t target = 0;
			bool matched = false;
			for (uint16_t i = 0; i < n; ++i) {
				Value caseVal = static_cast<Value>(fetch32());
				uint16_t caseTarget = fetch16();
				if (!matched && caseVal == sel) { target = caseTarget; matched = true; }
			}
			uint16_t defTarget = fetch16();
			mPC = matched ? target : defTarget;
			break;
		}

		// --- stack management ---
		case Op::PUSH: pushVal(0); break;          // reserve+zero a new top slot
		case Op::DUP:  pushVal(topVal()); break;   // genuine duplicate (+1)

		// --- unary ---
		case Op::NOT:  setTop(topVal() == 0 ? 1 : 0); break;
		case Op::SETB: setTop(topVal() != 0 ? 1 : 0); break;
		case Op::NEG:  setTop(-topVal()); break;
		case Op::BNOT: setTop(~topVal()); break;
		case Op::INC:  setTop(topVal() + 1); break;
		case Op::DEC:  setTop(topVal() - 1); break;

		// --- binary arithmetic/logic (left = second, right = top) ---
		case Op::ADD:  binOp([](Value l, Value r){ return l + r; }); break;
		case Op::SUB:  binOp([](Value l, Value r){ return l - r; }); break;
		case Op::MUL:  binOp([](Value l, Value r){ return l * r; }); break;
		case Op::DIV:  binOp([](Value l, Value r){
			if (r == 0) throw VmError("SOP divide by zero");
			return l / r; }); break;
		case Op::MOD:  binOp([](Value l, Value r){
			if (r == 0) throw VmError("SOP modulo by zero");
			return l % r; }); break;
		case Op::EXP:  binOp([](Value base, Value exp){
			Value res = 1;
			for (Value i = 0; i < exp; ++i) res *= base;  // exp<=0 => 1 (asm: 0 => 1)
			return res; }); break;
		case Op::BAND: binOp([](Value l, Value r){ return l & r; }); break;
		case Op::BOR:  binOp([](Value l, Value r){ return l | r; }); break;
		case Op::XOR:  binOp([](Value l, Value r){ return l ^ r; }); break;
		// SHR is a LOGICAL (unsigned) shift in the original (asm `shr`).
		case Op::SHL:  binOp([](Value l, Value r){ return l << r; }); break;
		case Op::SHR:  binOp([](Value l, Value r){
			return static_cast<Value>(static_cast<uint32_t>(l) >> r); }); break;

		// --- comparisons (signed; push 1/0) ---
		case Op::LT:   binOp([](Value l, Value r){ return l <  r ? 1 : 0; }); break;
		case Op::LE:   binOp([](Value l, Value r){ return l <= r ? 1 : 0; }); break;
		case Op::EQ:   binOp([](Value l, Value r){ return l == r ? 1 : 0; }); break;
		case Op::NE:   binOp([](Value l, Value r){ return l != r ? 1 : 0; }); break;
		case Op::GE:   binOp([](Value l, Value r){ return l >= r ? 1 : 0; }); break;
		case Op::GT:   binOp([](Value l, Value r){ return l >  r ? 1 : 0; }); break;

		// --- constant loads (overwrite top slot in place) ---
		case Op::SHTC: setTop(static_cast<Value>(fetch8()));  break;
		case Op::INTC: setTop(static_cast<Value>(fetch16())); break;
		case Op::LNGC: setTop(static_cast<Value>(fetch32())); break;

		// --- auto (local/param) scalar load/store; byte-addressed at fptr-off ---
		case Op::LAB: { uint16_t off = fetch16();
			setTop(static_cast<int8_t>(mStk[mFptr - off])); break; }       // sign-extend
		case Op::LAW: { uint16_t off = fetch16();
			int16_t w; std::memcpy(&w, &mStk[mFptr - off], 2);
			setTop(w); break; }                                            // sign-extend
		case Op::LAD: { uint16_t off = fetch16();
			setTop(readStk(mFptr - off)); break; }
		case Op::SAB: { uint16_t off = fetch16();
			mStk[mFptr - off] = static_cast<uint8_t>(topVal()); break; }    // no pop
		case Op::SAW: { uint16_t off = fetch16();
			uint16_t w = static_cast<uint16_t>(topVal());
			std::memcpy(&mStk[mFptr - off], &w, 2); break; }               // no pop
		case Op::SAD: { uint16_t off = fetch16();
			writeStk(mFptr - off, topVal()); break; }                      // no pop
		case Op::LEAA: { uint16_t off = fetch16();
			setTop(static_cast<Value>(mFptr - off)); break; }              // effective addr

		// --- inline code address (e.g. address of an inline string) ---
		case Op::LECA: setTop(static_cast<Value>(fetch16())); break;

		// --- procedures ---
		case Op::JSR: {
			uint16_t target = fetch16();
			// save return PC, SP, frame on the byte stack (mirrors do_JSR pushes)
			pushVal(static_cast<Value>(mPC));
			pushVal(static_cast<Value>(mSp + kValueSize)); // SP before this push set
			pushVal(static_cast<Value>(mFptr));
			enterFrame(target, /*thisIndex preserved via fptr-2*/ 0);
			break;
		}
		case Op::RTS: {
			Value ret = topVal();
			// restore frame saved by JSR
			mFptr = static_cast<uint32_t>(popVal());
			mSp   = static_cast<uint32_t>(popVal());
			mPC   = static_cast<uint32_t>(popVal());
			setTop(ret);
			break;
		}

		case Op::END:
			return topVal();

		// --- not yet implemented (need object system / runtime fns / link) ---
		case Op::RCRS: case Op::CALL: case Op::SEND: case Op::PASS:
		case Op::AIM:  case Op::AIS:
		case Op::LTBA: case Op::LTWA: case Op::LTDA: case Op::LETA:
		case Op::LABA: case Op::LAWA: case Op::LADA:
		case Op::SABA: case Op::SAWA: case Op::SADA:
		case Op::LSB:  case Op::LSW:  case Op::LSD:
		case Op::SSB:  case Op::SSW:  case Op::SSD:
		case Op::LSBA: case Op::LSWA: case Op::LSDA:
		case Op::SSBA: case Op::SSWA: case Op::SSDA: case Op::LESA:
		case Op::LXB:  case Op::LXW:  case Op::LXD:
		case Op::SXB:  case Op::SXW:  case Op::SXD:
		case Op::LXBA: case Op::LXWA: case Op::LXDA:
		case Op::SXBA: case Op::SXWA: case Op::SXDA:
		case Op::LEXA: case Op::SXAS: case Op::SOLE: case Op::BRK:
			unimplemented(op);
		}
	}
}

void Interpreter::unimplemented(Op op) {
	throw VmError(std::string("opcode ") + opName(op) + " (" +
	              hexByte(static_cast<uint8_t>(op)) + ") not implemented yet "
	              "(needs object system / runtime-function library / extern link layer)");
}

// --- self test --------------------------------------------------------------

bool Interpreter::selfTest() {
	// Build a code buffer: 14-byte PHDR + handler. Handler MHDR auto_size = 4
	// (2 for THIS + 2 for one auto word at fptr-4). Code begins at offset 16.
	auto makeProg = [](std::vector<uint8_t> body) {
		std::vector<uint8_t> c(14, 0);          // PHDR (zeros ok for this test)
		c.push_back(0x04); c.push_back(0x00);   // MHDR auto_size = 4
		c.insert(c.end(), body.begin(), body.end());
		return c;
	};
	auto S = [](uint8_t op){ return op; };
	bool ok = true;
	auto check = [&](const char* name, std::vector<uint8_t> body, Value expect) {
		Interpreter vm(makeProg(body));
		try {
			Value got = vm.execute(14);
			if (got != expect) {
				std::cerr << "VM selfTest FAIL: " << name << " expected "
				          << expect << " got " << got << "\n";
				ok = false;
			}
		} catch (const std::exception& e) {
			std::cerr << "VM selfTest FAIL: " << name << " threw: " << e.what() << "\n";
			ok = false;
		}
	};

	using O = Op;
	// 5 + 3 = 8
	check("add", { S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),5,
	               S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),3,
	               S((uint8_t)O::ADD),  S((uint8_t)O::END) }, 8);
	// 10 - 3 = 7  (left-right ordering)
	check("sub", { S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),10,
	               S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),3,
	               S((uint8_t)O::SUB),  S((uint8_t)O::END) }, 7);
	// 20 / 6 = 3  (signed integer divide, left/right)
	check("div", { S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),20,
	               S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),6,
	               S((uint8_t)O::DIV),  S((uint8_t)O::END) }, 3);
	// 2 < 5 -> 1
	check("lt",  { S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),2,
	               S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),5,
	               S((uint8_t)O::LT),   S((uint8_t)O::END) }, 1);
	// store 42 to auto word at fptr-4, then load it back
	check("auto", { S((uint8_t)O::PUSH), S((uint8_t)O::SHTC),42,
	                S((uint8_t)O::SAW), 4,0,                 // store, leaves 42 on stack
	                S((uint8_t)O::PUSH), S((uint8_t)O::LAW), 4,0, // reload into fresh slot
	                S((uint8_t)O::END) }, 42);

	if (ok) std::cerr << "VM selfTest: all passed\n";
	return ok;
}

} // namespace VM
