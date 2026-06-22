# Eye of the Beholder III — save game format (exhaustive RE notes)

Reverse-engineered for thirdeye, intended to be complete enough to write a save
editor. EOB3 saves live in the game's **`SAVEGAME/`** directory and are produced
/ consumed by EOB3's own AESOP runtime functions (`write_initial_tempfiles`,
`resume_items`, `resume_level`, `restore_items`, `restore_level_objects`,
`resume_cursor`) — these are **native EOB3 functions with no released source**
(only named in `arun.map`); the AESOP/32 runtime has no generic object-save. So
everything here is RE'd from the data, cross-checked with community work.

**Confidence key:** ✓ verified against ≥2 saves · ~ likely · ? uncertain/guess.

## Sources cross-referenced
- goldbox.games topic 3417 (Ishad Nha) — EOB3 offsets: party position, 627-byte
  records, equipment slots. https://forums.goldbox.games/index.php?topic=3417.0
- moddingwiki — EOB1/2 char record + ITEM.DAT format (the EOB3 record is a
  superset). …/Eye_of_the_Beholder_Save_Game_Format and …_item.dat_Format
- Synalysis `eobdata.grammar` — EOB1/2 `EOBDATA.SAV` char record (243 B).
- ASE3 automapper (zorbus.net) — 32×32 level maps, item-id→name list.

---

## 1. File inventory & conventions

In `SAVEGAME/`:

| File(s) | Size | Contents |
|---------|------|----------|
| `SAVEGAME.DIR` | 328 B | save-slot **names** (the load/save menu list) |
| `ITEMS.TMP` | ~20 KB | **main save**: game state + party position + character records + live item objects |
| `ITEMS_00.BIN`, `ITEMS_01.BIN` | ~20 KB | the two save **slots** (copies of `ITEMS.TMP`) |
| `LVL01.TMP` … `LVL14.TMP` | ~10–13 KB | per-dungeon-level dynamic state (objects), 14 levels |
| `LVLnn_00.BIN`, `LVLnn_01.BIN` | = | the level files for each save slot |

Convention: **`.TMP` = live state** (what the running game reads/writes);
**`_NN.BIN` = saved slot N**. Loading copies `_NN.BIN`→`.TMP` then `resume_*`
reads `.TMP`. The static maze (walls) is **not** in the save — it comes from
EYE.RES map resources (e.g. "Mausoleum 1"); ASE3's `Maps/NN.dat` (1024 B = 32×32)
are those static maps and do **not** appear in `LVLnn.TMP`.

