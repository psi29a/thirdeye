#include "gtest/gtest.h"

#include "config.hpp"
#include "../thirdeye/graphics/graphics.hpp"
#include "../thirdeye/resources/res.hpp"
#include "../thirdeye/vm/vm.hpp"
#include "../thirdeye/vm/objects.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace {
std::filesystem::path sampleRes() {
	return std::filesystem::path(TESTS_DATA_DIR) / "SAMPLE.RES";
}

// Build a one-handler SOP class: 14-byte header + MHDR(auto_size=2, THIS only) +
// `body`. The handler for message `msg` lives at offset 14.
VM::SopClass makeClass(uint16_t number, uint32_t parent, int msg,
                       std::vector<uint8_t> body) {
	VM::SopClass c;
	c.number = number;
	c.name = "C" + std::to_string(number);
	std::vector<uint8_t> code(14, 0);
	code.push_back(0x02); code.push_back(0x00); // MHDR auto_size = 2
	code.insert(code.end(), body.begin(), body.end());
	c.code = std::move(code);
	c.header.parent = parent;
	c.handlers[msg] = 14;
	return c;
}

// Build a multi-handler SOP class: 14-byte header (with static_size) + each
// handler as MHDR(auto_size=2) + body. Records each handler's entry offset.
VM::SopClass makeClassMulti(uint16_t number, uint32_t parent, uint16_t staticSize,
                            std::vector<std::pair<int, std::vector<uint8_t>>> hs) {
	VM::SopClass c;
	c.number = number;
	c.name = "C" + std::to_string(number);
	std::vector<uint8_t> code(14, 0);
	code[0] = staticSize & 0xFF;
	code[1] = (staticSize >> 8) & 0xFF;
	c.header.parent = parent;
	c.header.static_size = staticSize;
	for (auto& h : hs) {
		c.handlers[h.first] = static_cast<uint32_t>(code.size());
		code.push_back(0x02); code.push_back(0x00); // MHDR auto_size = 2
		code.insert(code.end(), h.second.begin(), h.second.end());
	}
	c.code = std::move(code);
	return c;
}

// Opcode bytes used by the fixtures below.
enum : uint8_t {
	PUSH = 0x04, SHTC = 0x1D, AIM = 0x26, LTBA = 0x28, PASS = 0x23, LAW = 0x2D,
	LSW = 0x3A, SSW = 0x3D, LSBA = 0x3F, SSBA = 0x42, END = 0x56
};
}

TEST (Palette_Test, Zeros_RES){
	std::vector<uint8_t> data(PALHEADEROFFSET+3,0);
	data[0] = 1;	// set number of colours
	GRAPHICS::Palette pal(data);
	EXPECT_EQ(0, pal[0].r);
	EXPECT_EQ(0, pal[0].g);
	EXPECT_EQ(0, pal[0].b);
	EXPECT_EQ(0, pal[0].a);
	EXPECT_EQ(1, pal.getNumOfColours());
}

TEST (Palette_Test, ProperlyShifted_RES){
	std::vector<uint8_t> data(PALHEADEROFFSET+3,0);
	data[0] = 1;	// set number of colours
	data[PALHEADEROFFSET] = 1;
	data[PALHEADEROFFSET+1] = 2;
	data[PALHEADEROFFSET+2] = 63;
	GRAPHICS::Palette pal(data);
	EXPECT_EQ(4, pal[0].r);
	EXPECT_EQ(8, pal[0].g);
	EXPECT_EQ(252, pal[0].b);
	EXPECT_EQ(0, pal[0].a);
	EXPECT_EQ(1, pal.getNumOfColours());
}

TEST (Palette_Test, Zeros_GFFI){
	std::vector<uint8_t> data(3,0);
	GRAPHICS::Palette pal(data, false);
	EXPECT_EQ(0, pal[0].r);
	EXPECT_EQ(0, pal[0].g);
	EXPECT_EQ(0, pal[0].b);
	EXPECT_EQ(0, pal[0].a);
	EXPECT_EQ(1, pal.getNumOfColours());
}

TEST (Palette_Test, ProperlyShifted_GFFI){
	std::vector<uint8_t> data = { 1, 2, 63 };
	GRAPHICS::Palette pal(data, false);
	EXPECT_EQ(4, pal[0].r);
	EXPECT_EQ(8, pal[0].g);
	EXPECT_EQ(252, pal[0].b);
	EXPECT_EQ(0, pal[0].a);
	EXPECT_EQ(1, pal.getNumOfColours());
}

