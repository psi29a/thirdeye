/*
 * objects.hpp
 *
 * AESOP object & message system: classes, instances, and SEND/PASS dispatch.
 *
 * Each SOP code resource is a "class": its bytecode, header (static size +
 * parent), the exported message handlers (message number -> entry offset), and
 * its imports. Classes form a hierarchy via the parent link; a message is
 * dispatched to the lowest class in an instance's hierarchy that handles it,
 * and PASS forwards to the next class up. Reference:
 * eob3_research/runtime/RTOBJECT.C + the thunk/MV/SD structs in RT.ASM.
 *
 * This models dispatch directly over the parent chain (rather than pre-flat-
 * tening into a thunk's message-vector list as the original does); the
 * observable SEND/PASS behaviour is the same.
 */

#ifndef THIRDEYE_VM_OBJECTS_HPP
#define THIRDEYE_VM_OBJECTS_HPP

#include "vm.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace VM {

// An extern-variable import: a "B:/W:/L:<name>" entry in a class's .IMPT. The
// def is "<XR-offset>,<source-class>" (RTLINK.C): the tag is looked up in the
// source ("friend") class's exports to find the variable's static offset.
struct ExternImport {
	std::string tag;          // e.g. "W:lvlobj" -- the .EXPT lookup key
	uint32_t sourceClass = 0; // resource number of the exporting class
};

// A loaded SOP class (one code resource + its metadata).
struct SopClass {
	uint16_t number = 0;                  // resource number
	std::string name;                     // object name (from .EXPT "N:OBJECT")
	std::vector<uint8_t> code;            // raw code resource bytes
	SopHeader header;                     // static_size / import / export / parent
	std::map<int, uint32_t> handlers;     // message number -> handler entry offset
	std::map<int32_t, std::string> imports; // runtime-fn number -> name
	std::map<uint32_t, ExternImport> externs; // XR byte offset -> extern import
	std::map<std::string, uint16_t> exportedVars; // "B:/W:/L:<name>" -> static offset
};

class ObjectSystem {
public:
	// Register a class. Classes can be added in any order; parent links are
	// resolved lazily at dispatch time by resource number.
	void addClass(SopClass cls);

	// Look up a class number by object name; returns false if unknown.
	bool findClassByName(const std::string& name, uint16_t& classNumber) const;

	// Create an instance of a class; returns its object-list index (the value
	// that travels on the stack as an object handle / THIS).
	int createInstance(uint16_t classNumber);

	// Runtime-function object management (see RTOBJECT.C):
	//   create_program(index, class): create an instance of `class` at object
	//     `index` (replacing whatever is there; index -1 = next free), send it
	//     MSG_CREATE, and return the index.
	//   create_object(class): like create_program with index -1.
	//   destroy_object(index): send MSG_DESTROY, then free the slot.
	int createProgram(int index, uint16_t classNumber);
	int createObject(uint16_t classNumber) { return createProgram(-1, classNumber); }
	Value destroyObject(int index);

	// Runtime-function dispatch hook shared by every handler we run.
	void setRuntimeCall(Interpreter::RuntimeCall fn) { mRuntimeCall = std::move(fn); }
	void setTrace(bool trace) { mTrace = trace; }
	void setMaxSteps(uint64_t maxSteps) { mMaxSteps = maxSteps; }

	// Dispatch `message` to object `objIndex`. Returns the handler's result, or
	// -1 if no handler exists anywhere in the object's class hierarchy.
	Value send(int objIndex, int message, std::vector<Value> args);

	const SopClass* classByNumber(uint16_t number) const;

	// Object-list indices below this are "entities" (physical game objects);
	// at/above are program objects (menus, kernel, ...). From SHARED.H
	// NUM_ENTITIES; the event layer uses it to scope entity-wide cancels.
	static constexpr int kNumEntities = 2000;

