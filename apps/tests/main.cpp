#include "gtest/gtest.h"

#include "config.hpp"
#include "../thirdeye/graphics/graphics.hpp"
#include "../thirdeye/resources/res.hpp"
#include "../thirdeye/vm/vm.hpp"
#include "../thirdeye/vm/objects.hpp"
#include "../thirdeye/vm/events.hpp"

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
	PUSH = 0x04, ADD = 0x09, MUL = 0x0B, SHTC = 0x1D, AIM = 0x26, LTBA = 0x28,
	PASS = 0x23, LAW = 0x2D,
	LABA = 0x32, SABA = 0x35,
	LSW = 0x3A, SSW = 0x3D, LSBA = 0x3F, SSBA = 0x42,
	LXW = 0x47, SXW = 0x4A, LXBA = 0x4C, SXAS = 0x53, SOLE = 0x55, END = 0x56
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

// --- AESOP/16 "1.10" VFX shape decoder ---
// Hand-build a one-shape 4x2 shape table and check the decoded pixels:
//   row 0: a string token of 4 literal pixels {10,11,12,13}
//   row 1: skip 1 transparent, then a run of 3 pixels of value 5 -> {0,5,5,5}
TEST (Bitmap_Test, DecodesVFXShape1_10) {
	std::vector<uint8_t> b = {
		'1', '.', '1', '0',          // version magic
		1, 0, 0, 0,                  // number_of_shapes = 1
		16, 0, 0, 0, 0, 0, 0, 0,     // dir entry 0: {offset=16, color=0}
		// subpicture header @16 (boundsy=h-1, boundsx=w-1, originy, originx,
		// xmin, ymin, xmax, ymax) for a 4x2 image:
		1, 0,                        // boundsy = 1 -> height 2
		3, 0,                        // boundsx = 3 -> width 4
		0, 0,                        // originy
		0, 0,                        // originx
		0, 0, 0, 0,                  // xmin
		0, 0, 0, 0,                  // ymin
		3, 0, 0, 0,                  // xmax
		1, 0, 0, 0,                  // ymax
		// row 0: string of 4 (marker = 2*4+1 = 9), then 4 literal pixels
		9, 10, 11, 12, 13,
		0,                           // end of row 0
		// row 1: skip 1 (marker 1, len 1), run of 3 value 5 (marker 2*3=6, val 5)
		1, 1,
		6, 5,
		0,                           // end of row 1
	};
	GRAPHICS::Bitmap bmp(b);
	EXPECT_EQ(1u, bmp.getNumberOfBitmaps());
	EXPECT_EQ(4u, bmp.getWidth(0));
	EXPECT_EQ(2u, bmp.getHeight(0));
	std::vector<uint8_t> px = bmp[0];
	ASSERT_EQ(8u, px.size());
	std::vector<uint8_t> expect = {10, 11, 12, 13, 0, 5, 5, 5};
	EXPECT_EQ(expect, px);
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

// --- Tagged address model: encode/decode round-trips per space ---
TEST (VM_Test, AddressEncodeDecode) {
	VM::Addr code = VM::decodeAddr(VM::makeAddr(VM::AddrSpace::Code, 543));
	EXPECT_EQ(VM::AddrSpace::Code, code.space);
	EXPECT_EQ(543u, code.offset);

	VM::Addr stat = VM::decodeAddr(VM::makeAddr(VM::AddrSpace::Static, 12, 2002));
	EXPECT_EQ(VM::AddrSpace::Static, stat.space);
	EXPECT_EQ(12u, stat.offset);
	EXPECT_EQ(2002u, stat.obj);          // object index survives the encoding

	// A plain small integer (e.g. a resource number) is not an address.
	EXPECT_EQ(VM::AddrSpace::Invalid, VM::decodeAddr(1382).space);
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

// --- Auto arrays: store/load via fptr-relative byte indexing (LABA/SABA) ---
TEST (VM_Test, AutoArrayStoreLoad) {
	std::vector<uint8_t> code(14, 0);
	code.push_back(0x08); code.push_back(0x00); // MHDR auto_size = 8
	// auto_bytes[3] = 7; return auto_bytes[3]   (array base at fptr-8)
	std::vector<uint8_t> body = {PUSH, SHTC, 3, PUSH, SHTC, 7, SABA, 8, 0,
	                             PUSH, SHTC, 3, LABA, 8, 0, END};
	code.insert(code.end(), body.begin(), body.end());
	VM::Interpreter vm{code};
	EXPECT_EQ(7, vm.execute(14));
}

// --- Inherited statics: parent and child each get their own block ---
// The instance allocates the whole chain's statics (base-class-first), and a
// handler's static offsets are relative to its DEFINING class's block.
TEST (Object_Test, InheritedStaticsHaveSeparateBlocks) {
	VM::ObjectSystem os;
	os.addClass(makeClassMulti(1, 0xFFFFFFFFu, /*static_size*/ 4, { // parent
		{2, {PUSH, SHTC, 5, SSW, 0, 0, END}}, // msg2: parent word[0] = 5
		{3, {PUSH, LSW, 0, 0, END}},          // msg3: return parent word[0]
	}));
	os.addClass(makeClassMulti(2, /*parent*/ 1, /*static_size*/ 4, {  // child
		{0, {PUSH, SHTC, 9, SSW, 0, 0, END}}, // msg0: child word[0] = 9
		{1, {PUSH, LSW, 0, 0, END}},          // msg1: return child word[0]
	}));
	int obj = os.createInstance(2);
	EXPECT_EQ(5, os.send(obj, 2, {})); // parent handler writes parent block
	EXPECT_EQ(9, os.send(obj, 0, {})); // child handler writes child block
	EXPECT_EQ(5, os.send(obj, 3, {})); // both survive: separate offsets
	EXPECT_EQ(9, os.send(obj, 1, {}));
}

// Two classes for the extern fixtures: class 1 ("provider") exports a static
// word "W:foo" at offset 0 and a byte array "B:arr" at offset 0; class 2
// ("consumer") imports them at XR offsets 0/2. The consumer handlers take the
// provider's OBJECT INDEX as a message parameter (LAW 0,0 = arg1).
namespace {
VM::ObjectSystem makeExternPair() {
	VM::ObjectSystem os;
	VM::SopClass provider = makeClassMulti(1, 0xFFFFFFFFu, /*static_size*/ 4, {
		{0, {PUSH, SHTC, 42, SSW, 0, 0, END}},            // msg0: word[0] = 42
		{1, {PUSH, LSW, 0, 0, END}},                      // msg1: return word[0]
		{2, {PUSH, SHTC, 2, PUSH, SHTC, 7, SSBA, 0, 0, END}}, // msg2: bytes[2] = 7
	});
	provider.exportedVars["W:foo"] = 0;
	provider.exportedVars["B:arr"] = 0;
	os.addClass(provider);

	VM::SopClass consumer = makeClassMulti(2, 0xFFFFFFFFu, 0, {
		// msg0(obj): return obj's W:foo        (extern scalar load)
		{0, {PUSH, LAW, 0, 0, LXW, 0, 0, END}},
		// msg1(obj): obj's W:foo = 77          (extern scalar store, leaves 77)
		{1, {PUSH, LAW, 0, 0, PUSH, SHTC, 77, SXW, 0, 0, END}},
		// msg2(obj): return obj's B:arr[2]     (SXAS merges the array index)
		{2, {PUSH, LAW, 0, 0, PUSH, SHTC, 2, SXAS, LXBA, 2, 0, END}},
		// msg3(n): SOLE -- handle if a live object exists at index n, else -1
		{3, {PUSH, LAW, 0, 0, SOLE, END}},
	});
	consumer.externs[0] = {"W:foo", /*sourceClass*/ 1};
	consumer.externs[2] = {"B:arr", /*sourceClass*/ 1};
	os.addClass(consumer);
	return os;
}
}

// --- Extern scalar: one object reads another object's public static ---
TEST (Object_Test, ExternScalarLoadAndStore) {
	VM::ObjectSystem os = makeExternPair();
	int prov = os.createInstance(1);
	int cons = os.createInstance(2);
	EXPECT_EQ(42, os.send(prov, 0, {}));     // provider sets its own word to 42
	EXPECT_EQ(42, os.send(cons, 0, {prov})); // consumer reads it via LXW
	EXPECT_EQ(77, os.send(cons, 1, {prov})); // consumer writes it via SXW
	EXPECT_EQ(77, os.send(prov, 1, {}));     // provider sees the new value
}

// --- Extern array: SXAS merges the index, LXBA reads across objects ---
TEST (Object_Test, ExternArrayWithSXAS) {
	VM::ObjectSystem os = makeExternPair();
	int prov = os.createInstance(1);
	int cons = os.createInstance(2);
	EXPECT_EQ(7, os.send(prov, 2, {}));     // provider: bytes[2] = 7
	EXPECT_EQ(7, os.send(cons, 2, {prov})); // consumer reads bytes[2] via extern
}

// --- SOLE: live object -> its handle (index); empty slot -> -1 ---
TEST (Object_Test, SoleChecksObjectList) {
	VM::ObjectSystem os = makeExternPair();
	int prov = os.createInstance(1);
	int cons = os.createInstance(2);
	EXPECT_EQ(prov, os.send(cons, 3, {prov}));
	EXPECT_EQ(-1, os.send(cons, 3, {99}));  // no object at index 99
}

// --- Event system (EVENT.C) ---
// A class for the event fixtures. Each handler records into the instance's own
// static word[0], so dispatch effects are observable via a read-back message:
//   M:5/M:7  record a digit (word[0] = word[0]*10 + N) -- captures fire ORDER
//   M:9      store the event parameter (arg1) into word[0]
//   M:8      return word[0]
namespace {
VM::SopClass makeEventClass(uint16_t number) {
	return makeClassMulti(number, 0xFFFFFFFFu, /*static_size*/ 4, {
		{5, {PUSH, LSW, 0, 0, PUSH, SHTC, 10, MUL, PUSH, SHTC, 5, ADD, SSW, 0, 0, END}},
		{7, {PUSH, LSW, 0, 0, PUSH, SHTC, 10, MUL, PUSH, SHTC, 7, ADD, SSW, 0, 0, END}},
		{9, {PUSH, LAW, 0, 0, SSW, 0, 0, END}},  // word[0] = parameter (arg1)
		{8, {PUSH, LSW, 0, 0, END}},             // return word[0]
	});
}
constexpr int32_t APP_EVENT = 40; // an application event type (>= 32)
}

// --- post_event then dispatch delivers the event's parameter to the handler ---
TEST (Event_Test, NotifyPostDispatchDeliversParameter) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	ev.notify(obj, /*message*/ 9, APP_EVENT, /*parameter*/ 7);
	ev.postEvent(/*owner*/ 0, APP_EVENT, /*parameter*/ 7);
	EXPECT_EQ(1u, ev.pendingEvents());
	ev.dispatchEvent();
	EXPECT_EQ(0u, ev.pendingEvents());      // event consumed
	EXPECT_EQ(7, os.send(obj, 8, {}));      // handler ran with parameter 7
}

// --- parameter must match: a different parameter does not fire the handler ---
TEST (Event_Test, ParameterMustMatch) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	ev.notify(obj, 9, APP_EVENT, /*parameter*/ 7);
	ev.postEvent(0, APP_EVENT, /*parameter*/ 99); // 99 != 7
	ev.dispatchEvent();
	EXPECT_EQ(0, os.send(obj, 8, {})); // never recorded -> still zero
}

