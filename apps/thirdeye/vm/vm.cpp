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
#include <sstream>

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
	// kStackGuard bytes of slack above the logical top: AESOP loads THIS (a word
	// at fptr-2) with a dword opcode, reading 2 bytes past the top of stack. The
	// original tolerated this because stk_off started at the top of a malloc'd
	// block with adjacent heap slack; we make that slack explicit so a benign
	// boundary straddle doesn't trip the bounds check while a real runaway still
	// does. mSp still starts at kStackBytes (see execute()).
	mStk.assign(kStackBytes + kStackGuard, 0);
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

// Guard every operand-stack access. The original ran on a fixed STK_SIZE block
// of raw memory; here mStk is a heap vector, so an out-of-range index (mSp
// marching past the ends -- e.g. a handler that nets pops because it loops on a
// stubbed runtime function returning the "wrong" value) would corrupt the heap.
// We trade the original's raw access for a clean VmError during bring-up.
void Interpreter::boundsCheck(uint32_t off, uint32_t size) const {
	if (static_cast<uint64_t>(off) + size > mStk.size())
		throw VmError("VM operand-stack access out of range (offset " +
		              std::to_string(off) + ", size " + std::to_string(size) +
		              ") -- stack overflow/underflow, likely a handler looping "
		              "on a stubbed runtime function");
}

Value Interpreter::readStk(uint32_t off) const {
	boundsCheck(off, 4);
	uint32_t v;
	std::memcpy(&v, &mStk[off], 4);
	return static_cast<Value>(v);
}

void Interpreter::writeStk(uint32_t off, Value v) {
	boundsCheck(off, 4);
	uint32_t u = static_cast<uint32_t>(v);
	std::memcpy(&mStk[off], &u, 4);
}

// --- frame entry / public execute -------------------------------------------

void Interpreter::enterFrame(uint32_t handlerOffset, uint16_t thisIndex,
                             const std::vector<Value>& args) {
	// Parameters sit just above the frame base: arg1 at fptr+0, arg2 at fptr+4,
	// ... (so the handler reads them via negative auto offsets). THIS sits at
	// fptr-2; locals + operand stack grow downward below the frame base.
	uint32_t n = static_cast<uint32_t>(args.size());
	mFptr = mSp - n * kValueSize;
	for (uint32_t i = 0; i < n; ++i)
		writeStk(mFptr + i * kValueSize, args[i]);

	mStk[mFptr - kOffThis] = thisIndex & 0xFF;    // THIS at fptr-2 (16-bit)
	mStk[mFptr - kOffThis + 1] = (thisIndex >> 8) & 0xFF;

	mPC = handlerOffset;
	uint16_t autoSize = fetch16();                // MHDR.auto_size (includes THIS)
	mSp = mFptr - autoSize - kValueSize;          // first free operand slot
	// mPC now points just past the 2-byte MHDR, at the first instruction.
}

Value Interpreter::execute(uint32_t handlerOffset, uint16_t thisIndex,
                           const std::vector<Value>& args) {
	mSp = kStackBytes;                            // top of (empty) stack
	enterFrame(handlerOffset, thisIndex, args);
	return run();
}

// --- dispatch loop ----------------------------------------------------------

