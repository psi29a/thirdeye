/*
 * objects.cpp
 *
 * AESOP object & message system. See objects.hpp.
 */

#include "objects.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib> // std::getenv (debug traces)
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace VM {

void ObjectSystem::addClass(SopClass&& cls) {
	uint16_t n = cls.number;
	mClasses[n] = std::move(cls);
}

const SopClass* ObjectSystem::classByNumber(uint16_t number) const {
	auto it = mClasses.find(number);
	return it == mClasses.end() ? nullptr : &it->second;
}

bool ObjectSystem::findClassByName(const std::string& name,
                                   uint16_t& classNumber) const {
	for (const auto& entry : mClasses) {
		if (entry.second.name == name) {
			classNumber = entry.first;
			return true;
		}
	}
	return false;
}

// Total static bytes an instance needs: the whole parent chain's blocks
// (RTLINK.C construct_thunk accumulates thdr.isize the same way).
uint32_t ObjectSystem::instanceStaticSize(uint16_t classNumber) const {
	uint32_t total = 0;
	uint32_t cur = classNumber;
	int depth = 0;
	while (cur != kNoParent) {
		const SopClass* cls = classByNumber(static_cast<uint16_t>(cur));
		if (cls == nullptr)
			break;
		total += cls->header.static_size;
		cur = cls->header.parent;
		if (++depth > kMaxGenerations)
			throw VmError("class parent chain too deep (class " +
			              std::to_string(classNumber) + ") -- cycle in .RES?");
	}
	return total;
}

// Offset of `defClass`'s static block within an instance: the sum of its
// ancestors' static sizes (the layout is base-class-first).
uint32_t ObjectSystem::staticBase(uint16_t defClass) const {
	const SopClass* cls = classByNumber(defClass);
	if (cls == nullptr)
		return 0;
	uint32_t base = 0;
	uint32_t cur = cls->header.parent;
	int depth = 0;
	while (cur != kNoParent) {
		const SopClass* p = classByNumber(static_cast<uint16_t>(cur));
		if (p == nullptr)
			break;
		base += p->header.static_size;
		cur = p->header.parent;
		if (++depth > kMaxGenerations)
			throw VmError("class parent chain too deep (class " +
			              std::to_string(defClass) + ") -- cycle in .RES?");
	}
	return base;
}

uint32_t ObjectSystem::resolveExtern(uint16_t importingClass, uint32_t xrOffset) {
	uint64_t key = (static_cast<uint64_t>(importingClass) << 32) | xrOffset;
	auto cached = mExternCache.find(key);
	if (cached != mExternCache.end())
		return cached->second;

	const SopClass* imp = classByNumber(importingClass);
	if (imp == nullptr)
		throw VmError("extern reference from unknown class " +
		              std::to_string(importingClass));
	auto it = imp->externs.find(xrOffset);
	if (it == imp->externs.end())
		throw VmError("class \"" + imp->name + "\" has no extern import at XR "
		              "offset " + std::to_string(xrOffset));

	// Find the exporting class: walk the source ("friend") chain derived->base;
	// the first class whose exports contain the tag defines the variable.
	const ExternImport& ext = it->second;
	uint32_t cur = ext.sourceClass;
	int depth = 0;
	while (cur != kNoParent) {
		const SopClass* cls = classByNumber(static_cast<uint16_t>(cur));
		if (cls == nullptr)
			throw VmError("friend program " + std::to_string(cur) +
			              " not found (resolving \"" + ext.tag + "\")");
		auto var = cls->exportedVars.find(ext.tag);
		if (var != cls->exportedVars.end()) {
			uint32_t offset = staticBase(static_cast<uint16_t>(cur)) + var->second;
			mExternCache[key] = offset;
			return offset;
		}
		cur = cls->header.parent;
		if (++depth > kMaxGenerations)
			throw VmError("class parent chain too deep resolving \"" + ext.tag +
			              "\"");
	}
	throw VmError("unresolved external reference \"" + ext.tag + "\" (class \"" +
	              imp->name + "\" -> class " + std::to_string(ext.sourceClass) +
	              ")");
}

uint8_t* ObjectSystem::staticsPtr(int objIndex, uint32_t offset, uint32_t size) {
	// Thirdeye runtime-owned buffer (e.g. savegame-picker slot names): a
	// sentinel obj index that has no live SOP object behind it. Checked
	// first so it doesn't trip the "dead object" throw below.
	if (mDynamicStatics) {
		if (uint8_t* p = mDynamicStatics(objIndex, offset, size))
			return p;
	}
	if (objIndex < 0 || static_cast<size_t>(objIndex) >= mObjList.size() ||
	    mObjList[objIndex] == kFreeSlot)
		throw VmError("extern access to dead object index " +
		              std::to_string(objIndex));
	std::vector<uint8_t>& s = mStatics[objIndex];
	// Out-of-range = the SOP reached for a static via a class that isn't in the
	// target's ancestor chain (e.g. dungeon's "init level" probes B:lvl on every
	// alive obj, including area-class singletons that aren't entities). DOS
	// AESOP just read garbage memory there; we return null so callers
	// (Interpreter::externPtr, the runtime CALLs that already check `if (uint8_t *p
	// = classStaticPtr(...))`) can sub in a zero/sinkhole and keep going.
	if (static_cast<uint64_t>(offset) + size > s.size())
		return nullptr;
	return s.data() + offset;
}

std::vector<int> ObjectSystem::objectsOfClass(uint16_t classNumber) const {
	std::vector<int> r;
	for (size_t i = 0; i < mObjList.size(); ++i)
		if (mObjList[i] == classNumber)
			r.push_back(static_cast<int>(i));
	return r;
}

int ObjectSystem::firstObjectOfClass(uint16_t classNumber) const {
	for (size_t i = 0; i < mObjList.size(); ++i)
		if (mObjList[i] == classNumber)
			return static_cast<int>(i);
	return -1;
}

Value ObjectSystem::objectLookup(int index) const {
	if (index < 0 || static_cast<size_t>(index) >= mObjList.size() ||
	    mObjList[index] == kFreeSlot)
		return -1;
	return index;
}

// The class number of the live object at `index`, or kFreeSlot if none.
uint16_t ObjectSystem::classOf(int index) const {
	if (index < 0 || static_cast<size_t>(index) >= mObjList.size())
		return kFreeSlot;
	return mObjList[index];
}

// True if `classNumber` is, or derives from, `baseClass` (walks N:PARENT).
bool ObjectSystem::isSubclassOf(uint16_t classNumber, uint16_t baseClass) const {
	for (int cur = classNumber, guard = 0; cur > 0 && guard < 32; ++guard) {
		if (cur == static_cast<int>(baseClass))
			return true;
		const SopClass *sc = classByNumber(static_cast<uint16_t>(cur));
		if (sc == nullptr)
			break;
		cur = sc->header.parent;
	}
	return false;
}

// Allocate (or replace) an instance at `index` (-1 = append). Does NOT send
// MSG_CREATE -- that's the caller's choice (create_program/create_object do).
int ObjectSystem::allocAt(int index, uint16_t classNumber) {
	if (index < 0)
		index = static_cast<int>(mObjList.size()); // next free (append)

	while (static_cast<size_t>(index) >= mObjList.size()) {
		mObjList.push_back(kFreeSlot);
		mStatics.emplace_back();
	}

	mObjList[index] = classNumber;
	// Fresh zeroed object state for the whole class chain (base-first layout).
	mStatics[index].assign(instanceStaticSize(classNumber), 0);
	return index;
}

int ObjectSystem::createInstance(uint16_t classNumber) {
	return allocAt(-1, classNumber); // allocate only; caller sends MSG_CREATE
}

int ObjectSystem::createInstanceAt(int index, uint16_t classNumber) {
	return allocAt(index, classNumber); // pinned slot; caller sends MSG_CREATE
}

size_t ObjectSystem::liveObjectCount() const {
	size_t n = 0;
	for (auto cls : mObjList) if (cls != kFreeSlot) ++n;
	return n;
}

void ObjectSystem::resetInstances() {
	// Drop every live instance. We do NOT send MSG_DESTROY here -- that's
	// the SOP's responsibility (`start` self-destroys before relaunch, the
	// menu/picker SOPs destroy their owned objects). This is the engine-
	// level cleanup that catches whatever leaked, matching the original
	// AESOP runtime's launch()-exec-replace semantics where the whole
	// process memory is replaced.
	mObjList.clear();
	mStatics.clear();
	mDepth = 0;
	// mClasses + mExternCache + mRuntimeCall + mDynamicStatics persist:
	// classes are parsed once at boot, the runtime hook bindings outlive
	// the relaunch loop.
}

int ObjectSystem::createProgram(int index, uint16_t classNumber) {
	int i = allocAt(index, classNumber);
	// The system sends MSG_CREATE to a freshly created instance (RTOBJECT.C
	// create_SOP_instance). A class with no MSG_CREATE handler is fine (-> -1).
	// The system sends a started program its entry message: MSG_CREATE if it has
	// one, else MSG_RESTORE (DEFS.H: both are "sent by system"). EOB3's `kernel`
	// has *no* MSG_CREATE handler and instead wires up its whole event loop (timer
	// -> "draw players", region clicks, ...) in MSG_RESTORE (M:2 -> PROCEDURE_727);
	// without this the in-game HUD never receives events and the party never draws.
	// Programs that *do* have MSG_CREATE (the menu, the PC objects, ...) get only
	// that -- their MSG_RESTORE is restore-from-save logic, not a creation entry.
	uint16_t defClass;
	uint32_t offset;
	if (resolve(classNumber, kMsgCreate, defClass, offset))
		send(i, kMsgCreate, {});
	else if (resolve(classNumber, kMsgRestore, defClass, offset))
		send(i, kMsgRestore, {});
	return i;
}

Value ObjectSystem::destroyObject(int index) {
	if (index < 0 || static_cast<size_t>(index) >= mObjList.size() ||
	    mObjList[index] == kFreeSlot)
		return -1;
	Value rtn = send(index, kMsgDestroy, {});
	mObjList[index] = kFreeSlot; // free the slot (statics left allocated)
	return rtn;
}

bool ObjectSystem::resolve(uint16_t startClass, int message, uint16_t& defClass,
                           uint32_t& offset) const {
	uint32_t cur = startClass;
	while (cur != kNoParent) {
		const SopClass* cls = classByNumber(static_cast<uint16_t>(cur));
		if (cls == nullptr)
			return false;
		auto it = cls->handlers.find(message);
		if (it != cls->handlers.end()) {
			defClass = static_cast<uint16_t>(cur);
			offset = it->second;
			return true;
		}
		cur = cls->header.parent; // walk up the class hierarchy
	}
	return false;
}

Value ObjectSystem::send(int objIndex, int message, std::vector<Value> args) {
	if (objIndex < 0 || static_cast<size_t>(objIndex) >= mObjList.size())
		return -1;
	uint16_t startClass = mObjList[objIndex];
	if (startClass == kFreeSlot)
		return -1; // no live object at this index

	// THIRDEYE_SENDS: log SEND count every 100 ms of wall-clock, dumping the
	// top message-id counts. Confirms what the bytecode is spinning on when
	// wall-clock disagrees with runtime-call frequency (a SEND is an opcode,
	// not a CALL -- a tight SEND loop is invisible to the runtime trace).
	static const bool kCountSends = std::getenv("THIRDEYE_SENDS") != nullptr;
	if (kCountSends) {
		static std::map<int, long> counts;
		static long total = 0;
		++counts[message]; ++total;
		static auto tPrev = std::chrono::steady_clock::now();
		auto now = std::chrono::steady_clock::now();
		auto sinceLog = std::chrono::duration_cast<std::chrono::milliseconds>(
		                    now - tPrev).count();
		if (sinceLog >= 100) {
			std::fprintf(stderr, "[sends %lldms] %ld total; top:",
			             (long long)sinceLog, total);
			std::vector<std::pair<long,int>> v;
			for (auto &kv : counts) v.emplace_back(kv.second, kv.first);
			std::sort(v.rbegin(), v.rend());
			for (size_t i = 0; i < std::min<size_t>(5, v.size()); ++i)
				std::fprintf(stderr, " msg%d=%ld", v[i].second, v[i].first);
			std::fprintf(stderr, "\n");
			std::fflush(stderr);
			tPrev = now;
			counts.clear();
			total = 0;
		}
	}
	uint16_t defClass;
	uint32_t offset;
	bool found = resolve(startClass, message, defClass, offset);
	// Debug (THIRDEYE_MONTRACE): trace creature draw/report dispatch to monster-range
	// objects -- which class handles it, with params. Gate cached so normal play pays
	// nothing. (Used to find the plane-2 / NPCstat bring-up; kept for combat work.)
	static const bool kMonTrace = std::getenv("THIRDEYE_MONTRACE") != nullptr;
	// Combat/AI trace. Attack chain (any object -- PCs too): use/attack request(163),
	// acquire NPC/feature target(77/78), roll to hit(71), roll for damage(43), take
	// damage(82), die(55), remove(22). Plus monster AI: watch(91), my turn(85),
	// LoS(107), advance(99).
	bool atkMsg = (message == 163 || message == 77 || message == 78 ||
	               message == 71 || message == 43 || message == 82 ||
	               message == 162 || message == 55 || message == 22 ||
	               message == 235); // 235 = All Attack button toggle
	bool aiMsg = (objIndex >= 1750 && objIndex <= 1900 &&
	              (message == 91 || message == 85 || message == 107 || message == 99));
	if (kMonTrace && (atkMsg || aiMsg)) {
		static int n = 0;
		if (n++ < 600) {
			std::cerr << "[mon-msg] idx " << objIndex << " msg " << message
			          << " handler " << (found ? defClass : -1);
			for (size_t a = 0; a < args.size(); ++a)
				std::cerr << " arg" << a << "=" << args[a];
			std::cerr << "\n";
		}
	}
	if (!found)
		return -1; // no handler anywhere in the hierarchy (matches RT_execute)
	return runHandler(defClass, offset, objIndex, message, args);
}

Value ObjectSystem::pass(int objIndex, int message, uint32_t parentClass,
                         std::vector<Value> args) {
	if (parentClass == kNoParent)
		return -1;
	uint16_t defClass;
	uint32_t offset;
	if (!resolve(static_cast<uint16_t>(parentClass), message, defClass, offset))
		return -1;
	return runHandler(defClass, offset, objIndex, message, args);
}

Value ObjectSystem::runHandler(uint16_t defClass, uint32_t offset, int objIndex,
                               int message, std::vector<Value>& args) {
	// Guard against runaway SEND/PASS recursion (each level also allocates a VM
	// stack, so an unbounded chain segfaults). During bring-up this typically
	// means a stubbed runtime function isn't advancing state, so a handler keeps
	// re-sending the same message. Fail gracefully instead.
	if (mDepth >= kMaxDepth)
		throw VmError("object message recursion too deep (>" +
		              std::to_string(kMaxDepth) +
		              ") -- likely a stubbed runtime function not advancing state");
	++mDepth;
	struct DepthPop {
		int& d;
		~DepthPop() { --d; }
	} depthPop{mDepth};

	const SopClass& cls = mClasses[defClass];

	Interpreter vm(cls.code);
	vm.setImports(cls.imports);
	vm.setTrace(mTrace);
	vm.setMaxSteps(mMaxSteps);
	if (objIndex >= 0 && static_cast<size_t>(objIndex) < mStatics.size())
		vm.setStatics(&mStatics[objIndex]); // object state persists across SENDs
	// Static offsets in the bytecode are relative to the DEFINING class's block
	// within the instance (the original's static_offset, from the SD entry).
	vm.setStaticBase(staticBase(defClass));
	if (mRuntimeCall)
		vm.setRuntimeCall(mRuntimeCall);

	// The cross-object link layer: extern opcodes resolve their XR offset
	// through the defining class's imports, then read/write the target object's
	// statics; SOLE probes the object list.
	vm.setExternResolve([this, defClass](uint32_t xr) {
		return resolveExtern(defClass, xr);
	});
	vm.setExternStatics([this](int obj, uint32_t off, uint32_t size) {
		return staticsPtr(obj, off, size);
	});
	vm.setObjectLookup([this](int idx) { return objectLookup(idx); });
	if (mTrace) {
		std::map<uint32_t, std::string> names;
		for (const auto& e : cls.externs)
			names[e.first] = e.second.tag + "@" + std::to_string(e.second.sourceClass);
		vm.setExternNames(std::move(names));
	}

	// Nested SEND just re-enters dispatch from the top.
	vm.setSendHook([this](int obj, int msg, std::vector<Value> a) {
		return send(obj, msg, std::move(a));
	});
	// PASS continues the *current* message at this class's parent.
	uint32_t parent = cls.header.parent;
	vm.setPassHook([this, objIndex, message, parent](std::vector<Value> a) {
		return pass(objIndex, message, parent, std::move(a));
	});

	// THIRDEYE_SLOWSEND: log any single SEND whose handler took >100 ms wall-
	// clock. Tells you exactly which (class, message) is the boot-time hot
	// loop (e.g. dungeon.init_level iterating 3000 objects).
	static const bool kSlowSend = std::getenv("THIRDEYE_SLOWSEND") != nullptr;
	auto t0 = kSlowSend ? std::chrono::steady_clock::now()
	                    : std::chrono::steady_clock::time_point{};
	auto rtn = vm.execute(offset, static_cast<uint16_t>(objIndex), args);
	if (kSlowSend) {
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		              std::chrono::steady_clock::now() - t0).count();
		if (ms >= 1) {
			std::fprintf(stderr, "[slowsend %lldms] obj %d <- msg %d (handler in class %s, depth %d)\n",
			             (long long)ms, objIndex, message, cls.name.c_str(), mDepth);
			std::fflush(stderr);
		}
	}
	return rtn;
}

} // namespace VM