// --- VM self-test (hand-assembled programs: arithmetic / stack / auto vars) ---
TEST (VM_Test, SelfTest) {
	EXPECT_TRUE(VM::Interpreter::selfTest());
}

// --- Read + parse a real SOP code resource out of SAMPLE.RES ---
TEST (VM_Test, ParsesStartCodeResource) {
	RESOURCES::Resource res{sampleRes()};
	std::vector<uint8_t> code = res.getAsset((uint16_t)7); // "start" object
	ASSERT_EQ(52u, code.size());

	VM::Interpreter vm{code};
	EXPECT_EQ(0u, vm.header().static_size);
	EXPECT_EQ(5u, vm.header().import_resource);   // start.IMPT
	EXPECT_EQ(6u, vm.header().export_resource);   // start.EXPT
	EXPECT_EQ(0xFFFFFFFFu, vm.header().parent);   // no parent
}

// --- Run a handler end-to-end: M:1 @ offset 46 is "SHTC 0; END" -> returns 0 ---
TEST (VM_Test, RunsLeafHandler) {
	RESOURCES::Resource res{sampleRes()};
	VM::Interpreter vm{res.getAsset((uint16_t)7)};
	EXPECT_EQ(0, vm.execute(46)); // handler entry offset from start.EXPT
}

// --- M:0 @ offset 14 does RCRS+CALL; with no runtime hook it stops cleanly ---
TEST (VM_Test, StopsAtUnimplementedRuntimeCall) {
	RESOURCES::Resource res{sampleRes()};
	VM::Interpreter vm{res.getAsset((uint16_t)7)};
	EXPECT_THROW(vm.execute(14), VM::VmError);
}

// --- The .IMPT export resolves the runtime function referenced by RCRS ---
TEST (VM_Test, ParsesImportsAndExports) {
	RESOURCES::Resource res{sampleRes()};
	auto imports = res.getImports("start");
	auto exports = res.getExports("start");
	EXPECT_EQ("0", imports["C:launch"]);   // runtime fn "launch" is number 0
	EXPECT_EQ("start", exports["N:OBJECT"]);
	EXPECT_EQ("14", exports["M:0"]);        // handler entry offsets
	EXPECT_EQ("46", exports["M:1"]);
}

// --- With a runtime hook wired in, M:0 dispatches CALL and runs to END ---
TEST (VM_Test, RunsHandlerThroughRuntimeCall) {
	RESOURCES::Resource res{sampleRes()};
	VM::Interpreter vm{res.getAsset((uint16_t)7)};
	vm.setImports({{0, "launch"}});

	std::string calledName;
	std::vector<VM::Value> calledArgs;
	std::string program;
	vm.setRuntimeCall([&](VM::Interpreter& vmref, const std::string& name,
	                      const std::vector<VM::Value>& args) -> VM::Value {
		calledName = name;
		calledArgs = args;
		// arg[1] is the program-name address; resolve the inline string.
		if (args.size() > 1)
			program = vmref.readCodeString(static_cast<uint32_t>(args[1]));
		return 0;
	});

	EXPECT_EQ(0, vm.execute(14));            // runs RCRS+CALL+END
	EXPECT_EQ("launch", calledName);         // resolved from imports
	ASSERT_EQ(4u, calledArgs.size());        // launch(0, <str>, 0, 0)
	EXPECT_EQ(0, calledArgs[0]);
	EXPECT_EQ(0, calledArgs[3]);
	EXPECT_EQ("xxx.exe", program);           // inline string arg resolved
}

// --- Object system: SEND dispatches to a handler and returns its result ---
TEST (Object_Test, SendDispatchesAndReturns) {
	VM::ObjectSystem os;
	os.addClass(makeClass(1, 0xFFFFFFFFu, 0, {PUSH, SHTC, 7, END})); // msg0 -> 7
	int obj = os.createInstance(1);
	EXPECT_EQ(7, os.send(obj, 0, {}));
}

// --- No handler anywhere in the hierarchy -> -1 (matches RT_execute) ---
TEST (Object_Test, SendUnknownMessageReturnsMinusOne) {
	VM::ObjectSystem os;
	os.addClass(makeClass(1, 0xFFFFFFFFu, 0, {PUSH, SHTC, 7, END}));
	int obj = os.createInstance(1);
	EXPECT_EQ(-1, os.send(obj, 99, {}));
}