Value Interpreter::run() {
	uint64_t steps = 0;
	for (;;) {
		if (mMaxSteps && ++steps > mMaxSteps)
			throw VmError("instruction budget exceeded (" +
			              std::to_string(mMaxSteps) +
			              ") -- likely an infinite loop (a stubbed runtime "
			              "function not advancing real state?)");
		uint32_t opPC = mPC;
		uint8_t raw = fetch8();
		if (raw > kMaxOpcode)
			throw VmError("invalid opcode " + hexByte(raw) +
			              " (>0x57) at PC=" + std::to_string(opPC) +
			              " -- corrupt code or bad PC");
		Op op = static_cast<Op>(raw);

		if (mTrace)
			std::cout << "  [vm] " << std::setw(5) << opPC << "  "
			          << hexByte(raw) << "  " << describe(opPC) << std::endl;

		switch (op) {
		// --- branches (absolute 16-bit target into the resource) ---
		// Branches read the top value but do NOT pop it (RT.ASM do_BRT/do_BRF
		// test [edi] without advancing edi). The leftover value is overwritten
		// in place by the next load/constant -- popping here would drift the
		// stack upward one slot per branch (a real bug over long loops).
		case Op::BRT: { uint16_t t = fetch16(); if (topVal() != 0) mPC = t; break; }
		case Op::BRF: { uint16_t t = fetch16(); if (topVal() == 0) mPC = t; break; }
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
		// Offsets are SIGNED (see autoAddr): locals/THIS below fptr, params above.
		case Op::LAB: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()));
			boundsCheck(p, 1);
			setTop(static_cast<int8_t>(mStk[p])); break; }                 // sign-extend
		case Op::LAW: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()));
			boundsCheck(p, 2);
			int16_t w; std::memcpy(&w, &mStk[p], 2);
			setTop(w); break; }                                            // sign-extend
		case Op::LAD: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()));
			setTop(readStk(p)); break; }
		case Op::SAB: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()));
			boundsCheck(p, 1);
			mStk[p] = static_cast<uint8_t>(topVal()); break; }             // no pop
		case Op::SAW: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()));
			boundsCheck(p, 2);
			uint16_t w = static_cast<uint16_t>(topVal());
			std::memcpy(&mStk[p], &w, 2); break; }                         // no pop
		case Op::SAD: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()));
			writeStk(p, topVal()); break; }                                // no pop
		case Op::LEAA: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()));
			setTop(makeAddr(AddrSpace::Stack, p)); break; }                // effective addr

		// --- inline code address (e.g. address of an inline string) ---
		case Op::LECA: setTop(makeAddr(AddrSpace::Code, fetch16())); break;

		// --- procedures ---
		case Op::JSR: {
			uint16_t target = fetch16();
			// A subroutine runs in the current object context: preserve THIS.
			uint16_t curThis = static_cast<uint16_t>(
				mStk[mFptr - kOffThis] | (mStk[mFptr - kOffThis + 1] << 8));
			// save return PC, SP, frame on the byte stack (mirrors do_JSR pushes)
			pushVal(static_cast<Value>(mPC));
			pushVal(static_cast<Value>(mSp + kValueSize)); // SP before this push set
			pushVal(static_cast<Value>(mFptr));
			enterFrame(target, curThis, {});
			break;
		}
		case Op::RTS: {
			// JSR saved (PC, SP, Fptr) just *above* the procedure's frame base, so
			// after enterFrame `mFptr` points straight at the saved Fptr and the
			// other two sit one/two slots higher. Read them there -- NOT from the
			// procedure's current operand top (mSp), which is deep below the auto
			// frame + temporaries. (The old code popped from mSp and also clobbered
			// its own read pointer via `mSp = popVal()`, so it only worked for
			// trivial zero-local procedures.) JSR is net +1: leave the procedure's
			// return value on the caller's operand stack, like CALL.
			Value ret = topVal();
			uint32_t savedFptr = static_cast<uint32_t>(readStk(mFptr));
			uint32_t savedSp   = static_cast<uint32_t>(readStk(mFptr + kValueSize));
			uint32_t savedPc   = static_cast<uint32_t>(readStk(mFptr + 2 * kValueSize));
			mFptr = savedFptr;
			mPC   = savedPc;
			mSp   = savedSp;
			pushVal(ret);
			break;
		}

		case Op::END:
			return topVal();

		// --- runtime-function call ---
		// RCRS resolves an import entry to a runtime-function reference and
		// pushes it (in place, like a load). The import operand is the runtime
		// function's number; we keep that number as the "handle" on the stack.
		case Op::RCRS: setTop(static_cast<Value>(fetch16())); break;
		// CALL pops `argc` arguments (top == last arg), then the slot below holds
		// the RCRS handle. We dispatch via the runtime hook and the result
		// replaces the handle slot.
		case Op::CALL: {
			uint8_t argc = fetch8();
			std::vector<Value> args(argc);
			for (int i = argc - 1; i >= 0; --i)
				args[i] = popVal();
			Value handle = topVal(); // runtime-function number from RCRS
			setTop(callRuntime(handle, args));
			break;
		}

		// --- object messages ---
		// SEND <argc> <message>: dispatch `message` to the object instance whose
		// handle sits just below the args. Result replaces the instance slot.
		case Op::SEND: {
			uint8_t argc = fetch8();
			uint16_t message = fetch16();
			std::vector<Value> args(argc);
			for (int i = argc - 1; i >= 0; --i)
				args[i] = popVal();
			Value instance = topVal(); // object handle below the args
			if (!mSendHook)
				throw VmError("SEND but no object system is wired in");
			setTop(mSendHook(static_cast<int>(instance), message, args));
			break;
		}
		// PASS <argc>: re-dispatch the CURRENT message to the parent class, using
		// the current THIS. Result replaces the reserved slot below the args.
		case Op::PASS: {
			uint8_t argc = fetch8();
			std::vector<Value> args(argc);
			for (int i = argc - 1; i >= 0; --i)
				args[i] = popVal();
			if (!mPassHook)
				throw VmError("PASS but no object system is wired in");
			setTop(mPassHook(args));
			break;
		}

		// --- array index building (scale + accumulate; see RT.ASM do_AIM/do_AIS) ---
		// idx = pop; top += idx * width  (top is the index accumulator).
		case Op::AIM: { uint16_t width = fetch16(); Value idx = popVal();
			setTop(topVal() + idx * static_cast<Value>(width)); break; }
		case Op::AIS: { uint8_t shift = fetch8(); Value idx = popVal();
			setTop(topVal() + (idx << shift)); break; }

		// --- constant tables (live in the code resource; index from top) ---
		case Op::LTBA: { uint16_t off = fetch16();
			setTop(static_cast<int8_t>(*codePtr(off + topVal(), 1))); break; }
		case Op::LTWA: { uint16_t off = fetch16();
			int16_t w; std::memcpy(&w, codePtr(off + topVal(), 2), 2);
			setTop(w); break; }
		case Op::LTDA: { uint16_t off = fetch16();
			int32_t d; std::memcpy(&d, codePtr(off + topVal(), 4), 4);
			setTop(d); break; }
		// effective address of a constant table (lives in code space)
		case Op::LETA: setTop(makeAddr(AddrSpace::Code, fetch16())); break;

		// --- static (object-state) scalars; per-instance storage ---
		case Op::LSB: { uint16_t off = fetch16();
			setTop(static_cast<int8_t>(*staticPtr(off, 1))); break; }
		case Op::LSW: { uint16_t off = fetch16();
			int16_t w; std::memcpy(&w, staticPtr(off, 2), 2); setTop(w); break; }
		case Op::LSD: { uint16_t off = fetch16();
			int32_t d; std::memcpy(&d, staticPtr(off, 4), 4); setTop(d); break; }
		case Op::SSB: { uint16_t off = fetch16();
			*staticPtr(off, 1) = static_cast<uint8_t>(topVal()); break; }   // no pop
		case Op::SSW: { uint16_t off = fetch16();
			uint16_t w = static_cast<uint16_t>(topVal());
			std::memcpy(staticPtr(off, 2), &w, 2); break; }                // no pop
		case Op::SSD: { uint16_t off = fetch16();
			int32_t d = topVal(); std::memcpy(staticPtr(off, 4), &d, 4); break; } // no pop
		// effective address of a static variable (this instance's object state);
		// the encoded offset is absolute within the instance's static storage
		case Op::LESA:
			setTop(makeAddr(AddrSpace::Static, mStaticBase + fetch16(),
			                currentThis()));
			break;

		// --- static arrays (index from top; index is byte-scaled via AIM/AIS) ---
		case Op::LSBA: { uint16_t off = fetch16();
			setTop(static_cast<int8_t>(*staticPtr(off + topVal(), 1))); break; }
		case Op::LSWA: { uint16_t off = fetch16();
			int16_t w; std::memcpy(&w, staticPtr(off + topVal(), 2), 2);
			setTop(w); break; }
		case Op::LSDA: { uint16_t off = fetch16();
			int32_t d; std::memcpy(&d, staticPtr(off + topVal(), 4), 4);
			setTop(d); break; }
		case Op::SSBA: { uint16_t off = fetch16(); Value data = popVal();
			*staticPtr(off + topVal(), 1) = static_cast<uint8_t>(data);
			setTop(data); break; }                                         // leaves data
		case Op::SSWA: { uint16_t off = fetch16(); Value data = popVal();
			uint16_t w = static_cast<uint16_t>(data);
			std::memcpy(staticPtr(off + topVal(), 2), &w, 2);
			setTop(data); break; }
		case Op::SSDA: { uint16_t off = fetch16(); Value data = popVal();
			std::memcpy(staticPtr(off + topVal(), 4), &data, 4);
			setTop(data); break; }

		// --- auto (local/param) arrays: addr = fptr - offset + index(top) ---
		case Op::LABA: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()))
			                          + topVal();
			boundsCheck(p, 1);
			setTop(static_cast<int8_t>(mStk[p])); break; }                 // sign-extend
		case Op::LAWA: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()))
			                          + topVal();
			boundsCheck(p, 2);
			int16_t w; std::memcpy(&w, &mStk[p], 2); setTop(w); break; }   // sign-extend
		case Op::LADA: { uint32_t p = autoAddr(static_cast<int16_t>(fetch16()))
			                          + topVal();
			setTop(readStk(p)); break; }
		case Op::SABA: { uint16_t off = fetch16(); Value data = popVal();
			uint32_t p = autoAddr(static_cast<int16_t>(off)) + topVal();
			boundsCheck(p, 1);
			mStk[p] = static_cast<uint8_t>(data);
			setTop(data); break; }                                         // leaves data
		case Op::SAWA: { uint16_t off = fetch16(); Value data = popVal();
			uint32_t p = autoAddr(static_cast<int16_t>(off)) + topVal();
			boundsCheck(p, 2);
			uint16_t w = static_cast<uint16_t>(data);
			std::memcpy(&mStk[p], &w, 2); setTop(data); break; }
		case Op::SADA: { uint16_t off = fetch16(); Value data = popVal();
			uint32_t p = autoAddr(static_cast<int16_t>(off)) + topVal();
			writeStk(p, data); setTop(data); break; }

		// --- extern (cross-object) scalars. The operand is a byte offset into
		// the executing class's XR list (resolved by the link layer to an offset
		// in the TARGET object's statics); the target object index is the low
		// word of the top slot, which the result then replaces. ---
		case Op::LXB: { uint16_t xr = fetch16();
			uint16_t obj = static_cast<uint16_t>(topVal());
			setTop(static_cast<int8_t>(*externPtr(obj, xr, 0, 1))); break; }
		case Op::LXW: { uint16_t xr = fetch16();
			uint16_t obj = static_cast<uint16_t>(topVal());
			int16_t w; std::memcpy(&w, externPtr(obj, xr, 0, 2), 2);
			setTop(w); break; }
		case Op::LXD: { uint16_t xr = fetch16();
			uint16_t obj = static_cast<uint16_t>(topVal());
			int32_t d; std::memcpy(&d, externPtr(obj, xr, 0, 4), 4);
			setTop(d); break; }
		case Op::SXB: { uint16_t xr = fetch16(); Value data = popVal();
			uint16_t obj = static_cast<uint16_t>(topVal());
			*externPtr(obj, xr, 0, 1) = static_cast<uint8_t>(data);
			setTop(data); break; }                                         // leaves data
		case Op::SXW: { uint16_t xr = fetch16(); Value data = popVal();
			uint16_t obj = static_cast<uint16_t>(topVal());
			uint16_t w = static_cast<uint16_t>(data);
			std::memcpy(externPtr(obj, xr, 0, 2), &w, 2);
			setTop(data); break; }
		case Op::SXD: { uint16_t xr = fetch16(); Value data = popVal();
			uint16_t obj = static_cast<uint16_t>(topVal());
			std::memcpy(externPtr(obj, xr, 0, 4), &data, 4);
			setTop(data); break; }

		// --- extern arrays: top slot = object index (low word) merged with the
		// byte-scaled array index (high word) by SXAS below ---
		case Op::LXBA: { uint16_t xr = fetch16();
			uint32_t u = static_cast<uint32_t>(topVal());
			setTop(static_cast<int8_t>(
				*externPtr(static_cast<uint16_t>(u), xr, u >> 16, 1))); break; }
		case Op::LXWA: { uint16_t xr = fetch16();
			uint32_t u = static_cast<uint32_t>(topVal());
			int16_t w;
			std::memcpy(&w, externPtr(static_cast<uint16_t>(u), xr, u >> 16, 2), 2);
			setTop(w); break; }
		case Op::LXDA: { uint16_t xr = fetch16();
			uint32_t u = static_cast<uint32_t>(topVal());
			int32_t d;
			std::memcpy(&d, externPtr(static_cast<uint16_t>(u), xr, u >> 16, 4), 4);
			setTop(d); break; }
		case Op::SXBA: { uint16_t xr = fetch16(); Value data = popVal();
			uint32_t u = static_cast<uint32_t>(topVal());
			*externPtr(static_cast<uint16_t>(u), xr, u >> 16, 1) =
				static_cast<uint8_t>(data);
			setTop(data); break; }
		case Op::SXWA: { uint16_t xr = fetch16(); Value data = popVal();
			uint32_t u = static_cast<uint32_t>(topVal());
			uint16_t w = static_cast<uint16_t>(data);
			std::memcpy(externPtr(static_cast<uint16_t>(u), xr, u >> 16, 2), &w, 2);
			setTop(data); break; }
		case Op::SXDA: { uint16_t xr = fetch16(); Value data = popVal();
			uint32_t u = static_cast<uint32_t>(topVal());
			std::memcpy(externPtr(static_cast<uint16_t>(u), xr, u >> 16, 4), &data, 4);
			setTop(data); break; }

		// effective address of an extern variable (in the target's statics)
		case Op::LEXA: { uint16_t xr = fetch16();
			uint16_t obj = static_cast<uint16_t>(topVal());
			if (!mExternResolve)
				throw VmError("LEXA but no extern link layer is wired in");
			setTop(makeAddr(AddrSpace::Extern, mExternResolve(xr), obj)); break; }

		// SXAS merges a popped (byte-scaled) array index into the high word of
		// the slot below, whose low word holds the source object index -- the
		// combined form the extern array opcodes above consume.
		case Op::SXAS: { Value idx = popVal();
			uint32_t u = static_cast<uint32_t>(topVal());
			setTop(static_cast<Value>((u & 0xFFFFu) |
			       (static_cast<uint32_t>(idx & 0xFFFF) << 16))); break; }

		// SOLE: object-list lookup -- the object handle (index) if a live object
		// exists at the index on top of stack, else -1.
		case Op::SOLE:
			if (!mObjectLookup)
				throw VmError("SOLE but no object system is wired in");
			setTop(mObjectLookup(static_cast<uint16_t>(topVal())));
			break;

		case Op::BRK: // the original's int-3 debugger hook
			unimplemented(op);
		}
	}
}

