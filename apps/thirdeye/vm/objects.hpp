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
#include <map>
#include <string>
#include <vector>

namespace VM {

// A loaded SOP class (one code resource + its metadata).
struct SopClass {
	uint16_t number = 0;                  // resource number
	std::string name;                     // object name (from .EXPT "N:OBJECT")
	std::vector<uint8_t> code;            // raw code resource bytes
	SopHeader header;                     // static_size / import / export / parent
	std::map<int, uint32_t> handlers;     // message number -> handler entry offset
	std::map<int32_t, std::string> imports; // runtime-fn number -> name
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

	// Runtime-function dispatch hook shared by every handler we run.
	void setRuntimeCall(Interpreter::RuntimeCall fn) { mRuntimeCall = std::move(fn); }
	void setTrace(bool trace) { mTrace = trace; }

	// Dispatch `message` to object `objIndex`. Returns the handler's result, or
	// -1 if no handler exists anywhere in the object's class hierarchy.
	Value send(int objIndex, int message, std::vector<Value> args);

	const SopClass* classByNumber(uint16_t number) const;

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

	std::map<uint16_t, SopClass> mClasses;
	std::vector<uint16_t> mObjList;              // object index -> class number
	std::vector<std::vector<uint8_t>> mStatics;  // object index -> static storage
	Interpreter::RuntimeCall mRuntimeCall;
	bool mTrace = false;

	static constexpr uint32_t kNoParent = 0xFFFFFFFFu;
};

} // namespace VM

#endif // THIRDEYE_VM_OBJECTS_HPP