// --- A child with no handler inherits the parent's (class hierarchy walk) ---
TEST (Object_Test, InheritsParentHandler) {
	VM::ObjectSystem os;
	os.addClass(makeClass(1, 0xFFFFFFFFu, 0, {PUSH, SHTC, 7, END})); // parent: msg0 -> 7
	VM::SopClass child;                                              // child, no handlers
	child.number = 2;
	child.name = "child";
	child.code = std::vector<uint8_t>(14, 0);
	child.header.parent = 1;
	os.addClass(child);
	int obj = os.createInstance(2);
	EXPECT_EQ(7, os.send(obj, 0, {})); // resolves up to the parent
}

// --- PASS forwards the current message to the parent class ---
TEST (Object_Test, PassForwardsToParent) {
	VM::ObjectSystem os;
	os.addClass(makeClass(1, 0xFFFFFFFFu, 0, {PUSH, SHTC, 7, END})); // parent: msg0 -> 7
	os.addClass(makeClass(2, 1, 0, {PUSH, PASS, 0, END}));           // child: msg0 -> PASS
	int obj = os.createInstance(2);
	EXPECT_EQ(7, os.send(obj, 0, {})); // child PASSes to parent
}

// --- A parameter is passed into the handler frame and read back ---
TEST (Object_Test, PassesParameterToHandler) {
	VM::ObjectSystem os;
	os.addClass(makeClass(1, 0xFFFFFFFFu, 0, {PUSH, LAW, 0, 0, END})); // msg0 -> arg1
	int obj = os.createInstance(1);
	EXPECT_EQ(99, os.send(obj, 0, {99}));
}

// --- Static variables: object state persists across SENDs, per instance ---
TEST (Object_Test, StaticVariablePersistsPerInstance) {
	VM::ObjectSystem os;
	os.addClass(makeClassMulti(1, 0xFFFFFFFFu, /*static_size*/ 4, {
		{0, {PUSH, SHTC, 5, SSW, 0, 0, END}}, // msg0: static word[0] = 5
		{1, {PUSH, LSW, 0, 0, END}},          // msg1: return static word[0]
	}));
	int a = os.createInstance(1);
	int b = os.createInstance(1);
	EXPECT_EQ(5, os.send(a, 0, {})); // write (store leaves value on stack)
	EXPECT_EQ(5, os.send(a, 1, {})); // persisted across a separate SEND
	EXPECT_EQ(0, os.send(b, 1, {})); // a different instance is unaffected
}

// --- Static array: store then load by index (SSBA/LSBA) ---
TEST (Object_Test, StaticByteArrayStoreLoad) {
	VM::ObjectSystem os;
	os.addClass(makeClassMulti(1, 0xFFFFFFFFu, /*static_size*/ 4, {
		// msg0: static_bytes[2] = 7   (stack: index, data)
		{0, {PUSH, SHTC, 2, PUSH, SHTC, 7, SSBA, 0, 0, END}},
		// msg1: return static_bytes[2]
		{1, {PUSH, SHTC, 2, LSBA, 0, 0, END}},
	}));
	int obj = os.createInstance(1);
	EXPECT_EQ(7, os.send(obj, 0, {}));
	EXPECT_EQ(7, os.send(obj, 1, {}));
}

// --- Constant table embedded in the code resource (LTBA) ---
TEST (Object_Test, LoadsConstantTableByte) {
	VM::SopClass c;
	c.number = 1;
	c.name = "tbl";
	c.header.parent = 0xFFFFFFFFu;
	std::vector<uint8_t> code(14, 0);
	c.handlers[0] = static_cast<uint32_t>(code.size()); // = 14
	code.push_back(0x02); code.push_back(0x00);          // 14-15 MHDR
	code.push_back(PUSH);                                 // 16
	code.push_back(SHTC); code.push_back(2);              // 17-18 index = 2
	code.push_back(LTBA); code.push_back(23); code.push_back(0); // 19-21 table @23
	code.push_back(END);                                 // 22
	code.push_back(10); code.push_back(20); code.push_back(30);  // 23-25 table data
	c.code = std::move(code);

	VM::ObjectSystem os;
	os.addClass(c);
	int obj = os.createInstance(1);
	EXPECT_EQ(30, os.send(obj, 0, {})); // table[2]
}

// --- Array-index multiply (AIM): top += index * width ---
TEST (Object_Test, ArrayIndexMultiply) {
	VM::ObjectSystem os;
	// base 3, index 4, width 2 -> 3 + 4*2 = 11
	os.addClass(makeClass(1, 0xFFFFFFFFu, 0,
	                       {PUSH, SHTC, 3, PUSH, SHTC, 4, AIM, 2, 0, END}));
	int obj = os.createInstance(1);
	EXPECT_EQ(11, os.send(obj, 0, {}));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
