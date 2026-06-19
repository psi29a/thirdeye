#include "internal.hpp"

#include "../vm/events.hpp"
#include "../vm/objects.hpp"

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
		// outstanding notify requests (release_owned_windows is still a stub).
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
		std::string program;
		for (VM::Value a : args) {
			std::string s = ctx.vm.readCodeString(static_cast<uint32_t>(a));
			if (!s.empty()) { program = s; break; }
		}
		throw Relaunch{program};
	}
	return false;
}

} // namespace THIRDEYE::runtime::rtobject