`SAVEGAME.DIR`: ASCII slot names separated by `\r\n`, e.g. `"Quick Start
Party\r\n___________…"` — one used slot, the rest blank (`_`-filled), ~21 slots,
terminated by `\x1a`. The one used slot ("Quick Start Party") is a
pre-rolled save game **shipped by Westwood/SSI** so a new player can pick
"Continue the Quest" on the title menu and start playing immediately. The
party is Sir Mikeal / Stonebeard / Salina / Lady Reeya on level 3 at the
Graveyard. Confirmed against the live game at
[archive.org/details/msdos_Eye_of_the_Beholder_III_-_Assault_on_Myth_Drannor_1993](https://archive.org/details/msdos_Eye_of_the_Beholder_III_-_Assault_on_Myth_Drannor_1993).

---

## 2. ITEMS.TMP — the main save

### 2.1 Game-state header (offsets 0..676)

Mostly unmapped. Known:

| off | sz | field | conf | note |
|-----|----|-------|------|------|
| 0   | 2  | ? (1a 00 = 26) | ? | maybe a record/object count |
| 252 | 1  | party X (0..255) | ✓ | column in the 32×32 maze |
| 253 | 1  | party Y (0..255) | ✓ | row |
| 254 | 1  | facing (0=N,1=E,2=S,3=W) | ✓ | |
| 255 | 1  | dungeon number (1..14) | ✓ | (Quick Start Party = level 3 @ 7,24 facing 1) |

(Tested by Ishad Nha by editing + reloading; re-verified here.)

### 2.2 Character records — 6 × 627 bytes, starting at offset 677

```text
PC1 @677  PC2 @1304  PC3 @1931  PC4 @2558  NPC1 @3185  NPC2 @3812   (stride 627)
```
Slots 0–3 are the player party, 4–5 are joined NPCs. The "Quick Start Party":
PC1 *Sir Mikeal*, PC2 *Stonebeard* (a dwarf — CON 19/CHA 8), PC3 *Salina*,
PC4 *Lady Reeya*, NPCs *Bug*, *Father Jon*.

Character record (offsets relative to the record start; ✓ fields cross-checked
across Sir Mikeal & Stonebeard):

Each PC field has a fixed +18 offset over its `PC` SOP class static offset
(EYE.RES res 1369). The file record is essentially the PC's static block
preceded by an 18-byte header (`+0..2` id/class, `+4` reserved, `+6` constant
619, `+8..15` 0xFF fill, `+17` 0x00). Cross-referenced with the bundled
Quick Start Party save:

| off | sz | field | PC static off | notes |
|-----|----|-------|---------------|-------|
| +0   | 2 | object index | — | live SOP object id (32..41 in this save) |
| +2   | 2 | class number | — | 1369 = "PC" |
| +6   | 2 | constant 619 | — | flags? unverified |
| +96  | ~14 | **backpack** item slots (W:inventory[0..13]) | 78 | u16 item-object ids; 0xFFFF empty |
| +127 | 2 | equip: body (W:inventory[14]) | 109 | item-object id |
| +129 | 2 | equip: bracers (W:inventory[15]) | 111 | |
| +131 | 2 | equip: right hand (W:inventory[16]) | 113 | |
| +133 | 2 | equip: ring 1 (W:inventory[17]) | 115 | |
| +135 | 2 | equip: ring 2 (W:inventory[18]) | 117 | |
| +137 | 2 | equip: boots (W:inventory[19]) | 119 | |
| +139 | 2 | equip: left hand (W:inventory[20]) | 121 | |
| +141 | 2 | equip: pouch A..C (W:inventory[21..23]) | 123..127 | |
| +147 | 2 | equip: necklace (W:inventory[24]) | 129 | |
| +149 | 2 | equip: helmet (W:inventory[25]) | 131 | |
| +151 | 2 | W:quiver — arrows type | 133 | |
| +153 | 2 | W:arrows — arrows quantity | 135 | |
| +155 | 20 | **B:name** | 137 | NUL-terminated, e.g. "Sir Mikeal" |
| +175 | 1 | **B:race** | 157 | Mikeal 0=human, Stonebeard 6=dwarf, Father Jon 4=half-elf, … |
| +176 | 1 | **B:classes** | 158 | multi-class bitfield; Mikeal=2 single, Stonebeard=7 multi |
| +177 | 2 | **W:portrait** | 159 | portrait sheet number (Mikeal 58, Stonebeard 24) |
| +179 | 1 | **B:PCstat** | 161 | status bits (poison/paralysis/…) |
| +180 | 1 | **B:alignment** | 162 | Mikeal 0=lawful good?, Stonebeard 4=neutral, … |
| +181 | 3 | **B:levels[3]** | 163 | per-class level; single-class trailing = 0 |
| +184 | 3 | **B:lost_levels[3]** | 166 | drained levels (level-drain effects) |
| +187 | 2 | **W:lost_hp** | 169 | HP lost to drain |
| +189 | 2 | **W:hpts** — HP current | 171 | Mikeal 97, Stonebeard 90 |
| +191 | 2 | **W:hmax** — HP max | 173 | |
| +193 | 2 | **W:hbon** | 175 | HP per-level bonus from CON |
| +195 | 2 | **W:food** (0..100) | 177 | =goldbox abs 872 |
| +197 | 12 | **L:experience[3]** | 179 | per-class XP; -1 = unused. Mikeal {600000,-1,-1} |
| +209 | 1 | **B:str** | 191 | Mikeal 18, Stonebeard 18 |
| +210 | 1 | **B:exc_str** (%) | 192 | Mikeal 94, Stonebeard 76 → "18/94", "18/76" |
| +211 | 1 | **B:int** | 193 | Mikeal 12, Stonebeard 14 |
| +212 | 1 | **B:wis** | 194 | 16, 14 |
| +213 | 1 | **B:dex** | 195 | 16, 16 |
| +214 | 1 | **B:con** | 196 | 17, 19 |
| +215 | 1 | **B:cha** | 197 | 17, 8 |

All ✓ verified across all 6 named records in the Quick Start save and patched
back into live PC objects via `runtime/eye.cpp resume_level`.

### Tail (+216..+626) — spell state + active effects

The tail covers the rest of the PC class's static block, **411 bytes**:

| off  | sz  | field | PC static off | conf | notes |
|------|-----|-------|---------------|------|-------|
| +216 | 1   | B:sparkle      | 198 | ~ | =0xFF for all 6 Quick Start records (default sentinel?) |
| +217 | 4   | L:magiceffects | 199 | ✓ | active-effect bitfield (0 on a fresh load) |
| +221 | 1   | B:tiger        | 203 | ✓ | tiger-transform state (0 for non-druids) |
| +222 | 1   | B:lost_str     | 204 | ✓ | drained STR (0 in this save) |
| +223 | 4   | ?              | 205 | ? | varies per PC (Mikeal `02 ae 1d ed`, Stonebeard `00 0e 00 90`, Lady Reeya `ff ff ff 00`, NPCs all `00`); possibly a checksum / hash / per-PC seed |
| +227 | 200 | B:spell_cnt[200]  | 209 | ✓ | per-class × per-circle × per-slot spell availability counters (mostly 0 in this save) |
| +427 | 200 | B:spell_stat[200] | 409 | ✓ | per-class × per-circle × per-slot spell prep state; first ~10 bytes = 0xFF "unmemorized" sentinel, scattered nonzero values for prepped spells (Salina has 30 nonzero slots — she's the cleric) |

We don't decode the spell-slot layout (per-class × per-circle × per-slot is a
lot of state for a future RE pass), but the parser captures both arrays as
raw bytes (`Character::spellCnt` / `spellStat`) so `write_initial_tempfiles` +
`resume_level` round-trip them losslessly. Active effects (`magiceffects`) +
the smaller named fields go to named parser outputs.

### 2.3 Item objects — the live-item stream

After the 10 PC-class records, `ITEMS.TMP` stores the **live item objects** that
the equipment/backpack slots point at. Each slot is a flat record:

```
+0  u16  id
+2  u16  class    (0xFFFF = empty/dead slot)
+4  N    static block       — N = SOP class's total `instanceStaticSize`
+4+N 4   trailer             — 4 bytes (purpose unknown; likely a free-list /
                               link pointer the writer keeps outside the SOP
                               static block). For empty slots N=0 and this is
                               the entire 4-byte payload (always 0xFFFFFFFF).
```

So empty slots are 8 bytes total, real items 8 + N. Verified against the Quick
Start Party save (445 real items + ~58 empties; class strides match
`instanceStaticSize`: dagger 1326 → 13 (+8 = 21-byte total), holy key 1530 →
12 (+8 = 20), spellbook 1377 → 15 (+8 = 23), and so on).

The 4-byte trailer's **closed-form decode is still pending** — across the
445 items in the Quick Start save we see 81 distinct trailer patterns. The
two largest buckets:

| trailer | count | example items |
|---------|-------|---------------|
| `ff ff 00 00` | 167 | "free" / unattached items (no owner, not on a floor)  |
| `ff 00 xx yy` | 164 | various, including most PC-equipped items (`ff 00 00 01`)|

Where the **actual carried-by-PC information lives is NOT the trailer** —
it's in the static block at byte +4 (the `B:lvl` field from `entities`, which
items repurpose as an owner reference):

