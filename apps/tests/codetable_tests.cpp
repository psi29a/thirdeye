// Regression tests for Code-space data access (Interpreter::codeDataPtr).
//
// No game data needed -- an Interpreter is constructed over a synthetic code
// buffer.
//
// The bug these lock in: Dungeon Hack's `roll_chance` resolved its probability
// table with staticBytePtr(), which rejects anything that isn't a Static or
// Extern address. But both HACK.RES call sites pass the table as
// `LETA "?:tableNN"` -- an AddrSpace::Code address into the code resource (the
// `tables` object has ORIGINAL_STATIC_SIZE 1, so the table cannot live in its
// statics at all). The resolve therefore always failed and roll_chance always
// returned 0, silently flattening every probability roll it drives.
//
// A second, smaller trap: the old code demanded 256 readable bytes up front,
// so a short table near the end of its buffer would fail to resolve even once
// the address space was right.

#include "gtest/gtest.h"

#include "../thirdeye/vm/vm.hpp"

#include <cstdint>
#include <vector>

namespace {

// A probability table as MAZE/HACK stores one: percentage weights terminated
// by 0xFF.
std::vector<uint8_t> codeWith(std::vector<uint8_t> table, size_t at) {
	std::vector<uint8_t> code(at, 0x00);
	code.insert(code.end(), table.begin(), table.end());
	return code;
}

TEST(CodeTable, ResolvesTableInTheMiddleOfTheCodeResource) {
	auto code = codeWith({10, 20, 30, 0xFF}, 64);
	VM::Interpreter vm(code);
	const uint8_t *p = vm.codeDataPtr(64, 4);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(p[0], 10);
	EXPECT_EQ(p[3], 0xFF);
}

// The case the old 256-byte demand broke: a valid short table whose buffer
// simply does not extend 256 bytes past it.
TEST(CodeTable, ShortTableNearEndOfBufferStillResolves) {
	auto code = codeWith({5, 5, 0xFF}, 8);   // 11 bytes total
	VM::Interpreter vm(code);
	EXPECT_EQ(vm.codeDataPtr(8, 256), nullptr);  // the old, over-wide probe
	const uint8_t *p = vm.codeDataPtr(8, 3);     // the narrowed probe
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(p[2], 0xFF);
}

TEST(CodeTable, RejectsRangesPastTheEnd) {
	std::vector<uint8_t> code(16, 0xAB);
	VM::Interpreter vm(code);
	EXPECT_NE(vm.codeDataPtr(0, 16), nullptr);   // exactly fits
	EXPECT_EQ(vm.codeDataPtr(0, 17), nullptr);   // one past
	EXPECT_EQ(vm.codeDataPtr(16, 1), nullptr);   // starts at the end
	// Must not wrap: off + size has to be computed wide enough that a huge
	// offset cannot alias back into range.
	EXPECT_EQ(vm.codeDataPtr(0xFFFFFFFFu, 8), nullptr);
}

// LETA tags a Code address; decodeAddr must round-trip it so the runtime can
// tell "code table" from "object static" before choosing how to resolve.
TEST(CodeTable, LetaAddressDecodesAsCodeSpace) {
	VM::Value addr = VM::makeAddr(VM::AddrSpace::Code, 1234);
	VM::Addr a = VM::decodeAddr(addr);
	EXPECT_EQ(a.space, VM::AddrSpace::Code);
	EXPECT_EQ(a.offset, 1234u);
	// A static address must NOT be mistaken for one.
	VM::Addr s = VM::decodeAddr(VM::makeAddr(VM::AddrSpace::Static, 12, 7));
	EXPECT_EQ(s.space, VM::AddrSpace::Static);
}

} // namespace
