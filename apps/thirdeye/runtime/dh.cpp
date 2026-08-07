#include "internal.hpp"

#include "../graphics/graphics.hpp"
#include "../resources/res.hpp"
#include "../vm/events.hpp"

#include <iostream>

#include <algorithm>  // std::min (readInto)
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>   // getenv
#include <cstring>   // memcpy/memset -- libstdc++ does not pull these in
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

// Dungeon Hack runtime functions -- the ~35 CALLs listed in
// ../../../../dh_research/ADDITIONAL_DH_RUNTIME_FUNCTIONS.TXT that exist in
// HACK.RES/OPEN.RES but not in the EOB3 runtime. Format specs for the files
// these functions read/write live in docs/dungeon_hack_maze.md; add real
// behaviour only where the SOP observes the return.

namespace THIRDEYE::runtime::dh {

namespace {

// Case-insensitive child resolve (dup of engine.cpp's helper; keeping dh.cpp
// self-contained rather than exposing it in internal.hpp until a second
// caller shows up).
std::filesystem::path resolveChildCI(const std::filesystem::path &parent,
                                     const std::string &name) {
	std::error_code ec;
	auto exact = parent / name;
	if (std::filesystem::exists(exact, ec)) return exact;
	if (!std::filesystem::is_directory(parent, ec)) return exact;
	std::string want = name;
	for (auto &c : want) c = static_cast<char>(
	    std::tolower(static_cast<unsigned char>(c)));
	for (auto &entry : std::filesystem::directory_iterator(parent, ec)) {
		std::string cand = entry.path().filename().string();
		for (auto &c : cand) c = static_cast<char>(
		    std::tolower(static_cast<unsigned char>(c)));
		if (cand == want) return entry.path();
	}
	return exact;
}

// Resolve a SOP-provided DOS path (`SAVEGAME\PC.DAT`, `SETTINGS.DAT`, etc.)
// against the game root, splitting on `\` and doing per-component
// case-insensitive lookup.
std::filesystem::path resolveDosPath(Context &ctx, const std::string &dosPath) {
	auto path = ctx.res.resourcePath().parent_path();
	std::string part;
	for (size_t i = 0; i <= dosPath.size(); ++i) {
		if (i == dosPath.size() || dosPath[i] == '\\' || dosPath[i] == '/') {
			if (!part.empty()) path = resolveChildCI(path, part);
			part.clear();
		} else {
			part.push_back(dosPath[i]);
		}
	}
	return path;
}

// The MAZE-only files (LEVELS.DAT / FEA%02d.DAT / ITEMS.DAT / VISIBLE.DAT)
// live under savegame/ — MAZE runs from there. Convenience wrapper.
std::filesystem::path savegamePath(Context &ctx, const std::string &name) {
	auto sg = resolveChildCI(ctx.res.resourcePath().parent_path(), "savegame");
	return resolveChildCI(sg, name);
}

// --- Dungeon Hack 3D view geometry -----------------------------------------
//
// Lifted verbatim from AESOP.EXE's own tables (draw_walls == 1f36:0785). See
// ../../../../dh_research/AESOP/README.md for how they were located and
// validated; `viewspace_tables.txt` there is the extraction these mirror.
//
// The view is 25 wall FACES over 18 distinct map cells, in 4 depth bands
// (11 + 7 + 5 + 2), nearest last. Per face: a map offset (per facing), a blit
// position relative to the view origin, a mirror flag, and a wallset
// sub-bitmap index chosen by the map cell's wall type.
constexpr int kViewCells = 25;

constexpr int8_t kCellDX[4][kViewCells] = {
	{ -3, -2, -1,  1,  2,  3, -2, -1,  0,  1,  2, -2, -1,  1,  2, -1,  0,  1, -1,  1, -1,  0,  1, -1,  1 },
	{  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  0,  0 },
	{  3,  2,  1, -1, -2, -3,  2,  1,  0, -1, -2,  2,  1, -1, -2,  1,  0, -1,  1, -1,  1,  0, -1,  1, -1 },
	{ -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, -1, -1,  0,  0 },
};
constexpr int8_t kCellDY[4][kViewCells] = {
	{ -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, -1, -1,  0,  0 },
	{ -3, -2, -1,  1,  2,  3, -2, -1,  0,  1,  2, -2, -1,  1,  2, -1,  0,  1, -1,  1, -1,  0,  1, -1,  1 },
	{  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  0,  0 },
	{  3,  2,  1, -1, -2, -3,  2,  1,  0, -1, -2,  2,  1, -1, -2,  1,  0, -1,  1, -1,  1,  0, -1,  1, -1 },
};
// Blit offsets relative to the view origin (AESOP adds 138 / 13 inline).
constexpr int16_t kCellX[kViewCells] = {
	-16, 16, 64, 104, 136, 168, -32, 16, 64, 112, 160, 0, 48, 112, 160,
	-32, 48, 128, 24, 128, -104, 24, 152, 0, 152 };
constexpr int16_t kCellY[kViewCells] = {
	27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 26, 20, 20, 26,
	20, 20, 20, 8, 8, 8, 8, 8, 0, 0 };
constexpr uint8_t kCellMirror[kViewCells] = {
	0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
// Index into the SOP's 18-entry per-cell arrays (floor_at/visible).
constexpr uint8_t kCellVis[kViewCells] = {
	1, 2, 3, 3, 4, 5, 8, 8, 9, 10, 10, 8, 9, 9, 10, 12, 13, 14, 13, 13,
	15, 16, 17, 16, 16 };
// Wallset sub-bitmap per (wall type, face). Only two wall types exist -- the
// table in AESOP.EXE is 2 rows; past that it runs into a squares lookup table.
constexpr uint8_t kCellPanel[2][kViewCells] = {
	{ 4, 4, 3, 3, 4, 4, 13, 13, 13, 13, 13, 5, 2, 2, 5, 16, 16, 16, 1, 1, 8, 8, 8, 0, 0 },
	{ 10, 10, 9, 9, 10, 10, 14, 14, 14, 14, 14, 11, 12, 12, 11, 17, 17, 17, 7, 7, 15, 15, 15, 6, 6 },
};

// --- init_viewspace / build_clipping tables ---------------------------------
//
// init_viewspace (1f36:040f) builds the SOP's 18 view CELLS (distinct from
// draw_walls' 25 faces) from three per-facing unit vectors: forward, left,
// right. Rows are base+F*3 (7 cells), +F*2 (5), +F (3), base (3) = 18.
constexpr int kViewSlots = 18;
constexpr int8_t kFwdX[4]   = {  0,  1,  0, -1 };   // DS:0x10eb
constexpr int8_t kLeftX[4]  = { -1,  0,  1,  0 };   // DS:0x10f3
constexpr int8_t kRightX[4] = {  1,  0, -1,  0 };   // DS:0x10fb
constexpr int8_t kFwdY[4]   = { -1,  0,  1,  0 };   // DS:0x1103
constexpr int8_t kLeftY[4]  = {  0, -1,  0,  1 };   // DS:0x110b
constexpr int8_t kRightY[4] = {  0,  1,  0, -1 };   // DS:0x1113

// build_clipping (1f36:05f4) occlusion table, DS:0x1117, 18 rows x 18 bytes of
// SIGNED clip contributions. 0x7f = no contribution, 0x7e = hard stop (cell
// occluded). Pixel edge = value * 8. Rows 0/6/7/11 are all-0x7e -- the extreme
// lateral cells at depths 3 and 2 lie outside the view cone and never draw;
// rows 15..17 (the party's own row) are all-0x7f and are never occluded.
constexpr uint8_t kClip[18][18] = {
	{ 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e },
	{ 0x7f, 0x7f, 0x02, 0x7f, 0x7f, 0x7f, 0x7f, 0xfe, 0x7e, 0x7f, 0x7f, 0x7f, 0x7e, 0x03, 0x7f, 0xfd, 0x7f, 0x7f },
	{ 0x7f, 0xfe, 0x7f, 0x08, 0x7f, 0x7f, 0x7f, 0xfe, 0x7e, 0x06, 0x7f, 0x7f, 0xfa, 0x03, 0x7f, 0xfd, 0x7f, 0x7f },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0xfa, 0x7e, 0x10, 0x7f, 0xfd, 0x7e, 0x13, 0x7f, 0x7f, 0x7f },
	{ 0x7f, 0x7f, 0x7f, 0xf2, 0x7f, 0x14, 0x7f, 0x7f, 0x7f, 0xf0, 0x7e, 0x14, 0x7f, 0xed, 0x10, 0x7f, 0x7f, 0x13 },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0xec, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7e, 0x14, 0x7f, 0xed, 0x7e, 0x7f, 0x7f, 0x13 },
	{ 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e },
	{ 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x06, 0x7f, 0x7f, 0xfa, 0x03, 0x7f, 0xfd, 0x7f, 0x7f },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0xfa, 0x7f, 0x10, 0x7f, 0xfd, 0x7e, 0x13, 0xfd, 0x7f, 0x13 },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0xf0, 0x7f, 0x7f, 0x7f, 0xed, 0x10, 0x7f, 0x7f, 0x13 },
	{ 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x03, 0x7f, 0xfd, 0x7f, 0x7f },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0xfd, 0x7f, 0x13, 0xfd, 0x7f, 0x13 },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0xed, 0x7f, 0x7f, 0x7f, 0x13 },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f },
};