std::string Interpreter::describe(uint32_t pc) const {
	if (pc >= mCode.size())
		return "??";
	Op op = static_cast<Op>(mCode[pc]);
	uint32_t a = pc + 1; // operand start

	auto pk8 = [&](uint32_t o) -> uint32_t {
		return o < mCode.size() ? mCode[o] : 0;
	};
	auto pk16 = [&](uint32_t o) -> uint32_t {
		return (o + 1 < mCode.size()) ? (mCode[o] | (mCode[o + 1] << 8)) : 0;
	};
	auto pk32 = [&](uint32_t o) -> uint32_t {
		return (o + 3 < mCode.size())
		           ? (mCode[o] | (mCode[o + 1] << 8) | (mCode[o + 2] << 16) |
		              (static_cast<uint32_t>(mCode[o + 3]) << 24))
		           : 0;
	};

	// Build the operand text on its own so we can pad it to a fixed column,
	// keeping the "; description" aligned across every line.
	std::ostringstream operand;
	switch (op) {
	case Op::BRT: case Op::BRF: case Op::BRA: case Op::JSR:
		operand << "->" << pk16(a); break;
	case Op::LECA:
		operand << '@' << pk16(a); break;
	case Op::SHTC: operand << pk8(a);  break;
	case Op::INTC: operand << pk16(a); break;
	case Op::LNGC: operand << pk32(a); break;
	case Op::CALL: case Op::PASS:
		operand << pk8(a) << (pk8(a) == 1 ? " arg" : " args"); break;
	case Op::AIS: operand << pk8(a);  break;
	case Op::AIM: operand << pk16(a); break;
	case Op::SEND:
		operand << pk8(a) << " args, msg " << pk16(a + 1); break;
	case Op::RCRS: {
		uint32_t n = pk16(a);
		operand << '#' << n;
		auto it = mImports.find(static_cast<int32_t>(n));
		if (it != mImports.end())
			operand << " (" << it->second << ')';
		break;
	}
	// import-indexed extern ops (resolve the XR offset to the extern's name)
	case Op::LXB: case Op::LXW: case Op::LXD: case Op::SXB: case Op::SXW: case Op::SXD:
	case Op::LXBA: case Op::LXWA: case Op::LXDA: case Op::SXBA: case Op::SXWA: case Op::SXDA:
	case Op::LEXA: {
		uint32_t n = pk16(a);
		operand << '#' << n;
		auto it = mExternNames.find(n);
		if (it != mExternNames.end())
			operand << " (" << it->second << ')';
		break;
	}
	// word-offset auto/static/table ops
	case Op::LAB: case Op::LAW: case Op::LAD: case Op::SAB: case Op::SAW: case Op::SAD:
	case Op::LABA: case Op::LAWA: case Op::LADA: case Op::SABA: case Op::SAWA: case Op::SADA:
	case Op::LEAA:
	case Op::LSB: case Op::LSW: case Op::LSD: case Op::SSB: case Op::SSW: case Op::SSD:
	case Op::LSBA: case Op::LSWA: case Op::LSDA: case Op::SSBA: case Op::SSWA: case Op::SSDA:
	case Op::LESA:
	case Op::LTBA: case Op::LTWA: case Op::LTDA: case Op::LETA:
		operand << '@' << pk16(a); break;
	default:
		break; // no operand
	}

	// Columns: mnemonic (5) | operand (18) | "; description"
	std::ostringstream s;
	s << std::left << std::setw(5) << opName(op) << ' '
	  << std::setw(18) << operand.str() << "; " << opDesc(op);
	return s.str();
}

