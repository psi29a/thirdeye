# Roadmap

What's done, what's in progress, what's next. See [progress.md](progress.md) for the detailed
narrative on completed work.

## Phase 0 — intro + title menu render ✅

## Phase 1 — VM core ✅ (essentially done)

- ✅ Stack VM for all 88 opcodes (branches/arith/logic/constants/auto/`JSR`/`RTS`/
  `CASE`/`RCRS`/`CALL`/`SEND`/`PASS`/statics/extern/table loads/effective-addr/`AIM`/`AIS`/
  `SXAS`/`SOLE`; `BRK` = the int-3 debugger hook, continues with a log like a
  debugger-less original). GTest-covered; CI on Linux/macOS/Windows.
- ✅ Objects + `SEND`/`PASS` dispatch + class-hierarchy inheritance + parameter passing
  (`ObjectSystem`).
- ✅ Static variables (scalar + array, per-instance), constant tables, `AIM`/`AIS`.
- ✅ Cross-object link layer: extern opcodes + `SXAS`/`SOLE` + auto arrays + per-class static
  bases for inherited statics.
- ⏳ A handle/lock resource-manager layer over `Resource` (can fold into Phase 2 — `Resource`
  keeps everything in memory anyway).

## Phase 2 — Runtime API + game loop 🚧 (mostly done)

- ✅ AESOP event system (`vm/events.hpp`, port of `EVENT.C`).
- ✅ Event-driven host loop (`pumpHost`): `dispatch_event`/`peek_event` pump SDL input,
  post a `SYS_TIMER` heartbeat, present, yield when idle.
- ✅ Windowing/region + timer: `assign_subwindow` regions + mouse hit-testing + `SYS_TIMER`
  make the menu clickable/hoverable.
- ✅ AESOP/16 `"1.10"` bitmap decoder.
- ✅ Text rendering pipeline (`text_window`/`text_style`/`text_color`/`text_xy`/`print`/
  `sprint`).
- ✅ EOB3 runtime-function set (`EYE.C`/`RTCODE.C`/`INTRFACE.C`) wired. **Digital SFX**
  (2026-07-03): `load_sound_block`/`sound_effect`/`set_sound_status`
  ([runtime/sound.cpp](../apps/thirdeye/runtime/sound.cpp), port of `SOUND32.C`) back onto the
  OpenAL `Mixer` (raw 8-bit mono @ 8 kHz, the same `playSound` path the intro keypress SFX
  uses). `load_sound_block` walks its Code-space table of 32-bit resource numbers (new
  `Interpreter::codeWord`) into an index→PCM bank — verified loading the shipped COMMON (45
  clips, base 0) + LEVEL (8 clips, base 50) banks per `SOUND.H`. `init_sound`/
  `shutdown_sound` arm/disarm the play gate. **Music trio wired** (2026-07-03):
  `load_music`/`unload_music` toggle a resident flag (WildMIDI reinits per playback, so no
  driver-level setup needed) and `play_sequence(LA, AD, PC)` picks the MT32 track by default
  (matches `Mixer` `mt32=true`; override with `THIRDEYE_MIDI_DEVICE=AD|PC|LA`), loads the
  XMI resource via `res.getAsset`, and hands it to `Mixer::playMusic` (existing WildMIDI +
  XMIDI-to-GS path already used by the intro cinematic). **Every `C:` import in EYE.RES is
  now handled — zero `[stub]` traces on the golden path.** Save/load + the Restore-Game
  picker flow end-to-end (see Phase 3). Much of `EYE.C` (combat-edge cases, level transition
  flow) still incremental.
- ✅ **Goal reached**: title menu + in-game HUD both render and are interactive (SOP-driven).

## Phase 3 — Playable EOB3 🚧 (dungeon is explorable + populated)

- ✅ **Dungeon view rendering** — walls/floor/depth across all 14 levels (4 tilesets, derived
  by area name), view-window clipping, correct stone palettes.
- ✅ **Movement** — arrow keys + compass-arrow mouse clicks walk/turn/strafe; `step_X/Y/FDIR`
  geometry + the bytecode's `impedance` collision; the 3D view redraws each step.
- ✅ **Mouse** — compass arrows, CAMP button, region clicks all work (correct under
  `--scale`/Retina).