// Top-left of Dungeon Hack's 3D view on the 320x200 screen. The SOP registers
// it as assign_subwindow(..., 138, 13, 313, 132) -- a 176x120 rect right of the
// inventory column and under the stone arch of the HUD backdrop -- and uses it
// as the copy_window destination for the floor page.
constexpr int kViewX = 138;
constexpr int kViewY = 13;
constexpr int kViewW = 176;   // (313 - 138 + 1)
constexpr int kViewH = 120;   // (132 -  13 + 1)

// DH exposes a single sequential-file API (open_file / read_* / close_file
// with no handle arg), so one "current" file is enough. Buffered eagerly at
// open so read_* is trivial cursor math.
struct DHFile {
	std::vector<uint8_t> buf;
	std::size_t cursor = 0;
	std::string name;
	bool open = false;
	void reset() { buf.clear(); cursor = 0; name.clear(); open = false; }
};
DHFile &currentFile() { static DHFile f; return f; }

// Shared PRNG for seed_random / randomize_array / roll_chance. Independent of
// rtcode.cpp's rnd() PRNG so seed_random(N) doesn't disturb SOP-visible rnd().
std::mt19937 &dhRng() {
	static std::mt19937 g(0xDEADBEEFu);
	return g;
}

// Fill `out` with the requested number of bytes from `buf` at `cursor`,
// zero-padding on short reads. Returns bytes actually read.
std::size_t readInto(DHFile &f, std::size_t n, uint8_t *out) {
	std::size_t have = (f.cursor < f.buf.size()) ? (f.buf.size() - f.cursor) : 0;
	std::size_t take = std::min(n, have);
	if (take) std::memcpy(out, f.buf.data() + f.cursor, take);
	if (take < n) std::memset(out + take, 0, n - take);
	f.cursor += take;
	return take;
}

