# Roadmap

What's done, what's in progress, what's next. See [progress.md](progress.md) for the detailed
narrative on completed work.

## Phase 0 — intro + title menu render ✅

## Phase 1 — VM core ✅ (essentially done)

- ✅ Stack VM for all 88 opcodes except `BRK` (branches/arith/logic/constants/auto/`JSR`/`RTS`/
  `CASE`/`RCRS`/`CALL`/`SEND`/`PASS`/statics/extern/table loads/effective-addr/`AIM`/`AIS`/
  `SXAS`/`SOLE`). GTest-covered; CI on Linux/macOS/Windows.
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
- 🚧 EOB3 runtime-function set (`EYE.C`/`RTCODE.C`/`INTRFACE.C`) wired. Still stubbed: sound,
  most of save/load, much of `EYE.C`.
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
- ✅ **Items & inventory (display)** — equipment screen renders the paper-doll + char-gen gear;
  slot regions register so interaction routes through the proven click path.

### 🚧 Combat — RE'd, monsters load, attack chain wired through to-hit; hit doesn't land yet

**What works** (`THIRDEYE_TESTMON=1`): monsters render (sword wraiths in colour), 4-per-cell,
re-face the party as you move. The attack flow is end-to-end driven — kernel `auto-attack`
(M:208, gated on `B:auto_button`) walks `W:player[]` and SENDs each PC `use/attack request`
(M:163) → `use` → `acquire NPC target` → `roll to hit` (M:71). Fires 100+ times correctly
targeting the wraith.

**Blockers:**
1. **`dice`/`rnd` were unimplemented** (stub → 0) — every combat roll was 0 → no hit ever.
   ✅ Now real (`engine.cpp`, shared PRNG). Also unblocks monster AI / `bounce` / anything
   random.
2. **Sword wraith is bit-64 incorporeal undead** — `roll to hit` ENDs immediately on a bit-64
   target (immune to normal weapons; needs magic/blessed). Correct EOB3 mechanic.
3. **Right-hand item trips bit 25 of `report(weapon, 1)`** — i.e. it's not a plain melee
   weapon. Points back at **auto-equip hand-slot mapping** (the sword likely landed in EOB3
   slot 18, not the right hand 16).
4. **`player to hit` (M:162)** — THAC0/level math via SEND to object index 2003 (combat-rules
   singleton, not yet disassembled).

**Precise next steps to land a kill**: disassemble + fix `player to hit` so THAC0/level math
gives a reachable target; handle magic-weapon vs bit-64. Then `roll for damage` (43) →
`take damage` (82) → `die` complete the loop. Debug: `THIRDEYE_ATTACK=1` enables + drives
auto-attack and logs wraith HP + PC hands; combat trace `[mon-msg]` covers msgs
163/77/78/71/43/82/91/85/99/107.

Then enable monster rendering by default (drop the `THIRDEYE_TESTMON` gate).

### 🚧 Save / load (the savegame format)

*Half done:*
- **`LVLnn.TMP` level-object format** RE'd + loaded (`loadLevelObjects`, see
  [eob3_savegame_format.md](eob3_savegame_format.md) §3).
- **`ITEMS.TMP` character record** documented (savegame doc §2).

*Remaining:*
- Read the party (name/HP/XP/stats/equipment) + position from a real `ITEMS.TMP` on load
  (currently the char-gen transfer supplies the party).
- `write_initial_tempfiles` (the new-game write).
- Wire it all to **"Continue the Quest"** so a save loads end-to-end.

Reference data in `../data/SAVEGAME/`: a used save **"Quick Start Party"** (named in
`SAVEGAME.DIR`) + the binary state files. Convention: **`.TMP` = live state**, **`_00/_01.BIN`
= the two saved slots** (load = copy `_NN.BIN`→`.TMP`, then `resume_*` reads `.TMP`). The
EOB1/2 character record matches our `CREATE.SAV` RE
(https://moddingwiki.shikadi.net/wiki/Eye_of_the_Beholder_Save_Game_Format); EOB3's item/level
`.TMP` formats are not on the wiki. EOB3 had a 16-bit-pointer save bug (daesop's
`/eob3menupatch` works around it for the original; our native VM sidesteps it).

### 🚧 EOB1/EOB2 → EOB3 party import

"Summon the Heroes of Darkmoon" (menu option 3): `start`→`LBL_521` creates `xfer` (1380) +
SENDs "transfer from Eye II" (M:14), which reads an EOB1/2 save and converts to EOB3's. Wire
M:14's runtime functions the same way as `convert created party` (M:16). A standalone
EOB1/2→EOB3 converter tool could share this reader.

### ⏳ Other stand-ins
- Level objects load the *saved* `LVLnn.TMP` (not the new-game source).
- Object link-chains/decflags are simplified.
- Party position carry-over on `change_level` is a stand-in.

## Phase 4 — Dungeon Hack

- Add the DH-only runtime functions (`eob3_research/ADDITIONAL_DH_RUNTIME_FUNCTIONS.TXT`).
- `.TBL` support (daesop `/create_tbl` generates these).
- `OPEN.RES`/`HACK.RES` flow.

## Phase 5 — Thirdeye-original features (post-EOB3)

Enhancements not in the original — opt-in, kept separate from the SOP-driven core so they
never change game behaviour.

- 🗺️ **Automapper (planned).** Toggle with **M**. Parchment/beige overlay showing the current
  dungeon level: cells the party has explored, party position + facing, discovered
  walls/doors/stairs. EOB3 itself has no in-game map. Sketch: track a per-level visited-cells
  bitset (mark each cell as the party enters it / as it falls in the view frustum — we already
  compute `view_X/view_Y/visible`), persist alongside the save, draw a top-down grid from
  `lvlmap` (walls/floor) + `lvlobj` (doors/stairs) clipped to visited cells, with the party
  arrow from `party_x/y/fdir`. Drawn on its own overlay surface (like the compass snapshot),
  gated so it never bleeds into the menu. Stretch: scrollable for large levels, level-to-level
  switching, fog-of-war on unexplored cells, optional note-pinning.

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
