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
guard** (`kMaxDepth`). `destroy_object` calls MSG_DESTROY and cancels the object's outstanding
notify requests. `release_owned_windows` is implemented (`EventSystem::releaseOwnedWindows`
matches RTOBJECT.C/GRAPHICS.C verbatim) but **not wired into destroy_object yet** — attempting
that broke the ALL ATTACK button (kernel's "swap request" path destroys some transient object
whose slot happens to have been recorded as the button subwindow's `owner`; reaping it killed
the button on the second frame). The start-self-destroy `resetInstances()` sweep at each menu
cycle boundary still bounds the leak in practice. Real fix pending: find the mis-owned
subwindow assignment.

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

✅ **Spell-book menu rendered, then vanished — the compass re-stamp was painting over it
(2026-07-14).** Player report: clicking the spellbook showed "just the compass with some
garbage at the bottom." `magic.create` (class 2377) builds its menu window at
(0,120)-(116,175) — the compass rect plus 7 rows — and `magic.update` draws the beige
"Auxiliary display" panel (bitmap 193:11) there. Two flattened-page bugs, fixed together:
- **Re-stamp over the menu.** The compass re-stamp's cover-detection only watched
  **fillRect**; a `draw_bitmap` covering the compass rect didn't suspend it, so every
  present blitted the stale compass back over the spell menu, leaving only the panel's
  bottom 7 rows visible. Fix in `Graphics::drawImage`: a bitmap blit whose clipped dest
  rect covers the whole compass rect sets `mCompassCovered`, same rule as fillRect.
- **Stale pixels through transparent panel pixels.** The panel + its scroll-arrow hash
  overlays (192:13/14, checkerboard with arrow-shaped index-0 holes) rely on the page
  model: on page 1 their transparent pixels reveal the pristine HUD Backdrop. Our
  flattened screen revealed the LIVE compass instead (gold needle shrapnel in the arrow
  glyphs — the "multicoloured mess" class of bug). Fix: `snapshotCompassUnderlay()`
  captures the compass-area pixels right after a Backdrop (190) draw that actually
  repaints the region (a clipped-elsewhere 190 draw must NOT recapture — that poisons
  the underlay with the live compass), and `drawImage` restores that underlay under any
  covering bitmap before it blits.
  Ground truth established along the way: **the compass art incl. a north needle is baked
  into Backdrop 190 itself** (187 is only the facing-needle overlay), and the DOS-faithful
  composite (backdrop+panel+overlays) shows a bright up-glyph / dark down-glyph — our
  post-fix render matches it pixel-class for pixel-class.
Self-healing on close: `magic.deactivate` SENDs dungeon `"draw compass"` → bitmap 187 →
`gCompassDirty` → `snapshotCompass()` re-arms the re-stamp. Verified headlessly both ways
with the new `THIRDEYE_SPELLMENU=<pc>[,<type>]` probe (runtime/event.cpp; menu open ~1800
presents, pristine compass after deactivate); `THIRDEYE_COMPASSDBG=1` traces cover/capture
events and dumps the underlay. NB the spell list renders empty for a PC with no memorized
spells of the requested type — that's data, not rendering.

✅ **VFX transparency is the RLE skip token, NOT palette index 0 — decoder-level fix
(2026-07-14, same session).** Player follow-up: the menu's ABORT SPELL text + scroll-arrow
glyphs showed "something behind" them instead of DOSBox's solid black. Ground truth from
`arun/vfx/VFX.INC` + the shape data: `VFX_shape_draw` does **no color keying** — the only
transparent pixels in a "1.10" shape are `skip` tokens in the RLE; a shape can PAINT
palette index 0 via run/string tokens and those render as real black. The spell panel's
arrow glyphs are painted-black runs, and the disabled-scroll checker overlays (192:13/14)
are fully opaque black+idx-20 checkerboards with a solid black arrow — zero transparency.
Our decoder collapsed skip and painted-black both to 0 and `drawImage` colorkeyed 0
unconditionally, deleting every painted-black pixel from every VFX shape.
Fix: `Bitmap::decodeVFXShapeMasked` returns a per-pixel opacity mask alongside the
indices; `DecodedShape` caches it; `drawImage` renders masked shapes through an ARGB
surface with true per-pixel alpha (mirror flips the mask too). Non-VFX formats (GFF
frames, CHARPICS) keep the legacy colorkey path. Verified: ABORT SPELL solid black,
opaque checker + black glyphs matching the DOS composite; full-frame regression clean
(3D view / party panels / HUD); ~82 fps vs ~85 pre-change.

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