// Load a per-level chunk from `<savegame>/<file>` into `dest[len]`. MAZE writes
// LEVELS.DAT as `4-byte header + (DEPTH+10)×0x400 chunks`; VISIBLE.DAT looks
// similar. If the file is missing or short, zero-fill so the SOP sees a
// deterministic empty level rather than random stack garbage.
void loadChunk(Context &ctx, const char *file, int levelIdx,
               std::size_t headerLen, std::size_t chunkLen,
               uint8_t *dest) {
	auto path = savegamePath(ctx, file);
	std::memset(dest, 0, chunkLen);
	std::ifstream in(path, std::ios::binary);
	if (!in) return;
	in.seekg(static_cast<std::streamoff>(headerLen +
	                                     static_cast<std::size_t>(levelIdx) * chunkLen));
	in.read(reinterpret_cast<char *>(dest),
	        static_cast<std::streamsize>(chunkLen));
}

}  // namespace

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
	// -------- host-loop hooks (from the original stub set) --------

	// page_flip(): buffer-swap in the DOS driver. Our host pump already
	// presents each frame; we just poke the surface so anything that was
	// drawn since the last present shows up in case the SOP is running
	// tight before it hits dispatch_event.
	// ponytail: no-op present is fine while pumpHost runs; upgrade if the
	// SOP page-flips without ever yielding.
	if (fn == "page_flip") {
		if (ctx.gfx) ctx.gfx->update();
		result = 0;
		return true;
	}

	// sequence_playing(): returns nonzero while a music sequence is still
	// playing. Our mixer is fire-and-forget with no query API, and DH's
	// intro polls this to decide when to advance. Return 0 = "done" so the
	// SOP progresses instead of spinning.
	// ponytail: honest for THIRDEYE_MUTE runs; wire to Mixer state when we
	// actually play the DH sequences.
	if (fn == "sequence_playing") {
		result = 0;
		return true;
	}

	// touch(bitmap): marks a bitmap dirty / forces a repaint of the given
	// page. Under our immediate-mode graphics there's nothing to invalidate.
	if (fn == "touch") {
		(void)args;
		result = 0;
		return true;
	}

	// -------- trivial helpers --------

	// pause(ms): sleep for `ms` milliseconds. DH uses this to pace intro
	// animations and menus. Cap at 200ms so a stuck loop doesn't hang the
	// engine; keep it real enough that timing-dependent SOPs still work.
	// ponytail: 200ms cap, remove if a real animation looks too fast.
	if (fn == "pause" && args.size() >= 1) {
		int ms = static_cast<int>(args[0]);
		if (ms > 0) {
			if (ms > 200) ms = 200;
			std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		}
		result = 0;
		return true;
	}

	// seed_random(seed): re-seed the DH PRNG (kept separate from rtcode's
	// rnd() so SOP-visible randomness isn't perturbed).
	if (fn == "seed_random" && args.size() >= 1) {
		dhRng().seed(static_cast<uint32_t>(static_cast<int32_t>(args[0])));
		result = 0;
		return true;
	}

	// roll_chance(prob_table_ptr): scan a byte array of percentages ending in
	// 0xFF, roll a d100, return the index whose cumulative weight crosses it.
	// ponytail: bounded scan (256 entries max), returns 0 if the table can't be
	// bound-checked -- rare enough not to be worth loud failure yet.
	if (fn == "roll_chance" && args.size() >= 1) {
		// The table lives in the CODE resource, not in object statics: both
		// HACK.RES call sites pass it as `LETA "?:tableNN"`, which is an
		// AddrSpace::Code address (`tables` has ORIGINAL_STATIC_SIZE 1, so a
		// probability table could not live in its statics at all). Resolving
		// it with staticBytePtr therefore always failed and roll_chance
		// always returned 0. Accept either space, and grow the probe window
		// instead of demanding 256 readable bytes up front so a short table
		// near the end of its buffer still resolves.
		const uint8_t *tbl = nullptr;
		std::size_t avail = 0;
		for (std::size_t want : {16u, 64u, 256u}) {
			const VM::Addr a = VM::decodeAddr(args[0]);
			const uint8_t *p =
			    (a.space == VM::AddrSpace::Code)
			        ? ctx.vm.codeDataPtr(a.offset, static_cast<uint32_t>(want))
			        : staticBytePtr(ctx, args[0], static_cast<uint32_t>(want));
			if (!p) break;              // window no longer fits; keep the last
			tbl = p;
			avail = want;
			// Stop early once the terminator is inside the resolved window.
			if (std::memchr(p, 0xFF, want) != nullptr) break;
		}
		if (!tbl) { result = 0; return true; }
		int sum = 0;
		int n = 0;
		while (static_cast<std::size_t>(n) < avail && tbl[n] != 0xFF) {
			sum += tbl[n];
			++n;
		}
		int roll = sum > 0 ? static_cast<int>(dhRng()() % sum) : 0;
		int idx = 0, acc = 0;
		for (idx = 0; idx < n; ++idx) {
			acc += tbl[idx];
			if (roll < acc) break;
		}
		result = idx + 1;      // ADDITIONAL doc: "from 1 to table length -1"
		return true;
	}

	// randomize_array(array_ptr, count): place 0..count-1 in random order.
	// Kernel calls this to shuffle a fixed set (e.g. permuting spawn slots).
	if (fn == "randomize_array" && args.size() >= 2) {
		int n = static_cast<int>(args[1]);
		if (n <= 0) { result = 0; return true; }
		uint8_t *arr = staticBytePtr(ctx, args[0], static_cast<uint32_t>(n));
		if (!arr) { result = 0; return true; }
		if (n > 255) n = 255;               // byte array; fits at most 256 vals
		for (int i = 0; i < n; ++i) arr[i] = static_cast<uint8_t>(i);
		for (int i = n - 1; i > 0; --i) {
			int j = static_cast<int>(dhRng()() % static_cast<uint32_t>(i + 1));
			std::swap(arr[i], arr[j]);
		}
		result = 0;
		return true;
	}

	// long2hex(number, array_ptr): write `number` as 8-char uppercase hex
	// (no trailing null; SOP always allocates a fixed-size destination).
	if (fn == "long2hex" && args.size() >= 2) {
		uint8_t *dst = staticBytePtr(ctx, args[1], 8);
		if (!dst) { result = 0; return true; }
		uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(args[0]));
		char buf[9];
		std::snprintf(buf, sizeof(buf), "%08X", v);
		std::memcpy(dst, buf, 8);
		result = 0;
		return true;
	}

	// xmsallocated(): amount of XMS available. dungeon.check_xms_memory
	// compares this against 655344 (0x9FFF0) and picks low-decoration mode
	// below the threshold. Return 2 MiB so we always land in high-decoration.
	if (fn == "xmsallocated") {
		result = static_cast<int32_t>(2 * 1024 * 1024);
		return true;
	}

	// text_background(bitmap_or_color): sets the fill used behind text boxes.
	// No visible-yet effect in our text path -- consumers (kernel, camp,
	// mouse-test) tolerate a no-op.
	// ponytail: wire when a DH text screen shows the wrong background.
	if (fn == "text_background") {
		(void)args;
		result = 0;
		return true;
	}

	// lock_resource(res_num) / unlock_resource(...): the DOS driver pinned
	// bitmaps/sounds in EMS/XMS while in use. We back everything by std::vector
	// so pin/unpin is a no-op.
	if (fn == "lock_resource" || fn == "unlock_resource") {
		(void)args;
		result = 0;
		return true;
	}

	// printer_on_line(): the automap-print check. No physical printer in a
	// modern build, so always false ("please pay attention to your Prodigy
	// modem" -- return 0 and DH shows a "no printer" dialog instead of
	// hanging on a BIOS status poll).
	// draw_auto_square(level, page, sx, sy, mx, my, &lvlvis, &lvlbit) -> byte
	//
	// One automap cell, ported from AESOP.EXE 1f36:0966. Each cell is a 9x9
	// box of LINES (not a bitmap): the outline in colour 0x66, passage stubs
	// into neighbouring cells in 0x67, and a highlight just inside an open
	// side in 0x69.
	//
	//   lvlvis[my*32+mx] & 4  -> cell not yet seen; skip the outline
	//   lvlbit[my*32+mx]      -> even bits 0/2/4/6 = passage N/E/S/W,
	//                            odd bits 1/3/5/7  = corner present
	//
	// Returns the lvlvis "unseen" bit, which the SOP stores and tests.
	if (fn == "draw_auto_square" && args.size() >= 7 && ctx.gfx) {
		// args[0] is the automap's WINDOW handle (subwindow 11 in a stock
		// session = the little parchment panel at (64,141)-(101,178)); the
		// coordinates that follow are absolute screen. AESOP passes the page
		// down to its line primitive, which clips for free -- our drawLine
		// writes straight to the screen, so clip explicitly or a cell at the
		// edge of the map spills over the HUD, exactly as the wall panels did.
		const int32_t win = static_cast<int32_t>(args[0]);
		int32_t wx0, wy0, wx1, wy1;
		const bool clip = ctx.events.windowRect(win, wx0, wy0, wx1, wy1) &&
		                  !ctx.events.windowIsOffscreen(win);
		if (clip)
			ctx.gfx->setClip(wx0, wy0, wx1 - wx0 + 1, wy1 - wy0 + 1);
		struct ClipGuard {
			GRAPHICS::Graphics *g; bool on;
			~ClipGuard() { if (on) g->clearClip(); }
		} guard{ ctx.gfx, clip };
		const int sx = static_cast<int>(args[1]);
		const int sy = static_cast<int>(args[2]);
		const int mx = static_cast<int>(args[3]) & 0x1f;
		const int my = static_cast<int>(args[4]) & 0x1f;
		const uint8_t *lvlvis = staticBytePtr(ctx, args[5], 1024);
		const uint8_t *lvlbit = staticBytePtr(ctx, args[6], 1024);
		if (!lvlvis || !lvlbit) { result = 0; return true; }
		const int at = my * 32 + mx;
		const uint8_t unseen = lvlvis[at] & 4;
		const uint8_t bits = lvlbit[at];
		auto line = [&](int x0, int y0, int x1, int y1, uint8_t c) {
			ctx.gfx->drawLine(sx + x0, sy + y0, sx + x1, sy + y1, c);
		};
		auto dot = [&](int x, int y, uint8_t c) { line(x, y, x, y, c); };
		if (unseen == 0) {
			line(2, 1, 7, 1, 0x66);           // top
			line(8, 2, 8, 7, 0x66);           // right
			line(2, 8, 7, 8, 0x66);           // bottom
			line(1, 2, 1, 7, 0x66);           // left
			if (!(bits & 0x02)) dot(8, 1, 0x66);   // corners, when not open
			if (!(bits & 0x08)) dot(8, 8, 0x66);
			if (!(bits & 0x20)) dot(1, 8, 0x66);
			if (!(bits & 0x80)) dot(1, 1, 0x66);
		}
		// Corner joins: both adjacent bits set -> carry the wall round.
		if ((bits & 0x03) == 0x03) line(8, 0, 9, 0, 0x67);
		if ((bits & 0x06) == 0x06) line(9, 0, 9, 1, 0x67);
		if ((bits & 0x0c) == 0x0c) line(9, 8, 9, 9, 0x67);
		if ((bits & 0x18) == 0x18) line(9, 9, 8, 9, 0x67);
		if ((bits & 0x30) == 0x30) line(1, 9, 0, 9, 0x67);
		if ((bits & 0x60) == 0x60) line(0, 9, 0, 8, 0x67);
		if ((bits & 0xc0) == 0xc0) line(0, 1, 0, 0, 0x67);
		if ((bits & 0x81) == 0x81) line(0, 0, 1, 0, 0x67);
		// Open sides: passage stub plus the inner highlight.
		if (bits & 0x01) { line(2, 0, 7, 0, 0x67); line(1, 1, 8, 1, 0x69); }
		if (bits & 0x04) { line(9, 2, 9, 7, 0x67); line(8, 1, 8, 8, 0x69); }
		if (bits & 0x10) { line(2, 9, 7, 9, 0x67); line(1, 8, 8, 8, 0x69); }
		if (bits & 0x40) { line(0, 2, 0, 7, 0x67); line(1, 1, 1, 8, 0x69); }
		result = unseen;
		return true;
	}

	if (fn == "printer_on_line") {
		result = 0;
		return true;
	}

	// notify(obj, msg, param): DH's 3-arg variant. EOB3's slot for notify
	// takes (obj, msg, event, param); event.cpp only claims args.size() >= 4,
	// so this 3-arg call was falling through to the generic stub. Shim it by
	// defaulting event=1 (the "timer" class used by DH's tick-driven notifies).
	if (fn == "notify" && args.size() == 3) {
		ctx.events.notify(args[0], static_cast<uint32_t>(args[1]),
		                  /*event=*/VM::Value{1}, args[2]);
		result = 0;
		return true;
	}

	// -------- 3D wall renderer (naive first pass) --------
	//
	// Real DH's `draw_walls(x, y, facing, view_mode, wallset, &lvlmap,
	// &floor_at)` iterates the party's forward cone and blits per-cell
	// wall panels. The wallset (e.g. resource 196 "Rock Wallset") is a
	// shape table with a sub-bitmap per (depth, position, wall-side)
	// tuple. First pass: blit sub 0 across the view rectangle so we can
	// SEE that pixels are being placed and iterate from there.
	// ponytail: single-blit placeholder; upgrade to per-cell dispatch
	// once we have the sub-bitmap layout mapped.
	if (fn == "draw_walls" && args.size() >= 7 && ctx.gfx) {
		// draw_walls(party_x, party_y, facing, view_window, wallset_id,
		//            &lvlmap[1024], &floor_at[18])
		//
		// Faithful port of AESOP.EXE's draw_walls (1f36:0785). Walks the 25
		// wall faces of the forward cone, looks each one's map cell up in
		// lvlmap, and blits the wallset sub-bitmap the geometry tables name
		// for that (wall type, face) at the face's fixed screen position.
		//
		// arg[3] is the view's WINDOW HANDLE (the SOP's W:view) -- the same
		// handle copy_window(W:view, W:hold) double-buffers through -- so the
		// destination origin comes from the window table, not a constant.
		// AESOP hardcodes 138/13; we prefer the registered rect and fall back
		// to those literals.
		int vx = kViewX, vy = kViewY;
		int vw = kViewW, vh = kViewH;
		{
			int32_t rx0, ry0, rx1, ry1;
			if (ctx.events.windowRect(static_cast<int32_t>(args[3]),
			                          rx0, ry0, rx1, ry1) &&
			    !ctx.events.windowIsOffscreen(static_cast<int32_t>(args[3]))) {
				vx = static_cast<int>(rx0);
				vy = static_cast<int>(ry0);
				vw = static_cast<int>(rx1 - rx0 + 1);
				vh = static_cast<int>(ry1 - ry0 + 1);
			}
		}
		const int px = static_cast<int>(args[0]);
		const int py = static_cast<int>(args[1]);
		const int facing = static_cast<int>(args[2]) & 3;
		const uint16_t wallsetId =
		    static_cast<uint16_t>(static_cast<int32_t>(args[4]));
		const uint8_t *lvlmap = staticBytePtr(ctx, args[5], 1024);
		const uint8_t *floorAt = staticBytePtr(ctx, args[6], 18);
		// THIRDEYE_DHWALL_FORCE=1 ignores the floor_at visibility gate --
		// useful diagnostic when working on init_viewspace/build_clipping.
		// Normal path uses real occlusion.
		// Sampled once: draw_walls runs every frame and the flag cannot change
		// mid-process, so this matches how the other debug switches are cached
		// (gRtTrace / gPerf are read once at boot).
		static const bool force =
		    std::getenv("THIRDEYE_DHWALL_FORCE") != nullptr;
		if (!lvlmap) { result = 0; return true; }

		// Clip every blit to the view rect. Several faces are POSITIONED
		// outside it on purpose -- e.g. face 20 sits at x=34 with a 129-wide
		// panel (34..163) while the view starts at 138 -- because only the
		// part inside the view should show. In AESOP the blit goes through the
		// view window, which clips for free; we draw straight to the screen,
		// so without this the wall spills left over the inventory column and
		// right past the arch, painting over the HUD as the party moves.
		int drawn = 0;
		ctx.gfx->setClip(vx, vy, vw, vh);
		try {
			auto &bmp = ctx.res.getAsset(wallsetId);
			for (int cell = 0; cell < kViewCells; ++cell) {
				// Map cell for this face. AESOP masks to 0x1f, i.e. the 32x32
				// grid wraps rather than clamping.
				int mx = (px + kCellDX[facing][cell]) & 0x1f;
				int my = (py + kCellDY[facing][cell]) & 0x1f;
				int8_t wall = static_cast<int8_t>(lvlmap[my * 32 + mx]);
				if (wall < 0) continue;            // 0xFF == no wall
				if (wall > 1) continue;            // only two wall types exist
				if (!force && floorAt &&
				    floorAt[kCellVis[cell]] == 0) continue;

				int sub = kCellPanel[wall][cell];
				int mirror = kCellMirror[cell];
				// Three faces reuse a sibling panel mirrored rather than
				// carrying their own art.
				if (sub == 14)      { sub = 13; mirror ^= 1; }
				else if (sub == 15) { sub = 8;  mirror ^= 1; }
				else if (sub == 17) { sub = 16; mirror ^= 1; }

				ctx.gfx->drawImage(bmp, static_cast<uint16_t>(sub),
				                   vx + kCellX[cell], vy + kCellY[cell],
				                   /*transparency=*/true, mirror,
				                   static_cast<uint32_t>(wallsetId), 0);
				++drawn;
			}
		} catch (const std::exception &e) {
			rt() << "  [draw_walls failed: " << e.what() << "]" << std::endl;
		}
		ctx.gfx->clearClip();   // must happen on the throw path too
		rt() << "  [draw_walls @(" << px << "," << py << ") f" << facing
		     << " wallset " << wallsetId << " -> " << drawn << " faces]"
		     << std::endl;
		result = 0;
		return true;
	}

	// init_viewspace(party_x, party_y, facing, &view_X[18], &view_Y[18]):
	// fill the SOP's 18 view cells with their map coordinates. Port of
	// AESOP.EXE 1f36:040f -- three per-facing unit vectors (forward/left/right)
	// laid out as rows of 7 / 5 / 3 / 3 cells receding to the party's own row.
	// The SOP reads these back to work out notblocks/floor_at per cell, so a
	// no-op here left all downstream visibility wrong.
	if (fn == "init_viewspace" && args.size() >= 5) {
		const int px = static_cast<int>(args[0]);
		const int py = static_cast<int>(args[1]);
		const int f = static_cast<int>(args[2]) & 3;
		uint8_t *vX = staticBytePtr(ctx, args[3], kViewSlots);
		uint8_t *vY = staticBytePtr(ctx, args[4], kViewSlots);
		if (!vX || !vY) { result = 0; return true; }
		// Mask each stored coordinate to 0..31 on the way out. AESOP's
		// build_clipping (1f36:05f4) masks with 0x1f, but the SOP's OWN
		// bytecode loop between init_viewspace and build_clipping reads
		// view_X/view_Y via LSBA (sign-extended!) to compute a lvlmap index.
		// If we leave -3 in the array as byte 0xFD, LSBA returns -3, the SOP
		// computes lvlmap[-3*32 + view_X] = 96 bytes BEFORE lvlmap starts --
		// which reads into lvlbit -- and every off-map cell ends up looking
		// like an occupied wall (notblocks stays 0). Masking here matches the
		// SOP's expected 32x32 wrap-around semantics.
		auto wrap = [](int v) {
			return static_cast<uint8_t>(v & 0x1f);
		};
		auto fill = [&wrap](uint8_t *d, int base, int fwd, int left, int right) {
			int b = base + fwd * 3;                       // depth 3: 7 cells
			d[0] = wrap(b + left * 3);
			d[1] = wrap(b + left * 2);
			d[2] = wrap(b + left);
			d[3] = wrap(b);
			d[4] = wrap(b + right);
			d[5] = wrap(b + right * 2);
			d[6] = wrap(b + right * 3);
			b = base + fwd * 2;                           // depth 2: 5 cells
			d[7]  = wrap(b + left * 2);
			d[8]  = wrap(b + left);
			d[9]  = wrap(b);
			d[10] = wrap(b + right);
			d[11] = wrap(b + right * 2);
			b = base + fwd;                               // depth 1: 3 cells
			d[12] = wrap(b + left);
			d[13] = wrap(b);
			d[14] = wrap(b + right);
			d[15] = wrap(base + left);                    // depth 0: 3 cells
			d[16] = wrap(base);
			d[17] = wrap(base + right);
		};
		fill(vX, px, kFwdX[f], kLeftX[f], kRightX[f]);
		fill(vY, py, kFwdY[f], kLeftY[f], kRightY[f]);
		result = 0;
		return true;
	}

	// build_clipping(&l_clip[18], &r_clip[18], &visible[18], &view_X[18],
	//                &view_Y[18], &notblocks[18], &lvlvis[1024], &floor_at[18])
	//
	// Port of AESOP.EXE 1f36:05f4. Per cell: start fully open (l=0, r=175),
	// then let every non-blocking cell contribute a clip edge from the
	// occlusion table; if the window closes (l > r) or a hard-stop entry is
	// hit, the cell is not visible. Visible cells get marked seen in lvlvis.
	// Finally floor_at = visible && notblocks, which is the gate draw_walls
	// tests -- so this is what makes occlusion real instead of drawing every
	// face in the cone.
	if (fn == "build_clipping" && args.size() >= 8) {
		uint8_t *lc  = staticBytePtr(ctx, args[0], kViewSlots * 2);  // words
		uint8_t *rc  = staticBytePtr(ctx, args[1], kViewSlots * 2);  // words
		uint8_t *vis = staticBytePtr(ctx, args[2], kViewSlots);
		uint8_t *vX  = staticBytePtr(ctx, args[3], kViewSlots);
		uint8_t *vY  = staticBytePtr(ctx, args[4], kViewSlots);
		uint8_t *nb  = staticBytePtr(ctx, args[5], kViewSlots);
		uint8_t *lvis = staticBytePtr(ctx, args[6], 1024);
		uint8_t *fa  = staticBytePtr(ctx, args[7], kViewSlots);
		if (!lc || !rc || !vis || !vX || !vY || !nb || !fa) {
			result = 0;
			return true;
		}
		auto setW = [](uint8_t *p, int i, int v) {
			p[i * 2] = static_cast<uint8_t>(v & 0xff);
			p[i * 2 + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
		};
		auto getW = [](const uint8_t *p, int i) {
			return static_cast<int16_t>(p[i * 2] | (p[i * 2 + 1] << 8));
		};
		for (int i = 0; i < kViewSlots; ++i) {
			setW(lc, i, 0);
			setW(rc, i, 175);          // 0xaf -- view is 176px wide
			vis[i] = 0;
			vX[i] &= 0x1f;             // 32x32 map wraps
			vY[i] &= 0x1f;
			if (kClip[i][0x10] == 0x7e) continue;   // cell outside the cone
			vis[i] = 1;
			for (int j = 0; j < kViewSlots; ++j) {
				if (static_cast<int8_t>(nb[j]) >= 2) continue;
				uint8_t raw = kClip[i][j];
				if (raw == 0x7f) continue;          // no contribution
				if (raw != 0x7e) {
					int v = static_cast<int8_t>(raw);
					if (v < 0 && getW(lc, i) < v * -8) setW(lc, i, v * -8);
					if (v > 0 && getW(rc, i) > v * 8 - 1) setW(rc, i, v * 8 - 1);
					if (getW(lc, i) <= getW(rc, i)) continue;  // still open
				}
				vis[i] = 0;            // hard stop, or window closed
				break;
			}
			if (vis[i] && lvis) {
				int at = vY[i] * 32 + vX[i];
				if (at >= 0 && at < 1024) {
					lvis[at] = static_cast<uint8_t>((lvis[at] | 1) & ~4);
				}
			}
		}
		for (int i = 0; i < kViewSlots; ++i)
			fa[i] = (vis[i] && nb[i]) ? 1 : 0;
		result = 0;
		return true;
	}
	// copy_window(src_page, dst_page): composite an offscreen page onto its
	// destination. This is how DH assembles the screen -- each HUD panel and
	// the dungeon view are drawn into their own page at page-local (0,0), then
	// copied to the screen rect the SOP registered for the destination. E.g.
	// `copy_window(16, 9)` puts the floor art into handle 9 = (138,13)-(313,132),
	// the dungeon view. Without this every panel piles up at screen (0,0).
	if (fn == "copy_window" && args.size() >= 2 && ctx.gfx) {
		int32_t src = static_cast<int32_t>(args[0]);
		int32_t dst = static_cast<int32_t>(args[1]);
		// Destination origin: an offscreen destination is addressed in its own
		// page-local coords (so 0,0), a screen rect at its registered corner.
		int dx = 0, dy = 0;
		int32_t rx0, ry0, rx1, ry1;
		if (!ctx.events.windowIsOffscreen(dst) &&
		    ctx.events.windowRect(dst, rx0, ry0, rx1, ry1)) {
			dx = static_cast<int>(rx0);
			dy = static_cast<int>(ry0);
		}
		bool ok = ctx.gfx->blitPage(src, dst, dx, dy);
		rt() << "  [copy_window " << src << " -> " << dst << " @ " << dx << ","
		     << dy << (ok ? "]" : " MISS]") << std::endl;
		result = 0;
		return true;
	}

	// Transition(): screen wipe/fade effect. Skipping keeps timings tight;
	// re-enable when we have per-transition presentation to show off.
	if (fn == "Transition") {
		(void)args;
		result = 0;
		return true;
	}

	// -------- generic sequential file I/O --------
	//
	// DH's open_file / close_file / read_number_from_file / read_array_from_file
	// operate on an implicit "current file" (no handle arg). One buffered file
	// at a time is all the SOP asks for.

	if (fn == "open_file" && args.size() >= 1) {
		std::string name = ctx.vm.readCodeString(
		    static_cast<uint32_t>(args[0]));
		DHFile &f = currentFile();
		f.reset();
		auto path = resolveDosPath(ctx, name);
		std::ifstream in(path, std::ios::binary);
		if (in) {
			f.buf.assign(std::istreambuf_iterator<char>(in),
			             std::istreambuf_iterator<char>());
		}
		f.name = name;
		f.open = true;
		rt() << "  [open_file \"" << name << "\" -> " << path.string()
		     << " (" << f.buf.size() << " bytes"
		     << (f.buf.empty() ? ", MISSING" : "") << ")]" << std::endl;
		result = f.buf.empty() ? 0 : static_cast<int32_t>(f.buf.size());
		return true;
	}

	if (fn == "close_file") {
		currentFile().reset();
		result = 0;
		return true;
	}

	// read_number_from_file(size): pulls a 1/2/4-byte little-endian integer.
	// Short reads return 0 (zero-padded).
	if (fn == "read_number_from_file" && args.size() >= 1) {
		int sz = static_cast<int>(args[0]);
		if (sz <= 0 || sz > 4) { result = 0; return true; }
		uint8_t bytes[4] = {0};
		readInto(currentFile(), static_cast<std::size_t>(sz), bytes);
		int32_t v = 0;
		for (int i = 0; i < sz; ++i)
			v |= static_cast<int32_t>(bytes[i]) << (8 * i);
		result = v;
		return true;
	}

	if (fn == "read_array_from_file" && args.size() >= 2) {
		int len = static_cast<int>(args[1]);
		if (len <= 0) { result = 0; return true; }
		uint8_t *dst = staticBytePtr(ctx, args[0],
		                             static_cast<uint32_t>(len));
		if (!dst) { result = 0; return true; }
		readInto(currentFile(), static_cast<std::size_t>(len), dst);
		result = 0;
		return true;
	}

	// -------- dungeon-load helpers --------
	//
	// These wrap the same file API but with fixed filenames + per-level offset
	// arithmetic. Format per docs/dungeon_hack_maze.md.

	// load_level_map(&buf, party_lvl): reads chunk `party_lvl` from
	// savegame/LEVELS.DAT (4-byte header + (DEPTH+10)×0x400 chunks). Falls
	// back to zeros when MAZE hasn't run yet.
	// ponytail: 0x400 buffer only; upgrade when we know SOP expects more.
	if (fn == "load_level_map" && args.size() >= 2) {
		int lvl = static_cast<int>(args[1]);
		uint8_t *dst = staticBytePtr(ctx, args[0], 0x400);
		if (!dst) { result = 0; return true; }
		loadChunk(ctx, "LEVELS.DAT", lvl, /*header=*/4, /*chunk=*/0x400, dst);
		result = 0x400;
		return true;
	}

	// load_visibility(&buf, party_lvl): shipped install has a real
	// VISIBLE.DAT (25 KB). Chunk layout is speculative; using 0x400 to match
	// LEVELS.DAT until confirmed.
	if (fn == "load_visibility" && args.size() >= 2) {
		int lvl = static_cast<int>(args[1]);
		uint8_t *dst = staticBytePtr(ctx, args[0], 0x400);
		if (!dst) { result = 0; return true; }
		loadChunk(ctx, "VISIBLE.DAT", lvl, /*header=*/0, /*chunk=*/0x400, dst);
		result = 0;
		return true;
	}

	// open_feature_file(level): opens savegame/FEA%02d.DAT and points the
	// current file at it, ready for sequential get_feature_record calls.
	if (fn == "open_feature_file" && args.size() >= 1) {
		int lvl = static_cast<int>(args[0]);
		char fname[16];
		std::snprintf(fname, sizeof(fname), "FEA%02d.DAT", lvl);
		DHFile &f = currentFile();
		f.reset();
		auto path = savegamePath(ctx, fname);
		std::ifstream in(path, std::ios::binary);
		if (in) {
			f.buf.assign(std::istreambuf_iterator<char>(in),
			             std::istreambuf_iterator<char>());
		}
		f.name = fname;
		f.open = true;
		result = f.buf.empty() ? 0 : static_cast<int32_t>(f.buf.size());
		return true;
	}

	// get_feature_record(&fea_in): reads one 8-byte record.
	if (fn == "get_feature_record" && args.size() >= 1) {
		uint8_t *dst = staticBytePtr(ctx, args[0], 8);
		if (!dst) { result = 0; return true; }
		readInto(currentFile(), 8, dst);
		result = 0;
		return true;
	}

	if (fn == "close_feature_file") {
		currentFile().reset();
		result = 0;
		return true;
	}

	return false;
}

