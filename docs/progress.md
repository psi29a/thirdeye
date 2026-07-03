# Progress narrative — what's been built and how

Chronological-ish notes on milestones, with the non-obvious findings that came out of each.
For the high-level phase plan see [roadmap.md](roadmap.md); for engine internals see
[architecture.md](architecture.md).

## VM / object plumbing

✅ **`create_program`/`create_object`/`destroy_object`** — backed by `ObjectSystem`.
`create_program(index, class)` *replaces* the object at `index` with a new instance and sends
it `MSG_CREATE` — this is AESOP's program-chain bootstrap. Per-instance statics live in a
`std::deque` so a running handler's static pointer stays valid when nested creates grow the
list. Safety nets: an **instruction budget** (`setMaxSteps`) and a **SEND/PASS recursion-depth
guard** (`kMaxDepth`).

✅ **Unified address model + effective-address opcodes.** Code/stack/static live in separate
buffers, so an effective address is a tagged `Value`. See [architecture.md](architecture.md).

✅ **Extern link layer.** `LXB…SXDA`/`LEXA`/`SXAS`/`SOLE` resolve through the class's `.IMPT`
`B:/W:/L:` entries (`ObjectSystem::resolveExtern`, cached; `staticsPtr` bounds-checks). The
`--debug` trace shows resolved extern names, e.g. `LXB #20 (B:party_lvl@1382)`.

✅ **MSG_RESTORE auto-dispatch.** EOB3's `kernel` (class 1382) has **no MSG_CREATE handler**;
it wires up its entire event loop in **MSG_RESTORE** (M:2 → `PROCEDURE_727`). `create_program`
sends MSG_CREATE if the class has one, **else MSG_RESTORE**. Targeted: objects with
MSG_CREATE (menu, PC objects) are unaffected.

✅ **RTS frame-restore bug fixed — the in-game HUD now renders.** `PROCEDURE_727` ran fully
but its `RTS` jumped to PC 0. Root cause in `vm.cpp` (`Op::RTS`): JSR saves (PC, SP, Fptr)
*just above* the procedure's frame base, so `mFptr` points right at them — but RTS was popping
them from the procedure's current operand top (`mSp`), which is far below the auto frame, and
`mSp = popVal()` mid-sequence clobbered its own read pointer. Fix: read the three saves at
`mFptr`, `mFptr+kValueSize`, `mFptr+2*kValueSize`, restore, and `pushVal(ret)` (JSR is net +1,
like CALL). GTest `VM_Test.JsrRtsReturnsValueAcrossAutoFrame` locks it in.

✅ **Class inheritance via `N:PARENT`.** A code object's real superclass is the `.EXPT`
`N:PARENT` resource number (e.g. `axe`→1688 `weapons`→1373 `arms`→1371 `items`), NOT the code
header's `parent` field. `registerClasses` reads `N:PARENT` and sets `header.parent` from it.

---

## Menu + program chain

✅ **The boot is a `peekmem(1264)` state machine.** `start.MSG_CREATE` reads a 4-char "mode"
from memory cell 1264 and CASEs on it: `"INTR"` → `create_program(-1, 1322 main menu)` + SEND
"get choice" (title menu); `"CINE"` / default → "enter game" (in-game HUD). `peekmem`/
`pokemem` are backed by a real `std::map` cell (`engine.cpp`), seeded by `bootObject`:
default → `"INTR"` (menu), `--skip-menu` → `"CHGN"` (run char-gen party transfer then enter
the game with the default party Bob/Carol/Ted/Alice; vs `"CINE"`, which enters with an empty
party since `resume_*` is stubbed).

✅ **Menu selections act, via the AESOP program chain.** `get choice` (M:9) returns the
chosen index (0–4); `start.create` CASEs on it. **Continue the Quest** (1) → destroy menu +
SEND "enter game" → HUD. **Abandon the Quest** (4) → destroy menu + END → clean quit.
**Introduction** (0) and **Gather a New Party** (2) hand off to external sub-programs
(`cine.exe`/`chgen.exe`): the bytecode `pokemem(1264, next-mode)` then `launch`es. In DOS
`launch` exec-replaces the process and the sub-program chain-launches `aesop eye start`
again. We can't exec, so `launch` throws a **`Relaunch`** that unwinds the VM to
`bootObject`, which runs thirdeye's stand-in (`runExternalProgram`) and re-enters
`start.MSG_CREATE`, routing on the poked mode. `bootObject` is a program-chain loop
(`while (!quit)`): a normal return or `QuitRequested` quits; a `Relaunch` runs the
sub-program (**outside** the catch handler — a throw from inside a `catch` escapes its own
`try`) then re-boots.