// --- a -1 notify parameter is a wildcard: any event parameter fires it ---
TEST (Event_Test, WildcardParameterMatchesAny) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	ev.notify(obj, 9, APP_EVENT, /*parameter*/ -1);
	ev.postEvent(0, APP_EVENT, /*parameter*/ 123);
	ev.dispatchEvent();
	EXPECT_EQ(123, os.send(obj, 8, {}));
}

// --- handlers fire in registration order (observed via the digit sequence) ---
TEST (Event_Test, HandlersFireInRegistrationOrder) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	ev.notify(obj, 5, APP_EVENT, -1); // M:5 first
	ev.notify(obj, 7, APP_EVENT, -1); // then M:7
	ev.postEvent(0, APP_EVENT, 0);
	ev.dispatchEvent();
	EXPECT_EQ(57, os.send(obj, 8, {})); // 0*10+5=5, 5*10+7=57 -> order 5 then 7
}

// --- cancel() removes a request so dispatch no longer fires it ---
TEST (Event_Test, CancelRemovesRequest) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	ev.notify(obj, 9, APP_EVENT, -1);
	ev.cancel(obj, 9, APP_EVENT, -1);
	ev.postEvent(0, APP_EVENT, 55);
	ev.dispatchEvent();
	EXPECT_EQ(0, os.send(obj, 8, {})); // cancelled -> never recorded
}

