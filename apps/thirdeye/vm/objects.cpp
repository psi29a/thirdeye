/*
 * objects.cpp
 *
 * AESOP object & message system. See objects.hpp.
 */

#include "objects.hpp"

namespace VM {

void ObjectSystem::addClass(SopClass cls) {
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

int ObjectSystem::createInstance(uint16_t classNumber) {
	mObjList.push_back(classNumber);
	// Allocate zeroed static-variable storage for this instance.
	uint16_t staticSize = 0;
	if (const SopClass* cls = classByNumber(classNumber))
		staticSize = cls->header.static_size;
	mStatics.emplace_back(staticSize, 0);
	return static_cast<int>(mObjList.size() - 1);
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
	uint16_t defClass;
	uint32_t offset;
	if (!resolve(startClass, message, defClass, offset))
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
	const SopClass& cls = mClasses[defClass];

	Interpreter vm(cls.code);
	vm.setImports(cls.imports);
	vm.setTrace(mTrace);
	if (objIndex >= 0 && static_cast<size_t>(objIndex) < mStatics.size())
		vm.setStatics(&mStatics[objIndex]); // object state persists across SENDs
	if (mRuntimeCall)
		vm.setRuntimeCall(mRuntimeCall);

	// Nested SEND just re-enters dispatch from the top.
	vm.setSendHook([this](int obj, int msg, std::vector<Value>& a) {
		return send(obj, msg, std::move(a));
	});
	// PASS continues the *current* message at this class's parent.
	uint32_t parent = cls.header.parent;
	vm.setPassHook([this, objIndex, message, parent](std::vector<Value>& a) {
		return pass(objIndex, message, parent, std::move(a));
	});

	return vm.execute(offset, static_cast<uint16_t>(objIndex), args);
}

} // namespace VM