✅ **`cine.exe` is wired to the real GFF player** (`runExternalProgram`→`playCinematic`).
Plays `INTRO.GFF` (loaded beside the `.RES` via `Resource::resourcePath`) through thirdeye's
existing `GFFI`+`Graphics::playVideo`+`Mixer` path, honoring `--skip-intro`, ESC/Enter to
skip, and window-close→quit.

✅ **ESC opens the in-game (camp) menu.** The `camp` object (1385) notifies msg 272
"toggle camp menu" on SYS_KEYDOWN **27 (ESC), 67 ('C'), 99 ('c')**. `pumpHost` no longer
special-cases ESC to quit; it posts ESC (0x1b) to the bytecode. Forced quit is now the
window close button (`SDL_QUIT`).

---

## Char-gen + party transfer

🚧 **"Gather a New Party" — partial; a no-source RE epic.** `CHGEN.EXE` is a separate DOS
program (own CPS/FNT/DAT assets in `../data/CHARGEN/`) that writes `CHARGEN\CREATE.SAV`; on
the `CHGN` reboot, `start`→`LBL_222` creates the **`xfer`** object (class 1380) and SENDs
**`convert created party`** (M:16) → **`transfer`** (M:15), which reads `CREATE.SAV` and
spawns 4 `PC` objects (class 1369). The transfer functions
(`open_transfer_file`/`player_attrib`/`item_attrib`/`read_initial_items`/
`write_initial_tempfiles`) have **no released source**, and `CREATE.SAV` is undocumented —
this is reverse-engineering, not a port.

✅ **The transfer now runs to completion and enters the game with the real default party**
(`engine.cpp TransferState`). `CREATE.SAV` format cracked from the default party
**Bob/Carol/Ted/Alice** — a short header then four fixed **345-byte PC records at 0x16**;
`player_attrib(pc, attr, size)` = read `size` LE bytes at file offset
**`(0x16-2) + pc*345 + attr`**. `attr` is a *direct byte offset into the record biased by +2*:
attr 2 = the record's first byte. Confirmed by the transfer's name-copy loop (attr 2..12 →
name[0..10] = "Bob\0…"); abilities/HP/XP follow at higher attrs. (An earlier `0x16+9` base
was off by 11 — read plausible-looking bytes but the wrong field; corrected base makes the
real party render.)

