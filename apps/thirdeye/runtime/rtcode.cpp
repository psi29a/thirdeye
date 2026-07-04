#include "internal.hpp"

#include "../resources/res.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>

namespace THIRDEYE::runtime::rtcode {

namespace {

// RTSYSTEM.C ascnum: numeric value of an ASCII string. Supports leading
// whitespace, +/-, 'c char literals, 0x hex and 0b binary prefixes; returns
// -1 if the string has no parseable value.
int32_t ascnum(const std::string &s) {
	size_t i = 0;
	while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
	bool neg = false;
	if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
		neg = s[i] == '-';
		++i;
	}
	if (i < s.size() && s[i] == '\'')
		return i + 1 < s.size() ? static_cast<int32_t>(s[i + 1]) : -1;
	int base = 10;
	if (s.compare(i, 2, "0x") == 0) { base = 16; i += 2; }
	else if (s.compare(i, 2, "0b") == 0) { base = 2; i += 2; }
	const char *begin = s.c_str() + i;
	char *end = nullptr;
	long v = std::strtol(begin, &end, base);
	if (end == begin) return -1;
	return static_cast<int32_t>(neg ? -v : v);
}

} // namespace

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
	// peekmem/pokemem model the original's raw memory cells. The boot object's
	// MSG_CREATE reads peekmem(1264) -- a 4-char "mode" -- and CASEs on it to
	// decide what to do (INTR -> title menu, CINE -> straight to game, ...). We
	// back them with a real map so that the boot state machine works and we can
	// seed the mode (see bootObject). Quiet: called in tight spots.
	if (fn == "peekmem" && args.size() >= 1) {
		auto it = ctx.mem.find(args[0]);
		result = it == ctx.mem.end() ? 0 : it->second;
		return true;
	}
	if (fn == "pokemem" && args.size() >= 2) {
		ctx.mem[args[0]] = args[1];
		result = 0;
		return true;
	}
	// absv: absolute value (RTCODE.C). The menu colours each option
	// 136 + absv(option - selected), so a stub (->0) flattens the highlight.
	if (fn == "absv" && args.size() >= 1) {
		result = args[0] < 0 ? -args[0] : args[0];
		return true;
	}
	// minv/maxv: min/max of two values (RTCODE.C math helpers). Stubbed -> 0 they
	// flatten clamped values (HP/AC/colour computations in the HUD).
	if (fn == "minv" && args.size() >= 2) {
		result = args[0] < args[1] ? args[0] : args[1];
		return true;
	}
	if (fn == "maxv" && args.size() >= 2) {
		result = args[0] > args[1] ? args[0] : args[1];
		return true;
	}
	// dice(count, sides, bonus): roll `count` d`sides` + bonus (RTCODE.C). Used
	// pervasively -- to-hit (1d20), damage, monster bounce/AI. Stubbed -> 0 it made
	// every combat roll a 0, so attacks ALWAYS missed (the to-hit `roll >= target`
	// could never pass). rnd(n): 0..n-1. Backed by a shared PRNG.
	if (fn == "dice" && args.size() >= 2) {
		static std::mt19937 rng(
		    static_cast<uint32_t>(std::chrono::steady_clock::now()
		                              .time_since_epoch().count()));
		int count = static_cast<int>(args[0]);
		int sides = static_cast<int>(args[1]);
		int bonus = args.size() > 2 ? static_cast<int>(args[2]) : 0;
		int sum = bonus;
		for (int i = 0; i < count && sides > 0; ++i)
			sum += 1 + static_cast<int>(rng() % static_cast<uint32_t>(sides));
		result = sum;
		return true;
	}
	// rnd(low, high): INCLUSIVE range -- RTCODE.C `low + rand() % (high-low+1)`.
	// This was misported as a one-arg modulo, so rnd(1,20) = rng()%1 = 0
	// forever: no to-hit roll ever landed, monster AI direction picks were
	// always 0, and the NPCstat report(1) seeding in loadLevelObjects was
	// added to paper over the misses -- which marked every monster petrified
	// (NPCstat bit 0x40) and produced one-hit kills + the "statue crumbles to
	// dust" message on every melee death. One wrong opcode-level semantic,
	// three gameplay bugs (see CLAUDE.md: the bug is always in our runtime).
	if (fn == "rnd" && args.size() >= 2) {
		static std::mt19937 rng(0x9e3779b9u);
		int lo = static_cast<int>(args[0]);
		int hi = static_cast<int>(args[1]);
		if (hi < lo) std::swap(lo, hi);
		result = lo + static_cast<int>(
		                  rng() % static_cast<uint32_t>(hi - lo + 1));
		return true;
	}
	// --- string helpers (RTCODE.C) ---
	if (fn == "string_len" && args.size() >= 1) {
		result = static_cast<VM::Value>(ctx.vm.readString(args[0]).size());
		return true;
	}
	// strval(string) / envval(name): numeric value of a string / of a DOS
	// environment variable, -1 if absent or unparseable.
	if (fn == "strval" && args.size() >= 1) {
		result = ascnum(ctx.vm.readString(args[0]));
		return true;
	}
	if (fn == "envval" && args.size() >= 1) {
		const char *env = std::getenv(ctx.vm.readString(args[0]).c_str());
		result = env ? ascnum(env) : -1;
		return true;
	}
	// copy_string(src, dest): strcpy into a SOP static array (e.g. the typed
	// save-slot name into the buffer savegame_title returned).
	if (fn == "copy_string" && args.size() >= 2) {
		std::string src = ctx.vm.readString(args[0]);
		uint8_t *dest = staticBytePtr(ctx, args[1],
		                              static_cast<uint32_t>(src.size()) + 1);
		if (dest)
			std::memcpy(dest, src.c_str(), src.size() + 1);
		result = 0;
		return true;
	}
	// load_string(array, string_res): copy an "S:"-prefixed string resource's
	// text (sans prefix) into a SOP static array.
	if (fn == "load_string" && args.size() >= 2) {
		result = 0;
		try {
			std::vector<uint8_t> &s =
			    ctx.res.getAsset(static_cast<uint16_t>(args[1]));
			if (s.size() >= 2 && s[0] == 'S' && s[1] == ':') {
				uint32_t n = static_cast<uint32_t>(s.size()) - 2;
				if (uint8_t *dest = staticBytePtr(ctx, args[0], n))
					std::memcpy(dest, s.data() + 2, n);
			}
		} catch (const std::exception &) {}
		return true;
	}
	// beep: PC-speaker chirp -- no-op (sound work deferred).
	// diagnose(dtype, parm): original debug/heap diagnostics -- no-op.
	if (fn == "beep" || fn == "diagnose") {
		result = 0;
		return true;
	}
	return false;
}

} // namespace THIRDEYE::runtime::rtcode