- ✅ **Level objects** — the whole level (doors/levers/stairs/decorations/monsters, 231 in
  LVL01) loads from `LVLnn.TMP` and renders.
  - **Fix (2026-07-01, superseded same day):** trees didn't render because
    `features.get_state` returned 15 — the loader was reading the wrong statics
    offset and a `+14 == 0x0F → decflags = 0` remap was patched in to compensate.
    The remap is **gone**: `loadLevelObjects` was rewritten to copy each record's
    statics verbatim (the original's `restore_range`), which puts the true
    `W:decflags` at `+10` and fixed rendering, tree-chopping, and monster HP in
    one stroke — see the "level-object restore made faithful" section below.
    ([savegame/lvl_tmp.cpp](../apps/thirdeye/savegame/lvl_tmp.cpp).)
- ✅ **Items & inventory (display)** — equipment screen renders the paper-doll + char-gen gear;
  slot regions register so interaction routes through the proven click path.
- ✅ **Item interaction (#29) — verified end-to-end (2026-07-03).** All three interaction
  types work through real clicks, after four uninit/sentinel fixes:
  - **Floor pickup** — kernel `W:in_hand@241` empty-hand sentinel must be **-1**
    (zero-fill read as "holding object 0" → every click took the DROP path); seeded in
    `resume_level`. Verified via real clicks for BOTH the party's own cell (region 85,
    near floor strip) and the cell ahead (region 86, mid strip → `step dist 1`): floor
    clicked → cell+quadrant → `get index of topmost item` (plane 1, `W:place == -1`,
    `B:region` = quadrant) → take → `in_hand = item`. Nothing was miscalibrated — items
    are clickable at the quadrant where the SOP draws them.
  - **Equip/move** — portrait click opens the equipment screen (M:240); clicking the
    right-hand slot takes the sword to hand (`in_hand -1 → 993`), clicking again places
    it back (`993 → -1`). (Historical note: this originally needed two "stored-as-sentinel"
    workarounds — re-seeding `items.W:itmflags` and applying `arms.B:bonus` from a
    "trailer byte" — because the §2.3 parser was reading each item's static block 4 bytes
    early. The 2026-07-19 CDESC-framing fix reads the block correctly, so itmflags and
    bonus arrive in-frame and both workarounds are gone.)
    Backpack slots `W:inventory[0..13]` are now parsed from ITEMS.TMP @+96 and restored
    (they were left zero-filled = "item object 0", so clicking an empty backpack slot
    picked up the kernel).
  - **Consumables** — rations picked from the floor → equipment screen → food-plate
    click: a full PC correctly refuses (" isn't hungry at the moment."); with food
    lowered (`THIRDEYE_STARVE=10`) the same click consumes the rations
    (`in_hand 999 → -1`) and refreshes the FOOD bar.
  Debug rig: `THIRDEYE_TESTITEM`/`ITEMHERE`/`ITEMREGION` (drop an item at/ahead of the
  party with a chosen quadrant), `THIRDEYE_TESTPICKUP` (drive M:248 directly),
  `THIRDEYE_ITEMDUMP=<obj>` (hex-dump live item statics), `THIRDEYE_STARVE=N`, and
  `in_hand` change logging under `THIRDEYE_CLICK`. The adventure panels' hand-weapon
  display (issue item 4) turned out to already work — the SOP always drew the two
  hand-slot icons next to each portrait; they render correctly (sword/shield/axe/
  spellbook/holy symbol per the QSP loadout) now that `equip[]` restoration fills
  `W:inventory[16/20]`, and they redraw on the kernel's own "refresh players" tick.
  **Fallout fix:** the "init level" post-send hook captured `[&ctx]` — a dangling
  reference to `defaultRuntimeCall`'s stack-local Context that only "worked" while the
  dead stack bytes held the old layout; adding the Context `mixer` member shifted the
  frame and it became a boot-time SEGFAULT. The hook now captures the long-lived
  `objects`/`res` directly (runtime/eye.cpp).

### ✅ Combat — basic attack/damage/die chain lands

`THIRDEYE_ATTACK=1` validates end-to-end: PC swings → `roll to hit` → `player to hit`
(THAC0 on `tables` singleton 2003) → `weapon damage` / `adjust damage` → `take damage` →
`die` → `remove`. Verified against troll (class 1934, HP 30) — HP drops on each hit, NPC
dies at 0 HP, monster is removed from the level. Verified against sword wraith (class 1904,
bit-6 incorporeal, `NPCstat & 0x40`) — `weapons.hand-to-hand` line 866 takes the special
bit-0x40 path and SENDs `die` directly (no `take damage`), and the wraith is removed on
first hit. Both flows reach `entities.remove` (msg 22) which unlinks from `lvlobj` plane 2
so `acquire NPC target` no longer finds the corpse.

**Subsystem wins along the way:**
- `dice`/`rnd` now real (shared PRNG in [engine.cpp](../apps/thirdeye/engine.cpp)).
- `player to hit` (M:162) — bytecode in `tables` class (2380), `INTC #07d3 ;2003` is the
  *object index* of the kernel's `tables` singleton, not the "marble plaque" class. THAC0
  = 20 − (level−1) / `B:table798[col]` × `B:table794[col]` − arg1.
- **Synthetic-spawn `W:carried` init** — `THIRDEYE_ATTACK` bypasses `LVLnn.TMP`, so NPCs
  came up with `W:carried = 0`. `NPC.die` loops on `W:carried != -1` and walks into obj 0,
  stalling the death path. Init `W:carried = -1` (NPC offset 5) on synthetic spawn fixes it.
  Real level NPCs already get this from the level loader.
- Typed equipment placement (`TransferState::rebuildPlacement`) — for chargen transfer, not
  exercised by the Quick Start `ITEMS.TMP` load.

**Stand-ins still in place** (not blockers):
- `[atk] mon … (live=N)` still shows the obj slot occupied after death; that's correct —
  `NPC.die` deliberately doesn't call `destroy_object`, just `remove` + `place(-1,-1,-1)`.
- ~~Wraith creature palette loads unconditionally at 0xC0 on every level.~~
  **Fixed (SOP-driven, 2026-06-23):** the 14 area-class singletons (mauslvl1, magelvl3,
  graveyrd, …) are now pre-created at object slots 1..14 from `ITEMS_00.BIN`'s native
  CDESC stream ([savegame/items_tmp.cpp](../apps/thirdeye/savegame/items_tmp.cpp)
  `loadAreaInstances`). With them live, the kernel's natural `SEND dungeon, "init level"`
  cascade fires `SEND area, "enter level"` → `set_palette(1, walls), set_palette(2, M1),
  set_palette(3, M2)` directly from EOB3 bytecode — no C++ hardcoded enumeration.
  Surfaced one VM bug: `staticsPtr` was throwing on extern accesses where the SOP probes
  a static via a class not in the target's ancestor chain (dungeon "init level" does
  `LXB B:lvl` on every alive object 1..1999); DOS-AESOP would have read whatever was
  at that linear-memory offset, so we now return a sinkhole instead of throwing
  (vm/objects.cpp `staticsPtr` + vm/vm.cpp `externPtr`).

**Monster-AI attack-back — landed (2026-07-01):** the `bounce`/`my turn` chain
was already firing (SYS_TIMER events + M:87 `schedule attack` re-arm), but the
monster never crossed into aggro because `C:distance` and `C:seek_direction`
were unimplemented runtime stubs returning 0. `NPC.my turn` used those to pick
a wander direction and gate the `advance_and_attack`/`valid_range_attack`
sends, so every NPC just wandered north forever. Implemented both in
[runtime/eye.cpp](../apps/thirdeye/runtime/eye.cpp) verbatim from EYE.C — the
32-entry sqrt table for `distance` and the octal N/NE/E/… direction map for
`seek_direction`. Verified with `THIRDEYE_ATTACK=1
THIRDEYE_ATTACK_NOAUTO=1`: synthetic troll adjacent to the party now walks
through `my turn` → `advance and attack` (M:99) → `contact` (M:89) → PC.take
damage (M:82) → front-row PC (Stonebeard, idx 33) takes 5/15/18/10 hp per
swing. Real level-3 NPCs also start firing `my turn` correctly.

**Magic-weapon vs bit-6 — not a runtime bug (2026-07-01):** RE'd end-to-end and
confirmed the SOP itself instant-kills bit-6 targets on *any* successful hit, with
no weapon-flag gate. `PC.roll to hit` (r1369 @3318) short-circuits to `return 1`
when `target.NPCstat & 0x40` is set (auto-hit), and `weapons.hand-to-hand attack`
(r1688 @866) then does `SEND target.die(1)` unconditionally on the same bit — no
LNGC of a weapon-magic constant appears anywhere in the handler, no subclass
overrides `hand-to-hand attack` (checked bare hands / magic dagger / magic staff /
two-handed sword). Bit 0x40 is set consistently across ethereals (sword wraith,
shadow, watch ghost, shadow hound, lich, shadow of death). So "one bare-hand
punch drops a wraith" *is* the shipped EOB3 behavior — our runtime is faithful.
Any change would be a deliberate deviation from `EYE.RES`, which violates the
project rule "our job is to run EYE.RES, not to RE it."

**Ethereal monsters "always missed" — FIXED (2026-07-03):** the incorporeal
auto-hit above only worked for freshly-spawned test monsters; every *real*
level-loaded grave mist / sword wraith was unhittable in play. Root cause: the
PC's `roll to hit` (r1369 M:71) reads the target's `W:NPCstat@1622:0 & 0x40`,
but `NPC.restore` caches `report(1)` into a scratch local and **never writes
`W:NPCstat` wholesale** (only conditionally ORs bit 0x20). The class stat flags
(incl. 0x40) are seeded into `W:NPCstat` when a monster is first *built*; the
shipped QSP `LVLnn.TMP` persists a stale `0x0001` there, so bit 0x40 read as 0
→ no auto-hit → normal d20 vs the mist's tough AC → every swing "missed."
`loadLevelObjects` now seeds `W:NPCstat = report(1)` for monsters before the
M:2 restore (savegame/lvl_tmp.cpp) — the "uninitialized static the original
loader would have set." Verified: real LVL03 mist (obj 1750) went
`NPCstat 0x0001 → 0x61C3`, and a live swing now `roll to hit → 1` (auto-hit) →
`die` → removed from the grid, no re-acquire. Debug: `THIRDEYE_MISTCHECK=1`
(dumps a real mist's stored vs class NPCstat), `THIRDEYE_ATKTRACE=1` (logs the
roll-to-hit/die/take-damage chain with return values).

