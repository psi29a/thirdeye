#ifndef THIRDEYE_RUNTIME_INTERNAL_HPP
#define THIRDEYE_RUNTIME_INTERNAL_HPP

// Shared scaffolding for apps/thirdeye/runtime/. Each *.cpp file in this dir
// mirrors one of ../eob3_research/runtime/*.C and exports one `tryHandle`
// entry; engine.cpp's `defaultRuntimeCall` chains them.
//
// What's where:
//   rtcode.cpp    -- math/random/peekmem (RTCODE.C)
//   event.cpp     -- event queue + dispatch_event host pump (EVENT.C)
//   rtobject.cpp  -- create_program/create_object/destroy_object (RTOBJECT.C)
//   eye.cpp       -- EOB3-specific glue: transfer, step, change_level (EYE.C)
//   graphics.cpp  -- windowing, draw, text (GRAPHICS.C / INTRFACE.C)
//
// Globals (`gXxx`) are owned by engine.cpp; we declare them extern here so the
// category files can share the trace/perf/clip/compass state that crosses cuts.

#include "../vm/vm.hpp"

#include <chrono>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace RESOURCES { class Resource; }
namespace GRAPHICS { class Graphics; }
namespace VM { class ObjectSystem; class EventSystem; }
namespace THIRDEYE::savegame { struct TransferState; }

namespace THIRDEYE::runtime {

// Thrown by pumpHost to unwind the SOP main loop on quit/ESC. Caught in
// bootObject (engine.cpp).
struct QuitRequested {};

// Thrown by `launch` to model AESOP's program chain. In the original, launch()
// exec-replaces the process with a sub-program (cine.exe/chgen.exe); when that
// finishes it chain-launches "aesop eye start" again, which re-reads the mode
// the bytecode pokemem'd into cell 1264 and routes on it. We can't exec, so
// launch() unwinds the VM back to bootObject, which runs our internal
// equivalent of the named program and then re-enters start.MSG_CREATE -- same
// effect.
struct Relaunch { std::string program; };

// Per-call references handed to every category. These come from the engine's
// call site; the host TU owns the storage. Reference members make copy/move
// assignment implicitly deleted -- the explicit deletions below silence
// MSVC C5027 and document intent. Aggregate init was the old call style;
// we keep an explicit constructor so callers can still write Context{...}.
struct Context {
	VM::ObjectSystem &objects;
	VM::EventSystem &events;
	GRAPHICS::Graphics *gfx;     // null in headless mode
	RESOURCES::Resource &res;
	std::map<int32_t, int32_t> &mem;
	savegame::TransferState &xfer;
	VM::Interpreter &vm;

	Context(VM::ObjectSystem &objects_, VM::EventSystem &events_,
	        GRAPHICS::Graphics *gfx_, RESOURCES::Resource &res_,
	        std::map<int32_t, int32_t> &mem_, savegame::TransferState &xfer_,
	        VM::Interpreter &vm_)
	    : objects(objects_), events(events_), gfx(gfx_), res(res_),
	      mem(mem_), xfer(xfer_), vm(vm_) {}

	Context(const Context &) = default;
	Context(Context &&) = default;
	Context &operator=(const Context &) = delete;
	Context &operator=(Context &&) = delete;
};

// --- Engine-owned globals (defined in engine.cpp) -------------------------

// Runtime-call trace gate. Set from --debug before the VM runs; per-call
// logging routes through rt() so it's a no-op when off.
extern bool gRtTrace;

// Set when the bytecode redraws the compass facing indicator (draw_bitmap to
// page 104, resource 187, on a turn); consumed by the next compass-page
// refresh to re-snapshot. See Graphics::snapshotCompass/restoreCompass.
extern bool gCompassDirty;

// THIRDEYE_PERF=1: per-present timing instrument. Counts draw_bitmap calls
// since the last present, sums their wall-clock cost, and logs the gap from
// the previous present.
extern bool gPerf;
extern int gDrawCount;
extern long gDrawNanos;
extern std::chrono::steady_clock::time_point gLastPresent;

// "draw objects" (M:225 in dungeon class) clips each view cell's draws to a
// horizontal strip via set_x1/set_x2. We track the most recent pair as a
// single global and apply it to the view-page clip in draw_bitmap. -1 means
// "no narrowing" (use the full view extent).
extern int gViewClipX1, gViewClipX2;

// Set at go(); for timing prints.
extern std::chrono::steady_clock::time_point gBootStart;
extern bool gFirstPresentLogged;

// Gated trace stream (no-op unless gRtTrace).
std::ostream &rt();

// The host seam (see CLAUDE.md). Called by event.cpp's dispatch_event /
// peek_event. Pumps SDL input into AESOP events, presents the frame, yields
// the CPU when the queue is idle. Throws QuitRequested on window-close/ESC.
void pumpHost(GRAPHICS::Graphics &gfx, VM::EventSystem &events);

// printf-style substitution shared by print()/sprint(): fill %d/%u/%i, %s
// and %% in `fmt` from `args[start..]`. %s args are addresses (read via vm).
std::string formatSop(const std::string &fmt, const std::vector<VM::Value> &args,
                      size_t start, VM::Interpreter &vm);

// Load a dungeon level's tiles (the map resource -> lvlmap, wall-set bitmap
// number -> wallset, palette into the 0xB0 region). Used by both resume_level
// (initial) and change_level (in-game transitions). Returns false if the
// dungeon object or map isn't found.
bool loadDungeonLevel(int level, VM::ObjectSystem &objects,
                      RESOURCES::Resource &res, GRAPHICS::Graphics *gfx);

// --- Category dispatch entries -------------------------------------------
//
// Each returns true iff `fn` matches a name this category owns (whether the
// handler succeeded or no-op'd); on true, `result` is the return value.

namespace rtcode   { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace event    { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace rtobject { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace eye      { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace graphics { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }

} // namespace THIRDEYE::runtime

#endif // THIRDEYE_RUNTIME_INTERNAL_HPP
