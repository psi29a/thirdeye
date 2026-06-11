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
// 32..255 are application events the SOP code defines for itself.
enum : int32_t {
	SYS_FREE = 0,       // an empty / invalidated slot (matches no notify request)
	SYS_TIMER = 1,      // heartbeat; matches when event param >= request param
	SYS_KEYDOWN = 17,
	FIRST_INPUT_EVENT = 2, LAST_INPUT_EVENT = 17,
	FIRST_SYS_EVENT = 0, LAST_SYS_EVENT = 31,
	FIRST_APP_EVENT = 32, LAST_APP_EVENT = 255,
	NUM_EVTYPES = 256,
};

class EventSystem {
public:
	explicit EventSystem(ObjectSystem& objects) : mObjects(objects) { reset(); }

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
	static constexpr int32_t NSX_TYPE = 0x00FF; // event-type mask in status

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
	void removeEvent(int32_t type, int32_t parameter, int32_t owner);
	int32_t scanEventRange(int32_t firstType, int32_t lastType) const;

	ObjectSystem& mObjects;

	std::array<NReq, NR_LSIZE> mNR{};
	std::array<int32_t, NUM_EVTYPES> mNRfirst{}; // chain head per event type

	std::array<Event, EV_QSIZE> mQueue{};
	uint32_t mHead = 0;
	uint32_t mTail = 0;

	int32_t mCurrentEventType = SYS_FREE; // event being dispatched (cancel guard)
	bool mVerbose = false;
};

} // namespace VM

#endif // THIRDEYE_VM_EVENTS_HPP
