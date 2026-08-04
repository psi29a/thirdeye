#include "internal.hpp"

#include "../graphics/graphics.hpp"

// Dungeon Hack runtime functions -- the ~35 CALLs listed in
// ../../../../dh_research/ADDITIONAL_DH_RUNTIME_FUNCTIONS.TXT that exist in
// HACK.RES/OPEN.RES but not in the EOB3 runtime. Add stubs as the boot log
// surfaces them; go deep only where the SOP actually observes the return.

namespace THIRDEYE::runtime::dh {

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
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
	// ponytail: no-op; revisit if a DH screen shows stale content.
	if (fn == "touch") {
		(void)args;
		result = 0;
		return true;
	}

	return false;
}

} // namespace THIRDEYE::runtime::dh