// --- drain dispatches every queued event; send_event posts + drains ---
TEST (Event_Test, DrainAndSendEvent) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	ev.notify(obj, 5, APP_EVENT, -1);
	ev.postEvent(0, APP_EVENT, 0);
	ev.postEvent(0, APP_EVENT, 0);
	ev.drainEventQueue();               // fires M:5 twice -> 5, then 55
	EXPECT_EQ(0u, ev.pendingEvents());
	EXPECT_EQ(55, os.send(obj, 8, {}));

	ev.sendEvent(0, APP_EVENT, 0);      // posts + drains immediately -> 555
	EXPECT_EQ(555, os.send(obj, 8, {}));
}

// --- system events defer behind pending application events (priority) ---
// A queued SYS event is re-queued behind any pending APP event so the app's
// response to an earlier action isn't pre-empted (EVENT.C dispatch_event). The
// digit order proves it: tens = first to fire, ones = second.
TEST (Event_Test, SystemEventDefersToAppEvent) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	ev.notify(obj, 5, VM::SYS_KEYDOWN, -1); // M:5 on a system (input) event
	ev.notify(obj, 7, APP_EVENT, -1);       // M:7 on an application event

	ev.postEvent(0, VM::SYS_KEYDOWN, 0);    // queued first...
	ev.postEvent(0, APP_EVENT, 0);          // ...but an app event is also pending

	ev.drainEventQueue();
	// 7 (app) ran before 5 (sys) despite the sys event being queued first.
	EXPECT_EQ(75, os.send(obj, 8, {}));
}