// Native mini-MAZE.EXE stand-in. Real MAZE writes per-cell entropy tables
// (see docs/dungeon_hack_maze.md); until we ship a full port or the user
// runs MAZE under DOSBox, this writes *structurally valid empty* files
// so phase-two consumes zero-content dungeons instead of tripping on
// missing files. Idempotent -- only writes what doesn't already exist,
// so a real MAZE run's output is preserved.
//
// File formats (matching the RE'd MAZE writers):
//   LEVELS.DAT   = 4-byte header + (DEPTH+10) x 0x400 chunks
//   FEA%02d.DAT  = 8-byte header record + 8-byte all-zero terminator
//   ITEMS.DAT    = 8-byte all-zero terminator (empty item stream)
//
// DEPTH is read from savegame/SETTINGS.DAT byte 4; falls back to 15 (the
// shipped-settings value) if the file is missing/short.
// Carve a perfect maze into one 32x32 level chunk: 0xFF = open, 0x00 = wall
// (the encoding load_level_map/draw_walls consume -- lvlmap == -1 means "no
// wall here"). Iterative recursive-backtracker.
//
// Cells sit on EVEN coordinates (0,2,..,30 -> 16x16 cells) with the shared
// wall between two cells on the odd coordinate between them. That choice is
// deliberate: it puts a cell at (0,0), which is where the SOP starts the party
// until FEA feature records supply a real entry point -- an odd-coordinate
// scheme would seal the party in rock on turn one. Row/column 31 is odd and
// stays solid, giving a border for free.
//
// ponytail: a plain perfect maze -- no rooms, loops or features. This is not
// MAZE.EXE's algorithm and will not reproduce DH's dungeons; it exists so the
// engine has something walkable. Swap it for a real MAZE port when authentic
// layouts matter (docs/dungeon_hack_maze.md).
void carveMaze(uint8_t *grid, uint32_t seed) {
	constexpr int kW = 32;      // grid is 32x32 bytes
	constexpr int kCells = 16;  // cells on even coords 0,2,...,30
	std::memset(grid, 0x00, static_cast<size_t>(kW) * kW);   // solid rock
	std::mt19937 rng(seed);
	std::vector<uint8_t> seen(static_cast<size_t>(kCells) * kCells, 0);
	std::vector<std::pair<int, int>> stack;
	stack.emplace_back(0, 0);
	seen[0] = 1;
	grid[0] = 0xFF;                                          // party start
	static constexpr int kDir[4][2] = { {0,-1}, {1,0}, {0,1}, {-1,0} };
	while (!stack.empty()) {
		const int cx = stack.back().first, cy = stack.back().second;
		int avail[4], n = 0;
		for (int d = 0; d < 4; ++d) {
			const int nx = cx + kDir[d][0], ny = cy + kDir[d][1];
			if (nx < 0 || ny < 0 || nx >= kCells || ny >= kCells) continue;
			if (seen[static_cast<size_t>(ny) * kCells + nx]) continue;
			avail[n++] = d;
		}
		if (n == 0) { stack.pop_back(); continue; }
		const int d = avail[rng() % static_cast<uint32_t>(n)];
		const int nx = cx + kDir[d][0], ny = cy + kDir[d][1];
		// Knock out the shared wall, then open the neighbour cell.
		grid[(2 * cy + kDir[d][1]) * kW + (2 * cx + kDir[d][0])] = 0xFF;
		grid[(2 * ny) * kW + (2 * nx)] = 0xFF;
		seen[static_cast<size_t>(ny) * kCells + nx] = 1;
		stack.emplace_back(nx, ny);
	}
}

