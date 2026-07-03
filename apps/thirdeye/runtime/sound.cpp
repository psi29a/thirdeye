#include "internal.hpp"

#include "../resources/res.hpp"
#include "../sound/sound.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

// SOUND32.C -- the EOB3 digital-sound runtime. The bytecode loads a bank of PCM
// clips with load_sound_block, then plays them by index with sound_effect.
// We back both with the OpenAL Mixer (raw 8-bit mono @ 8 kHz, same format the
// intro keypress SFX already uses via Mixer::playSound).

namespace THIRDEYE::runtime::sound {

namespace {
// The loaded sound bank: clip index -> raw PCM bytes. SOUND.H bank layout is
// COMMON indices 0..49, LEVEL indices 50..63; load_sound_block(first_block,...)
// picks the base from BLK_COMMON(0)/else. We key by the same index the SOP
// passes to sound_effect, so the two agree without modelling EMS blocks.
std::map<int, std::vector<uint8_t>> gBank;
bool gSoundOn = true;
} // namespace

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
	// set_sound_status(on): master enable/disable (the SOP toggles it around
	// cutscenes and the options screen). We honour it as a play gate.
	if (fn == "set_sound_status") {
		if (!args.empty())
			gSoundOn = args[0] != 0;
		result = 0;
		return true;
	}
	// load_sound_block(first_block, last_block, array): `array` is a Code-space
	// table of resource numbers, NUL-terminated; each resource is a raw PCM
	// clip. Index base is 0 for the COMMON bank (first_block == BLK_COMMON == 0),
	// else FIRST_LEVEL(50). We load each clip's bytes into gBank at that index.
	if (fn == "load_sound_block" && args.size() >= 3) {
		constexpr int kBlkCommon = 0, kFirstLevel = 50;
		int firstBlock = static_cast<int>(args[0]);
		int idx = (firstBlock == kBlkCommon) ? 0 : kFirstLevel;
		// Clear the range we're about to (re)load so a level change doesn't leave
		// the previous level's clips shadowing the new ones.
		for (auto it = gBank.begin(); it != gBank.end();) {
			if (it->first >= idx) it = gBank.erase(it);
			else ++it;
		}
		int loaded = 0;
		for (uint32_t i = 0;; ++i) {
			// The array holds 32-bit resource numbers (SOUND32.C `ULONG *array`):
			// element i is at word offset i*2; the low word is the number (they
			// fit in 16 bits and are nonzero, so a zero low word = terminator).
			int32_t res = ctx.vm.codeWord(args[2], i * 2);
			if (res <= 0)
				break; // NUL terminator (or unreadable table)
			try {
				gBank[idx++] = ctx.res.getAsset(static_cast<uint16_t>(res));
				++loaded;
			} catch (const std::exception &) {
				++idx; // keep index alignment even if one clip is missing
			}
		}
		if (std::getenv("THIRDEYE_SNDTRACE"))
			std::cerr << "[snd] load_sound_block(" << firstBlock << ".."
			          << args[1] << ") loaded " << loaded << " clips (base "
			          << (firstBlock == kBlkCommon ? 0 : kFirstLevel) << ")\n";
		result = 0;
		return true;
	}
	// sound_effect(index): play bank clip `index` on a free mixer voice. The
	// original scans PHYSICAL(4) channels for a free one and drops the effect if
	// all are busy; Mixer::playSound already picks a free source or no-ops.
	if (fn == "sound_effect" && args.size() >= 1) {
		auto it = gBank.find(static_cast<int>(args[0]));
		bool have = it != gBank.end() && !it->second.empty();
		if (std::getenv("THIRDEYE_SNDTRACE"))
			std::cerr << "[snd] sound_effect(" << args[0] << ") "
			          << (have ? "PLAY " : "no-clip ")
			          << (have ? it->second.size() : 0u) << "B\n";
		if (gSoundOn && ctx.mixer != nullptr && have) {
			ctx.mixer->update(); // reclaim finished voices (lazy, no host-pump hook)
			ctx.mixer->playSound(it->second);
		}
		result = 0;
		return true;
	}
	return false;
}

} // namespace THIRDEYE::runtime::sound
