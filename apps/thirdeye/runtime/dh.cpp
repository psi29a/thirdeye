#include "internal.hpp"

#include "../graphics/graphics.hpp"
#include "../resources/res.hpp"
#include "../vm/events.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
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
		uint8_t *tbl = staticBytePtr(ctx, args[0], 256);
		if (!tbl) { result = 0; return true; }
		int sum = 0;
		int n = 0;
		while (n < 256 && tbl[n] != 0xFF) { sum += tbl[n]; ++n; }
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
void ensureSavegameFiles(const std::filesystem::path &dhRoot) {
	namespace fs = std::filesystem;
	std::error_code ec;
	auto sg = resolveChildCI(dhRoot, "savegame");
	fs::create_directories(sg, ec);   // no-op if it exists

	// Read DEPTH from SETTINGS.DAT (offset 4). SETTINGS.DAT ships with the
	// game so it's normally there; default to 15 if not.
	int depth = 15;
	{
		auto settings = resolveChildCI(sg, "SETTINGS.DAT");
		std::ifstream in(settings, std::ios::binary);
		if (in) {
			char buf[16] = {0};
			in.read(buf, sizeof(buf));
			if (in.gcount() > 4) depth = static_cast<uint8_t>(buf[4]);
		}
	}
	const int levels = depth + 10;

	auto writeIfMissing = [&](const fs::path &p,
	                          const std::vector<uint8_t> &bytes) {
		if (fs::exists(p, ec)) return;
		std::ofstream out(p, std::ios::binary);
		if (out) out.write(reinterpret_cast<const char *>(bytes.data()),
		                   static_cast<std::streamsize>(bytes.size()));
	};

	// LEVELS.DAT: 4-byte header + N x 0x400 zero chunks.
	{
		std::vector<uint8_t> data(4 + static_cast<size_t>(levels) * 0x400, 0);
		writeIfMissing(sg / "LEVELS.DAT", data);
	}

	// FEA00..FEA{levels-1}.DAT: 8-byte header + 8-byte terminator = 16 bytes
	// of zeros. The 22-case type switch in the SOP reader still runs on the
	// header's type byte (0 = level-header record); the terminator is what
	// stops the inner get_feature_record loop.
	{
		std::vector<uint8_t> data(16, 0);
		for (int i = 0; i < levels; ++i) {
			char name[16];
			std::snprintf(name, sizeof(name), "FEA%02d.DAT", i);
			writeIfMissing(sg / name, data);
		}
	}

	// ITEMS.DAT: just the terminator (8 zero bytes = empty stream).
	{
		std::vector<uint8_t> data(8, 0);
		writeIfMissing(sg / "ITEMS.DAT", data);
	}
}

} // namespace THIRDEYE::runtime::dh