✅ **World item pool now populates (2026-07-14).** Player report: "no loot or potions or
food to be had from any source." Root cause: `loadAreaInstances` only pre-created slots 1..14
(area singletons) from `ITEMS_00.BIN`; the item pool at slots 15..999 (potions, scrolls,
weapons, quest items — targets of `niche.W:contents`, `monster.W:carried`, container
contents) was never instantiated, so every world-item reference resolved to an empty slot.
The comment even flagged the TODO ("Larger slots restored elsewhere by our own ITEMS.TMP
path -- the SOP equivalent would be `restore_items` over the full 0..999 range"). Fix:
widened `loadAreaInstances` to take a slot range, and (a) `write_initial_tempfiles` now
pre-instantiates slots 15..999 before serializing so fresh `ITEMS.TMP` contains the world
pool going forward, (b) `resume_level` also gap-fills after the ITEMS.TMP §2.3 stream so
existing saves get world items on load. Verified: 463 world objects gap-filled on the
shipped QSP save; 4 monsters that carry items (undead beast → 104, chimera → 225, shambling
mound → 406, groaning spirit → 426) now drop them on death. Grave mists stay barren —
authentic to the original (all 37 ship with `W:carried = -1`, XP-only fodder).

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

✅ **EOB2 → EOB3 party import ("Summon the Heroes of Darkmoon") working end-to-end**
(issue #31). Verified with a real GOG EOB2 save: menu option 3 → M:14 "transfer from Eye
II" → M:15 "transfer" imports PERICLES/"STUMPY"/WOLFSPIRIT/LAURANN with stats, portraits
and gear. Two findings made this nearly free: (1) **CREATE.SAV is EOB2 save format** —
CHGEN.EXE writes an EOB2-shaped file (same 20-byte save-name header, 345-byte PC records
at 0x16, item array at 0x894), which is why M:14 and M:16 share the M:15 handler and
`TransferState` needed no new reader; (2) `CHARCOPY.EXE` does no conversion — it stages
the EOB2 save verbatim as `TRANSFER.SAV`. Our stand-in is `THIRDEYE_EOB2_SAVE=<path>`
(boot-time copy + EOB2 header sniff). One real bug fixed on the way: `open_transfer_file`
returned 0 on success, but per `arun/src/EYE.H:199` it returns a `void *` handle — M:16
ignores the return so char-gen never noticed, M:14 branches on it ("run CHARCOPY" dialog).
EOB1 saves are rejected by design, matching the original CHARCOPY ("Eye of the Beholder
III won't work with Eye I save games"): EOB1's record layout differs (243-byte records at
0x02) and the intended path is EOB1 → EOB2's importer → EOB3.

## Savegame item stream — the CDESC-framing fix (2026-07-19)

✅ **Every initial floor item in the game now spawns.** `ITEMS.TMP`'s §2.3 item stream is
one native `save_range` **CDESC** stream (RTOBJECT.C/.H): `{u16 slot, u32 name, u16 size}`
+ `size` bytes of verbatim instance statics per record, `name == 0xFFFFFFFF` for a dead
slot. `parseItemStream` had been reading it as a **4-byte** `{u16 id, u16 class}` header +
a class-size lookup + a "4-byte trailer" — which shifted every static block 4 bytes early
and read the block's real last 4 bytes as a standalone trailer. The load-bearing casualty:
the shifted read discarded the entity block's placement fields, so **no item with
`W:place == -1` (on a dungeon floor) ever materialized** — the Burial Glen wands, the axe
cache, every "on the ground here, you'll find…" in the walkthroughs was silently absent
from a fresh QSP boot. The fix reads the true 8-byte CDESC header and copies the whole
`size`-byte block verbatim; floor items now spawn, `loadLevelObjects` links them into
lvlobj plane 1, and they render in the 3D view. See §2.3 in
[eob3_savegame_format.md](eob3_savegame_format.md).

**Three workarounds fell out with the misframe** (all were compensating for the 4-byte
shift, none were real): the "magical bonus in trailer byte 3" reader (`arms.B:bonus` is in
the block at its true offset), the `items.W:itmflags` re-seed via `report(1)` (itmflags is
in the block too — the shifted read produced 0xFFFF, whose bit 0x400 read as CURSED and
made every restored item refuse to unequip), and the "owner lives at `B:lvl@4`" misread
(ownership is `items.W:place@0`: a holder object id, or -1 for a floor item). The writer now
emits CDESC too (with a u16-size overflow guard, CodeRabbit), so our saves are
byte-compatible with the DOS record layout. Found while driving phase 3 of the control
channel below — the "why is `items` empty on the QSP save?" question.

## Live control channel + agent-driven play (2026-07-19)

✅ **A running thirdeye can be driven and inspected over a socket.**
`THIRDEYE_CTL=<path>` opens a Unix domain socket (macOS/Linux; a no-op stub on Windows,
per the design's non-goals) polled once per host-loop pump — no threads, zero cost when
unset. Line protocol ([control_channel.md](control_channel.md)): `key`/`click`/`map`/`dump`
inject input and snapshot the screen exactly as `THIRDEYE_AUTOWALK` does internally;
`party`/`monsters`/`items`/`cell`/`lvlmap`/`obj`/`peek` read the object system directly
(the same statics the SOP reads — strictly better than OCR'ing screenshots); `poke`/`send`
mutate live state for debugging. The parser is a pure function (unit-tested on all
platforms); a headless ctest (`control_e2e`) boots the real binary, drives
`ping→party→key→party`, and asserts the pose changed.

**Proof of concept — an agent played Burial Glen end-to-end** via
[scripts/glen_drive.py](../scripts/glen_drive.py): BFS pathfinding on the live wall map
(`lvlmap`), ALL-ATTACK combat (select PC name-plates → the button appears under the arrow
pad), chopping hackable trees to open the tree maze, auto-dismissing the Florn Falconhand
cutscene, and picking floor items into backpacks — all 26 pieces of the item-stream fix's
newly-spawned treasure, into the party's packs. Bugs it surfaced are fixed or documented:
the "engine crash" that was really a total-party-kill death screen; floor-item pickup
requires standing ON the cell (a feature click region — the fruit trees — occludes
ahead-cell items); stowing into an occupied backpack slot swaps the occupant onto the
cursor; and the wall map (not lvlobj plane 0) is the real source of maze walls.

## Dungeon Hack — boot + runtime bring-up (2026-08-05)

DH's `OPEN.RES` (intro) and `HACK.RES` (game) now boot end-to-end through the SOP VM
under filename auto-detection: OPEN→`opening`, HACK→`phase-one`. The boot loop learned
HACK.BAT's errorlevel semantics — remembering that batch `if ERRORLEVEL n` matches
*n and above*, tested high-to-low: `>= 3` re-runs phase-one (`:CONTINUE`), `2` re-runs
the intro first (`:CHECKDEMO`), `1` quits, `0` falls through to MAZE then phase-two.
We land 2 and 3+ on the same target for now because the cross-.RES hop back to OPEN.RES
isn't wired up. So the title-menu attract loop functions.

> **Corrected 2026-08-09.** This paragraph originally went on to claim phase-two
> could be reached without a `THIRDEYE_BOOT=phase-two` override. It cannot.
> Tracing `phase-one`'s bytecode later showed that `main screen` returns 1, 2 or
> 3 and **never 0**, so the batch never falls through to MAZE + phase-two. Only
> the debug override reaches gameplay. See the errorlevel-contract section
> further down, and [dungeon_hack.md](dungeon_hack.md#where-the-errorlevel-actually-comes-from-2026-08-09).

Phase-two now runs its tick loop against real DH
runtime support — file I/O primitives (`open_file`/`read_*`/`close_file`) resolve
DOS-backslash paths against `<dh_root>/`, the SAVEGAME loaders read shipped
`PC.DAT`/`SETTINGS.DAT`/`VISIBLE.DAT`, and the MAZE-consumer wrappers
(`load_level_map`, `open_feature_file`+`get_feature_record`) read chunked files per the
format spec in [dungeon_hack_maze.md](dungeon_hack_maze.md), zero-filling when MAZE
hasn't populated them.

**MAZE.EXE reverse-engineered** ([../dh_research/MAZE/](../../dh_research/MAZE/) has the
Ghidra headless decompile + strings + xrefs; [dungeon_hack_maze.md](dungeon_hack_maze.md)
is the reading). The non-obvious finds: MAZE isn't a level layout writer — it emits
per-cell **entropy tables** that phase-two turns into geometry procedurally (`0xDB`
sentinels → random bytes, everything else → `0xFF`, per 0x400-byte chunk × (DEPTH+10)
levels). SETTINGS.DAT is a fixed **4-byte SEED + 12-byte struct** (the 12 keys from
the strings table in listed order; only the first 16 bytes of the shipped 27-byte file
are read). FEA files are streams of typed **8-byte records** with a 22-case switch on
the type byte. items.dat records are 8-byte, source stride 5, permuted `[3,1,0,2,4]`.
Confirmed against the shipped `SETTINGS.DAT`: `SEED=0x000156e0, DEPTH=15`. DGROUP base
in MAZE resolves as `file_offset = 0x7d70 + (pushed_off)`, which is what let us pin
every filename push to its writer function.

Only 5 stubs remain in the DH boot path — all pure-renderer (`init_viewspace`,
`build_clipping`, `copy_window`, `draw_walls`, `Transition`). Phase-two runs its
tick loop and dispatches per-cell logic.

> **Superseded (2026-08-07):** all five have since been implemented — see
> *The maze renders* and *Occlusion + text-box + clip* below. A phase-two
> session with movement now reports zero stubbed CALLs.

**Screen layout + wall art (2026-08-06).** Two engine-level differences from
EOB3 had to be fixed before DH could draw a correct screen. Both DH-gated;
EOB3 verified pixel-identical to a pre-change baseline frame.

*Page compositing.* EOB3 flattens every page onto one surface. DH instead draws
each panel into its **own offscreen page** at page-local `(0,0)` and then
`copy_window`s it to a screen rect — `copy_window(16, 9)` is the floor art
landing in the dungeon view at `(138,13)`. The distinguishing signal is
`assign_window` (offscreen page, page-local coords) vs `assign_subwindow`
(absolute screen rect); both had been funnelling into the same
`EventSystem::assignWindow`, so every DH panel drew at screen `(0,0)` and
stomped the previous one. `Win::offscreen` records which is which, `draw_bitmap`
redirects into the page's surface (Graphics swaps its `mScreen` pointer, so all
existing draw routines follow with no changes), and `copy_window` blits to the
destination's registered origin. That alone turned the DH screen from
"items floating on black in the top-left" into the real layout: inventory
paper-doll column on the left, 3D view on the right under the stone arch.

*Palette regions.* DH carves the DAC up differently from EOB3's
`PAL_FIXED/PAL_WALLS/PAL_M1/PAL_M2` (`00/B0/C0/E0`): fixed at `0x00` (225
colours), **walls at `0xF0`**, **floor at `0xE0`**, both 16 colours, loaded by
the kernel as `set_palette(1, wallpal[lvl])` / `set_palette(2, floorpal[lvl])`.
Probing the art pins it — wallset shapes index `0xF6..0xFD`, floor shapes
`0xC4..0xEE`. With EOB3's bases nothing ever loaded DAC 225–255, so every
wallset pixel resolved to `(0,0,0)`: **the dungeon view rendered solid black
even though the shape decoder was working perfectly** (sub 8 decodes to 12,384
bytes / 12,102 non-zero pixels). A long detour went into suspecting the decoder
before the palette dump made it obvious — the lesson is to check
index→colour resolution before blaming a decoder for black output.

With both in, a wallset panel blits into the view as real cave-wall art
(see [screenshots/dh_wall_panel.png](screenshots/dh_wall_panel.png)).

**The maze renders (2026-08-06).** `draw_walls` is now a faithful port of
AESOP.EXE's own routine, and DH draws a real perspective dungeon view —
[screenshots/dh_wall_render_corridor.png](screenshots/dh_wall_render_corridor.png).

Getting there needed the DH runtime's geometry tables, which only exist inside
`AESOP.EXE`. The chain that unlocked them is worth remembering because it
generalises to every DH runtime function:

1. `AESOP.EXE` has **no** function-name strings, so its 620 functions are
   anonymous. The name → number map is in **`HACK.RES` resource 3** ("Low
   level functions", 330 entries) — `daesop -k HACK.RES 3`.
2. Those numbers are global and multiples of 4 (max 656 → 165 entries), and
   index a **far-pointer array at file `0x1c8f8`** — found by scanning the MZ
   **relocation table** for runs of relocations at a 4-byte stride, since a
   far-pointer array relocates every entry's segment word.
3. Ghidra segment = stored segment + `0x1000`. Validated by cross-check:
   `build_clipping` was independently identified as `1f36:05f4` from its
   parameter usage, and table entry 636 reads `0f36:05f4` — exact offset
   match. That one agreement confirmed the whole mapping, which now yields
   the address of *any* DH runtime function (`explode_save` = `1f36:14db`,
   etc.).

`draw_walls` (`1f36:0785`) walks **25 wall faces over 18 distinct map cells**
in 4 depth bands (11+7+5+2, nearest last), reading `lvlmap[my*32+mx]` per face
and blitting the wallset sub-bitmap its tables name for that (wall type, face).
Only two wall types exist. Three faces reuse a sibling panel mirrored
(`14→13`, `15→8`, `17→16`). The `+138 / +13` blit origin is hardcoded in
AESOP as `0x8a`/`0xd`, independently confirming the view rect derived from the
SOP's `assign_subwindow(..., 138, 13, 313, 132)`.

Verified data-driven, not just "it draws something": an all-walls map yields
25 faces (closed chamber), a synthetic map with a single wall ahead yields
exactly **1**, and a hand-built corridor yields **12** with side walls
stepping back in correct perspective.

A cautionary note from the same session: an earlier attempt to locate the
geometry by scanning for a plausible **byte pattern** produced a table that
looked right for a few rows and was subtly garbage — the DGROUP base was off
by `0xD5`. The relocation-table route is the one to trust.

**Occlusion + text-box + clip (2026-08-07).** Three loose ends closed:

* `init_viewspace` / `build_clipping` ported verbatim from AESOP.EXE with the
  clip contribution table lifted from `DS:0x1117`. Occlusion is real now:
  cells outside the view cone (rows 0/6/7/11 of the table are all-`0x7e`)
  don't render, and cells behind walls cull correctly. `notblocks` gets
  populated by the SOP's own bytecode loop between the two calls.
* One trap in that loop cost a while to find: the SOP reads `view_X`/`view_Y`
  via `LSBA` (sign-extended byte) BEFORE `build_clipping` runs its `& 0x1f`
  mask. So an unmasked value like `-3` becomes `-96` when scaled, and
  `lvlmap[y*32 + x]` reads 96 bytes BEFORE `lvlmap` — into `lvlbit`, which
  is a different array — and every off-map cell reads as an occupied wall.
  Fix: mask coordinates to 0..31 on the way out of `init_viewspace` (the
  SOP's 32×32 wrap semantics). Party-at-(0,0) starts wouldn't have worked
  otherwise.
* The message bar went white on movement because DH boots with mode `INTR`,
  which leaves `text_window` erase on flat-fill. Flat-fill samples a pixel
  just inside the box, one stray light pixel turned the whole bar white,
  and it perpetuated (next wipe sampled its own fill). Same fix as EOB3's
  green-bar problem, DH-gated on the `Backdrop` resource (59) that marks
  "now in-game".
* `draw_walls` clips every blit to the view rect. Face 20 sits at absX 34
  with a 129-wide panel (34..163) while the view starts at 138 — the wall
  was overwriting the inventory column and the arch on every step.

**Phase-two renders a real gameplay HUD.** First frame is the DH title splash
("Advanced Dungeons & Dragons Forgotten Realms — DUNGEON HACK" on wooden door);
by ~frame 50 the full in-game UI is drawn: character portrait ("Kathra
Shallowtaint" from the shipped `PC.DAT`), HP bar, compass with N-facing arrow,
direction-arrow pad, CAMP button, floor items visible (bread, gems, robe, chest,
book, plate+sword), and the wooden/stone bezel framing the dungeon-view area.
The center of the dungeon view is empty because `draw_walls` still stubs to
zero (SOP calls it once at boot; without a return that signals "walls updated,
please redispatch tick," the SOP never asks again). See
[screenshots/dh_phase_two_hud.png](screenshots/dh_phase_two_hud.png). Only the
3D wall rendering (`draw_walls` + `init_viewspace` + `build_clipping`) stands
between phase-two and a playable dungeon.

> **Superseded (2026-08-07):** the wall renderer landed — `draw_walls`,
> `init_viewspace` and `build_clipping` are all implemented (ported from
> AESOP.EXE) and the view renders with real occlusion. What actually stands
> between phase-two and a playable dungeon is dungeon *content*: the native
> mini-MAZE seeds an all-walls map, so the party starts sealed in rock. See
> *The maze renders* below.

A **native mini-MAZE** (`ensureSavegameFiles` in `runtime/dh.cpp`) seeds
structurally-valid empty `savegame/LEVELS.DAT` + `FEA*.DAT` + `ITEMS.DAT` at
first HACK.RES boot (idempotent — real MAZE output is preserved). DEPTH is
read from the shipped `SETTINGS.DAT`. This is what lets phase-two consume
zero-content dungeons instead of tripping on missing files.

Notable RE side-finding during the items.dat trace: **DH's SOP kernel reads
`SETTINGS.DAT` with a *different, wider* layout than MAZE writes**. MAZE emits
4B SEED + 12B struct = 16 bytes; kernel reads `4 discard + 19 setting + 4 long
= 27 bytes` (matching the shipped file exactly). MAZE and the SOP kernel
disagree on the file's format, and the shipped 27-byte file is the SOP's
truth. **ITEMS.DAT is never opened by any DH SOP** — the only `open_file`
targets across all HACK.RES code resources are `PC.DAT`, `SETTINGS.DAT`,
`SETSAVE.DAT`. So MAZE writes ITEMS.DAT but nothing in phase-two reads it
via the SOP file API — it's either an AESOP.EXE internal consumer or a
save/restore artifact.

## Dungeon Hack is walkable (2026-08-07)

Two pieces closed the gap between "renders correctly" and "you can play with it".

**A native maze generator.** `ensureSavegameFiles` used to seed an all-walls
LEVELS.DAT, which was structurally valid but left the party sealed in rock —
`draw_walls -> 0 faces` was the *correct* render of being inside solid stone.
`carveMaze` now writes a real connected maze per level. One non-obvious
constraint drove the design: cells sit on **even** coordinates (0,2,…,30 →
16×16 cells, shared walls on the odd coordinate between them) specifically so
`(0,0)` is open, because that is where the SOP starts the party until FEA
supplies an entry point — the conventional odd-coordinate maze layout would
have walled the party in on turn one. Row/column 31 is odd and stays solid,
giving a border for free. Seeded from SETTINGS.DAT's own `SEED` field so a
given install reproduces its dungeon; verified byte-identical across
regeneration, every level fully connected with no islands, and the party
verified walking `(0,0)→(1,0)→(2,0)`, turning, then `(2,1)→(2,2)` with the
route matching the generated map cell for cell.

This is emphatically **not** MAZE.EXE's algorithm — a plain perfect maze with
no rooms, loops or decoration. Real DH layouts still need the generator port.

**FEA stairs.** The decoder fell out of `dungeon`'s own CASE table, which has
31 entries: type `0` ends the record loop (that's what the all-zero terminator
does) and `1..30` are the feature types in exactly the order MAZE's string
table lists them — so **4 = stairs up, 5 = stairs down**, and 30 names + the
terminator is precisely `CASE #001f`. Type 5 forwards `fea[4..7]` to
`create teleporter` (message 495) against class 2870, which daesop resolves as
*"current stairs down"*, and those four bytes line up with the `teleporters`
object's `dest_x` / `dest_y` / `dest_lvl` / `dest_fdir` externs. So a stairs
record is `[5][x][y][?][dest_x][dest_y][dest_lvl][dest_fdir]`.

We emit one down-stairs per level on the open cell furthest from the start
(84 steps away on level 0, BFS-verified reachable). The SOP takes it and
builds the object: `create_program(1000, 2870)`.

Placing a feature immediately surfaced a new stub — `draw_auto_square`, the
automap tile renderer, which nothing had called while the dungeon was empty.
It is now the only stub left in a DH session, which is a good illustration of
why getting something playable matters: the HUD overdraw, the white message
bar and this all became visible only once the thing could actually be used.

### `draw_auto_square`, and the rest of the feature types (2026-08-06)

`draw_auto_square` turned out not to be a bitmap blit at all. AESOP.EXE
`1f36:0966` draws each automap cell as a **9×9 box of lines**: outline in
colour `0x66`, passage stubs toward open neighbours in `0x67`, an inner
highlight on an open side in `0x69`. `lvlvis & 4` marks a cell unseen (skip the
outline); in `lvlbit` the even bits 0/2/4/6 are passages N/E/S/W and the odd
bits are corners.

One thing had to be added rather than ported: AESOP hands the page down to its
line primitive, so its clipping is free, while our `drawLine` writes straight
to the screen. Without an explicit clip a map-edge cell spills over the HUD —
the same failure the wall panels had. With it, the parchment shows the explored
corridor and the party arrow, matching the walked route.

That closed the last stub: **a DH session now runs with zero stubs.**

With the automap working the remaining feature types went in quickly:

- **Creatures** (type 12) — `fea[4]` selects one of the level's three monster
  slots via `mon_types[lvl*12 + fea[4]*4]`; slot 2 additionally sends
  `make boss monster` (message 233).
- **Doors** (type 1) → `create door` (message 493, class 2855). Bytes 6/7 form
  a 16-bit link id matching the `doors` object's `W:button_num` /
  `W:lock_num`. Placed only on corridor cells — open along exactly one axis —
  so they sit in passages rather than floating in junctions.
- **Buttons** (type 6) → `create thing` (message 496, class 2894); types 2 and
  7 are the same shape with classes 2813 / 2897.

Worth recording as a process note: while wiring the creatures up we reported
"0 monsters spawning" and started hunting a VM extern bug. That was a bad grep,
not a bug — a `--debug` trace showed `SEND msg 494` executing and jumping
straight into `create monster`. Monsters had been spawning the whole time. The
VM trace is cheap; reach for it before theorising.

### MAZE.EXE, actually ported (2026-08-07)

The placeholder generator above is gone. The real one is in
[apps/thirdeye/runtime/dh_maze.cpp](../apps/thirdeye/runtime/dh_maze.cpp), and
the full reading is in
[docs/dungeon_hack_maze.md](dungeon_hack_maze.md#the-generator-itself-2026-08-07).
Three findings mattered.

**The chunk was never entropy.** This document previously recorded that
`savegame/LEVELS.DAT` holds "per-cell entropy" that phase-two expands
procedurally, because `FUN_1325_4017` looked like it wrote `random(0..255)`
over `0xDB` sentinels. It does not. `FUN_1766_00c1` takes *two* 16-bit
arguments that Borland pushes as one dword, so `random(0, 1)` decompiles as the
single literal `FUN_1766_00c1(0x10000)`. Read correctly the pass is
`0xDB → random(0,1)` (a wall with one of two textures), everything else
`→ 0xFF` (floor) — the chunk is a plain 32×32 tile map all along. The rule to
carry forward: **any `FUN_1766_00c1(0xNNNN0000)` is `random(0, 0xNNNN)`.**

**The PRNG is R250, and it is the whole ballgame.** MAZE ships its own
generator in segment 1766 — a 250-word lagged-Fibonacci XOR with lags 250/103,
seeded by a Lehmer LCG with 16 words forced to a staircase bit pattern. Every
layout decision is a draw from that stream, so no substitute PRNG can produce
DH's dungeons no matter how faithful the surrounding code is. Two traps: the
staircase loop's bound is `!= 179`, not `< 250` (running to the end of the
array shifts the mask to zero and wipes the state), and the global immediately
below the state array is a call counter, easy to absorb into the ring by
mistake.

**Each level is seeded independently.** `FUN_1325_375f` opens with
`srand(seed + level + 1)`, which means a port can reproduce level *N* without
replaying every draw the program made before it — the single most useful fact
for incremental work on this.

**All five layout algorithms are in.** MAZE assigns each level a *zone* up
front (levels 0–3 are zone 1, the deepest is zone 4, one random middle level is
zone 0, the rest roll 1–3) and switches on it. Zones 0 and 2 are the maze
branch — a stackless recursive backtracker over odd coordinates `(1,1)…(29,29)`,
where each cell holds the direction it was entered from while the walk is inside
it, so no stack is needed. Zones 1, 3 and 4 run `FUN_1325_0e3c`, which places
rooms and corridors *first* and then backtracks through whatever rock is left,
so the maze grows around the rooms. Zone 4 falls out with no doors at all: with
no rooms placed, its door loop starts past the end of the room list.

Also superseded: the placeholder's even-coordinate scheme, which existed so
`(0,0)` would be walkable. MAZE puts cells on odd coordinates and ships the
arrival point in the FEA header record (bytes 1/2/3 = x, y, facing). It walks
the entry until it lands in a dead end, marks that cell, and puts the party on
the one open neighbour facing away down the corridor — so we emit that.

Two quirks are reproduced deliberately. `FUN_1325_08cb`'s perimeter-door loop
starts one room late, so the first room on every zone-2 level never gets
perimeter doors; and doors only fit where the rock forms a clean passage, never
at a corner or junction. Both would be easy to "fix" into a desync. Three loops
in the original can spin forever (they don't decrement on rejection) — those we
did cap.

One more find worth recording: **the working grid is a CP437 character map**.
`0xDB █` rock, `0xB3 │`/`0xC4 ─` doors, `0x18 ↑`/`0x19 ↓` stairs, `0xB0 ░` pit.
`main` dumps it verbatim into `SEED.TXT` *before* the finalize pass throws the
semantics away, which makes SEED.TXT a byte-exact oracle: run real MAZE under
DOSBox with a known seed, run ours, diff. We have not been able to do that yet,
so nothing here is validated against real MAZE output — only against the
disassembly and structural invariants.

Verified: 25 levels generate with zones distributed correctly, every level
structurally sound (only the three on-disk byte values, frame sealed), every
entry and stairs walkable and chained, and the zone-0 level a provably perfect
maze — 225 cells, 224 corridors, fully connected, no loops. In game the party
appears at `(3,26)` facing north and walks up column 3, matching the generated
map cell for cell. Zero stubs, zero errors, 115 tests green, EOB3 unaffected.

Still ours rather than MAZE's: the feature-placement tail (15 passes driven by
the `FREQ_*` settings), which includes the real stairs pass — so we pick stairs
by BFS from the entry and feed them forward as the next level's arrival, MAZE's
chaining with our placement. That tail is now fully decoded in
`dh_research/MAZE/FEATURES.md`, so it is a transcription job rather than an RE
one.

### MAZE's feature-placement tail — the dungeon gets populated (2026-08-07)

Geometry was only half of MAZE. The other half is the fifteen passes
`FUN_1325_375f` runs after the layout, plus two whole-dungeon ones, and those
are what turn corridors into a dungeon. They are in now.

**Regions turned out to be the backbone.** `FUN_1325_2081` floods each level
from the party's arrival cell and labels every connected patch of floor with
its region id — literally, as the character `'A' + id`. Doors bound regions,
and each region records how many doors lie between it and the entry. That
*depth* is what every later pass steers by, and it is the thing that makes a
randomly generated dungeon **solvable**: `FUN_1325_3004` never hides a key
deeper in the maze than the shallowest lock it opens. Get that wrong and you
generate a door nobody can ever get through.

Which we did, at first. The initial cut produced 358 locks and 327 keys — 31
doors with no key anywhere in the dungeon. Two causes, both worth recording.
The door's own cell holds a door glyph, not a region letter, so reading the
region off it always yielded region 0; the key has to be keyed to the floor
*beside* the door, and specifically the shallower side. And the teleporter
repair path could drop a teleporter on the party's arrival cell, after which
re-labelling bailed and the level came out with **zero** regions — no keys, no
stairs, no monsters. Two of 25 levels were silently empty. Both fixed; the
count is now exactly 385 locks and 385 keys, and that 1:1 pairing is a test.

**Counts come from the `FREQ_*` settings** through `FUN_1325_0117` — roll a
percentage, then roll dice, both from an 8-entry table indexed by the setting
byte. All seven tables are transcribed and were verified byte-for-byte against
the binary before being hard-coded. It is satisfying when the shipped settings
explain themselves: `HINT_SHEET_FREQ = 0` maps to a `0%` row, and sure enough a
full dungeon contains no hint sheets at all.

**Two record streams, not one.** `FUN_1325_121d` emits 9-byte *feature*
records (30 types) and `FUN_1325_11af` emits 5-byte *item* records (12 types).
They are separate numbering schemes — earlier notes had conflated them, which
is why the feature-name table never seemed to line up.

A live 25-level dungeon at the shipped settings now carries 489 doors, 385
locks with 385 matching keys, 1147 creatures, 1041 decorations, 105
illusionary walls, 13 doors disguised as solid wall, 81 arches, 35 windows,
72 shelves, 63 traps, 3 pit pairs and 18 teleporters. In game that is 186
objects on level 0 alone, against 12 before. Walking forward six cells now
gets the party **spun** — the compass flips without a turn key, which is a
spinner trap doing exactly its job.

Deviations are deliberate and marked in the source: the region walk is our own
connected-components pass (MAZE's frontier array is not fully traced, so we
reproduce its shape rather than its cell order); three loops that the original
only exits by luck are capped; and the arrival cell is claimed the moment it
is chosen so nothing gets built on top of the player. Still not validated
against a real MAZE run — SEED.TXT remains the oracle for that.

Zero stubs, zero errors, 119 tests green, EOB3 unaffected.

### DH file writers, and the phase-one errorlevel contract (2026-08-09)

Driving DH's own menus turned up three stubs — `create_file`,
`write_array_to_file`, `write_long_to_file` — and they were the whole reason
phase-one could never persist anything. They are implemented now, alongside
`write_number_to_file` and `update_file`. Writes buffer in memory and flush at
`close_file`; the SOP writes sequentially and never seeks, so that is both
simpler and safer than holding an `ofstream` open across VM calls.

Both files DH writes now round-trip byte-perfectly: `PC.DAT` at 33 bytes
(a 20-byte name plus a 13-byte stat block) and `SETTINGS.DAT` at 27 bytes
(`u32 seed` + 19-byte struct + a trailing counter). The whole phase-one flow —
menu, character selection, the Customization screen, Play — now runs with
**zero stubs**.

**Seed 0 is not a seed.** DH's Customization screen displays "(random)" and
writes 0; `1325:3aee` substitutes the BIOS timer for it. We were about to take
that 0 literally, which would have handed every install the same dungeon. The
substitution now lives in `generateDungeon` (where MAZE does it), and the seed
actually used comes back in `DungeonOut::seedUsed` and goes into `LEVELS.DAT`'s
4-byte header — which is exactly where MAZE records it too.

**The errorlevel contract, mapped.** `phase-one`'s `create` handler is just
`init_*` then `SEND "main screen"` then `END`, and `END` returns top-of-stack.
Since phase-one imports no exit-code function at all, the DOS errorlevel *is*
what `main screen` returns. That handler is a `while(1)` around a 5-way CASE on
the menu selection: Show Intro returns **2** (`:CHECKDEMO`), Continue runs
`enter game` and returns **3** (`:CONTINUE`), and Choose/Create Character run
`customize` and return **1**. Cases 0 and 1 line up with `HACK.BAT` exactly,
which is good evidence the reading is right.

And then it doesn't work: **no path returns 0**, which is what the batch needs
to run MAZE and enter phase-two. The branch that would return 0 is dead code
behind `SHTC #01; BRT`. Clicking Play genuinely returns 1 — `:EXIT`. Either
DH's own 16-bit `AESOP.EXE` transforms the interpreter's exit code (EOB3's
`AESOP.C` is only a `spawnvp` launcher that collapses everything non-zero to
1), or this install's `HACK.BAT` isn't retail's — it ships `DEMOGNBG.EXE` and a
`G.BAT`, which smells like a bundled build. That is a question about
`AESOP.EXE`, not about our runtime, and it is where the new-game path is
currently blocked. Full trace in
[dungeon_hack.md](dungeon_hack.md#where-the-errorlevel-actually-comes-from-2026-08-09).

### The DH new-game path — no env var required (2026-08-10)

The blocker turned out to be a wrong assumption about where AESOP's exit code
comes from. We were using the boot handler's return value. It is not that.

AESOP creates the boot object, runs it, **destroys it**, and exits with a code —
and `phase-one`'s `destroy` handler is, in its entirety at the end:

```text
1116: LSB  "B:staticVar0"
1119: END
```

The exit code is that static. `main screen` only *sets* it on the way out: 2 for
Show Intro, 3 after `enter game`, 1 to quit — and the Choose/Create Character →
Customize → Play path never assigns it at all, so it keeps its initial **0**,
which is exactly what `HACK.BAT` needs to run MAZE and enter `phase-two`.

What made this convincing rather than merely plausible: `destroy` branches on
the same static, and at 0 it paints the **"Generating"** bitmap — the splash
that sits on screen while MAZE runs. It also calls `shutdown_graphics` only at
1, the real quit. The bytecode is describing the batch file's control flow back
to us.

So `bootObject` now sends `MSG_DESTROY` after the boot handler returns and uses
*that* as the errorlevel, and the `phase-one` → `phase-two` hop runs the dungeon
generator first — HACK.BAT's `..\maze` step, which overwrites unconditionally,
so each new game gets a new dungeon from the settings phase-one just wrote.

Verified with no `THIRDEYE_BOOT` anywhere: Show Intro returns 2, Continue loops
the menu on the empty save picker, and Choose Character → Customize → Play
returns 0, regenerates 25 levels with a freshly rolled seed (seed 0 means
"random", and a second new game gets a different dungeon), boots phase-two with
186 objects on level 0, and the party walks six cells east while monster AI
ticks. Zero stubs, zero errors, 121 tests, EOB3 unaffected.

The lesson worth keeping: when a return value looks wrong, check *which* return
value the caller is actually supposed to read. We had the right contract and the
wrong source for three sessions.

### DH's UI panels were see-through (2026-08-11)

The camp menu rendered with the dungeon showing through it and scattered
coloured specks over the rest. It looked like a decode bug. It wasn't.

Resource 53 ("Camp display") decodes perfectly — 177x127, landing exactly in
its subwindow. The problem was one line in `draw_bitmap`: every draw passes
`transparency = true`, and for non-VFX bitmaps that meant "colour-key palette
index 0". But the older AESOP bitmap format is a **sparse scanline** format —
each row names its y, then runs at explicit x positions, and pixels no run
covers are simply never written. That omission *is* the transparency, exactly
as the VFX format's skip token is. A run may legitimately paint index 0, and
the original draws it as solid black.

We memset the buffer to 0 and then keyed 0 away, so painted black and never-
painted became indistinguishable. The camp panel is **25% painted black over
99.4% coverage**, so a quarter of it turned into a window onto the dungeon.

This is the same bug the VFX path had before `decodeVFXShapeMasked` (the
0.89.0 "spell-book text renders solid" fix) — it just survived on the older
format. `decodeScanlineMasked` now reports coverage for both formats and
`drawImage` never colour-keys.

**Measuring the blast radius properly mattered here.** Frame diffing said 39 of
63 EOB3 frames changed, which looked alarming — until a control run of the
*same build against itself* differed in the same proportion. The harness is
nondeterministic (a damage splat lands on a different portrait run to run), so
frame diffs can't answer this; that trap is already recorded in this file from
the draw_walls work. The deterministic answer is to ask the decoder directly:
sweep every bitmap resource and count painted-black pixels.

| | bitmaps | affected |
|---|---|---|
| EOB3 | 312 | **0** — every one is a VFX shape, already masked |
| Dungeon Hack | 632 | **454** contain painted black |

So EOB3 is provably untouched, and DH's UI has been see-through everywhere:
"Stone Frame" alone has 15546 painted-black pixels, plus the banners, Overlay,
Textbar and the Customize Screen. The camp panel now renders solid with all
eleven options legible and the selection highlight visible.

### Camp options work — my "not selectable" call was wrong (2026-08-11)

I reported that Dungeon Hack's camp menu could be opened but not used: the menu
toggles (`SEND 523`) and one accelerator gets delivered (`SEND 531`), but
`camp option selected` (525) never fired, so Rest / Save / Exit looked
unreachable. That conclusion was wrong, and it was wrong because I only tried
the keyboard.

`SEND 525` fires on a **mouse click**, and the option handlers run. Proven
twice by driving it headlessly: clicking one row rendered the **Hall of Fame**
screen (high-score table with real entries), and one row lower opened DH's
**save-slot picker** — twelve `<empty>` slots plus CANCEL SAVE, drawn
correctly. Nothing needed fixing.

The keyboard path behaves differently, which is what misled me.
`camp accelerators` (531) maps ASCII letters — `r` Rest, `p` Pray, `m`
Memorize, `s` Save, `b`, `e`/`x` Exit — but its handler only does
`SEND "show"`, i.e. it *highlights* the row. Activation is a separate step, so
an accelerator alone never produces a 525. Highlighting without activating is
exactly what "one 531 and no 525" looks like from the trace, and I read it as
"input isn't reaching the object."

`camp option selected` dispatches an 11-entry CASE, in menu order: 0 Rest,
1 Pray for Spells, 2 Memorize Spells, 3 Show Creature Totals, 4 Show Hall of
Fame, 5 Exit Game, 6 Save Game, 7 Restore Game, 8 Turn Sounds Off, 9 Show
Numbers, 10 Break Camp. Case 5 sends `decision` (msg 530) with string 864,
*"Are you sure you wish to quit the current game?"* — so Exit Game is
confirm-gated, which is also why a repeating autowalk click kept dismissing it.

For the harness: CAMP button at native `(17,190)`; the menu registers one
region covering `(138,13)-(313,132)` and derives the row from the mouse y in
`menu`'s `PROCEDURE_70` (class 2944). Row 4 is at about y=81, row 6 at about
y=90.

**The useful consequence:** the DH save UI is already live. When we get to DH
save/load, the picker, slot list and cancel path are all working — the missing
piece is purely our side of `explode_save`.

### VGA palette animation — DH's menu highlight (2026-08-12)

The camp menu's selection highlight never moved: arrow keys did nothing, and
neither did hovering. Only *clicking* an option highlighted it. That last
detail is what cracked it — it ruled out the keyboard entirely and pointed at
the one thing hover and arrows share.

`menu`'s `illuminate choice` (msg 593) does not redraw anything. It calls
**`set_palette(1, table14[select])`**. Comparing two of those palettes shows
the trick:

```text
pal38[26..37]:  3f 3f 3f | 3c 12 11 | 37 0c 0b | 33 07 06
pal39[26..37]:  3c 12 11 | 3f 3f 3f | 37 0c 0b | 33 07 06
                ^^^^^^^^   ^^^^^^^^
```

`3f 3f 3f` is max-intensity white in 6-bit VGA, and it moves down one slot per
palette. Each menu row is drawn in its own palette index; the highlight is a
**DAC swap**, not a redraw. Eleven palettes, eleven rows.

Our surfaces are `ARGB8888`, so palette indices are resolved *when we draw* and
a later `set_palette` cannot reach pixels already on screen. Hence: arrows and
hover inert, clicking fine (because `camp option selected` redraws the menu, so
the new palette is applied at draw time).

The fix is a shadow index plane. Each surface gets a parallel `uint16_t` buffer
recording which palette index produced each text pixel; `setPaletteRange` then
repaints the pixels whose index falls in the range that just changed, exactly
as the DAC would. `kNoIndex` means "not from a palette index, leave alone", and
anything drawn over a recorded pixel — a bitmap, a fill, a page composite —
clears it back, so the plane can only ever under-claim. Page compositing copies
the source page's plane across so text composited from a page still animates.

Verified against a recorded play session (`THIRDEYE_RECORD`): the highlight now
steps through ten distinct rows and moves in both directions, where before it
only ever sat at two.

**The first cut of this was wrong and shipped a visible regression** — magenta
speckle over the dungeon view — and it is worth recording why, because the
mistake was structural rather than a slip. The plane recorded which index drew
each pixel, and I tried to keep it honest by clearing it wherever something
else drew: `drawImage`, the fill sites, page compositing. That is unmaintainable.
`mScreen` has roughly *thirty* writers, and every one I missed left a stale
index that the next palette swap happily repainted in a text colour. Two
successive theories (a per-surface plane map dereferencing destroyed pages; the
sentinel entries) were both wrong, and each "fix" left the speckle exactly where
it was.

The design that works does not track invalidation at all — it **verifies**. Each
DAC change carries the colour the entry held before, and a pixel is only
recoloured if it *still holds that colour*. Anything that overdrew it leaves
something different there, so the pixel is skipped and forgotten. Missed
writers stop being a correctness problem: the worst case is a pixel that does
not animate, never one painted wrongly. All the invalidation hooks came back
out.

Measured on the identical recorded walk: magenta went 748 px → 16 px, and those
16 are a 4×4 block at (119,17) that is present with the palette path disabled
too — pre-existing, not ours. EOB3 unchanged: 2522 frames in 30 s against a
2493–2517 baseline, 0 errors, 0 stubs, and its own magenta count identical
before and after (7 px).

Scope, honestly: this covers **text** only. A palette-animated *bitmap* would
need the same recording inside `drawImage`; nothing we run appears to use it,
and the plane's clear-on-overdraw rule means such a case fails safe (no
highlight) rather than wrong.

Three wrong turns worth remembering. I claimed the menu objects leaked (they
don't — handles are recycled); I claimed `nchoices` was unset (it's 11); and I
"measured" `SEND 593` firing zero times, which was meaningless because 593 is a
direct bytecode SEND and never appears in the notify trace I was grepping. The
thing that actually settled it was the user's observation that the *mouse*
didn't highlight either.
