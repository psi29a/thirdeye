/*
 * events.hpp
 *
 * AESOP event dispatcher: the FIFO event queue + notification-request list that
 * drive the running game. A faithful C++ port of eob3_research/runtime/EVENT.C
 * (John Miles, Eye III engine).
 *
 * The model: SOP objects register interest in an event type via notify(); other
 * code (or, eventually, the input/timer layer) posts events via post_event();
 * dispatch_event() pulls one event off the FIFO and SENDs the registered message
 * to every matching client. This is the engine's main loop: the kernel object's
 * bytecode calls dispatch_event() repeatedly, and that is what turns queued input
 * and timer ticks into object messages.
 *
 * Ported close to the original (the cancel-during-dispatch and app-vs-system
 * event-priority semantics are subtle and observable): the notify list is the
 * original's index-linked free-list, the queue its circular buffer. Differences
 * from EVENT.C: PollMod() (sound polling) and the kbhit debug dump are dropped,
 * and dispatch calls ObjectSystem::send() in place of RT_execute().
 */

#ifndef THIRDEYE_VM_EVENTS_HPP
#define THIRDEYE_VM_EVENTS_HPP

#include "objects.hpp"

#include <array>
#include <cstdint>

namespace VM {

// Event types (SHARED.H). Types 0..31 are system/input events the engine raises;
// 32..255 are application events the SOP code defines for itself. The *_REGION
// events carry a window handle as their parameter (see add_region_event).
enum : int32_t {
	SYS_FREE = 0,       // an empty / invalidated slot (matches no notify request)
	SYS_TIMER = 1,      // heartbeat; matches when event param >= request param
	SYS_MOUSEMOVE = 2,
	SYS_ENTER_REGION = 3,
	SYS_LEAVE_REGION = 4,
	SYS_LEFT_CLICK = 5,
	SYS_LEFT_RELEASE = 6,
	SYS_RIGHT_CLICK = 7,
	SYS_RIGHT_RELEASE = 8,
	SYS_CLICK = 9,
	SYS_RELEASE = 10,
	SYS_LEFT_CLICK_REGION = 11,
	SYS_LEFT_RELEASE_REGION = 12,
	SYS_RIGHT_CLICK_REGION = 13,
	SYS_RIGHT_RELEASE_REGION = 14,
	SYS_CLICK_REGION = 15,
	SYS_RELEASE_REGION = 16,
	SYS_KEYDOWN = 17,
	FIRST_INPUT_EVENT = 2, LAST_INPUT_EVENT = 17,
	FIRST_SYS_EVENT = 0, LAST_SYS_EVENT = 31,
	FIRST_APP_EVENT = 32, LAST_APP_EVENT = 255,
	NUM_EVTYPES = 256,
};

class EventSystem {
public:
	explicit EventSystem(ObjectSystem& objects) : mObjects(objects) { reset(); }

	// Holds an ObjectSystem& (a reference member), so it is inherently non-copyable
	// and non-movable; spell that out to silence MSVC C5027 (move-assign implicitly
	// deleted). EventSystem is a long-lived singleton in practice -- never reassigned.
	EventSystem(const EventSystem&) = delete;
	EventSystem& operator=(const EventSystem&) = delete;
	EventSystem(EventSystem&&) = delete;
	EventSystem& operator=(EventSystem&&) = delete;

	// Clear the queue and rebuild the notify free-list (init_event_queue +
	// init_notify_list).
	void reset();

	// --- the AESOP code-resource calls (the runtime functions) ---
	// notify(index, message, event, parameter): SEND `message` to object `index`
	// whenever an event of type `event` with a matching `parameter` is dispatched.
	void notify(int32_t index, uint32_t message, int32_t event, int32_t parameter);
	// cancel(): undo a matching notify() request. -1 means "any" for message and
	// (via match) parameter; event == -1 cancels across all event types.
	void cancel(int32_t index, uint32_t message, int32_t event, int32_t parameter);
	// post_event(owner, event, parameter): append an event to the FIFO.
	void postEvent(int32_t owner, int32_t event, int32_t parameter);
	// send_event(): post then drain (dispatch everything queued before returning).
	void sendEvent(int32_t owner, int32_t event, int32_t parameter);
	// peek_event(): true if an event is queued and ready.
	bool peekEvent();
	// dispatch_event(): pull one event and fulfil its notification requests.
	void dispatchEvent();
	// drain_event_queue(): dispatch until the queue is empty.
	void drainEventQueue();
	// flush_event_queue(owner, event, parameter): drop matching pending events.
	void flushEventQueue(int32_t owner, int32_t event, int32_t parameter);
	// flush_input_events(): drop all queued input events (and invalidate one in
	// flight).
	void flushInputEvents();
	// cancel_entity_requests(client): drop all notify requests for an object
	// (called when it is destroyed); client == -1 drops all entity clients'.
	void cancelEntityRequests(int32_t client);