Value Interpreter::callRuntime(Value handle, const std::vector<Value>& args) {
	auto it = mImports.find(handle);
	std::string name = (it != mImports.end())
	                       ? it->second
	                       : ("fn#" + std::to_string(handle));
	if (!mRuntimeCall)
		throw VmError("runtime function \"" + name + "\" called, but no runtime "
		              "library is wired in yet");
	return mRuntimeCall(*this, name, args);
}

uint8_t* Interpreter::staticPtr(uint32_t off, uint32_t size) {
	if (mStatics == nullptr)
		throw VmError("static variable access but no instance storage is set");
	off += mStaticBase; // statics are relative to the defining class's block
	if (static_cast<uint64_t>(off) + size > mStatics->size())
		throw VmError("static variable access out of range (offset " +
		              std::to_string(off) + ", size " + std::to_string(size) + ")");
	return mStatics->data() + off;
}

uint8_t* Interpreter::externPtr(uint16_t obj, uint32_t xrOffset, uint32_t extra,
                                uint32_t size) {
	if (!mExternResolve || !mExternStatics)
		throw VmError("extern variable access but no link layer is wired in");
	return mExternStatics(obj, mExternResolve(xrOffset) + extra, size);
}

const uint8_t* Interpreter::codePtr(uint32_t off, uint32_t size) {
	if (static_cast<uint64_t>(off) + size > mCode.size())
		throw VmError("table/code access out of range (offset " +
		              std::to_string(off) + ", size " + std::to_string(size) + ")");
	return mCode.data() + off;
}

