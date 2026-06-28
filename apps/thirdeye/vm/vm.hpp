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
 * All 88 opcodes are implemented except BRK (the original's int-3 debugger
 * hook, which still throws VmError). The extern (cross-object) family
 * LXB..SXDA / LEXA / SXAS resolves through hooks supplied by the ObjectSystem
 * (the link layer ported from RTLINK.C): the 16-bit operand is a byte offset
 * into the executing class's XR (external reference) list -- the same offset
 * space RCRS uses -- and the TARGET OBJECT INDEX travels on the stack (low
 * word of the top slot; for the array forms the high word holds the
 * byte-scaled array index, merged in by SXAS).
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

// AESOP addresses are flat pointers in the original; here code/stack/static live
// in separate buffers, so an "effective address" (LECA/LEAA/LESA/LETA/LEXA) is a
// tagged value: the high nibble selects the space, the low bits hold the offset
// (and, for static/extern, the owning object). The tag sits in bits the engine's
// small integer values never use, and index arithmetic (AIM/AIS) only touches the
// low offset bits, so a tagged address survives being indexed.
//
// NB: the "no space" value is named Invalid, not None -- on Linux, SDL_syswm.h
// pulls in X11's <X.h>, which #defines None as 0L and would textually mangle the
// enumerator (the preprocessor runs before the scoped-enum scope applies).
enum class AddrSpace : uint32_t { Invalid = 0, Code = 0x8, Stack = 0x9, Static = 0xA, Extern = 0xB };

struct Addr { AddrSpace space; uint32_t offset; uint16_t obj; };

inline Value makeAddr(AddrSpace s, uint32_t offset, uint16_t obj = 0) {
	uint32_t tag = static_cast<uint32_t>(s) << 28;
	if (s == AddrSpace::Static || s == AddrSpace::Extern) {
		// 14 bits each for offset and object index; fail loudly rather than
		// silently truncating (offsets are 16-bit UWORDs in the original, but
		// real EOB3 static blocks stay well under 16K).
		if (offset > 0x3FFFu || obj > 0x3FFFu)
			throw VmError("static/extern address out of encodable range (offset " +
			              std::to_string(offset) + ", obj " + std::to_string(obj) + ")");
		return static_cast<Value>(tag | (static_cast<uint32_t>(obj) << 14) | offset);
	}
	return static_cast<Value>(tag | (offset & 0x0FFFFFFFu));
}