	// --- interface / input layer (port of INTRFACE.C) ---
	// assign_subwindow: register a rectangular window (in 320x200 screen coords)
	// owned by `owner`; returns the handle the SOP code passes to notify() for
	// region events. Handles 0/1 (PAGE1/PAGE2) are pre-created full-screen.
	int32_t assignWindow(int32_t owner, int32_t x1, int32_t y1, int32_t x2,
	                     int32_t y2);
	// release_window: free a window handle.
	void releaseWindow(int32_t handle);
	// get_x1/get_y1/get_x2/get_y2: a window's rectangle (left/top/right/bottom).
	// Returns false if the handle isn't a live window. The menu uses get_y1 of a
	// hovered option's window to work out which option it is.
	bool windowRect(int32_t handle, int32_t &x1, int32_t &y1, int32_t &x2,
	                int32_t &y2) const;
	// set_x1/set_y1/set_x2/set_y2(wnd, val): mutate a single edge of an existing
	// window. Matches GIL2VFX_set_x1/x2/y1/y2 in the original runtime, which
	// just assigns panes[wnd].{x0,y0,x1,y1}. The save-picker uses these to
	// narrow window 99 to a 13-pixel column for the slot numbers ("1".."12")
	// then widen it back to 175 for the slot names ("Quick Start Party").
	void setWindowEdge(int32_t handle, char which, int32_t val);
	// Feed host input. mouseMove posts SYS_MOUSEMOVE (coalesced) and the
	// ENTER/LEAVE region events; mouseButton posts CLICK/RELEASE plus the
	// *_CLICK_REGION events for whichever registered region holds the mouse.
	void mouseMove(int32_t x, int32_t y);
	void mouseButton(bool left, bool right);
	// Heartbeat: keep a single SYS_TIMER event live, updating its parameter to
	// the monotonic `heartbeat` (INTRFACE.C timer_callback). SYS_TIMER notify
	// requests match when the heartbeat >= their threshold, so this drives timed
	// behaviour (menu fade-in, cursor blink, animation).
	void postTimer(int32_t heartbeat);

	// Current mouse position (last posted via mouseMove). The SOP's mouse_XY
	// runtime call reads these to figure out which save-slot row / button
	// the user clicked.
	int32_t pointX() const { return mPointX; }
	int32_t pointY() const { return mPointY; }

	// Diagnostics / tests: number of live (non-tombstone) events in the queue.
	size_t pendingEvents() const;

	// When set, dispatch logs each message it delivers (bring-up visibility).
	void setVerbose(bool v) { mVerbose = v; }

private:
	// A notification-request list entry (EVENT.H NREQ). next/prev index into
	// mNR (-1 = none); SYS_FREE-typed entries form the free chain.
	struct NReq {
		int32_t next = -1;
		int32_t prev = -1;
		int32_t client = -1;
		uint32_t message = 0;
		int32_t parameter = 0;
		int32_t status = 0; // low byte = event type (NSX_TYPE)
	};
	struct Event {
		int32_t type = SYS_FREE;
		int32_t owner = 0;
		int32_t parameter = 0;
	};

	static constexpr int NR_LSIZE = 768;   // max notify requests
	static constexpr int EV_QSIZE = 128;   // circular event-queue capacity
	static constexpr int32_t NSX_TYPE = 0x00FF;       // event-type mask in status
	static constexpr int32_t NSX_IN_REGION = 0x100;   // mouse currently in region
	static constexpr int32_t NSX_OUT_REGION = 0x200;  // mouse currently out of region
	static constexpr int MAX_WINDOWS = 256;           // window-handle table size

	// A registered window/region: an inclusive rectangle in screen coords.
	struct Win {
		int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		int32_t owner = -1;
		bool used = false;
	};

	// A request matches an event when the parameters agree: -1 is a wildcard,
	// SYS_TIMER matches >= (heartbeat), everything else matches ==. SYS_FREE
	// never matches.
	static int32_t matchParameter(int32_t eventType, int32_t eventParam,
	                              int32_t testParam);

	void addNotifyRequest(int32_t client, uint32_t message, int32_t event,
	                      int32_t parameter);
	void deleteNotifyRequest(int32_t client, uint32_t message, int32_t event,
	                         int32_t parameter);

	void addEvent(int32_t type, int32_t parameter, int32_t owner);
	int32_t fetchEvent();                                   // index or -1
	int32_t nextEvent() const;                              // index or -1
	int32_t findEvent(int32_t type, int32_t parameter) const; // index or -1
	void removeEvent(int32_t type, int32_t parameter, int32_t owner);
	int32_t scanEventRange(int32_t firstType, int32_t lastType) const;

	// Region helpers (INTRFACE.C): is the mouse inside window `wnd`? and post a
	// region event to every listener whose window currently holds the mouse.
	bool mouseInWindow(int32_t wnd) const;
	void addRegionEvent(int32_t type, int32_t owner);

	ObjectSystem& mObjects;

	std::array<NReq, NR_LSIZE> mNR{};
	std::array<int32_t, NUM_EVTYPES> mNRfirst{}; // chain head per event type

	std::array<Event, EV_QSIZE> mQueue{};
	uint32_t mHead = 0;
	uint32_t mTail = 0;

	std::array<Win, MAX_WINDOWS> mWindows{}; // window-handle table (0/1 = pages)
	int32_t mPointX = 0, mPointY = 0;        // current mouse position (screen)
	bool mLastLeft = false, mLastRight = false; // last button state (edge detect)

	int32_t mCurrentEventType = SYS_FREE; // event being dispatched (cancel guard)
	int32_t mLastTimerBeat = INT32_MIN;   // last heartbeat we posted a timer for
	bool mVerbose = false;
};

} // namespace VM

#endif // THIRDEYE_VM_EVENTS_HPP