	// --- the cross-object link layer (RTLINK.C construct_thunk, on demand) ---
	// Instance statics are laid out base-class-FIRST: an instance of class C
	// allocates the whole parent chain's statics, and each class's block sits
	// after every ancestor's (the thunk SD entry's static_base).

	// Total static bytes an instance of `classNumber` needs (whole chain).
	uint32_t instanceStaticSize(uint16_t classNumber) const;
	// Byte offset of `defClass`'s static block within such an instance.
	uint32_t staticBase(uint16_t defClass) const;
	// Resolve an extern import of `importingClass` (keyed by XR byte offset) to
	// the variable's absolute offset within the TARGET instance's statics:
	// search the source class chain derived->base for the exporting class, then
	// add that class's static base. Cached; throws VmError if unresolved
	// (matches the original's "Unresolved external reference" abort).
	uint32_t resolveExtern(uint16_t importingClass, uint32_t xrOffset);
	// Bounds-checked pointer into object `objIndex`'s static storage; throws on
	// a dead slot or out-of-range access.
	uint8_t* staticsPtr(int objIndex, uint32_t offset, uint32_t size);
	// Pointer to a static variable declared at `offset` within `definingClass`'s
	// own block (accounts for the base-class-first layout), in object `objIndex`.
	uint8_t* classStaticPtr(int objIndex, uint16_t definingClass, uint32_t offset,
	                        uint32_t size) {
		return staticsPtr(objIndex, staticBase(definingClass) + offset, size);
	}
	// Live object indices whose (exact) class is `classNumber`.
	std::vector<int> objectsOfClass(uint16_t classNumber) const;
	// First live object of `classNumber`, or -1.
	int firstObjectOfClass(uint16_t classNumber) const;
	// SOLE: the object handle (its index) if a live object exists, else -1.
	Value objectLookup(int index) const;

private:
	// Find the lowest class at/above `startClass` that handles `message`.
	bool resolve(uint16_t startClass, int message, uint16_t& defClass,
	             uint32_t& offset) const;

	// Re-dispatch `message` for `objIndex` starting at `parentClass` (PASS).
	Value pass(int objIndex, int message, uint32_t parentClass,
	           std::vector<Value> args);

	// Run a resolved handler with THIS = objIndex, wiring in the hooks.
	Value runHandler(uint16_t defClass, uint32_t offset, int objIndex,
	                 int message, std::vector<Value>& args);

	// Allocate/replace an instance at `index` (-1 = append) without sending
	// MSG_CREATE; returns the index.
	int allocAt(int index, uint16_t classNumber);

	std::map<uint16_t, SopClass> mClasses;
	std::map<uint64_t, uint32_t> mExternCache;   // (class<<32)|XR-off -> offset
	std::vector<uint16_t> mObjList;              // object index -> class number
	// Per-instance static storage. A std::deque keeps element addresses stable
	// as the list grows, so a running handler's static pointer stays valid even
	// if a nested create_program adds objects.
	std::deque<std::vector<uint8_t>> mStatics;
	Interpreter::RuntimeCall mRuntimeCall;
	bool mTrace = false;
	uint64_t mMaxSteps = 0;
	int mDepth = 0; // current SEND/PASS recursion depth

	static constexpr uint32_t kNoParent = 0xFFFFFFFFu;
	static constexpr int kMaxDepth = 256;       // max SEND/PASS recursion depth
	static constexpr int kMaxGenerations = 16;  // max class chain depth (MAX_G)
	static constexpr uint16_t kFreeSlot = 0xFFFF; // empty object-list entry
	static constexpr int kMsgCreate = 0;        // MSG_CREATE (DEFS.H)
	static constexpr int kMsgDestroy = 1;       // MSG_DESTROY (DEFS.H)
	static constexpr int kMsgRestore = 2;       // MSG_RESTORE (DEFS.H)
};

} // namespace VM

#endif // THIRDEYE_VM_OBJECTS_HPP