| static[+4] | meaning |
|-----------|---------|
| 32–35     | PC object index (Sir Mikeal / Stonebeard / Salina / Lady Reeya) |
| 1–14      | dungeon level number (item is on the floor or in a container) |
| 0xFF      | "no location" — orphan / pool |

This is what should drive a save's "is this item on PC X" vs "is it on
floor / in container" determination, not the trailer. The trailer carries
something else (free-list/link pointer? per-item flags?), and we currently
parse it as an opaque 4 bytes — enough for byte-perfect round-trip via
`write_initial_tempfiles`, but the *semantic* needs another RE pass. A
sample of equipped items:

| id | cls | owner | trailer | statics[0..5] |
|----|-----|-------|---------|---------------|
| 994 | 1338 chain mail | 32 (Mikeal) | `ff 00 00 01` | `00 00 0d 00 20 00` |
| 985 | 1343 shield     | 33 (Stonebeard) | `ff 00 00 01` | `00 00 0d 00 21 00` |
| 977 | 1345 spellbook  | 34 (Salina) | `ff ff 00 00` | `00 00 0c 00 22 00` |
| 969 | 1343 shield     | 35 (Lady Reeya) | `ff 00 00 00` | `00 00 0d 00 23 00` |

✅ **Implemented** in [savegame/items_tmp.cpp](../apps/thirdeye/savegame/items_tmp.cpp)
(`parseItemStream`). `resume_level` recreates each item via
`createProgram(id, cls)` + writes the saved static block, then patches the
PCs' `W:inventory[14..25]` from the parsed `equip[]`.