void ensureSavegameFiles(const std::filesystem::path &dhRoot) {
	namespace fs = std::filesystem;
	std::error_code ec;
	auto sg = resolveChildCI(dhRoot, "savegame");
	fs::create_directories(sg, ec);   // no-op if it exists

	// SETTINGS.DAT layout (docs/dungeon_hack_maze.md): u32 SEED at offset 0,
	// then the 12-byte settings struct whose first byte is DEPTH. It ships
	// with the game so it is normally present; fall back to the shipped
	// values if not. Feeding the game's own seed into our generator means the
	// same settings reproduce the same dungeon run-to-run -- not DH's dungeon,
	// but at least a stable one.
	uint32_t seed = 0x000156e0;   // shipped SETTINGS.DAT seed
	int depth = 15;               // shipped DEPTH
	{
		auto settings = resolveChildCI(sg, "SETTINGS.DAT");
		std::ifstream in(settings, std::ios::binary);
		if (in) {
			uint8_t buf[16] = {0};
			in.read(reinterpret_cast<char *>(buf), sizeof(buf));
			if (in.gcount() >= 5) {
				seed = static_cast<uint32_t>(buf[0]) |
				       (static_cast<uint32_t>(buf[1]) << 8) |
				       (static_cast<uint32_t>(buf[2]) << 16) |
				       (static_cast<uint32_t>(buf[3]) << 24);
				depth = buf[4];
			}
		}
	}
	const int levels = depth + 10;

	// Resolve the name case-insensitively BEFORE testing existence: reads go
	// through savegamePath()/resolveChildCI, so on a case-sensitive filesystem
	// a real MAZE (or DOSBox) run that produced `levels.dat` would not be seen
	// by a fixed-case `LEVELS.DAT` check. We would then drop an empty
	// uppercase stub next to it and the SOP could load whichever the directory
	// iterator yields first -- silently serving an empty dungeon over the real
	// one. Checking and writing the same resolved path keeps the "real MAZE
	// output is preserved" guarantee honest.
	auto writeIfMissing = [&](const std::string &name,
	                          const std::vector<uint8_t> &bytes) {
		auto p = resolveChildCI(sg, name);
		if (fs::exists(p, ec)) return;
		std::ofstream out(p, std::ios::binary);
		if (out) out.write(reinterpret_cast<const char *>(bytes.data()),
		                   static_cast<std::streamsize>(bytes.size()));
	};

	// LEVELS.DAT: 4-byte header + N x 0x400 chunks, one carved maze per level
	// (each level gets seed+level so they differ but stay reproducible).
	// Kept around afterwards so the FEA writer can place stairs on cells that
	// are actually open.
	std::vector<uint8_t> levelData(4 + static_cast<size_t>(levels) * 0x400, 0);
	for (int l = 0; l < levels; ++l)
		carveMaze(levelData.data() + 4 + static_cast<size_t>(l) * 0x400,
		          seed + static_cast<uint32_t>(l));
	writeIfMissing("LEVELS.DAT", levelData);

	// FEA00..FEA{levels-1}.DAT -- feature records for each level:
	//
	//     [8-byte header]  [8-byte body record] x N  [8 zero bytes]
	//
	// The SOP reads the header with its own get_feature_record call, then
	// loops reading body records and switching on byte 0 (dungeon's CASE
	// table has 31 entries: 0 ends the loop, 1..30 are the feature types in
	// the order MAZE's string table lists them -- 4 = stairs up,
	// 5 = stairs down). For a placement feature byte 1/2 are the cell x/y.
	//
	// Stairs forward bytes 4..7 to `create teleporter` (message 495) on class
	// 2870 "current stairs down", and those four line up with the
	// `teleporters` object's dest_x / dest_y / dest_lvl / dest_fdir externs.
	//
	// ponytail: stairs only -- no doors, buttons, items or monsters. Enough to
	// descend; the other 28 feature types need their SOP cases read first.
	{
		// [0]=type, [1]=x, [2]=y, [3] unused by the cases we emit, [4..7]
		// are type-specific (stairs: dest x/y/lvl/fdir; creature: [4] is the
		// monster slot).
		auto feaRecord = [](uint8_t type, int x, int y,
		                    int b4, int b5, int b6, int b7) {
			return std::array<uint8_t, 8>{
				type, static_cast<uint8_t>(x), static_cast<uint8_t>(y), 0,
				static_cast<uint8_t>(b4), static_cast<uint8_t>(b5),
				static_cast<uint8_t>(b6), static_cast<uint8_t>(b7) };
		};
		for (int i = 0; i < levels; ++i) {
			std::vector<uint8_t> data(8, 0);        // header
			// Put the down-stairs on the open cell furthest from the party
			// start, so reaching it means actually walking the maze.
			const uint8_t *grid = levelData.data() + 4 +
			                      static_cast<size_t>(i) * 0x400;
			int bx = -1, by = -1, best = -1;
			for (int cy = 0; cy < 32; cy += 2)
				for (int cx = 0; cx < 32; cx += 2)
					if (grid[cy * 32 + cx] == 0xFF && cx + cy > best) {
						best = cx + cy; bx = cx; by = cy;
					}
			if (bx >= 0 && i + 1 < levels) {
				auto r = feaRecord(5, bx, by, 0, 0, i + 1, 0);
				data.insert(data.end(), r.begin(), r.end());
			}
			// Type 1 = "current door" -> `create door` (msg 493); bytes 6/7
			// form a 16-bit link id (the `doors` object's W:button_num /
			// W:lock_num), left 0 here for a plain unlinked door. Type 6 =
			// "regular button" -> `create thing` (msg 496, class 2894); types
			// 2 and 7 are the same shape with classes 2813 / 2897.
			// Doors go on corridor cells -- a cell open exactly along one
			// axis -- so they sit in a passage rather than floating in a
			// junction.
			{
				int doors = 0;
				std::mt19937 drng(seed + 0x5eedu + static_cast<uint32_t>(i));
				auto isOpen = [&](int x, int y) {
					return x >= 0 && y >= 0 && x < 32 && y < 32 &&
					       grid[y * 32 + x] == 0xFF;
				};
				for (int cy = 0; cy < 32 && doors < 4; cy += 2)
					for (int cx = 0; cx < 32 && doors < 4; cx += 2) {
						if (!isOpen(cx, cy) || cx + cy < 8) continue;
						const bool ns = isOpen(cx, cy - 1) && isOpen(cx, cy + 1);
						const bool ew = isOpen(cx - 1, cy) && isOpen(cx + 1, cy);
						if (ns == ew) continue;          // junction or dead end
						if (drng() % 10 != 0) continue;  // sparse
						auto d = feaRecord(1, cx, cy, 0, 0, 0, 0);
						data.insert(data.end(), d.begin(), d.end());
						auto b = feaRecord(6, cx, cy, 0, 0, 0, 0);
						data.insert(data.end(), b.begin(), b.end());
						++doors;
					}
			}
			// Type 12 = "level creature": the SOP sends `create monster`
			// (msg 494) with the class taken from mon_types[lvl*12 + fea[4]*4],
			// i.e. fea[4] picks one of the level's three monster slots
			// (dungeon's W:monster_nums[.., 3]); slot 2 also gets
			// `make boss monster` (msg 233). Sprinkle a few on open cells
			// away from the start so there is something down there.
			{
				int placed = 0;
				std::mt19937 mrng(seed + 0x9e37u + static_cast<uint32_t>(i));
				for (int cy = 0; cy < 32 && placed < 3; cy += 2)
					for (int cx = 0; cx < 32 && placed < 3; cx += 2) {
						if (grid[cy * 32 + cx] != 0xFF) continue;
						if (cx + cy < 12) continue;         // not on the doorstep
						if (mrng() % 24 != 0) continue;     // sparse
						auto m = feaRecord(12, cx, cy,
						                   static_cast<int>(mrng() % 3),
						                   0, 0, 0);
						data.insert(data.end(), m.begin(), m.end());
						++placed;
					}
			}
			data.insert(data.end(), 8, 0);          // terminator
			char name[16];
			std::snprintf(name, sizeof(name), "FEA%02d.DAT", i);
			writeIfMissing(name, data);
		}
	}

	// ITEMS.DAT: just the terminator (8 zero bytes = empty stream).
	{
		std::vector<uint8_t> data(8, 0);
		writeIfMissing("ITEMS.DAT", data);
	}
}

} // namespace THIRDEYE::runtime::dh