**XP + level-up — verified end-to-end (2026-07-01):** the whole chain runs on the
bytecode already. Troll kill → `NPC.die(killer)` (msg 55) → `NPC.report(1012)` returns
class XP value (1400 for troll) → kernel `M:103 experience(1400)` divides by
qualifying-PC count (via `M:35 qualify`) → each qualifying PC gets `M:103 experience`
→ PC bytecode loops the 3 multi-class slots, checks `M:174 racial level limits` on
`tables` singleton (obj 2003, class 2380), adds XP to `L:experience[0..2]` @179,
and on threshold cross (`M:175 experience level`) bumps `B:levels[i]` @163, sends
`M:176 new hit points` and adds the roll to `W:hpts` @171 + `W:hmax` @173. Verified
with `THIRDEYE_ATTACK=1 THIRDEYE_XPBLAST=500000 THIRDEYE_XPTRACE=1`: Stonebeard
6/6/0 → 8/8/0 with HP 60→102, Fast Eddie 8 → 10, and PCs at racial cap correctly
stay put (Sir Mikeal 7 fighter, PC 35 at 13). No C++ change needed for the chain
itself — helpers `THIRDEYE_ATTACK_RESPAWN` (auto-respawn the test monster) and
`THIRDEYE_XPBLAST=N` (one-shot direct M:103) live in
[runtime/event.cpp](../apps/thirdeye/runtime/event.cpp) for regression checks.

### ✅ Runtime-import survey + batch fill (2026-07-02)

Diffed **every `C:` import in EYE.RES (118)** against the names our runtime
handles; 57 were falling through to `[stub -> 0]`. `defaultRuntimeCall` now
logs the first call to each unhandled function unconditionally (`[stub] name`,
engine.cpp) so new gaps surface themselves. Ground-truth discovery along the
way: the EOB3-specific 16-bit `EYE.C` (spells, save cluster, `step_square_*`)
lives in **`../eob3_research/arun/src/`**, not `runtime/` (see CLAUDE.md
gotchas — `grep -a` required). Implemented, all ported verbatim from the C:

- **Geometry**: `step_square_X/Y` + `step_region` (half-cell projectile
  flight), `step_X/Y` extended with the `MTYP_ML/MM/MR` maze-passage moves +
  the 32-cell coordinate wrap.
- **Spell queries**: `spell_request` (rest-tick "any spell still to
  memorize?") and `spell_list` (spell-menu list builder), both reading the
  PC's `B:spell_stat`/`B:spell_cnt` arrays through a new bounds-checked
  `staticBytePtr` helper (runtime/internal.cpp).
- **Strings** (RTCODE.C): `string_len`, `strval`, `envval` (full `ascnum`
  port incl. `0x`/`0b`/`'c`), `copy_string`, `load_string`;
  `string_compare` fixed to `stricmp` semantics (case-insensitive).
- **Text metrics**: `get_text_x/y`, `char_width`, `font_height`, `crout`,
  `dprint`, `text_refresh_window` (recorded no-op — we present whole-screen).
- **Graphics**: `draw_line` (+ `Graphics::drawLine`, Bresenham),
  `draw_rectangle`, `hash_rectangle` (+ `hashRect` checkerboard),
  `solid_bar_graph` (the HP/food bars, verbatim port), `magic_field`
  (shield/prayer portrait borders incl. the dashed both-fields pattern),
  `visible_bitmap_rect` (sprite click hit-testing; writes 4 WORDs into SOP
  statics), `mouse_in_window`, `pixel_fade` (instant present for now).
- **No-op'd knowingly**: cursor set (`show/hide/lock/unlock_mouse`,
  `standby/resume_cursor`, `set_wait_pointer`), cache (`flush/thrash_cache`),
  `init/shutdown_graphics/interface`, `wait_vertical_retrace`, `beep`,
  `diagnose`, `create_initial_binary_files` (dev-time TXT→BIN translation).

**Still stubbed**: none. Music trio (`load_music`/`play_sequence`/
`unload_music`) landed 2026-07-03, closing the last `[stub]` hole on the
golden path (see Phase 2 note). Everything else got wired 2026-07-03: `do_dots`/`do_ice` (verbatim ports of
EYE.C's fixed-point particle physics in runtime/graphics.cpp; the per-pixel
sprite-occlusion mask reads are skipped since we composite pages onto one
surface — particles clip to the view window and save/restore the exact pixels
they cover; frame-paced like the originals' vblank waits, runaway-guarded;
*watch item: first in-game fireball/cone cast*), `getkey` (blocks on
SYS_KEYDOWN with the host pump running, per INTRFACE.C),
`init_sound`/`shutdown_sound` (arm/disarm the play gate), and **`pixel_fade`
is now a real dissolve**: Graphics keeps a copy of the last PRESENTED frame
and dissolves from it to the current surface content over N presented frames
— verified with a frame-dump curve showing the exact 30-step ramp to black on
a level transition plus the outtake's fade-in. (`THIRDEYE_DUMP` moved into
`Graphics::update()` so mid-CALL presents — fades, particles — are captured.)

Golden-path regression (title → restore → walk): **zero stubs remain** —
verified with `THIRDEYE_TIMING=1` first-call trace over a 15s autowalk run.

**Spell cast** (2026-07-03) — the runtime infrastructure a spell walks
through is all present (M:54 → M:319 → M:340; every `C:` call it makes,
`dice`/`rnd`/`spell_request`/`spell_list`/`do_dots`/`do_ice`/`magic_field`,
implemented). The `THIRDEYE_SPELL` synthetic harness in [runtime/event.cpp](../apps/thirdeye/runtime/event.cpp)
reaches `cast_mage_spell` and fires the correct missile count (castlvl 9
÷ 2 = 5 for magic missile) but doesn't land damage because it passes
`sp=0` — that field is the memorized-slot dispatch key. The proper
end-to-end path is now unblocked: since the Camp UI is native SOP (see
Phase 6c correction below), Memorize Spells actually works in-engine — a
manual C-Enter-Enter-pick-spell in a safe cell followed by the game's own
cast UI is the golden verification and needs no code, just a play
session. Melee-damage-and-die is already green (`THIRDEYE_ATTACK`).

NB the harness key script changed — with a save present the title menu
defaults to "Continue the Quest", so it's `THIRDEYE_AUTOWALK=0d,0d,…` now
(the old leading `5000` walks you into "Gather a New Party"/chargen).