`write_initial_tempfiles` serializes these; `resume_items`/`restore_items` are
EOB3-native (no released source). (See `create_sav_and_item_format.md` for the
char-gen item format these derive from — the inventory ids/types and the
`table123` type→class map.)

---

## 3. LVLnn.TMP — per-dungeon-level state

A `LVLnn.TMP` is the dynamic state of one dungeon level: the **level objects**
(monsters, floor items, decorations, doors, wall features). It is **not** a fixed
array — sizes vary per level (LVL01=12514 B, LVL02=11839, LVL03=13436, …) because
the object count + type mix differ. (The earlier "500×25 records" reading was a
coincidence: `12514 = 500×25 + 14` happens to factor for LVL01 only.)

### 3.1 It's a stream of variable-length object records

The file is a back-to-back sequence of object records. Each record:

```c
+0  u8   type/flag — object kind; influences record size (0x1a/0xff seen on actives)
+1  u16  id        — per-level object id, sequential from 1000 (1000,1001,1002,…)
+3  u16  CLASS     — the SOP object class number, stored DIRECTLY (the key find!)
…
+11 u8   x         — cell column (0–31)   ┐ on the 22/25-byte (complex) records
+12 u8   y         — cell row    (0–31)   │  (verified: these are floor cells)
+14 u8   decflags  — features orientation/state (1/2/4/8 = the wall side; 0x0f for
                     floor objects like stairs)  ┘
```

**`+3` is the object's class number, not a type index** — verified against LVL01
(`0x0900`=2304 "mausoleum stairs down", `0x08c7`=2247 "mausoleum lever",
`0x08?0`=2208 "mausoleum Star Trek door", `0x07ee`=2030 "solid wall", …). So
**no type→class table is needed**: `restore_level_objects` just `create_object`s the
class in the record. ✅ **Implemented** (`engine.cpp loadLevelObjects`, on by default):
for each record `create_program(id, class)`, set its position in the `entities` base
(`place`/`x`/`y`/`lvl`) + `decflags` in `features`, and link `id` into the dungeon's
`lvlobj[0][y][x]`. Run on the first in-game frame (after init level clears `lvlobj`).
**LVL01 loads 231 objects, zero crashes / draw failures** — the dungeon is fully
populated (stairs/doors/levers/decorations) and stable while walking.