✅ **The party renders in the HUD panels (`--skip-menu`).** Three pieces beyond the transfer:
(1) **text wrap/clip** — `printText` word-wraps + clips to the bound window so the epigraph
stays in its box. (2) **`player[]` population** — the kernel's `player[]` array (the
PC-object-index list "draw players" walks) is filled from the live PC objects by their
`PC_num` slot in `resume_level` (a stand-in for the savegame's party reconstruction), and an
empty **`entities`** singleton is created at fixed obj index 15 so the party-draw's extern
writes land somewhere live. (3) **`sprint`** — printf-style print to a text window (`%d`/
`%s`, reads Code/Static/Extern strings via `Interpreter::readString`), draws the character
**names**. ✅ **HP readout** — `print(wndnum, res, args…)` is printf-style (HP uses res
646 = `"%d of %d"`), so `print` + `sprint` now share a `formatSop` helper (Bob 51/51, Carol
80/80, Ted 28/28, Alice 91/91).

✅ **Inventory / starting gear — fully RE'd** (see
[create_sav_and_item_format.md](create_sav_and_item_format.md)). `CHARGEN\ITEM.DAT` is the
EOB1 14-byte item format. A PC's inventory is 26 word slots at record offset **219** holding
item **ids** (0 = empty); the party's items are an EOB1 item array near the end of CREATE.SAV
at file **0x894**, ids running from **434** (= ITEM.DAT's count), so
`record(id) = 0x894 + (id−434)*14`. `item_attrib(pc, slot, attr)` resolves slot→id→record:
**attr 1 = type (+4)** (the xfer's `table123` maps type→EOB3 item object: 0→43…41→69,
resolving to classes 1323 "axe", 1325 "short sword", 1349 "robe", …), **attr 0 = bits (+2)** →
`itmflags`, **attr 2 = value (+13)** → `bonus`; attr 1 returns −1 for an empty slot.

✅ **The transferred party is auto-equipped (CREATE.SAV→EOB3 inventory-slot remap).** Bug:
party gear showed in the **backpack**, not worn. RE: kernel's `transfer` (xfer M:15) does an
**identity** placement — `W:inventory[slot] = item` from `item_attrib(pc, slot)`. But slot
orders differ. EOB3's `W:inventory` (PC class 1369, 26-element array @ static 81) draws via
`show equipment screen` (M:192) iterating all 26 slots — so EOB3 fixes **backpack at 0-13,
worn equipment at 14-25** by slot index. CREATE.SAV (the EOB1/2 char-gen order) stores worn
gear at slots **0-6 (+17)**, so identity placement dumps it all in the backpack. Fix
(`TransferState::itemAttrib`): a `eob3ToCreate[26]` remap routes the EOB3 slot the bytecode
asks for to the CREATE.SAV slot that holds that gear.

---

## Dungeon view + walking

✅ **The dungeon 3D view renders** (stone corridor with depth + correct colours). Three
enablers in `engine.cpp` (`--skip-menu`):
1. **`load_resource(dest, resource)`** — copies a resource's bytes into an object's static
   buffer (decodes the Static/Extern-tagged address arg).
2. **`init level`** picks the map from `B:party_lvl`. For the char-gen party it's 0 → no
   map, so the **`resume_level` stand-in seeds the party position** (level 1 = Mausoleum 1)
   before `init level` runs.
3. **`resume_level` also loads the level state** the real one would read from `LVLnn.TMP`:
   the maze into the dungeon's `B:lvlmap`@0, the wall set into `L:wallset`@7168, and the
   wall palette into the wall-colour region. **Resolved by NAME from the directory**, not
   hard-coded: maps are sequential resources (level N = (N−1)th after `"Mausoleum 1"`), and
   walls/palette derive from the map's area keyword (`"Mausoleum 1"` → `"Mausoleum"` →
   `"Mausoleum walls"` 253 / `"Mausoleum palette"` 386).

✅ **View clipping done.** `draw_bitmap` composites onto one screen surface (no AESOP page
model), so wide wall shapes drawn at x≤152 but up to 65 px wide would bleed past the view's
right edge into the character panels. Page-94 draws are clipped to the dungeon's view window
(`assign_subwindow` 0,0–175,119 = 176×120) via `Graphics::setClip`/`clearClip` →
`SDL_SetClipRect`.

✅ **Right-hand dungeon walls X-mirrored** (`draw_bitmap` mirror flag). `draw_bitmap`'s
**arg[6]** is the GIL2VFX flip flag (1=X, 2=Y, 3=both). `drawImage` now takes a `mirror`
param and reverses the indexed pixels in place.

✅ **Left-edge walls no longer vanish (signed draw coordinates).** The dungeon view
legitimately draws left-side wall shapes at **negative x** — e.g. depth-1 left front wall
is `draw_bitmap(94, 253, 16, -32, 20, …)`. `Graphics::drawImage`'s `posX`/`posY` were
`uint16_t`, so −32 wrapped to 65504. Fix: now `int`.

✅ **The dungeon is WALKABLE — movement works end to end.** The kernel owns movement: it
notifies arrow scan codes (0x4800 up→"move forward" M:203, 0x5000 down→backward,
0x4b00/0x4d00→strafe, 0x4700/0x4900→turn) on **SYS_KEYDOWN (event 17)**. Chain:
key → SEND "move forward" → SEND "move request"(dir) → buffers the direction in
`staticVar225` → the **"timer tick"** handler (M:215, SENT by SYS_TIMER, ~30 Hz) consumes
the buffer and SENDs "step" (M:229) → computes the new position and collision-checks via
SEND "impedance" then commits `party_x/y/fdir`. The missing piece was the
**`step_X`/`step_Y`/`step_FDIR` runtime functions** (native `EYE.C`, were stubbed).
Implemented as pure geometry: direction codes 1=turn left, 2=forward, 3=turn right,
4=strafe left, 5=backward, 6=strafe right; facing 0=N,1=E,2=S,3=W with
N=−y/E=+x/S=+y/W=−x.

