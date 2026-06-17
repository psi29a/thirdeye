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
terminated by `\x1a`.

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

| off | sz | field | conf | notes |
|-----|----|-------|------|-------|
| +0   | 2 | object index | ✓ | the live SOP object id (32,33,34,35,…) the PC is created at |
| +2   | 2 | class number | ✓ | 1369 = "PC" |
| +6   | 2 | ? (=619) | ? | constant across PCs |
| +96  | ~14 | **backpack** item slots | ✓ | u16 item-*object* ids (e.g. 999,998,997,996,995); 0xFFFF = empty |
| +127 | 2 | equip: body armor | ✓ | item-object id; 0xFFFF empty |
| +129 | 2 | equip: bracers | ~ | |
| +131 | 2 | equip: right hand (weapon) | ✓ | |
| +133 | 2 | equip: left ring | ~ | |
| +135 | 2 | equip: right ring | ~ | |
| +137 | 2 | equip: boots | ~ | |
| +139 | 2 | equip: left hand | ✓ | |
| +141 | 2 | equip: pouch A | ~ | |
| +143 | 2 | equip: pouch B | ~ | |
| +145 | 2 | equip: pouch C | ~ | |
| +147 | 2 | equip: necklace | ~ | |
| +149 | 2 | equip: helmet | ~ | |
| +151 | 2 | arrows type | ~ | |
| +153 | 2 | arrows quantity | ~ | |
| +155 | ~21 | **name** (NUL-terminated) | ✓ | "Sir Mikeal", … |
| +176 | 1 | race/sex? | ? | Mikeal 2, Stonebeard 7 |
| +177 | 1 | ? | ? | Mikeal 58, Stonebeard 24 |
| +181 | 1 | level? (=10) | ? | both 10 |
| +189 | 2 | **HP current** | ✓ | Mikeal 97, Stonebeard 90 |
| +191 | 2 | **HP max** | ✓ | |
| +195 | 1 | **food %** (0..100) | ✓ | =goldbox abs 872 |
| +197 | 4 | **XP** (class 1, LE) | ✓ | Mikeal 600000, Stonebeard 500000 |
| +209 | 1 | **STR** | ✓ | both 18 |
| +210 | 1 | **STR %** (exceptional) | ✓ | Mikeal 94, Stonebeard 76 → "18/94", "18/76" |
| +211 | 1 | **INT** | ✓ | Mikeal 12, Stonebeard 14 |
| +212 | 1 | **WIS** | ✓ | 16, 14 |
| +213 | 1 | **DEX** | ✓ | 16, 16 |
| +214 | 1 | **CON** | ✓ | 17, 19 |
| +215 | 1 | **CHA** | ✓ | 17, 8 |

Still to map within the record: AC, alignment, portrait, multi-class levels &
XP (classes 2/3), memorized/known spells, status effects (poison/paralysis), the
remaining ~0xFF-filled tail (+217..+626, mostly empty here). Ishad Nha reported
~103/627 bytes mapped; the table above is a superset of the cross-verified ones.

### 2.3 Item objects

After the character records, `ITEMS.TMP` stores the **live item objects** that the
equipment/backpack slots point at: `u16 class` (an EOB3 item code class, e.g. 1347
"…", 1323 "axe", 1325 "short sword", 1349 "robe") + the item's static block,
~21-byte stride (varies by item class). `write_initial_tempfiles` serializes
these; `resume_items`/`restore_items` recreate them and re-link the slots.
(See `create_sav_and_item_format.md` for the char-gen item format these derive
from — the inventory ids/types and the `table123` type→class map.)

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
