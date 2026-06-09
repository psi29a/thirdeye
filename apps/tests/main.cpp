#include "gtest/gtest.h"

#include "config.hpp"
#include "../thirdeye/graphics/graphics.hpp"
#include "../thirdeye/resources/res.hpp"
#include "../thirdeye/vm/vm.hpp"

#include <filesystem>
#include <string>

namespace {
std::filesystem::path sampleRes() {
	return std::filesystem::path(TESTS_DATA_DIR) / "SAMPLE.RES";
}
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