✅ **All 14 levels load — multi-level dungeon + `change_level` wired.** Level-loading is
extracted into `loadDungeonLevel(level)`, used by both `resume_level` (initial) and
**`change_level`** (runtime fn 254). The 14 maps are sequential resources; there are only
**four tilesets** — Mausoleum (walls 253/pal 386), Forest (228/385), Ruins (236/387),
Marble (297/388) — each a `"<area> walls"` + `"<area> palette"`. The level→tileset table is
reconstructed from the map's area keyword.

✅ **Mouse drives the in-game HUD** (no code needed — the region machinery was already
wired). The kernel `assign_subwindow`s the compass arrows (handles 64-70 = (117,125)-
(176,160), a 3×2 grid) and notifies "arrow clicked"(N) on SYS_CLICK_REGION; "arrow clicked"
CASEs N → the move messages.

✅ **Keyboard movement bindings (layout-independent).** `pumpHost` maps **physical
scancodes** (`SDL_SCANCODE_*`): **W/S** = move forward/back, **A/D** = strafe left/right,
**Q/E** = turn left/right, **C** → camp. The arrow keys + ASCII still go through the keysym
fallback.

✅ **Compass facing indicator persists across HUD redraws** (page-104 RE'd). The compass +
rotating facing indicator live on AESOP **page 104**. The bytecode only redraws the
indicator on a **turn**; in the original `refresh_window(104, 103)` re-composites page 104
every frame. We flatten pages onto one surface, so the close-inventory HUD redraw erased the
indicator. Fix: snapshot the compass rect (x0-116, y120-168) when the compass page
refreshes, and re-apply it each present while in-game (gated on `mTextRestoreBg`). The
re-snapshot is **gated on a `gCompassDirty` flag set by the 187 draw** so an incidental
page-104 refresh during inventory interaction can't snapshot a *partial* compass over the
good one.

✅ **Stair / feature draw artifacts when strafing — fixed (two distinct bugs).**
(1) **Uninitialized `W:next` chain pointer.** `features.draw` (M:30, class 1994) ends with a
per-cell chain walk: `LXW W:next; if W:next != -1 AND staticVar2 != 0: SEND W:next, "draw"`.
Our `loadLevelObjects` only set `W:next` for monsters; features were left at `createProgram`'s
zero-initialized default. With `W:next == 0` the bytecode recursively SENDed "draw" to
**object index 0** with the current viewcell — painting whatever object 0's draw returns into
the cell's strip. Fix: `setS(kEntities, 6, -1, 2)` for every placed object before the monster
path.
(2) **`set_x1`/`set_x2` were stubs (the visible "stair in the wall" sliver).** `draw objects`
(M:225, dungeon class) clips each view-cell's draws to a horizontal strip via
`set_x1(view, x1)` + `set_x2(view, x2)`, then restores `(0, 175)` at end. Stubs returned 0,
so wide bitmaps (a stair shape composited for an oblique view-cell) bled past the cell's
column. Fix: file-scope `gViewClipX1`/`gViewClipX2`, wired by `set_x1`/`set_x2`, and
`draw_bitmap` on `kViewPage` intersects the existing view-window clip with the narrowed
strip.

---

## Level objects + items

✅ **In-game objects render — the level-OBJECT pipeline is end-to-end** (a skull door draws
in the 3D view). The maze (`lvlmap`, 1 byte/cell: `0x00`=wall, `0xff`=open floor) is only
walls+floor; everything interactive — doors, buttons, items, monsters, decorations — lives
in **`lvlobj`** (`W:lvlobj`@1024, 6144 B). Decoded:
- **`lvlobj` layout = `plane*2048 + y*64 + x*2` bytes** = 3 planes of 32×32 words.
- **"draw objects" (M:225)** iterates the view frustum cells (`view_X[]`@7172/`view_Y[]`
  @7190/`visible[]`@7208, 18 cells), reads `lvlobj[plane][y][x]`, and if `!= -1` SENDs
  "draw"(view, viewcell) to that object index.
- **Position lives in entities** (`place`@0, `x`@2, `y`@3, `lvl`@4, base-class-first).
  `features."draw"` is gated on `report(4)` (skull door's `report` returns bitmap id 257) and
  reads **`W:decflags`** for orientation/state — **`decflags = 0x0001`** renders a clean door
  facing the party.

✅ **Automatic level objects — the whole level loads from `LVLnn.TMP`** (`engine.cpp
loadLevelObjects`, on by default; disable with `THIRDEYE_NO_OBJECTS`). Key RE: **each record
stores its SOP class directly at `+3`** (a u16: 2304=stairs-down, 2247=lever, 2208=door,
2030=solid wall, 2072=ceiling pit, 2289=floor pit, …), so *no type→class table is needed*.
Records are variable-length; the loader **scans every offset for a valid record signature**
(id@+1 in 1000–4999, class@+3 in 1300–2450, cell@+11/+12 in 0–31) and dedupes by id. For
each: `create_program(id, class)` + set position + `decflags` + link `id` into
`lvlobj[0][y][x]`. **LVL01 → 231 objects placed, 0 crashes, 0 draw failures.**

✅ **The equipment / inventory screen works.** Clicking a character's portrait region opens
that PC's **equipment screen** — the paper-doll body + equipment slots + the character's
actual gear from the char-gen transfer (Bob: blue robe, dagger, sword, mace, a quiver of
arrows). The screen registers clickable slot regions (handles 84-92) on open, so item
**interaction** (move/equip/use) routes through the same proven region-click path.

---

## Text / HUD

✅ **AESOP text output** (`Graphics` text windows + `engine.cpp`). `text_window`/
`text_style`(font, justify)/`text_color`/`text_xy`/`print` are wired. Per-window state
(font/cursor/colour/justify/bound-window extent) in `Graphics`. `print(wndnum, res)` renders
a string resource (`"S:"` + text), **centered** when justify=2. Glyphs are tinted to the
colour via `SDL_SetSurfaceColorMod`. **Text-box clear has two modes** because we collapse
the original's draw pages onto one surface: (1) **flat-fill** for the title menu — "Menu
shapes" (bitmap 188) bakes the options into the backdrop; (2) **backdrop-restore** for the
in-game HUD — `Graphics` keeps a text-free snapshot of the bitmap art (`mBackdrop`, updated
in `drawImage`; `printText` never touches it) and `text_window` re-blits the box from it,
so name/HP overlay the panel art instead of a flat rectangle. `bootObject` selects the mode
per boot (`setTextRestoreBackground`).

✅ **Screen-switch clearing fixed — `fill_rectangle` implemented.** The character-stats
screen (M:193) `fill_rectangle(page, x1,y1,x2,y2, color)` to erase the panel before
redrawing. Stubbed, it left the old screen showing through the new one. Now real
(`Graphics::fillRect` → `SDL_FillRect`; mirrors the fill into `mBackdrop`).

✅ **Text left/top clipping — fixed the HP that bled onto the maze view.** `printText` only
clipped right/bottom; the equipment-screen HP arrives with a **page-local** cursor (`text_xy
htab=4` while the bound window is the right panel `x0=178`), so it drew at absolute screen
`(4,16)` — top-left, over the 3D view. The original (`GIL2VFX.C` ~L629) draws each glyph at
`htab − x0` relative to the window and **VFX clips negatives**. `printText` now skips glyphs
at `x < winX0` and lines at `y < winY0`. Debug: `THIRDEYE_TEXTDBG=1` logs every text draw's
window rect + cursor + string.

✅ **In-game runtime-call trace now actually turns on.** `gRtTrace` (gating the
`[drew]`/`[text]`/`CALL` lines) was only set in `runResourceVM`, **not** in `bootObject` —
so `--debug` silently produced no in-game runtime trace. `bootObject` now sets it too.

---

## Bitmaps + fonts

⚠️ **Font format:** EYE.RES fonts are the AESOP/16 `"2."` VFX format (`graphics/font.cpp`):
a 4-byte version, u32 char-count/height/background, a u32 offset table, then per char a u32
pixel-width + width*height bytes (1 byte/pixel, 0 = transparent). thirdeye's `Font` detects
the `"2."` magic (older format kept for the intro's GFF fonts); fonts are cached per id
(`text_style` fires every redraw). Not yet done: per-index colour remap (we mask), window
clipping.

## Sound + music (2026-07-03)

✅ **Digital SFX pipeline** covered under the earlier *Char-gen* section — `load_sound_block` /
`sound_effect` / `set_sound_status` back onto the OpenAL `Mixer`.

✅ **XMIDI music trio wired** (`runtime/sound.cpp`). `load_music` / `unload_music` toggle a
resident flag (WildMIDI reinits per playback, so no driver-level state to hold), and
`play_sequence(LA, AD, PC)` picks the MT32 (LA) resource by default — matches
`Mixer(mt32=true)` which converts MT32→GS via `XMIDI_CONVERT_MT32_TO_GS`. Override via
`THIRDEYE_MIDI_DEVICE=AD|PC|LA`. Bytes come from `res.getAsset(resnum)` and go straight into
the existing `Mixer::playMusic` path already used by the intro cinematic. **Every `C:` import
in EYE.RES is now handled** — a `THIRDEYE_TIMING=1` first-call trace over a 15s autowalk shows
zero `[stub]` lines. `THIRDEYE_SNDTRACE=1` logs `[snd] play_sequence res=… (NB XMI)` on each
trigger.

⚠️ **Spell cast — partial verification** (2026-07-03). Added `THIRDEYE_SPELL[=<class>]`
end-to-end test rail in [runtime/event.cpp](../apps/thirdeye/runtime/event.cpp): spawns a
target monster in front of the party (default troll, HP 50, `B:region=4` so
`find_hitable_monsters` counts it), then SENDs `magic.cast_spell` (M:54) to the magic
singleton (obj 2005 = `firstObjectOfClass(2377)`) with `(spell_class, sp=0, caster=first
PC)`. **Cast chain runs**: M:54 → SEND M:319 `cast` on the spell instance → SEND M:83
`mage level` (returns 9) → SEND M:340 `cast mage spell` → the CASE dispatch on `B:sp`
executes. **5 `sound_effect(0)` fires** — matching castlvl 9 ÷ 2 = 5 missiles for magic
missile. **But no damage lands on the target** because `B:sp=0` we pass isn't a real
spellbook slot value; the CASE branch doesn't reach the `missile spell` (M:335) →
`spell missiles.throw` (M:38) → `find hitable monsters` (M:70) → `roll for damage` (M:43) →
NPC `take damage` (M:82) chain that would actually deplete HP. `daesop`'d proof: the M:70
handler in `utils` (which does the damage-target selection) never appears in the `--debug`
trace after the cast fires.
**What this means**: the runtime infrastructure the spell path needs is *present* — every
`C:` runtime function it calls (`dice`, `rnd`, `post_event`, `spell_request`, `spell_list`,
`do_dots`, `do_ice`, `magic_field`, particle-mask save/restore, etc.) is implemented and
the SEND chain propagates through 4 levels of the class hierarchy. What's *missing* is a
real memorized spellbook: the correct `sp` value that maps to magic missile in
`cast_mage_spell`'s CASE dispatch table only lives in a PC that's been through the memorize
UI. That UI is hosted by `CHGEN.EXE` (Phase 6c, Camp UI). Full end-to-end verification
(cast → damage → die) will fall out naturally once Phase 6c lands and a real memorized
spell can be selected the way a player would. As a partial cross-check today,
melee-damage-and-die is already green (`THIRDEYE_ATTACK` — troll dies in 8s, sword wraith
dies on contact); the same M:82 `take damage` → M:55 `die` → `entities.remove` (M:22) chain
is what a working spell hit would drive.

✅ **AESOP/16 `"1.10"` bitmap decoder done** (`graphics/bitmap.cpp`). Every EYE.RES bitmap
is the native VFX shape-table format: a 4-byte `"1.10"` version, a u32 shape count, a
directory of `count`×8-byte `{u32 offset, u32 color}` entries (at byte 8), then per shape a
24-byte header (`boundsy=h-1, boundsx=w-1, originy, originx, xmin, ymin, xmax, ymax`) followed
by a per-line RLE token stream: `0`=end-of-line, `1`=skip *n* transparent (next byte), even
`m`=run of `m>>1` of the next byte, odd `m`=string of `m>>1` literal bytes; one end-token per
row, `height` rows. `Bitmap` detects the `"1.10"` magic and uses this path; the older
row/span format (the intro's GFF frames) keeps the original decoder. Reference: daesop
`convert.cpp` new-bitmap writers (`addNew*Token`); GTest `Bitmap_Test.DecodesVFXShape1_10`.
