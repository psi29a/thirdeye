# Live control channel — agent-driven play & debugging

Status: **design** (nothing implemented yet). This doc is the hand-off spec:
any agent (or human) should be able to implement a phase from it without
re-deriving context. Target: an interactive protocol for driving a running
thirdeye — inject input, query game state, snapshot the screen — culminating
in an agent playing the Burial Glen end-to-end and saving.

## Motivation

Today's automation is **fire-and-forget**: `THIRDEYE_AUTOWALK` plays a fixed
token script decided before launch; `THIRDEYE_DUMP` snapshots every present;
diagnosis happens post-mortem from BMPs and logs. That's fine for regression
walks but useless for anything *reactive* — combat (where's the mist now?),
exploration (what did that chest drop?), or interactive debugging ("click
that button, now show me the cell state — without restarting").

The fix is a live, bidirectional control channel. Two capabilities:

1. **Act**: inject keys/clicks into the running game, exactly as AUTOWALK
   does internally, but on demand instead of on a fixed cadence.
2. **Observe**: query the *object system* directly — party pose, monster HP,
   floor items, cell contents. This is strictly better than OCR'ing
   screenshots: we own the VM, so we can read the same statics the SOP
   reads. Screenshots stay available for visual confirmation.

An agent (Claude via Bash, a Python script, a human with `socat`) then runs a
sense→decide→act loop against a live game. The same channel doubles as an
interactive debugger.

## What already exists (reuse, don't reinvent)

All in [engine.cpp](../apps/thirdeye/engine.cpp)'s `pumpHost` region
(~line 243 onward) — the host seam called from every `dispatch_event` /
`peek_event` ([runtime/internal.hpp](../apps/thirdeye/runtime/internal.hpp)):

- **Key injection**: `events.postEvent(0, VM::SYS_KEYDOWN, scancode)` — how
  AUTOWALK `Key` tokens fire (engine.cpp ~457).
- **Click injection**: `gfx.mouseToLogical(x, y, lx, ly)` →
  `events.mouseMove(lx, ly)` → `events.mouseButton(left, right)`, with the
  **release ~5 pumps later** (`events.mouseButton(false, false)`) — see the
  AUTOWALK `Lclick`/`Rclick` phase-5 release (engine.cpp ~474). The delay
  matters: the SOP polls button state across pumps; an instant release is
  missed.
- **Automap toggle**: host-side only — `THIRDEYE::automap::toggle()`
  (a VM SYS_KEYDOWN can't reach it).
- **Screenshot**: `Graphics::saveScreenshot(path)` (what THIRDEYE_DUMP
  calls, graphics.cpp ~1092).
- **Object reads**: `automap.cpp` has the read patterns to copy —
  `lvlobjHead()` (automap.cpp:132) and `featureDisabled()` (automap.cpp:171).

## Design

### Transport

- Env var `THIRDEYE_CTL=<path>` → engine creates a **Unix domain socket**
  (stream) at `<path>` at boot, `unlink`s any stale file first, sets
  `O_NONBLOCK` on the listen fd.
- **No threads.** Polled entirely from `pumpHost`, once per pump (~30 Hz):
  non-blocking `accept()`, non-blocking `read()` of complete lines,
  handlers run synchronously, replies `write()`n back. One client at a
  time is fine (reject or replace on second accept — implementer's choice,
  document which).
- Not set ⇒ zero cost, zero code active. macOS + Linux first; Windows
  (AF_UNIX exists on Win10+, else named pipes) explicitly out of scope v1.
- Latency budget: a command waits at most one pump (~33 ms). Fine.

### Protocol

Line-oriented text, UTF-8, `\n`-terminated. One command per line. Every
command produces one reply block terminated by a line reading `ok` or
`err <message>`. Data lines precede the terminator. No JSON — parseable
with awk/grep, writable without a library.

```
key <hex>                inject SYS_KEYDOWN (4800=fwd 5000=back 4b00/4d00=strafe
                         4700/4900=turn 0d=Enter 1b=Esc — AUTOWALK codes)
click <L|R> <x> <y>      press at logical 320x200 coords; auto-release ~5 pumps
                         later (queued by the handler, no client action needed)
map                      toggle the automap overlay
dump <path>              write a BMP of the current frame to <path>

party                    party pose + members
cell <x> <y>             lvlobj chain heads at that cell, all 3 planes
items                    floor items (W:place == -1) on the party's level
monsters                 live NPCs on the party's level
peek <obj> <off> <n>     hex dump of an object's statics (debug)
send <obj> <msg> [args]  SEND a message to an object (debug, dangerous — it
                         runs SOP bytecode; document as "you can corrupt state")
save <slot>              run the save flow into slot N (phase 2+; may start as
                         err unimplemented)
ping                     liveness check, replies ok immediately
```

Reply sketches (stable, documented formats — agents will parse these):

```
> party
pose x=15 y=10 fdir=3 lvl=1
pc slot=0 obj=32 name=Sir_Mikeal hp=34/40 status=0x00
pc slot=1 obj=33 name=Stonebeard hp=7/45 status=0x00
ok

> cell 14 10
plane0 head=1050 chain=1050:2208,1268:2268
plane1 head=-1
plane2 head=-1
ok

> items
item obj=103 cls=1376 x=21 y=8 region=2
ok

> monsters
npc obj=1764 cls=1904 x=15 y=9 hp=12
ok
```

Naming: `chain=` entries are `slot:class` pairs in chain order. Spaces in
names become `_`. Numbers decimal except `peek` output and `status` bits.

### Query internals (the offsets an implementer needs)

All class numbers / offsets verified in this codebase — grep for them in
[savegame/lvl_tmp.cpp](../apps/thirdeye/savegame/lvl_tmp.cpp),
[automap.cpp](../apps/thirdeye/automap.cpp), [runtime/eye.cpp](../apps/thirdeye/runtime/eye.cpp):

| What | How |
|---|---|
| kernel object | `objects.firstObjectOfClass(1382)` |
| party x/y/fdir/lvl | kernel class-1382 bytes @243/244/245/246 |
| W:in_hand | kernel @241 (u16, -1 = empty hand) |
| W:current_screen | kernel @265 (also `uiScreenActive()` in internal.hpp) |
| player[] slots | see `kPlayerSlots` handling in runtime/eye.cpp resume_level |
| PC class | 1369; name @137 (20 B), hpts @171, hmax @173, PCstat @161 |
| dungeon object | `objects.firstObjectOfClass(1381)` |
| lvlobj cell | dungeon class-1381 @ `1024 + plane*2048 + y*64 + x*2` (u16 head, 0xFFFF = empty; planes: 0=features, 1=items, 2=monsters) |
| entities statics | class 1370: W:place@0, B:x@2, B:y@3, B:lvl@4, B:region@5, W:next@6, W:prev@8 |
| monster? | `objects.isSubclassOf(cls, 1622)` (NPC) |
| item? | `objects.isSubclassOf(cls, 1371)` (items) |
| feature disabled (tree cut, door open) | features class-1994 @0 bit 0x8000 (`featureDisabled`, automap.cpp) |
| NPC hitpts offset | not pinned here — read it from `daesop -k EYE.RES 1622` EXPT before implementing `monsters` hp field |

Chain walks must carry a guard (≤ 2000 hops) — see the invariant checker in
lvl_tmp.cpp for the pattern; a corrupt chain must produce `err`, not a hang.

### Files & integration

- New: `apps/thirdeye/control.{cpp,hpp}` — socket lifecycle, line buffering,
  command dispatch. Public surface kept minimal:
  `control::init(path)`, `control::pump(ControlDeps&)`, `control::shutdown()`.
  `ControlDeps` = `{Graphics*, EventSystem&, ObjectSystem&, Resource&}` —
  same references `pumpHost` already holds; gfx nullable for headless.
- Hook: one call in `pumpHost` next to the AUTOWALK block, gated on the env
  var having been set. The deferred click-release is a tick counter inside
  control.cpp, mirroring AUTOWALK's phase-5 logic.
- CLI: also accept `--ctl=<path>` in main.cpp (sets the same config).

## Phases

**Phase 1 — channel + act + dump.** Socket, `ping`/`key`/`click`/`map`/
`dump`. Acceptance: from Bash, boot with `THIRDEYE_CTL`, walk a corridor by
sending `key 4800`s, `dump` before/after, pixels differ; a second client
connect behaves as documented; killing the client mid-command doesn't wedge
the pump (write must tolerate EPIPE).

**Phase 2 — observe.** `party`/`cell`/`items`/`monsters`/`peek`/`send`.
Acceptance: on `--load-save` into a known slot, `party` matches the save's
ITEMS position record; `cell` on a known door cell lists the door+button
chain (cf. the chain-order work in lvl_tmp.cpp); `items` on LVL03 lists the
wands the GameBanshee legend places at its annotation #2
(`../eob3_research/gamebanshee/legends/burialglen.json`).

**Phase 3 — the Burial Glen POC.** An agent drives, via the channel only:
QSP start on LVL03 → kill the grave mists (query `monsters`, face, attack
via weapon-icon clicks, re-query until dead) → pick up loot at the legend's
annotations #2–#4 → find the axe, equip it, chop trees toward the gate
(verify with `featureDisabled` via `cell`) → reach exit B (Forest Trail
gate) → camp → save to a slot → verify the slot's `.BIN`s exist and parse.
The driving script (Python, lives in `scripts/`) is throwaway-quality; the
*channel* is the deliverable. Expect this phase to surface runtime bugs
(axe equip, chop mechanics) — that's a feature; file/fix them as found.

**Phase 4 — skill.** `.claude/skills/thirdeye-drive/SKILL.md` so any future
session can drive a live game without rediscovering the protocol. Contents:
how to boot with the channel (always `THIRDEYE_MUTE=1`), the command table,
reply grammar, the scan-code crib, known screen coordinates (weapon icons,
CAMP button, compass arrows — harvest these during phase 3), and the
sense→decide→act loop pattern with the "one command may take a pump" caveat.
Trigger phrases: "drive the game", "play the glen", "live debug", "control
channel".

## Testing

- Protocol unit tests (apps/tests): feed the line parser fragmented/joined
  input, assert command splits; no socket needed if the parser is a pure
  function over a buffer — write it that way.
- One headless e2e in CI: boot with `THIRDEYE_CTL` + `--load-save`, script
  `ping`→`party`→`key`→`party`, assert the pose changed. Keep it under the
  existing ctest umbrella.
- The valgrind walk (`scripts/ci-valgrind.sh`) must stay green with the
  channel compiled in but env-var-off (zero-cost path).

## Non-goals (v1)

- Windows transport, multiple simultaneous clients, authentication (it's a
  local debug socket; document that it's as trusted as ptrace).
- Frame-perfect input timing (one-pump granularity is enough for EOB3).
- A general RPC layer — this is a debug/drive channel, not an API contract;
  the format may change between versions without ceremony.

## Open questions (decide during phase 1, document the answer here)

1. Second client: reject with `err busy`, or drop the first? (Lean: reject.)
2. `save <slot>`: reuse the SOP camp flow via clicks (honest, slower) or
   call the save cluster directly (faster, risks divergence)? (Lean: clicks
   in phase 3; direct call only if the click route proves flaky.)
3. Should `dump` reply with image dimensions/palette info for the agent, or
   stay path-only? (Lean: path-only; the agent reads the BMP itself.)