### ✅ Text pipeline + level-object restore made faithful (2026-07-02, PM)

Three user-visible bugs, one root theme — we had approximated subsystems the
originals define exactly:

- **Message log drifted right + swallowed spaces ("Sir Mikealremarks…").**
  `Graphics::printText` was a from-intuition word-wrapper: it collapsed
  runs of spaces (log strings like 1252 *" remarks, …"* begin with a
  meaningful leading space), centered against the whole window, and CLIPPED
  at the bottom — leaving the cursor stuck mid-line, so every subsequent log
  message continued from there (the mid-window green text). Rewritten as a
  faithful port of `GIL2VFXA_print_buffer` + `GIL2VFX_cout`
  (arun/asm/GIL2VFXA.ASM): wrap is computed against the space remaining
  from the *current cursor*, wrap boundaries eat only the boundary spaces,
  justify offsets htab per line (center = remaining width, right = x2−w+1),
  and `\n` on the bottom line **scrolls the window content up** (cursor
  stays) — which is what keeps the log left-flowing, since every log string
  starts with `\n`. Also: `%0`–`%9` in format strings are vsprint COLOUR
  CODES, not printf — formatSop now drops them (plus gained `%x`/`%c`/`%a`).
- **Beige text log after the Florn cutscene.** The "clear to black under
  Backdrop 190" fix existed *only as a comment* — the code never cleared, so
  the outtake's `wipe_window(96, 20)` light-brown fill showed through the
  Backdrop's transparent log interior (and got re-captured into the
  text-restore backdrop). Now actually clears (runtime/graphics.cpp
  draw_bitmap).
