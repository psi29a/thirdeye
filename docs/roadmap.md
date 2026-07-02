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
- 🚧 EOB3 runtime-function set (`EYE.C`/`RTCODE.C`/`INTRFACE.C`) wired. Still stubbed: sound
  (the SDL_mixer+WildMIDI pipe exists; the SOP-driven sound calls are stubs). Save/load + the
  Restore-Game picker now flow end-to-end (see Phase 3). Much of `EYE.C` (combat-edge cases,
  level transition flow) still incremental.
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
  - **Fix (2026-07-01):** the record byte `+14` is *not* raw `W:decflags` for floor
    features. QSP LVL03 stores `0x0F` for all 89 movable trees / 169 graves / map
    transitions (per doc "0x0f for floor objects"), which used to land in
    `W:decflags` verbatim → `features.get_state` returned 15 → the tree's
    `render` CASE only handles states 0..3 → tree fell through `CASE_DEFAULT`
    and never drew. Result: `impedance` correctly blocked movement at (22,22)
    on the Graveyard–Forest boundary but the party saw an "open" path and got
    "You can't go that way." with nothing visible in front. Loader now maps
    `+14 == 0x0F → decflags = 0` while wall-side values (1/2/4/8 for doors) go
    through unchanged, so LVL01 mausoleum doors/levers keep their orientation.
    ([savegame/lvl_tmp.cpp](../apps/thirdeye/savegame/lvl_tmp.cpp) 76.)
- ✅ **Items & inventory (display)** — equipment screen renders the paper-doll + char-gen gear;
  slot regions register so interaction routes through the proven click path.

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

**Still stubbed** (visible in the `[stub]` log): the sound set (`init_sound`,
`load_sound_block`, `set_sound_status`, `sound_effect`, `load_music`,
`unload_music`, `play_sequence`, `shutdown_sound` — deferred on purpose),
`do_dots`/`do_ice` (fireball/cone-of-cold particle animations — blocking
GIL2VFX loops with per-pixel occlusion reads; purely visual, damage happens
in bytecode), and `getkey` (blocking key wait; no SOP path hit yet).

Golden-path regression (title → restore → walk): **only sound stubs remain**.
NB the harness key script changed — with a save present the title menu
defaults to "Continue the Quest", so it's `THIRDEYE_AUTOWALK=0d,0d,…` now
(the old leading `5000` walks you into "Gather a New Party"/chargen).

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
  ([`parseItemStream`](../apps/thirdeye/savegame/items_tmp.hpp)): variable-
  stride records `(u16 id, u16 class, N statics, 4 trailer)`, where N comes
  from the SOP class's total `instanceStaticSize` via a caller-supplied lookup.
  Unit-tested + verified against the Quick Start save.
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
- ~~The 4-byte trailer in each §2.3 record is currently skipped~~ ✅ RE'd:
  - **byte 3 = magical bonus (signed int8)** -- verified end-to-end
    against named items (Father Jon's ring of protection +3, Sir Mikeal's
    +1 plate/sword/shield, etc.). Accessor: `ItemRecord::magicalBonus()`.
  - bytes 0..2 = placement word + flags, partially decoded; see
    [`../eob3_research/SAVEGAME/README.md`](../eob3_research/SAVEGAME/README.md)
    for the per-pattern breakdown and trailer log artifact.
  - Probe behind `THIRDEYE_DUMP_TRAILERS=1` for further RE.
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

### 🚧 EOB1/EOB2 → EOB3 party import

"Summon the Heroes of Darkmoon" (menu option 3): `start`→`LBL_521` creates `xfer` (1380) +
SENDs "transfer from Eye II" (M:14), which reads an EOB1/2 save and converts to EOB3's. Wire
M:14's runtime functions the same way as `convert created party` (M:16). A standalone
EOB1/2→EOB3 converter tool could share this reader.

**De-risked**: `CHARCOPY.EXE` (the standalone DOS utility that stages the EOB1/2 save into
the EOB3 dir) does **no format conversion** — it shells out to DOS `copy` and leaves the
EOB1/2 save verbatim at `temptemp.sav` for M:14 to ingest. So the whole import lives in the
SOP M:14 handler; **no DOS RE needed**. Full CHARCOPY artifact dump:
[`../eob3_research/CHARCOPY/`](../eob3_research/CHARCOPY/README.md).

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

- Add the DH-only runtime functions (`eob3_research/ADDITIONAL_DH_RUNTIME_FUNCTIONS.TXT`).
- `.TBL` support (daesop `/create_tbl` generates these).
- `OPEN.RES`/`HACK.RES` flow.

## Phase 5 — Thirdeye-original features (post-EOB3)

Enhancements not in the original — opt-in, kept separate from the SOP-driven core so they
never change game behaviour.

- 🚀 **Qt-based launcher (planned).** First-run UX so a fresh user doesn't have to fight DOS
  install layouts. The launcher:
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

- 🗺️ **Automapper (planned).** Toggle with **M**. Parchment/beige overlay showing the current
  dungeon level: cells the party has explored, party position + facing, discovered
  walls/doors/stairs. EOB3 itself has no in-game map. Sketch: track a per-level visited-cells
  bitset (mark each cell as the party enters it / as it falls in the view frustum — we already
  compute `view_X/view_Y/visible`), persist alongside the save, draw a top-down grid from
  `lvlmap` (walls/floor) + `lvlobj` (doors/stairs) clipped to visited cells, with the party
  arrow from `party_x/y/fdir`. Drawn on its own overlay surface (like the compass snapshot),
  gated so it never bleeds into the menu. Stretch: scrollable for large levels, level-to-level
  switching, fog-of-war on unexplored cells, optional note-pinning.

## Phase 6 — Replace CHGEN.EXE with a C++20 chargen 🚧

Goal: drop the dependency on the original `CHARGEN/CHGEN.EXE`, run all
character generation natively. CHGEN.EXE also hosts the in-game Camp UI
(Rest / Memorize / Pray / Scribe / Load / Save), so a full replacement
covers both subsystems.

Full RE artifact dump + 3-phase plan: [`../eob3_research/CHGEN/`](../eob3_research/CHGEN/README.md).
Quick map:

| | Goal | Status |
|---|---|---|
| **Phase 6a — File-format readers** | ITEM.DAT, ITEMTYPE.DAT, CHARPICS.BMP, EOSPREFS.DAT. Reuse thirdeye's existing CPS/palette/font decoders for the rest. | ✅ ITEM.DAT, ITEMTYPE.DAT, CHARPICS.BMP readers landed in [`apps/thirdeye/chargen/`](../apps/thirdeye/chargen/) with unit + bundled-data tests; CHARPICS also wired as a third variant in the `Bitmap` decoder (auto-detected by file-size header) so all 89 portraits load. EOSPREFS.DAT stubbed pending need. |
| **Phase 6b — Chargen UI** | Race / class / alignment / stat-roll / portrait / starting-equipment flow. Writes `CREATE.SAV` in our already-RE'd format (so the existing `xfer` transfer keeps working). | ⏳ pending |
| **Phase 6c — Camp UI in thirdeye** | Self-host Rest / Memorize / Pray / Scribe / Save / Load. Drops CHGEN.EXE entirely. | ⏳ pending |

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
