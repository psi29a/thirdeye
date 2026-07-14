#include "internal.hpp"

#include "../vm/events.hpp"
#include "../vm/objects.hpp"

#include <ostream> // for rt() << ... << std::endl (internal.hpp only fwd-decls)
#include <string>

namespace THIRDEYE::runtime::rtobject {

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
	// --- object management (RTOBJECT.C) -- real, backed by the ObjectSystem ---
	if (fn == "create_program" && args.size() >= 2) {
		int index = static_cast<int>(args[0]);
		int rtn = ctx.objects.createProgram(index,
		                                    static_cast<uint16_t>(args[1]));
		rt() << "  [-> obj " << rtn << "]" << std::endl;
		result = rtn;
		return true;
	}
	if (fn == "create_object" && args.size() >= 1) {
		int rtn = ctx.objects.createObject(static_cast<uint16_t>(args[0]));
		rt() << "  [-> obj " << rtn << "]" << std::endl;
		result = rtn;
		return true;
	}
	if (fn == "destroy_object" && args.size() >= 1) {
		// RTOBJECT.C destroy_object: MSG_DESTROY, then cancel the object's
		// outstanding notify requests. release_owned_windows(index) is NOT
		// wired here yet -- a straight port broke the ALL ATTACK button
		// (kernel destroys some transient object during "swap request" whose
		// owner slot the button's subwindow was assigned to; reaping it
		// killed the button on the second frame). The start-self-destroy
		// resetInstances() sweep already reaps leaked windows at each menu
		// cycle boundary, so the mid-game leak is bounded in practice.
		// TODO: find the mis-owned subwindow before re-enabling this.
		VM::Value rtn = ctx.objects.destroyObject(static_cast<int>(args[0]));
		ctx.events.cancelEntityRequests(static_cast<int>(args[0]));
		rt() << "  [destroyed]" << std::endl;
		result = rtn;
		return true;
	}
	// launch(mode, program, arg1, arg2): hand off to a secondary program. In
	// AESOP this exec-replaces the process (CHARGEN, the CINE intro player, ...);
	// the sub-program chain-launches back into "start", which re-reads cell 1264
	// (the bytecode pokemem'd the next mode just before launching). We model that
	// by unwinding to bootObject, which plays our internal equivalent and re-boots
	// start -- so a menu choice's poke+launch routes to the right next mode
	// instead of falling through to the following CASE branch.
	if (fn == "launch") {
		// launch(mode, program, arg1, arg2). mode is an int, the rest are Code-
		// space string addresses. Collect every non-empty string: the first is
		// the program ("CINE.EXE"), the rest are the sub-program's own args
		// (e.g. "INTRO.GFF" / "FINALE.GFF" for CINE.EXE) — runExternalProgram
		// dispatches on them.
		Relaunch r;
		for (const VM::Value &a : args) {
			std::string s = ctx.vm.readCodeString(static_cast<uint32_t>(a));
			if (s.empty()) continue;
			if (r.program.empty()) r.program = std::move(s);
			else                   r.extras.push_back(std::move(s));
		}
		throw r;
	}
	return false;
}

} // namespace THIRDEYE::runtime::rtobject
