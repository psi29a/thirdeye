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

	if (typ >= FIRST_SYS_EVENT && typ <= LAST_SYS_EVENT &&
	    scanEventRange(FIRST_APP_EVENT, LAST_APP_EVENT) != -1) {
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

size_t EventSystem::pendingEvents() const {
	size_t n = 0;
	for (uint32_t t = mTail; t != mHead; t = (t + 1) % EV_QSIZE)
		if (mQueue[t].type != SYS_FREE)
			++n;
	return n;
}

} // namespace VM