// --- flush_input_events drops queued input events but keeps app events ---
TEST (Event_Test, FlushInputEvents) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);

	ev.postEvent(0, VM::SYS_KEYDOWN, 0); // input event
	ev.postEvent(0, APP_EVENT, 0);       // application event
	EXPECT_EQ(2u, ev.pendingEvents());
	ev.flushInputEvents();
	EXPECT_EQ(1u, ev.pendingEvents());   // only the app event survives
}

// --- region (windowed mouse) events: a click inside a registered window fires
// the handler with the window handle as the parameter; a click outside does not.
TEST (Event_Test, RegionClickInsideFiresHandler) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	// Register a window and ask to be told (msg 9 = "record parameter") on a
	// click in it. The notify parameter is the window handle.
	int32_t win = ev.assignWindow(/*owner*/ obj, /*x1*/ 10, /*y1*/ 10,
	                              /*x2*/ 50, /*y2*/ 50);
	EXPECT_GE(win, 2); // 0/1 are the pages
	ev.notify(obj, /*message*/ 9, VM::SYS_CLICK_REGION, /*parameter*/ win);

	ev.mouseMove(20, 20);                 // inside the window
	ev.mouseButton(/*left*/ true, false); // press
	ev.drainEventQueue();

	// The handler ran with the event parameter = the window handle.
	EXPECT_EQ(win, os.send(obj, 8, {}));
}

TEST (Event_Test, RegionClickOutsideIgnored) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	int32_t win = ev.assignWindow(obj, 10, 10, 50, 50);
	ev.notify(obj, 9, VM::SYS_CLICK_REGION, win);

	ev.mouseMove(200, 200);               // outside the window
	ev.mouseButton(true, false);
	ev.drainEventQueue();

	EXPECT_EQ(0, os.send(obj, 8, {}));    // handler never fired
}

// A released window no longer produces region events for clicks over it.
TEST (Event_Test, ReleasedWindowProducesNoRegionEvent) {
	VM::ObjectSystem os;
	os.addClass(makeEventClass(1));
	VM::EventSystem ev(os);
	int obj = os.createInstance(1);

	int32_t win = ev.assignWindow(obj, 10, 10, 50, 50);
	ev.notify(obj, 9, VM::SYS_CLICK_REGION, win);
	ev.releaseWindow(win);

	ev.mouseMove(20, 20);                 // inside the (now released) rect
	ev.mouseButton(true, false);
	ev.drainEventQueue();

	EXPECT_EQ(0, os.send(obj, 8, {}));
}

// --- get_x1/get_y1/get_x2/get_y2: a window's rectangle (the menu reads get_y1
// of a hovered option to compute which option it is). ---
TEST (Event_Test, WindowRectReturnsAssignedRectangle) {
	VM::ObjectSystem os;
	VM::EventSystem ev(os);
	int32_t w = ev.assignWindow(/*owner*/ 5, /*x1*/ 10, /*y1*/ 107,
	                            /*x2*/ 175, /*y2*/ 114);
	int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	ASSERT_TRUE(ev.windowRect(w, x1, y1, x2, y2));
	EXPECT_EQ(10, x1);
	EXPECT_EQ(107, y1);
	EXPECT_EQ(175, x2);
	EXPECT_EQ(114, y2);
	ev.releaseWindow(w);
	EXPECT_FALSE(ev.windowRect(w, x1, y1, x2, y2)); // released -> no rect
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
