#include "internal.hpp"

#include <chrono>
#include <random>

namespace THIRDEYE::runtime::rtcode {

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
	if (fn == "rnd" && args.size() >= 1) {
		static std::mt19937 rng(0x9e3779b9u);
		int n = static_cast<int>(args[0]);
		result = n > 0 ? static_cast<int>(rng() % static_cast<uint32_t>(n)) : 0;
		return true;
	}
	return false;
}

} // namespace THIRDEYE::runtime::rtcode
