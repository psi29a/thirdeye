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
#include <functional>
#include <map>
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
	// list index bound to THIS. `args` are the message/handler parameters, laid
	// out above the frame base as the original engine does (see enterFrame).
	// Returns the value left on top of stack at END.
	Value execute(uint32_t handlerOffset, uint16_t thisIndex = 0,
	              const std::vector<Value>& args = {});

	const SopHeader& header() const { return mHeader; }

	// When enabled, each executed instruction is printed (PC + mnemonic).
	void setTrace(bool trace) { mTrace = trace; }

	// Safety budget for a single execute(): throw VmError after this many
	// instructions (0 = unlimited). Guards against infinite loops during
	// bring-up when stubbed runtime functions don't advance real state.
	void setMaxSteps(uint64_t maxSteps) { mMaxSteps = maxSteps; }

	// Runtime-function dispatch hook. CALL resolves the RCRS handle to a name
	// via the import map, then invokes this. The Interpreter& lets the function
	// dereference address arguments (e.g. readCodeString for string pointers).
	// Should return the function result; throw VmError if unsupported. If unset,
	// CALL throws.
	using RuntimeCall = std::function<Value(
		Interpreter& vm, const std::string& name, const std::vector<Value>& args)>;
	void setRuntimeCall(RuntimeCall fn) { mRuntimeCall = std::move(fn); }

	// Object-message hooks (supplied by the ObjectSystem). SEND dispatches a
	// message to a target object instance; PASS re-dispatches the *current*
	// message to the parent class. Both return the handler's result; if unset,
	// the corresponding opcode throws.
	using SendHook =
		std::function<Value(int objIndex, int message, std::vector<Value>& args)>;
	using PassHook = std::function<Value(std::vector<Value>& args)>;
	void setSendHook(SendHook fn) { mSendHook = std::move(fn); }
	void setPassHook(PassHook fn) { mPassHook = std::move(fn); }

	// Per-instance static-variable storage (object state). The pointer is owned
	// by the caller (the ObjectSystem instance) so writes persist across SENDs.
	// If unset, static opcodes throw.
	void setStatics(std::vector<uint8_t>* statics) { mStatics = statics; }

	// Read a NUL-terminated string from an address that points into the code
	// resource (as produced by LECA for inline string/data). Returns "" if the
	// address isn't a plausible in-code string (0, in the header, OOB, or
	// non-printable) -- so it's safe to probe arbitrary integer arguments.
	std::string readCodeString(uint32_t addr) const;

	// Map of runtime-function number (as referenced by RCRS) -> function name,
	// built from the code object's .IMPT dictionary.
	void setImports(std::map<int32_t, std::string> imports) {
		mImports = std::move(imports);
	}

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
	uint64_t mMaxSteps = 0; // instruction budget for one execute() (0 = unlimited)

	std::map<int32_t, std::string> mImports; // runtime-fn number -> name
	RuntimeCall mRuntimeCall;                 // dispatch hook for CALL
	SendHook mSendHook;                       // dispatch hook for SEND
	PassHook mPassHook;                       // dispatch hook for PASS
	std::vector<uint8_t>* mStatics = nullptr; // instance static storage (object state)

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

	void enterFrame(uint32_t handlerOffset, uint16_t thisIndex,
	                const std::vector<Value>& args);
	Value run(); // dispatch loop until END; returns top of stack

	// Byte address of an auto (local/param) variable. Offsets are SIGNED: locals
	// and THIS live below the frame base (positive offset), parameters above it
	// (negative offset), per the original frame layout.
	uint32_t autoAddr(int16_t off) const {
		return static_cast<uint32_t>(static_cast<int32_t>(mFptr) - off);
	}

	// Bounds-checked pointer into instance static storage (throws if no storage
	// is set or the access is out of range).
	uint8_t* staticPtr(uint32_t off, uint32_t size);
	// Bounds-checked pointer into the code resource (for constant tables).
	const uint8_t* codePtr(uint32_t off, uint32_t size);

	// Resolve an RCRS handle to a function name and dispatch via mRuntimeCall.
	Value callRuntime(Value handle, const std::vector<Value>& args);

	// Human-readable, annotated form of the instruction at `pc` (mnemonic +
	// decoded operand + description). Does not modify VM state. Used for tracing.
	std::string describe(uint32_t pc) const;

	[[noreturn]] void unimplemented(Op op);
};

} // namespace VM

#endif // THIRDEYE_VM_VM_HPP