- **Axe wouldn't chop the movable trees.** `LVLnn.TMP` is just save_range's
  CDESC stream (same as ITEMS.TMP), and instance statics are meant to be
  restored VERBATIM (`restore_range`). Our loader instead scanned for
  record signatures and hand-invented fields — critically it read the byte
  at statics+5 (`B:region`, the wall-side/quadrant) as `W:decflags`, whose
  real home is statics+10. The movable trees store decflags `0x0010` — and
  bit 0x10 is the *"can be cut"* flag `movable trees.attacked` checks before
  chopping — so every axe blow hit the "unusually resistant" branch.
  (The old `0x0F → 0` mapping was itself a patch over reading the wrong
  field: 0x0F was the region sentinel, never a decflags value.) The loader
  ([savegame/lvl_tmp.cpp](../apps/thirdeye/savegame/lvl_tmp.cpp)) now walks
  the CDESC records exactly and memcpy's the statics verbatim, then relinks
  (place, cell chains) as before. Fallout fixed for free: monster HP is
  stored as −1 and rolled by `NPC.restore` via `C:dice` (the old loader
  set HP from the CDESC *size* field — the troll's "HP 30" matched its
  record being 30 bytes, pure coincidence), NPCstat re-seeds in `restore`
  via `report`, movable trees keep their real `B:direction`, and LVL03 now
  places 331 objects (was 231 with the heuristic scan). Verified live:
  party at (21,22) facing the tree — sword swing prints the "stout axe"
  remark, axe swings chop (`sound_effect(54)` ×3, state 0→1→2, tree
  disables on the third), 105/105 tests green.
  *Watch-item:* wall features (doors/levers) previously rendered with
  wall-side-as-state; their true decflags now flow through — LVL01
  mausoleum doors should be eyeballed on the next play session.

  **Follow-up hang fix (same day):** the verbatim copy surfaced that some
  saved records carry LIVE chain pointers (the QSP monsters at (12,14) ship
  with `next=1773` / `prev=1772` — save_range dumped them mid-chain). Keeping
  the file's `W:prev` while our loader rebuilt `W:next` in its own insertion
  order let the SOP's unlink (`prev.next = my.next`) write `1772.next = 1772`
  the first time a monster left a shared cell — a self-cycle every chain
  walker (draw/AI) then spun on forever. With graphics the VM step budget is
  deliberately unlimited (the SOP main loop legitimately never returns), so
  this presented as a macOS beachball on strafe near the treeline. Loader now
  clears `W:next`/`W:prev` before relinking and keeps the cell chains
  properly doubly linked (old head's `prev` ← new head). Repro (walk to the
  treeline, strafe) runs clean; axe-chop + 105 tests still green.

  **CodeRabbit round (2026-07-02):** PR #53 review triage — 9 real, 3 pushed
  back with the original sources as evidence (`ascnum` char-literal sign,
  `solid_bar_graph`'s raw `val*3 >= max` threshold, `load_string`'s
  byte-identical copy are all faithful to `arun/src`). Real fixes landed:
  text scroll condition corrected to GIL2VFXA.ASM's exact-fit semantics
  (`vtab + 2*charH - 1 > y1`), a wrap-scan progress guard (a boundary space
  at an edge-parked cursor could read `text[-1]` / stall), `%X` uppercase,
  endian-safe `visible_bitmap_rect` writes, `strnlen` on save-slot names,
  wider try/catch in `saveRange`. Bigger catch while verifying the "double
  reload" comment against `EYE.C`/`RTOBJECT.C`: our `change_level` neither
  saved the departing level (doors/kills resurrected on return) nor did the
  loader destroy stale objects (`restore_range` tears down live slots — dead
  file slots included — before restoring; ours left the old level's entities
  alive and event-registered as ghosts, and double-restored monsters per
  transition). `change_level` now `save_range`s the old level to `LVLoo.TMP`
  and defers the reload to the "init level" hook; `loadLevelObjects` destroys
  before recreating (MSG_DESTROY cancels the SOP's notify requests).

  **Level transitions verified end-to-end (2026-07-02, evening):** graveyard →
  mausoleum → graveyard round-trips headlessly and live. Three more loader
  fixes fell out of the verification (matched against `RTOBJECT.C`
  `restore_range` + the `teleporters` class disassembly):
  - **MSG_RESTORE goes to EVERY restored object, not just monsters** —
    `restore_range` does `RT_execute(index, MSG_RESTORE)` unconditionally
    (restoring=1 at all level-load call sites in EYE.C). `teleporters.restore`
    is where the step-on trigger arms (`C:notify(THIS, trigger, event 32,
    y<<16|x)`); with monsters-only, level exits never fired. The loader now
    uses a faithful `ObjectSystem::createInstance` (bare `create_SOP_instance`:
    alloc + MSG_CREATE, no restore fallback) and sends M:2 to every placed
    object.
  - **The restore happens INSIDE `change_level`** (as in EYE.C), not deferred
    to the "init level" hook — the teleporter path SENDs area "enter level"
    with no "init level" in between, so the deferral left the new level empty.
  - **`loadLevelObjects` clears all 3 `lvlobj` planes first** (init_level's
    wipe) — nothing else does it on a mid-game transition, and a stale cell
    would alias a same-numbered slot that belongs to a different object on the
    new level.

  Also fixed: the compass snapshot re-stamp painted over the transition
  outtake's story text — `restoreCompass()` now suspends when a fill covers
  the whole compass rect and re-arms on the next `snapshotCompass()`
  (graphics/graphics.cpp). Verified in frames: outtake text flows full-width,
  compass back in place after arrival. Persistence proven along the way: the
  mists chasing the party round-trip through `LVLoo.TMP` (on the next boot of
  the re-saved file they were parked mid-chase, blocking the path). Headless
  harness gotchas discovered: repeated `kill -INT` builds macOS crash history
  and AppKit then blocks the next launch on a "reopen windows?" modal
  (`defaults write com.eob3.thirdeye ApplePersistenceIgnoreState YES` fixes
  it); `THIRDEYE_DUMP` substitutes only a plain `%d` (a `%03d` writes one
  literal file, overwritten every present).

### 🚧 Save / load (the savegame format)

*Done:*
- **`LVLnn.TMP` level-object format** RE'd + loaded
  ([savegame/lvl_tmp.cpp](../apps/thirdeye/savegame/lvl_tmp.cpp), see
  [eob3_savegame_format.md](eob3_savegame_format.md) §3).
- **`ITEMS.TMP` character record** documented + parsed into a structured
  [`ItemsTmp`](../apps/thirdeye/savegame/items_tmp.hpp) (party position + per-PC
  name/HP/XP/stats/equipment-slot item-object ids). Unit-tested.
- **`SAVEGAME.DIR` slot list** parsed ([`savegame_dir.hpp`](../apps/thirdeye/savegame/savegame_dir.hpp))
  for the load/save menu.
- **`resume_level` integration** (gated on `THIRDEYE_CONTINUE=1`): seeds the
  party at the saved position (level 3 @ (7,24) facing E for Quick Start),
  **recreates the saved item objects** from §2.3 (445 items in the Quick Start
  save — daggers, holy keys, spellbooks, …), **patches each chargen-created
  PC's name/HP/XP/abilities** from the parsed records, and copies the saved
  `equip[]` into `PC.W:inventory[14..25]`. Verified end-to-end: PC 32 ends up
  with `R=993 L=992` (Sir Mikeal's saved weapons) instead of `R=996 L=-1`
  (chargen's). Strength bonuses + real weapons together drop a troll 30→18 HP
  in 8 seconds.
- **Live item-object stream (§2.3)** RE'd + parsed
  ([`parseItemStream`](../apps/thirdeye/savegame/items_tmp.hpp)): native CDESC
  records `(u16 slot, u32 name, u16 size, size statics)` — the same
  `save_range` format as the rest of the file. **(Corrected 2026-07-19:** the
  original reading here was a 4-byte `(u16 id, u16 class, N statics, 4
  trailer)` frame, which read every static block 4 bytes early and dropped the
  placement fields — hiding every initial floor item in the game. See §2.3 in
  [eob3_savegame_format.md](eob3_savegame_format.md).) Unit-tested + verified
  against the Quick Start save.
- **Title-menu → Restore-Game picker → in-game flow** wired end-to-end. The
  SOP's `savegame_title`/`string_compare`/`restore_items`/`restore_level_objects`
  runtime functions now do the real work (read `SAVEGAME.DIR`, expose slot
  names through a runtime-owned dynamic-statics buffer routed via
  `ObjectSystem::setDynamicStaticsHook`, copy `ITEMS_NN.BIN` → `ITEMS.TMP` /
  `LVLnn_NN.BIN` → `LVLnn.TMP`, then self-dispatch into `resume_level` to
  materialize live PCs + items). The picker now renders correctly thanks to
  two GIL2VFX fixes: `set_x1/x2/y1/y2` mutate the bound subwindow's edges
  (`EventSystem::setWindowEdge` + `Graphics::updateTextWindowsFor`), and
  `text_window` is a pure rebind — the explicit `wipe_window` runtime
  function handles clearing (matches `GIL2VFX_select_text_window` vs
  `GIL2VFX_wipe_window` separation). Mouse-click routing through `mouse_XY()`
  wired against `EventSystem::pointX/pointY`.

- **In-game save — landed (2026-07-02).** The serializer core is
  [`savegame::saveRange`](../apps/thirdeye/savegame/lvl_tmp.cpp): RTOBJECT.C
  `save_range`'s SF_BIN format (0x1A byte, then per slot a CDESC
  `{u16 slot, u32 class, u16 size}` + raw instance statics; dead slot =
  `0xFFFFFFFF/0`), verified byte-compatible against the shipped `*_00.BIN`
  files — a live-game LVL03 dump comes out at **exactly** the shipped 13436 B
  and ITEMS within 7 B (the live object population differs slightly from a
  fresh save, as it should). Runtime functions wired in
  [runtime/eye.cpp](../apps/thirdeye/runtime/eye.cpp): `save_game(slot, lvl)`
  (live items → `ITEMS_nn.BIN`, live level objs → `LVLxx_nn.BIN`, other
  levels' `.TMP` copied over), `suspend_game(lvl)` (live →
  `ITEMS.TMP`/`LVLxx.TMP`),
  `read_save_directory` (SAVEGAME.DIR → the slot-name buffer; the original's
  `savegame_dir[]`), `write_save_directory` (buffer → 12 CRLF lines + 0x1A,
  byte-format-identical to the shipped file). `savegame_title` no longer
  re-reads disk once its slot buffer is populated, so the save UI's
  `copy_string`-rename → `write_save_directory` flow can't be clobbered by a
  picker re-render. `resume_items` is a deliberate no-op (our `resume_level`
  path rebuilds PCs + items already). Checks: `SaveRange_Test` unit test +
  `THIRDEYE_SAVETEST=N` one-shot (runtime/event.cpp) that dumps live state
  through the serializer mid-game.

- **Slot-mapping fix (2026-07-02):** slot numbers map to file suffixes
  VERBATIM (EYE.C `set_save_slotnum`) — picker slot 1 ("Quick Start Party" =
  SAVEGAME.DIR line 1) = `ITEMS_01.BIN`/`LVLxx_01.BIN`. **Slot 0 is the
  new-game *initial* state** ("Read initial (slot 0) items" in EYE.C;
  `save_game` abends on slot 0), and a real DOS new-game legitimately
  overwrites it — on this install `ITEMS_00.BIN` holds an old DOS-era rolled
  party ("THELMA"), which is how the bug surfaced: our previous `slot-1`
  mapping restored slot 0's THELMA party instead of the QSP.
  `restore_items`/`restore_level_objects`/`save_game` now use the slot
  verbatim; verified end-to-end (restore → "Sir Mikeal" HP 97/97 STR 18/94
  equip 994/993/992; all 105 unit tests green incl. `ParsesQuickStartParty`).

*Remaining:*
- ~~**Drop the `THIRDEYE_CONTINUE` gate.**~~ ✅ done. `bootObject` auto-detects:
  `--skip-menu` + `SAVEGAME/ITEMS.TMP` present → boot mode `CINE`; missing →
  `CHGN`. New `--chargen` flag forces `CHGN` even when a save exists
  (start-a-new-game). `resume_level` matches: file-existence check replaces
  the env var, and it can now `createProgram` PCs from scratch on the CINE
  path (defensive today since the SOP's CINE branch still runs chargen-
  transfer; live the moment we bypass that).
- ~~`write_initial_tempfiles` (the new-game write).~~ ✅ done. Full
  serializer in [runtime/eye.cpp](../apps/thirdeye/runtime/eye.cpp) `if (fn ==
  "write_initial_tempfiles")`: seeds `ITEMS.TMP` bytes 0..676 from
  `ITEMS_00.BIN` scaffold, writes all 10 PC records @677 field-by-field from
  live PC statics (name/race/classes/portrait/PCstat/alignment/levels[3]/
  lost_levels[3]/lost_hp/hpts/hmax/hbon/food/xp[3]/stats/spell_cnt/spell_stat),
  appends a fresh item stream by walking every live subclass-of-`items`
  entity, and copies `LVLnn_00.BIN → LVLnn.TMP` for all 14 levels via
  `restoreLevels(dir, 0)`. Chargen-transfer's new-game save now round-trips
  through `resume_level`.
- ~~The 4-byte trailer in each §2.3 record~~ ❌ **there is no trailer**
  (corrected 2026-07-19). The "trailer" was an artifact of misreading the
  8-byte CDESC header as 4 bytes: the last 4 bytes of each item's real static
  block were being read as a standalone trailer, and "byte 3 = magical bonus"
  was just the block's final byte. The static block carries `arms.B:bonus` and
  `items.W:itmflags` at their true offsets, so the bonus/itmflags side-channel
  patch-ups are gone; `ItemRecord::magicalBonus()` and `THIRDEYE_DUMP_TRAILERS`
  were removed with the fix. Placement lives in the entity block:
  `items.W:place@0` (holder object id, or -1 for a dungeon floor with
  B:x/B:y/B:lvl set).
- ~~Per-PC unmapped fields~~ ✅ done: race/classes/portrait/PCstat/alignment/
  levels[3]/lost_levels[3]/lost_hp/hbon/xp[1..2] now all parsed and patched.
  Remaining unmapped: memorized/known spells (the big B:spell_cnt/spell_stat
  arrays at PC offset 209/409 — likely fill most of the +216..+626 tail), AC,
  L:magiceffects, B:sparkle. None blocking gameplay today.

Reference data in `../data/SAVEGAME/`: the **"Quick Start Party"** save —
*not* the user's; this is a pre-rolled save game that Westwood/SSI shipped
with every EOB3 install so a new player can hit "Continue the Quest" on
the title menu and play immediately without rolling a party. Confirmed
against a live install at archive.org/details/msdos_Eye_of_the_Beholder_III_-_Assault_on_Myth_Drannor_1993
(menu screenshot: "1 Quick Start Party"; after restore: Sir Mikeal,
Stonebeard, Salina, Lady Reeya on level 3 at the Graveyard, message log
shows `Restoring "Quick Start Party" ... Done.`). The other shipped party
— Bob/Carol/Ted/Alice in `CHARGEN/CREATE.SAV` — is a developer sample
party left behind in the chargen-transfer file; not advertised to the
player, but consumed by `chargen-transfer` if "Begin a New Quest" is
chosen without rolling first.

Convention: **`.TMP` = live state**, **`_00/_01.BIN` = the two saved slots**
(load = copy `_NN.BIN`→`.TMP`, then `resume_*` reads `.TMP`). The EOB1/2
character record matches our `CREATE.SAV` RE
(https://moddingwiki.shikadi.net/wiki/Eye_of_the_Beholder_Save_Game_Format);
EOB3's item/level `.TMP` formats are not on the wiki. EOB3 had a 16-bit-
pointer save bug (daesop's `/eob3menupatch` works around it for the
original; our native VM sidesteps it).

### ✅ EOB2 → EOB3 party import (2026-07-13)

"Summon the Heroes of Darkmoon" (menu option 3): `start`→`LBL_521` creates `xfer` (1380) +
SENDs "transfer from Eye II" (M:14). Working end-to-end against a real GOG EOB2 save
(EOBDATA0.SAV → PERICLES/"STUMPY"/WOLFSPIRIT/LAURANN in-game with portraits, HP, gear).

Two runtime pieces landed:
- **`open_transfer_file` return convention fixed** — per `arun/src/EYE.H:199` it returns a
  `void *` handle (non-zero = success). We returned 0-on-success; M:16 never checked, but
  M:14 gates on it (`staticVar0 == 0` → "run CHARCOPY" dialog), so the bug was latent
  until this path.
- **CHARCOPY.EXE stand-in** — `THIRDEYE_EOB2_SAVE=<path>` copies the EOB2 save to
  `TRANSFER.SAV` beside the .RES at boot (the real utility is just drive-walk + DOS
  `copy` + `ren temptemp.sav transfer.sav`, no format conversion). No new reader was
  needed: **CREATE.SAV is EOB2-format** (CHGEN writes one), same 345-byte PC records at
  0x16, same item array at 0x894 — which is why M:14 and M:16 share the M:15 "transfer"
  handler, and why `TransferState` already parses it.

**EOB1 saves are rejected by design**, matching the original CHARCOPY ("Eye of the
Beholder III won't work with Eye I save games"): EOB1's layout differs (243-byte records
at 0x02, no save-name header). The supported path is EOB1 → EOB2's own import → EOB3.
The shim sniffs the header (save-valid flag at 0x14-0x15) and refuses non-EOB2 files.
Full CHARCOPY artifact dump:
[`../eob3_research/CHARCOPY/`](../eob3_research/CHARCOPY/README.md).

Still open (nice-to-have): a file-picker UI instead of the env var.

### ⏳ Other stand-ins
- ~~Level objects load the *saved* `LVLnn.TMP` (not the new-game source).~~ ✅
  covered by `write_initial_tempfiles` above — new-game boot writes fresh
  `LVL??.TMP` from `LVL??_00.BIN` before `loadLevelObjects` reads them.
- ~~Object link-chains/decflags are simplified.~~ ✅ real: features/decorations
  chain properly in plane 0 (single per cell), monsters prepend to a real
  linked list in plane 2 (`W:next@6` with `-1` terminator, up to 4 per cell,
  entities.remove walks `W:prev/W:next` on death). `W:decflags` for floor
  features maps `+14 == 0x0F → 0` so render CASEs land on state 0 (see the
  Level objects fix note above); wall-side flags (1/2/4/8) pass through.
- Party position carry-over on `change_level` — the SOP handles it (kernel
  updates `B:party_x/y/lvl`); our `change_level` just refreshes
  `loadLevelObjects`, no separate C++ carry-over needed.

## Phase 4 — Dungeon Hack

- ✅ **OPEN.RES / HACK.RES flow.** Filename-based auto-detect routes
  OPEN.RES→`opening`, HACK.RES→`phase-one`. `bootObject` interprets
  HACK.BAT errorlevels (0/1/2/3) so phase-one loops the title and can
  chain to phase-two. `THIRDEYE_BOOT=<name>` overrides the boot object
  for debugging. See [progress.md](progress.md) DH section.
- 🚧 **DH-only runtime functions.** Trivial helpers landed (`page_flip`,
  `sequence_playing`, `touch`, `pause`, `seed_random`, `roll_chance`,
  `randomize_array`, `long2hex`, `xmsallocated`, `text_background`,
  `lock_resource`/`unlock_resource`, `printer_on_line`); DH-variant
  3-arg `notify` shim landed; file I/O primitives (`open_file`,
  `close_file`, `read_number_from_file`, `read_array_from_file`) read
  from `<dh_root>/SAVEGAME/` with DOS-backslash path resolution;
  dungeon loaders (`load_level_map`, `load_visibility`,
  `open_feature_file`, `get_feature_record`, `close_feature_file`)
  read chunks per the format spec in
  [dungeon_hack_maze.md](dungeon_hack_maze.md) or zero-fill when files
  are missing. Only 5 stubs left, all pure-renderer: `init_viewspace`,
  `build_clipping`, `copy_window`, `draw_walls`, `Transition`. See
  [`apps/thirdeye/runtime/dh.cpp`](../apps/thirdeye/runtime/dh.cpp).
- 🚧 **MAZE.EXE (dungeon generator) integration.** MAZE is a small
  Borland C++ utility that writes `LEVELS.DAT` / `FEA%02d.DAT` /
  `ITEMS.DAT` into `savegame/` per fresh game — full RE writeup in
  [dungeon_hack_maze.md](dungeon_hack_maze.md). Two paths: bootstrap
  by running MAZE under DOSBox once, or reimplement natively.
- ✅ **Offscreen page compositing.** DH draws each HUD panel into its own
  page at page-local (0,0) then `copy_window`s it to a screen rect;
  `assign_window` now marks pages offscreen, `draw_bitmap` redirects into
  the page surface, and `copy_window` blits to the destination origin.
  Before this every DH panel piled up at screen (0,0). EOB3 has no
  `copy_window` so its flattened path is untouched (verified
  pixel-identical).
- ✅ **DH palette-region map.** DH's bases are fixed=0x00 / walls=0xF0 /
  floor=0xE0, not EOB3's 0x00/0xB0/0xC0/0xE0. With EOB3's table the
  wallset's 0xF6..0xFD indices hit never-loaded (black) DAC entries — the
  reason the dungeon view rendered solid black. `kFirstColorDH`, gated on
  `gDungeonHack`; `THIRDEYE_PALBASE` overrides for bring-up.
- ✅ **3D wall rendering.** `draw_walls` is a faithful port of AESOP.EXE's
  own routine (`1f36:0785`): 25 wall faces over 18 map cells in 4 depth
  bands, geometry tables lifted verbatim from the binary. Renders a real
  perspective dungeon view — see
  [screenshots/dh_wall_render_corridor.png](screenshots/dh_wall_render_corridor.png).
  Validated data-driven: a synthetic map with one wall ahead draws exactly
  1 face, a corridor draws 12, an all-walls map 25.
  How the tables were located and validated:
  [`../../dh_research/AESOP/README.md`](../../dh_research/AESOP/README.md).
- ✅ **`init_viewspace` + `build_clipping`.** Both ported verbatim from
  `AESOP.EXE` (`1f36:040f` / `1f36:05f4`) with an occlusion table lifted
  from `DS:0x1117`. Occlusion is now real: cells outside the view cone
  don't render, and blocked cells cull correctly with the SOP's own
  `notblocks` results.
- ✅ **Text/message-bar erase** now uses backdrop-restore in the DH HUD
  (gated on `gDungeonHack && draw_bitmap(1, 59, …)` — DH's HUD-Backdrop
  equivalent of EOB3's 190). Previously flat-fill sampled a stray pixel
  and the whole message bar went white on movement.
- ✅ **HUD clip.** `draw_walls` clips every blit to the view rect —
  panels legitimately positioned outside the view (e.g. face 20 at x=34
  spanning a 129-wide panel while view starts at 138) no longer paint
  over the inventory column or the arch.
- ⏳ `.TBL` support (daesop `/create_tbl` generates these).

## Phase 5 — Thirdeye-original features (post-EOB3)

Enhancements not in the original — opt-in, kept separate from the SOP-driven core so they
never change game behaviour.

- ✅ **Qt-based launcher** — landed in [`apps/launcher/`](../apps/launcher/) (Qt-based, macOS/Linux/Windows
  packaging). Detects existing EOB3 installs, configures settings, and downloads +
  installs the game from the archive.org mirror (in-tree ARJ decoder, no shell-out).
  History: initial (518688e) → polish (7167ea3) → download+install (5323928).
  Original plan below is kept for context of what "done" means:
  - **Detects an existing EOB3 install.** Looks in common spots (next to the binary, a few
    well-known paths) for `EYE.RES` + `CHARGEN/` + `SAVEGAME/`. If found, just launches
    thirdeye with the right `--game-data` pointing at it.
  - **Configures settings.** Resolution scale, sound on/off, controls, save-game directory,
    log verbosity. Persists to a `thirdeye.cfg` next to the binary or in the user's app-data
    dir.
  - **Offers to download the game if missing.** Pulls the original DOS disk images from
    [archive.org/download/eye-of-the-beholder-3](https://archive.org/download/eye-of-the-beholder-3)
    (CC-licensed abandonware mirror). The zips contain DOS disk dumps with `.ARJ`-packed
    contents — the launcher needs a built-in ARJ unpacker (ARJ is a well-documented format;
    a small in-tree decoder is cleanest, no shell-out to a system `arj` binary the user
    probably doesn't have).
  - **Installs the unpacked game** into a thirdeye-owned data dir (NOT inside the repo —
    Thirdeye ships no assets and that policy is non-negotiable).
  - **Qt for the UI** (cross-platform; matches our Linux/macOS/Windows targets). Stays in a
    separate `apps/launcher/` so the CLI runtime has zero Qt dependency.

  Phasing: (a) detection + settings UI on a known install (immediate value); (b) ARJ unpacker
  + install flow; (c) download/progress UI + checksums. Each phase ships independently.

- ✅ **Automapper** — landed in [`apps/thirdeye/automap.{hpp,cpp}`](../apps/thirdeye/automap.hpp).
  Toggle with **M**. Per-level 32×32 visited bitset + notable-cell bitmap, persisted as
  `MAPS.TMP` / `MAPS_nn.BIN` alongside the save. Party FOV marks cells as walked;
  interactables/monsters flag cells as notable. Rendered as an overlay that suspends
  Graphics backdrop tracking so it doesn't corrupt the SOP's text-restore snapshot.
  History: initial (b68f163) → review fixes (atomic MAPS load, sidecar gating, font probe once, 381371a).

## Phase 6 — Replace CHGEN.EXE with a C++20 chargen 🚧

Goal: drop the dependency on the original `CHARGEN/CHGEN.EXE`, run all
character generation natively.

**Correction (2026-07-03):** the earlier assumption that CHGEN.EXE hosts
the in-game Camp UI was **wrong**. The Camp UI lives entirely inside
EYE.RES bytecode — class 1385 `camp` exports the full menu (M:272
toggle_camp_menu, M:294 rest, M:302 init_spell_menu, M:306 build_spell_array,
M:295 spell_menu_selected, M:281 spell_menu_entry_selected, M:296
scribe_scroll, M:298 save_option_selected, M:299 load_option_selected,
M:308 sleep, M:309 show_time, M:297 drop_character, M:307 can_heal, M:285
interrupt_resting). Pressing `C` in-game already opens the full camp screen
(Rest / Pray for Spells / Memorize Spells / Scribe Scroll / Drop Companion /
Break Camp / Save Game / Restore Game / Turn Sounds On / Show Bar Graphs /
Exit Game) with the party portraits + HP intact; Rest correctly refuses
in a monster-adjacent cell with the shipped *"This doesn't seem like such a
great place to rest,"* line; Memorize prompts *"Select the mage who will
memorize spells"* with the mage's portrait circled. No C++ Camp UI work
needed — every runtime `C:` call it uses is already implemented (we saw
zero `[stub]` traces on the camp path). That leaves this phase focused
purely on chargen.

Full RE artifact dump + 3-phase plan: [`../eob3_research/CHGEN/`](../eob3_research/CHGEN/README.md).
Quick map:

| | Goal | Status |
|---|---|---|
| **Phase 6a — File-format readers** | ITEM.DAT, ITEMTYPE.DAT, CHARPICS.BMP, EOSPREFS.DAT. Reuse thirdeye's existing CPS/palette/font decoders for the rest. | ✅ ITEM.DAT, ITEMTYPE.DAT, CHARPICS.BMP readers landed in [`apps/thirdeye/chargen/`](../apps/thirdeye/chargen/) with unit + bundled-data tests; CHARPICS also wired as a third variant in the `Bitmap` decoder (auto-detected by file-size header) so all 89 portraits load. EOSPREFS.DAT stubbed pending need. |
| **Phase 6b — Chargen UI** | Race / class / alignment / stat-roll / portrait / starting-equipment flow. Writes `CREATE.SAV` in our already-RE'd format (so the existing `xfer` transfer keeps working). | ✅ Landed in [`apps/thirdeye/chargen/screen.cpp`](../apps/thirdeye/chargen/screen.cpp) (~2100 LOC): `Step` state machine covers EntryScreen → PickRace → PickClass → PickAlignment → PickPortrait → EnterName → ShowStats / ModifyStats; `writeCreateSav` overlays live-rolled fields (race/class/alignment/portrait/name/stats) onto the bundled CREATE.SAV template and writes a class-appropriate equipment kit. The existing `xfer` transfer path then ingests it verbatim. Race×class kit is validated by `validateClassKits` at boot. |
| **Phase 6c — Camp UI in thirdeye** | ~~Self-host Rest / Memorize / Pray / Scribe / Save / Load.~~ Not applicable — the Camp UI is native SOP (class 1385 in EYE.RES), not hosted by CHGEN.EXE. See the correction note above. | ✅ verified 2026-07-03 (headless `C`-key path renders the full menu; Rest gates correctly; Memorize prompts the mage select) |

What's already done that this phase doesn't need to redo:
- `CREATE.SAV` writer format ✅ (we read it; see [docs/create_sav_and_item_format.md](create_sav_and_item_format.md))
- EOB1-type → EOB3-class map (`table123`) ✅ (in [savegame/transfer.cpp](../apps/thirdeye/savegame/transfer.cpp))
- Typed-equipment-slot placement ✅ ([equipment_slots.md](equipment_slots.md))
- CPS / palette / font decoders ✅ (in [apps/thirdeye/graphics/](../apps/thirdeye/graphics/))

What's new RE work:
- ITEM.DAT — ✅ header decoded, 14-byte record layout known, reader landed + tested.
- ITEMTYPE.DAT — ✅ record stride known (16 B × 64), reader landed; per-field semantics
  partly guessed (fields 6/7/8 still notional — confirm via CHGEN.EXE xrefs when Phase 6b
  needs them).
- CHARPICS.BMP — ✅ proprietary header + offset table + per-row RLE decoded. 89 portraits
  load through the unified `Bitmap` interface. Horizontal alignment is centered (statistically
  validated against 70/75 non-empty portrait silhouettes); DOSBox-X dynamic-analysis as the
  next refinement path if needed. Full RE notes: [`../eob3_research/CHGEN/decompiled/README.md`](../eob3_research/CHGEN/decompiled/README.md).
- Chargen UI state machine — DOSBox screenshot reference walkthrough captured in
  [`../eob3_research/CHGEN/dosbox_screenshots/`](../eob3_research/CHGEN/dosbox_screenshots/)
  (9 screens: slot picker → race → class → alignment → stat roll → portrait pick → stat modify
  → name entry → 4-PC complete). Maps the flow Phase 6b needs to reproduce.
- AD&D 2e ruleset (race × class limits, stat-roll rules) — mostly documented externally; mechanical work.

## Phase 7 — Replace other DOS binaries (longer-term)

Same template as Phase 6 but for the rest of the install:

| EXE | What | Notes |
|---|---|---|
| `CHARCOPY.EXE` | EOB1/2 save staging | RE done ([../eob3_research/CHARCOPY/](../eob3_research/CHARCOPY/README.md)); replace by reading EOB1/2 saves directly in M:14 |
| `CINE.EXE` | Intro/outro cinematic player | Replace with GFF player thirdeye already has |
| `SOUND.EXE` | Sound driver shim | Thirdeye uses SDL_mixer + WildMIDI; not needed |
| `AESOP.EXE` / `INTERP.EXE` | The SOP interpreter | **Already replaced** by thirdeye's VM |
| `DOS4GW.EXE` | Watcom DOS extender | Only used by AESOP/32 paths; not needed |
| `CHARGEN/MGA.OVL` / `XGA.OVL` | VGA mode-switching overlays | Not needed (SDL handles modes) |

---

## Per-subsystem to-do list

In dependency order, what `thirdeye` still needs to be a full engine (the runtime-function
catalog is the biggest single chunk):

1. **SOP VM.** ✅ Done (see [architecture.md](architecture.md)).
2. **Object & message system.** ✅ Done.
3. **Runtime-function library** — the `CALL` targets, the engine's native API surface. The
   largest chunk: graphics/dungeon rendering, sound, mouse/keyboard input, file/savegame I/O,
   string/math helpers, event posting. Reference: `EYE.C` (EOB3 set), `RTCODE.C` (generic
   helpers), `GRAPHICS.C`, `MOUSE.C`, `EVENT.C`, `SOUND32.C`. Many map onto subsystems
   thirdeye already has (graphics/sound/res) — wire them up.
4. **Resource manager integration.** The VM lazily loads/locks/unlocks resources by handle
   (`RTR_*` in `RTRES.C`). `Resource` covers reading; it needs a handle/lock model for the
   running game.
5. **Event queue + main loop.** ✅ Done.
6. **Save / load.** See above.
7. **Dungeon Hack support.** See Phase 4.

**Opcodes we don't fully understand yet:** the 88 opcodes themselves are documented, but
several *runtime functions* have only guessed semantics (especially DH-only ones) — these
need verification against the original `AESOP.EXE`/`INTERP.EXE` behaviour or the docs.