**Parsing note — records are variable-length; don't guess the size table.** Sizes seen
include 8/22/25/26/… and the +0 type/flag influences the size, but guessing it drifts
(e.g. a 26-byte door read as 8 → its `+11/+12` position is lost). The robust approach
the loader uses: **scan every byte offset for a valid record signature** — `id@+1` in
1000–4999, `class@+3` a real object class (1300–2450), `cell@+11/+12` in 0–31 — and
dedupe by id. The combined filters make a false positive astronomically unlikely
(~1.6e-5 per offset), so it finds every position-bearing object regardless of record
size. (The earlier "8-byte records have no position" reading was a mis-sizing artifact:
those were really ~26-byte door records.)

### 3.2 The 8-byte object slot (the common case)

```c
+0 u8  type        (0x00/0xff = an empty/inactive slot)
+1 u16 id
+3 u16 next/link   (0xffff = null)   ─┐ inactive slots read `…ff ff ff ff 00`
+5 u16 prev/link   (0xffff = null)   ─┘ (both links null = unlinked)
+7 u8  kind/flags
```

Active 8-byte objects carry a real link/position in the body instead of the null
`ff ff ff ff`. Objects are **chained** (the null-link sentinel = `0xffff`), which is
how multiple objects share a cell. (Exact body fields — cell index vs. in-cell
position vs. link — are still being pinned down.)

### 3.3 The 25-byte object (monster)

id at +1 as usual; the **cell position is two bytes at +11/+12** (`x`, `y`, both
0–31) — verified against the maze: those cells are floor (`0xff`), where a monster
would stand. The remaining body is monster state (a second position/target at
+22/+23, HP, type, facing, …), not yet fully labelled.

### 3.4 How it drives the engine

The id space (1000+) **is** the set of SOP object ids the native
`restore_level`/`restore_level_objects` recreates on level load: each record becomes
a live SOP object (a door / monster / item / decoration instance, class chosen by
the type byte), linked into the dungeon's **`lvlobj`** spatial table (`W:lvlobj`,
32×32×3 words — per cell: object-list head + links). The dungeon's *draw objects* /
*impedance* / *collide* handlers then read `lvlobj` — this is what drives **doors,
items on the floor, and monsters** to appear + interact. thirdeye loads
`lvlmap` (walls) but not `lvlobj` yet, so the dungeon is currently object-empty.

**Still to do for in-game objects:** (a) finish the per-type field map (door
state/open, monster type+HP, item-object ref); (b) the type→class table; (c) the
**new-game** object source — a fresh game doesn't read `LVLnn.TMP`, it's *written* by
native `write_initial_tempfiles` from the level definition, so where the initial
doors/decorations come from (bytecode-placed vs. a data resource) still needs
tracing. Loading the **saved** `LVLnn.TMP` (the "Continue the Quest" path) is the
shorter route to seeing a populated level.

---

## 4. Notes for a save editor

- Edit **character stats** in `ITEMS.TMP` (and the matching `ITEMS_NN.BIN`):
  abilities `+209..215`, HP `+189/191`, XP `+197`, food `+195`, name `+155`.
- Edit **party position** at file `+252..255` (X, Y, facing, dungeon).
- **Equipment / inventory** edits change the `+96..` and `+127..153` *item-object
  ids*, which must point at real item objects in §2.3 — adding a new item means
  appending an item object and pointing a slot at it (ASE3 sidesteps this by
  editing the running game's memory instead).
- Always edit `.TMP` **and** the active `_NN.BIN` together (the game may reload
  from either); back up first.
- This format is **not yet field-complete** — treat ? rows as guesses.
