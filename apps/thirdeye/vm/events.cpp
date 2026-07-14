/*
 * events.cpp
 *
 * AESOP event dispatcher. See events.hpp. Ported from
 * eob3_research/runtime/EVENT.C.
 */

#include "events.hpp"

#include <iostream>

namespace VM {

// EVENT.C match_parameter().
int32_t EventSystem::matchParameter(int32_t eventType, int32_t eventParam,
                                    int32_t testParam) {
	if (eventType == SYS_FREE)
		return 0;
	if (testParam == -1)
		return 1;
	if (eventType == SYS_TIMER)
		return eventParam >= testParam; // heartbeat: fire once we reach the time
	return eventParam == testParam;
}

size_t EventSystem::liveWindowCount() const {
	size_t n = 0;
	for (const auto &w : mWindows) if (w.used) ++n;
	return n;
}

// init_event_queue() + init_notify_list().
void EventSystem::reset() {
	mHead = mTail = 0;
	mQueue.fill(Event{});

	for (int i = 0; i < NUM_EVTYPES; ++i)
		mNRfirst[i] = -1;
	mNRfirst[SYS_FREE] = 0;             // SYS_FREE chain == the free list
	for (int i = 0; i < NR_LSIZE; ++i) {
		mNR[i] = NReq{};
		mNR[i].next = i + 1;
		mNR[i].prev = i - 1;
	}
	mNR[NR_LSIZE - 1].next = -1;
	mCurrentEventType = SYS_FREE;

	// Window table: handles 0/1 are PAGE1/PAGE2, the full 320x200 screen.
	mWindows.fill(Win{});
	for (int i = 0; i < 2; ++i)
		mWindows[i] = Win{0, 0, 319, 199, -1, true};
	mPointX = mPointY = 0;
	mLastLeft = mLastRight = false;

	// Timer rebase: in DOS every program launch() EXEC-REPLACES the process,
	// so INTRFACE.C's heartbeat restarts at 0 for each program (menu, game,
	// ...). Our single process keeps SDL ticks monotonic across the chain, so
	// without a rebase the game session inherits the menu's elapsed
	// heartbeats -- and every SOP notify threshold computed from the kernel's
	// tick counter (monster my-turn = tick+N, ~100) is instantly and forever
	// expired against a heartbeat already in the thousands: every monster
	// acted on EVERY tick and the QSP party was dead one second after boot.
	// reset() runs at each program relaunch (the exec-replace stand-in), so
	// re-arming the baseline here restores the DOS timeline.
	mTimerBase = INT32_MIN;
	mLastTimerBeat = INT32_MIN;
}

// add_notify_request(): pop a slot off the free chain and append it to the end
// of the requested event type's chain (handlers fire in registration order).
void EventSystem::addNotifyRequest(int32_t client, uint32_t message,
                                   int32_t event, int32_t parameter) {
	int32_t i = mNRfirst[SYS_FREE];
	if (i == -1) {
		std::cerr << "[events] notify list full (" << NR_LSIZE
		          << " requests) -- dropping notify(" << client << ", " << message
		          << ", " << event << ")" << std::endl;
		return; // original abend()s; during bring-up, fail soft
	}
	NReq& NR = mNR[i];
	mNRfirst[SYS_FREE] = NR.next;

	NR.next = -1;
	NR.client = client;
	NR.message = message;
	NR.parameter = parameter;
	NR.status = (0 & ~NSX_TYPE) | event;

	int32_t nxt = mNRfirst[event];
	if (nxt == -1) {
		mNRfirst[event] = i;
		NR.prev = -1;
	} else {
		int32_t cur = nxt;
		while (nxt != -1)
			nxt = mNR[cur = nxt].next;
		mNR[cur].next = i;
		NR.prev = cur;
	}
}

// delete_notify_request(): unlink every request matching the (client, message,
// parameter) filter and return it to the free chain. event == -1 sweeps all.
void EventSystem::deleteNotifyRequest(int32_t client, uint32_t message,
                                      int32_t event, int32_t parameter) {
	bool allEvents = false;
	if (event == -1) {
		event = 1;
		allEvents = true;
	}

	do {
		int32_t nxt = mNRfirst[event];
		while (nxt != -1) {
			int32_t cur = nxt;
			NReq& NR = mNR[cur];
			nxt = NR.next;
			int32_t prev = NR.prev;

			if (NR.client != client)
				continue;
			if (message != static_cast<uint32_t>(-1) && message != NR.message)
				continue;
			if (!matchParameter(event, NR.parameter, parameter))
				continue;

			NR.client = -1; // invalidate
			NR.status = (NR.status & ~NSX_TYPE) | SYS_FREE;

			if (nxt != -1)
				mNR[nxt].prev = prev;
			if (prev != -1)
				mNR[prev].next = nxt;
			else
				mNRfirst[event] = nxt;

			// append to the free chain
			NR.next = -1;
			int32_t fnxt = mNRfirst[SYS_FREE];
			if (fnxt == -1) {
				mNRfirst[SYS_FREE] = cur;
				NR.prev = -1;
			} else {
				int32_t fcur = fnxt;
				while (fnxt != -1)
					fnxt = mNR[fcur = fnxt].next;
				mNR[fcur].next = cur;
				NR.prev = fcur;
			}
		}
	} while (allEvents && (++event < NUM_EVTYPES));
}

// cancel_entity_requests(): drop all requests for `client` (or, for -1, every
// entity client). Called when an object is destroyed.
void EventSystem::cancelEntityRequests(int32_t client) {
	for (int32_t event = 1; event < NUM_EVTYPES; ++event) {
		int32_t nxt = mNRfirst[event];
		while (nxt != -1) {
			int32_t cur = nxt;
			NReq& NR = mNR[cur];
			nxt = NR.next;
			int32_t prev = NR.prev;

			if (client == -1) {
				// all entity clients only (skip non-entity / freed slots)
				if (NR.client >= ObjectSystem::kNumEntities || NR.client == -1)
					continue;
			} else if (NR.client != client) {
				continue;
			}

			NR.client = -1;
			NR.status = (NR.status & ~NSX_TYPE) | SYS_FREE;

			if (nxt != -1)
				mNR[nxt].prev = prev;
			if (prev != -1)
				mNR[prev].next = nxt;
			else
				mNRfirst[event] = nxt;

			NR.next = -1;
			int32_t fnxt = mNRfirst[SYS_FREE];
			if (fnxt == -1) {
				mNRfirst[SYS_FREE] = cur;
				NR.prev = -1;
			} else {
				int32_t fcur = fnxt;
				while (fnxt != -1)
					fnxt = mNR[fcur = fnxt].next;
				mNR[fcur].next = cur;
				NR.prev = fcur;
			}
		}
	}
}

// --- event queue ------------------------------------------------------------

// add_event(): write at head, advance; if the buffer wraps onto the tail, drop
// the oldest event (the original's circular-overflow behaviour).
void EventSystem::addEvent(int32_t type, int32_t parameter, int32_t owner) {
	mQueue[mHead].type = type;
	mQueue[mHead].owner = owner;
	mQueue[mHead].parameter = parameter;
	mHead = (mHead + 1) % EV_QSIZE;
	if (mHead == mTail)
		mTail = (mTail + 1) % EV_QSIZE;
}

int32_t EventSystem::nextEvent() const {
	return mTail != mHead ? static_cast<int32_t>(mTail) : -1;
}

int32_t EventSystem::fetchEvent() {
	if (mTail == mHead)
		return -1;
	int32_t t = static_cast<int32_t>(mTail);
	mTail = (mTail + 1) % EV_QSIZE;
	return t;
}

// find_event(): index of the first queued event matching type + parameter, else
// -1. Used to coalesce mouse-move/timer events instead of flooding the queue.
int32_t EventSystem::findEvent(int32_t type, int32_t parameter) const {
	for (uint32_t t = mTail; t != mHead; t = (t + 1) % EV_QSIZE) {
		const Event& EV = mQueue[t];
		if (EV.type != type)
			continue;
		if (!matchParameter(EV.type, EV.parameter, parameter))
			continue;
		return static_cast<int32_t>(t);
	}
	return -1;
}

// remove_event(): tombstone (set SYS_FREE) every queued event matching the
// (type, parameter, owner) filter; -1 is a wildcard for type and owner.
void EventSystem::removeEvent(int32_t type, int32_t parameter, int32_t owner) {
	for (int t = 0; t < EV_QSIZE; ++t) {
		Event& EV = mQueue[t];
		if (owner != -1 && EV.owner != owner)
			continue;
		if (type != -1 && EV.type != type)
			continue;
		if (!matchParameter(EV.type, EV.parameter, parameter))
			continue;
		EV.type = SYS_FREE;
	}
}

// scan_event_range(): index of the first queued event whose type is in
// [firstType, lastType], else -1.
int32_t EventSystem::scanEventRange(int32_t firstType, int32_t lastType) const {
	uint32_t t = mTail;
	while (t != mHead) {
		const Event& EV = mQueue[t];
		t = (t + 1) % EV_QSIZE;
		if (EV.type < firstType || EV.type > lastType)
			continue;
		return static_cast<int32_t>((t + EV_QSIZE - 1) % EV_QSIZE);
	}
	return -1;
}

bool EventSystem::peekEvent() { return nextEvent() != -1; }

// dispatch_event(): pull one event and SEND its message to each matching notify
// client. System/input events defer to pending application events (re-queued at
// the back) so user actions can't pre-empt the app's response to earlier ones.
// The walk stops early if a fired handler cancels the request or changes the
// event being dispatched (faithful to EVENT.C).
void EventSystem::dispatchEvent() {
	int32_t evi = fetchEvent();
	if (evi == -1)
		return;

	int32_t typ = mQueue[evi].type;
	int32_t par = mQueue[evi].parameter;
	int32_t own = mQueue[evi].owner;

	if (typ == SYS_FREE)
		return;

	int32_t pendingApp = scanEventRange(FIRST_APP_EVENT, LAST_APP_EVENT);
	if (typ >= FIRST_SYS_EVENT && typ <= LAST_SYS_EVENT && pendingApp != -1) {
		if (mVerbose)
			std::cout << "    [event " << typ << " p" << par
			          << " DEFERRED behind app event " << mQueue[pendingApp].type
			          << "]" << std::endl;
		addEvent(typ, par, own); // defer behind the pending application events
		return;
	}

	mCurrentEventType = typ;

	int32_t nxt = mNRfirst[typ];
	while (nxt != -1) {
		NReq& NR = mNR[nxt];
		nxt = NR.next;

		if ((NR.status & NSX_TYPE) != typ)
			break;
		if (NR.client == -1)
			break;
		if (typ != mCurrentEventType)
			break;

		if (matchParameter(typ, par, NR.parameter)) {
			if (mVerbose)
				std::cout << "    [event " << typ << " p" << par << "] -> SEND "
				          << NR.message << " to obj " << NR.client << std::endl;
			// The handler receives the event descriptor {parameter, owner}.
			mObjects.send(NR.client, static_cast<int>(NR.message), {par, own});
		}
	}
}

void EventSystem::drainEventQueue() {
	while (peekEvent())
		dispatchEvent();
}

// --- the thin code-resource wrappers ----------------------------------------

void EventSystem::notify(int32_t index, uint32_t message, int32_t event,
                         int32_t parameter) {
	addNotifyRequest(index, message, event, parameter);
}

void EventSystem::cancel(int32_t index, uint32_t message, int32_t event,
                         int32_t parameter) {
	deleteNotifyRequest(index, message, event, parameter);
}

void EventSystem::postEvent(int32_t owner, int32_t event, int32_t parameter) {
	addEvent(event, parameter, owner);
}

void EventSystem::sendEvent(int32_t owner, int32_t event, int32_t parameter) {
	addEvent(event, parameter, owner);
	drainEventQueue();
}

void EventSystem::flushEventQueue(int32_t owner, int32_t event,
                                  int32_t parameter) {
	removeEvent(event, parameter, owner);
}

void EventSystem::flushInputEvents() {
	for (int32_t i = FIRST_INPUT_EVENT; i <= LAST_INPUT_EVENT; ++i)
		removeEvent(i, -1, -1);
	if (mCurrentEventType >= FIRST_INPUT_EVENT &&
	    mCurrentEventType <= LAST_INPUT_EVENT)
		mCurrentEventType = SYS_FREE;
}

// --- interface / input layer (port of INTRFACE.C) ---------------------------

int32_t EventSystem::assignWindow(int32_t owner, int32_t x1, int32_t y1,
                                  int32_t x2, int32_t y2) {
	// Subwindow coordinates are absolute in the page's buffer space (GIL2VFX
	// stores them verbatim), so we keep the rectangle as-is. Handles 0/1 are the
	// pages; allocate the first free slot at/after 2.
	for (int32_t i = 2; i < MAX_WINDOWS; ++i) {
		if (!mWindows[i].used) {
			mWindows[i] = Win{x1, y1, x2, y2, owner, true};
			return i;
		}
	}
	std::cerr << "[events] window table full (" << MAX_WINDOWS << ")" << std::endl;
	return -1;
}

void EventSystem::releaseWindow(int32_t handle) {
	if (handle >= 2 && handle < MAX_WINDOWS)
		mWindows[handle].used = false;
}

void EventSystem::releaseOwnedWindows(int32_t owner) {
	// Handles 0/1 (PAGE1/PAGE2) are the pre-created full-screen pages -- never
	// owned by a SOP object, never reaped.
	for (int32_t i = 2; i < MAX_WINDOWS; ++i)
		if (mWindows[i].used && mWindows[i].owner == owner)
			mWindows[i].used = false;
}

bool EventSystem::windowRect(int32_t handle, int32_t &x1, int32_t &y1,
                             int32_t &x2, int32_t &y2) const {
	if (handle < 0 || handle >= MAX_WINDOWS || !mWindows[handle].used)
		return false;
	const Win &w = mWindows[handle];
	x1 = w.x0; y1 = w.y0; x2 = w.x1; y2 = w.y1;
	return true;
}

void EventSystem::setWindowEdge(int32_t handle, char which, int32_t val) {
	if (handle < 0 || handle >= MAX_WINDOWS || !mWindows[handle].used)
		return;
	Win &w = mWindows[handle];
	switch (which) {
		case 'l': w.x0 = val; break;
		case 't': w.y0 = val; break;
		case 'r': w.x1 = val; break;
		case 'b': w.y1 = val; break;
	}
}

// mouse_in_window(): inclusive rectangle test against window `wnd`.
bool EventSystem::mouseInWindow(int32_t wnd) const {
	if (wnd < 0 || wnd >= MAX_WINDOWS || !mWindows[wnd].used)
		return false;
	const Win& w = mWindows[wnd];
	return mPointX >= w.x0 && mPointX <= w.x1 && mPointY >= w.y0 &&
	       mPointY <= w.y1;
}

// add_region_event(): for each listener on `type`, if the mouse is inside that
// listener's window (NR->parameter), post an event carrying the window handle.
void EventSystem::addRegionEvent(int32_t type, int32_t owner) {
	int32_t nxt = mNRfirst[type];
	while (nxt != -1) {
		NReq& NR = mNR[nxt];
		nxt = NR.next;
		int32_t r = NR.parameter;
		if (mouseInWindow(r))
			addEvent(type, r, owner);
	}
}

// mouse_event_handler(): record the position, post a (coalesced) SYS_MOUSEMOVE,
// and raise ENTER/LEAVE region events as the mouse crosses registered windows.
void EventSystem::mouseMove(int32_t x, int32_t y) {
	mPointX = x;
	mPointY = y;

	int32_t param = (static_cast<int32_t>(static_cast<uint32_t>(y) << 16)) |
	                (x & 0xFFFF);
	int32_t ev = findEvent(SYS_MOUSEMOVE, -1);
	if (ev == -1)
		addEvent(SYS_MOUSEMOVE, param, -1);
	else
		mQueue[ev].parameter = param; // coalesce: update the pending move

	// ENTER_REGION: fire once on entry (latched via NSX_IN_REGION).
	for (int32_t nxt = mNRfirst[SYS_ENTER_REGION]; nxt != -1;) {
		NReq& NR = mNR[nxt];
		nxt = NR.next;
		if (mouseInWindow(NR.parameter)) {
			if (NR.status & NSX_IN_REGION)
				continue;
			NR.status |= NSX_IN_REGION;
			addEvent(SYS_ENTER_REGION, NR.parameter, -1);
		} else {
			NR.status &= ~NSX_IN_REGION;
		}
	}

	// LEAVE_REGION: fire once on exit (latched via NSX_OUT_REGION).
	for (int32_t nxt = mNRfirst[SYS_LEAVE_REGION]; nxt != -1;) {
		NReq& NR = mNR[nxt];
		nxt = NR.next;
		if (!mouseInWindow(NR.parameter)) {
			if (!(NR.status & NSX_OUT_REGION))
				continue;
			NR.status &= ~NSX_OUT_REGION;
			addEvent(SYS_LEAVE_REGION, NR.parameter, -1);
		} else {
			NR.status |= NSX_OUT_REGION;
		}
	}
}

// timer_callback(): keep one SYS_TIMER event in the queue, updating its
// heartbeat parameter rather than flooding the queue with new events.
void EventSystem::postTimer(int32_t heartbeat) {
	// Only inject a tick when the heartbeat actually advances. pumpHost calls this
	// every poll of the kernel's tight dispatch loop; if we re-posted every call the
	// SYS_TIMER event would be perpetually pending, so timer-tick (and the full
	// dungeon redraw it drives) would fire on *every* iteration -- pegging a core and
	// running animations ~3x too fast. Gating to the ~30 Hz heartbeat (SDL ticks >> 5)
	// makes the loop idle (SDL_Delay) between ticks: correct rate, low CPU.
	if (heartbeat == mLastTimerBeat)
		return;
	mLastTimerBeat = heartbeat;
	// Rebase to the first heartbeat seen after reset() so each program in the
	// chain starts at ~0, like the DOS exec-replace did (see reset()).
	if (mTimerBase == INT32_MIN)
		mTimerBase = heartbeat;
	heartbeat -= mTimerBase;
	int32_t ev = findEvent(SYS_TIMER, -1);
	if (ev == -1)
		addEvent(SYS_TIMER, heartbeat, -1);
	else
		mQueue[ev].parameter = heartbeat;
}

// mouse_button_event_handler(): on each button edge, post the plain click /
// release events plus the region variants for the window under the mouse.
void EventSystem::mouseButton(bool left, bool right) {
	if (left != mLastLeft) {
		addEvent(left ? SYS_CLICK : SYS_RELEASE, 0, -1);
		addEvent(left ? SYS_LEFT_CLICK : SYS_LEFT_RELEASE, 0, -1);
		addRegionEvent(left ? SYS_LEFT_CLICK_REGION : SYS_LEFT_RELEASE_REGION, -1);
		addRegionEvent(left ? SYS_CLICK_REGION : SYS_RELEASE_REGION, -1);
		mLastLeft = left;
	}
	if (right != mLastRight) {
		addEvent(right ? SYS_CLICK : SYS_RELEASE, 0, -1);
		addEvent(right ? SYS_RIGHT_CLICK : SYS_RIGHT_RELEASE, 0, -1);
		addRegionEvent(right ? SYS_RIGHT_CLICK_REGION : SYS_RIGHT_RELEASE_REGION, -1);
		addRegionEvent(right ? SYS_CLICK_REGION : SYS_RELEASE_REGION, -1);
		mLastRight = right;
	}
}

size_t EventSystem::pendingEvents() const {
	size_t n = 0;
	for (uint32_t t = mTail; t != mHead; t = (t + 1) % EV_QSIZE)
		if (mQueue[t].type != SYS_FREE)
			++n;
	return n;
}

} // namespace VM