inline Addr decodeAddr(Value v) {
	uint32_t u = static_cast<uint32_t>(v);
	AddrSpace s = static_cast<AddrSpace>((u >> 28) & 0xF);
	switch (s) {
	case AddrSpace::Code:
	case AddrSpace::Stack:
		return { s, u & 0x0FFFFFFFu, 0 };
	case AddrSpace::Static:
	case AddrSpace::Extern:
		return { s, u & 0x3FFFu, static_cast<uint16_t>((u >> 14) & 0x3FFFu) };
	default:
		return { AddrSpace::Invalid, 0, 0 };
	}
}

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
		std::function<Value(int objIndex, int message, std::vector<Value> args)>;
	using PassHook = std::function<Value(std::vector<Value> args)>;
	void setSendHook(SendHook fn) { mSendHook = std::move(fn); }
	void setPassHook(PassHook fn) { mPassHook = std::move(fn); }

	// Per-instance static-variable storage (object state). The pointer is owned
	// by the caller (the ObjectSystem instance) so writes persist across SENDs.
	// If unset, static opcodes throw.
	void setStatics(std::vector<uint8_t>* statics) { mStatics = statics; }

	// Byte offset of the executing handler's DEFINING class's static block
	// within the instance storage (the original's `static_offset`, from the
	// thunk SD entry). Instance statics are laid out base-class-first, so a
	// child's statics sit after every ancestor's; 0 for single-class objects.
	void setStaticBase(uint32_t base) { mStaticBase = base; }

	// --- extern (cross-object) link layer, supplied by the ObjectSystem ---
	// Resolve an extern-variable reference -- a byte offset into the executing
	// class's XR list (what the LXB..LEXA operand holds) -- to the variable's
	// absolute offset within the TARGET instance's static storage. Mirrors the
	// thunk's pre-resolved XDR entries (RTLINK.C construct_thunk).
	using ExternResolve = std::function<uint32_t(uint32_t xrOffset)>;
	// Bounds-checked pointer into object `objIndex`'s static storage.
	using ExternStatics =
		std::function<uint8_t*(int objIndex, uint32_t offset, uint32_t size)>;
	// SOLE: object-list lookup; returns the object handle (its index) if a live
	// object exists at that index, else -1.
	using ObjectLookup = std::function<Value(int objIndex)>;
	void setExternResolve(ExternResolve fn) { mExternResolve = std::move(fn); }
	void setExternStatics(ExternStatics fn) { mExternStatics = std::move(fn); }
	void setObjectLookup(ObjectLookup fn) { mObjectLookup = std::move(fn); }

	// Optional: XR offset -> human-readable extern name, used by the trace.
	void setExternNames(std::map<uint32_t, std::string> names) {
		mExternNames = std::move(names);
	}

	// Read a NUL-terminated string from a tagged Code-space address (as produced
	// by LECA/LETA for inline string/data). Returns "" if `addr` isn't a Code
	// address or doesn't point at a plausible string -- so it's safe to probe
	// arbitrary integer arguments.
	std::string readCodeString(Value addr) const;
	// Read a NUL-terminated string from a Code, Static, or Extern address (the
	// latter two via the extern-statics hook). Used by sprint() for PC name etc.
	std::string readString(Value addr) const;

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
	std::map<uint32_t, std::string> mExternNames; // XR offset -> extern name (trace)
	RuntimeCall mRuntimeCall;                 // dispatch hook for CALL
	SendHook mSendHook;                       // dispatch hook for SEND
	PassHook mPassHook;                       // dispatch hook for PASS
	std::vector<uint8_t>* mStatics = nullptr; // instance static storage (object state)
	uint32_t mStaticBase = 0;                 // defining class's static block offset
	ExternResolve mExternResolve;             // XR offset -> offset in target statics
	ExternStatics mExternStatics;             // access another object's statics
	ObjectLookup mObjectLookup;               // SOLE: index -> handle or -1

	static constexpr uint32_t kStackBytes = 16384; // matches RT.ASM STK_SIZE
	// Slack above the logical top (mSp starts at kStackBytes). Absorbs AESOP's
	// dword-read of the word-sized THIS slot at fptr-2 and similar 1-3 byte
	// boundary straddles, the way the original's malloc slack did. A genuine
	// runaway (mSp marching far past the ends) still trips boundsCheck.
	static constexpr uint32_t kStackGuard = 256;
	static constexpr int kValueSize = 4;
	static constexpr int kOffThis = 2; // THIS lives at fptr-2

	// bytecode fetch helpers (advance mPC)
	uint8_t  fetch8();
	uint16_t fetch16();
	uint32_t fetch32();

	// little-endian access into the byte stack (bounds-checked)
	void  boundsCheck(uint32_t off, uint32_t size) const;
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

	// The current THIS (object index), stored at fptr-2.
	uint16_t currentThis() const {
		return static_cast<uint16_t>(mStk[mFptr - kOffThis] |
		                             (mStk[mFptr - kOffThis + 1] << 8));
	}

	// Bounds-checked pointer into instance static storage (throws if no storage
	// is set or the access is out of range). `off` is relative to the defining
	// class's static block (mStaticBase is added).
	uint8_t* staticPtr(uint32_t off, uint32_t size);
	// Bounds-checked pointer into the code resource (for constant tables).
	const uint8_t* codePtr(uint32_t off, uint32_t size);
	// Resolve + access an extern variable: `xrOffset` is the XR-list operand,
	// `extra` an additional byte offset (array indexing), `obj` the target
	// object index from the stack. Throws if no link layer is wired in.
	uint8_t* externPtr(uint16_t obj, uint32_t xrOffset, uint32_t extra,
	                   uint32_t size);

	// Resolve an RCRS handle to a function name and dispatch via mRuntimeCall.
	Value callRuntime(Value handle, const std::vector<Value>& args);

	// Human-readable, annotated form of the instruction at `pc` (mnemonic +
	// decoded operand + description). Does not modify VM state. Used for tracing.
	std::string describe(uint32_t pc) const;

	[[noreturn]] void unimplemented(Op op);
};

} // namespace VM

#endif // THIRDEYE_VM_VM_HPP