std::string Interpreter::readCodeString(Value addr) const {
	// Only Code-space addresses point at inline strings; anything else (a plain
	// integer arg, a stack/static address, ...) is "not a string".
	Addr a = decodeAddr(addr);
	if (a.space != AddrSpace::Code)
		return "";
	uint32_t off = a.offset;
	if (off < 14 || off >= mCode.size()) // before/at header or OOB
		return "";
	std::string s;
	for (uint32_t i = off; i < mCode.size() && mCode[i] != 0; ++i) {
		char c = static_cast<char>(mCode[i]);
		if (c < 0x20 || static_cast<unsigned char>(c) > 0x7e)
			return ""; // not a printable string -- probably a real numeric arg
		s.push_back(c);
	}
	return s;
}

std::string Interpreter::readString(Value addr) const {
	// Like readCodeString but also follows Static/Extern addresses (e.g. a PC's
	// "name" field, reached via LESA) -- used by sprint(). Reads a NUL-terminated
	// string from whichever space the tagged address points at.
	Addr a = decodeAddr(addr);
	if (a.space == AddrSpace::Code)
		return readCodeString(addr);
	if (a.space == AddrSpace::Static || a.space == AddrSpace::Extern) {
		if (!mExternStatics)
			return "";
		std::string s;
		for (uint32_t i = 0; i < 256; ++i) { // sane cap on a static string field
			const uint8_t* p = mExternStatics(a.obj, a.offset + i, 1);
			if (p == nullptr || *p == 0)
				break;
			s.push_back(static_cast<char>(*p));
		}
		return s;
	}
	return "";
}

void Interpreter::unimplemented(Op op) {
	throw VmError(std::string("opcode ") + opName(op) + " (" +
	              hexByte(static_cast<uint8_t>(op)) + ") not implemented "
	              "(BRK is the original's int-3 debugger hook)");
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
