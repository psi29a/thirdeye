/*
 * vm.hpp
 *
 * AESOP SOP bytecode interpreter (stack machine) for thirdeye.
 *
 * Phase 1 of the engine: a faithful C++ port of the original AESOP/32 runtime
 * dispatcher (eob3_research/runtime/RT.ASM, proc RT_execute).
 *
 * Execution model (from RT.ASM) -- note the non-obvious stack discipline:
 *   - One contiguous, byte-addressed stack region that grows DOWNWARD. `mSp`
 *     (== the original's edi) is the byte offset of the top VALUE slot; `mFptr`
 *     (== fptr) is the frame base at handler entry. Auto (local/param) variables
 *     and THIS live just below fptr; the operand stack grows below them.
 *   - VALUE slots are 32-bit. A binary op takes  left = second-from-top,
 *     right = top  (so SUB = left-right, DIV = left/right, LE = left<=right).
 *   - PUSH reserves+zeroes a new top slot. Load/constant opcodes overwrite the
 *     current top slot IN PLACE (they do not push) -- which is why real AESOP
 *     bytecode emits a PUSH before each value load. Scalar stores (SAB/SAW/SAD)
 *     write the variable but LEAVE the value on the stack (assignment-as-expr).
 *   - PC is a byte offset into the SOP code resource; branch targets are
 *     absolute 16-bit offsets into the resource.
 *   - A handler/procedure begins with a 2-byte auto_size (MHDR); on entry the VM
 *     reserves that many bytes of auto space (THIS included) below fptr.
 *
 * Implemented this milestone: branches, CASE, stack ops, arithmetic/logic/
 * compare, constants, JSR/RTS, END, and auto-variable scalar load/store + LECA.
 *
 * NOT yet implemented (throw VmError so the boundary is explicit, not silent):
 *   RCRS, CALL (runtime-fn library), SEND/PASS (object/message system),
 *   static/extern variable families, table/array loads, AIM/AIS, SOLE, SXAS,
 *   BRK. These depend on the object system + runtime-function library to come.
 */

#ifndef THIRDEYE_VM_VM_HPP
#define THIRDEYE_VM_VM_HPP

#include "opcodes.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace VM {

using Value = int32_t; // a SOP stack slot / variable value (may hold int or addr)

// Thrown for anything we cannot (yet) execute: invalid opcodes AND opcodes that
// depend on subsystems not built yet. The message names the opcode so the wall
// is obvious during bring-up.
class VmError : public std::runtime_error {
public:
	explicit VmError(const std::string& what) : std::runtime_error(what) {}
};

// On-disk SOP program header (PHDR), 14 bytes at the start of a code resource.
// Layout matches daesop's SOP_script_header. Code/handlers begin at offset 14.
struct SopHeader {
	uint16_t static_size = 0;          // bytes of static (object-state) space
	uint32_t import_resource = 0;      // resource # of the <object>.IMPT dictionary
	uint32_t export_resource = 0;      // resource # of the <object>.EXPT dictionary
	uint32_t parent = 0xFFFFFFFFu;     // parent object resource # (0xffffffff if none)
};
static_assert(sizeof(Value) == 4, "AESOP VALUE is 32-bit");

class Interpreter {
public:
	// `code` is the raw bytes of a SOP .CODE resource (including the 14-byte header).
	explicit Interpreter(std::vector<uint8_t> code);

	// Execute the handler/procedure whose MHDR begins at `handlerOffset` (an
	// absolute offset into the code resource, >= 14). `thisIndex` is the object
	// list index bound to THIS. Returns the value left on top of stack at END.
	Value execute(uint32_t handlerOffset, uint16_t thisIndex = 0);

	const SopHeader& header() const { return mHeader; }

	// When enabled, each executed instruction is printed (PC + mnemonic).
	void setTrace(bool trace) { mTrace = trace; }

	// Runs a tiny hand-assembled program and checks results. Returns true on
	// success. Lets us validate the core before real handler entry points exist.
	static bool selfTest();

private:
	std::vector<uint8_t> mCode;
	SopHeader mHeader{};

	// Unified downward-growing byte stack (operand stack + auto frames).
	std::vector<uint8_t> mStk;
	uint32_t mSp = 0;    // byte offset of top VALUE slot (== edi)
	uint32_t mFptr = 0;  // current frame base (== fptr)
	uint32_t mPC = 0;    // byte offset into mCode
	bool mTrace = false; // print each executed instruction

	static constexpr uint32_t kStackBytes = 16384; // matches RT.ASM STK_SIZE
	static constexpr int kValueSize = 4;
	static constexpr int kOffThis = 2; // THIS lives at fptr-2

	// bytecode fetch helpers (advance mPC)
	uint8_t  fetch8();
	uint16_t fetch16();
	uint32_t fetch32();

	// little-endian access into the byte stack
	Value readStk(uint32_t off) const;
	void  writeStk(uint32_t off, Value v);

	// VALUE-stack helpers (operate on mSp)
	void  pushVal(Value v) { mSp -= kValueSize; writeStk(mSp, v); }
	Value popVal()         { Value v = readStk(mSp); mSp += kValueSize; return v; }
	Value  topVal() const  { return readStk(mSp); }
	void  setTop(Value v)  { writeStk(mSp, v); }

	// binary op helper: right = pop, left = top; top = f(left,right)
	template <typename F> void binOp(F f) {
		Value r = popVal();
		setTop(f(topVal(), r));
	}

	void enterFrame(uint32_t handlerOffset, uint16_t thisIndex);
	Value run(); // dispatch loop until END; returns top of stack

	[[noreturn]] void unimplemented(Op op);
};

} // namespace VM

#endif // THIRDEYE_VM_VM_HPP
