# EOB3 char-gen transfer: CREATE.SAV + ITEM.DAT format

Reverse-engineered for thirdeye's `xfer` party transfer ("Gather a New Party").
The char-gen program (`CHARGEN/CHGEN.EXE`) writes **`CHARGEN/CREATE.SAV`**; on the
`CHGN` boot the `xfer` SOP object (class 1380) reads it back via the runtime
functions `open_transfer_file` / `player_attrib` / `item_attrib` /
`read_initial_items` and rebuilds the party (4 `PC` objects, class 1369) plus
their starting items (EOB3 item objects). None of those runtime functions have
released source (they're only named in `arun.map`), so this is RE'd from the data
+ the `xfer` bytecode. Implemented in `apps/thirdeye/engine.cpp` (`TransferState`).

Reference for the EOB1 item layout (which `ITEM.DAT` and the CREATE.SAV item array
both use): https://moddingwiki.shikadi.net/wiki/Eye_of_the_Beholder_item.dat_Format

---

## 1. CHARGEN/ITEM.DAT — base item table (EOB1 14-byte format, verified)

```
offset 0x00  u16  NumberOfItems            ( = 434 )
offset 0x02  Item[NumberOfItems]           ( 14 bytes each )
then         u16  NumberOfItemName          ( = 123 )
then         char ItemName[123][35]         ( 35-byte fixed strings )
```

Item record (14 bytes):

| off | size | field   | meaning |
|-----|------|---------|---------|
| +0  | u8   | unid    | name-string index when unidentified |
| +1  | u8   | id      | name-string index when identified |
| +2  | u8   | bits    | flags: 0x80 glow/magic, 0x40 identified, 0x20 cursed, 0x08 life-drain |
| +3  | u8   | pic     | inventory icon number |
| +4  | u8   | type    | item type (0..56+; see `table123` below) |
| +5  | u8   | subpos  | placement (floor/wall 0..7, inventory slot 0..26, compartment 8) |
| +6  | i16  | pos     | position = x + y*32; ≤0 = consumed |
| +8  | i16  | next    | next item index (linked list) |
| +10 | i16  | prev    | previous item index |
| +12 | u8   | level   | dungeon level |
| +13 | i8   | value   | value / kind code; −1 = consumed |

`434 items * 14 + 2 + 2 + 123*35 = 10385` = the exact ITEM.DAT size. ✓

---

## 2. CHARGEN/CREATE.SAV — the created party

```
0x000  header: "CHARGEN\0" "..\0" "aesop.exe\0" 0x00 0x01     (16 bytes)
0x016  PC record [0]  (Bob)    345 bytes
0x16f  PC record [1]  (Carol)  345 bytes
0x2c8  PC record [2]  (Ted)    345 bytes
0x421  PC record [3]  (Alice)  345 bytes
0x57a  ...party-level data (not yet mapped: spells, etc.)...
0x894  item array: EOB1 14-byte records, the party's items, ids 434.. (see §4)
```

### PC record (345 bytes, fixed stride 0x159)

`player_attrib(pc, attr, size)` reads `size` little-endian bytes at file offset
**`(0x16 - 2) + pc*345 + attr`** — i.e. `attr` is a byte offset into the 345-byte
record **biased by +2** (attr 2 = the record's first byte). Confirmed by the
transfer's name-copy loop (`attr 2..12` → `name[0..10]`).

Field map (record offset = `attr − 2`), from the default party:

| attr | size | record off | field | Bob |
|------|------|-----------|-------|-----|
| 2..12 | 11 | 0  | name (NUL-terminated, 11 bytes) | "Bob" |
| 14   | 1 | 12 | STR | 11 |
| 16   | 1 | 14 | STR% (exceptional) | 0 |
| 18   | 1 | 16 | INT | 18 |
| 20   | 1 | 18 | WIS | 13 |
| 22   | 1 | 20 | DEX | 18 |
| 24   | 1 | 22 | CON | 10 |
| 26   | 1 | 24 | CHA | 16 |
| 29   | 2 | 27 | HP | 51 |
| 33..40 | 1 | 31.. | class/race/level/portrait/food (partly mapped) | |
| 41   | 4 | 39 | XP (class 1) | 750000 |
| 45   | 4 | 43 | XP (class 2), −1 if none | −1 |
| 49   | 4 | 47 | XP (class 3), −1 if none | −1 |

(The ability scores are stored as `current,max` byte pairs starting at record
offset 11, so attr 14/16/18/… read the "current" byte of each pair.)

### Inventory slots (record offset 219)

26 word slots at record offset **219** (`attr ≈ 221..`): `inventory[slot]` =
the item **id** (0 = empty). Default party:

```
Bob:   slot0=435 slot2=436 slot3=437 slot4=438 slot5=439 slot17=434
Carol: slot0=441 slot2=442 slot3=443 slot4=444 slot17=440
Ted:   slot0=446 slot1=450 slot2=447 slot3=448 slot4=449 slot5=451 slot6=452 slot17=445
Alice: slot0=455 slot1=454 slot2=456 slot3=457 slot4=458 slot5=459 slot6=460 slot17=453
```

---

## 3. The party item array (CREATE.SAV @ 0x894)

The char-gen's items are stored as **EOB1 14-byte records** (same layout as
ITEM.DAT §1) beginning at file **0x894**, with ids running from **434** (=
ITEM.DAT's `NumberOfItems`). So:

```
item record for id N  =  file 0x894 + (N - 434) * 14
```

The default party uses ids 434..460 (27 items, 0x894..0xa0e). Each record is a
copy of an ITEM.DAT template (id 434 == ITEM.DAT item 1, id 435 == item 2, …).

`item_attrib(pc, slot, attr)` resolves `inventory[slot]` → id → that record and
returns:

| attr | record field | used as |
|------|--------------|---------|
| 0 | bits (+2)  | the new item object's `itmflags` |
| 1 | type (+4)  | key into `table123` → the EOB3 item object class |
| 2 | value (+13, signed) | the new item object's `bonus` |

attr 1 returns **−1 for an empty slot** (id < 434), which the transfer treats as
"no item" and skips.

---

## 4. table123 — EOB item type → EOB3 item object

`xfer`'s `table123` (constant LONG table at code offset 123) is a 2-column map
`[item_type, eob3_object]`. The transfer matches `item_attrib(…,1)` (the type)
against column 0 and creates the column-1 object (a free slot from
`PROCEDURE_1906`, ids 100..999). The map:

```
type:  0  1  2  5  7  9 10 11 12 13 14 15 16 18 19 20 21 22 24 25 27 28 29 30 31 32 34 35 39 41
obj : 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68  0  0  0 69
```

The object value resolves to EOB3 item code classes (1323 "axe", 1325 "short
sword", 1349 "robe", …). Types 34/35/39 map to 0 (handled specially / no object).

---

## 5. The transfer flow (`xfer` SOP, classes 1380/1369)

```
start (CHGN) -> create xfer(1380) -> SEND "convert created party" (M:16)
  open_transfer_file("CHARGEN\CREATE.SAV")
  SEND "transfer" (M:15):
    read_initial_items()
    for pc in 0..3:
      create PC object (class 1369) at index 32+pc
      copy name (player_attrib attr 2..12) + abilities/HP/XP (attr 14..49)
      for slot in 0..25:
        id = item_attrib(pc, slot, 1)            ; -1 -> skip
        find id's type in table123 -> obj class
        create item object (free slot), set itmflags=attr0, bonus=attr2
        PC.inventory[slot] = item object
    write_initial_tempfiles()                    ; (still a stub here)
```

---

## 6. thirdeye implementation status

`engine.cpp` `TransferState`:
- `open_transfer_file` — buffers CREATE.SAV (maps the `\` DOS path to the `.RES` sibling). **real**
- `player_attrib` — `(0x16-2) + pc*345 + attr`, size 1/2/4. **real** (names, abilities, HP, XP)
- `item_attrib` — slot→id→record, attr 0/1/2 = bits/type/value. **real** (starting gear transfers)
- `read_initial_items` / `arrow_count` — stubs (return 0); the party items live in
  CREATE.SAV, so the transfer doesn't need ITEM.DAT loaded to build the party.
- `write_initial_tempfiles` — stub. Writing the live save (`ITEMS.TMP`/`LVLnn.TMP`)
  is the remaining savegame round-trip; see the Phase-3 save/load note in CLAUDE.md.

**Result:** `--skip-menu` transfers the real default party — Bob/Carol/Ted/Alice
with correct portraits, names, ability scores, HP, *and* their starting gear
(axes, swords, robes, …) as live EOB3 item objects in their inventory slots.

### Still to map
- PC record attrs 33..40 (class/race/level/portrait/food — partially identified).
- The party-level region 0x57a..0x894 (spells, etc.).
- `ITEM.DAT`'s `ITEMTYPE.DAT` companion (1026 B) — per-type templates.

---

## 7. Live-save formats (`SAVEGAME/*.TMP`) — structural RE so far

These are the `write_initial_tempfiles` / `resume_*` / `restore_*` files (the
savegame round-trip / "Continue the Quest"). Convention: **`.TMP` = live state**,
**`_00.BIN`/`_01.BIN` = the two saved slots** (load copies `_NN.BIN`→`.TMP`).
The native functions have no source, so this is structural RE from the data; it's
**mapped at the layout level but not field-complete** (a dedicated effort — see
the note below).

### `LVLnn.TMP` (per-dungeon-level state, 14 levels, ~10–13 KB)

`LVL01.TMP` = 12514 = **500 level-object records × 25 bytes + a 14-byte trailer**.
Each record: `u8 flag, u16 id, …22 bytes…`. The ids run **1000, 1001, 1002…**
(sequential per level — the level's monster / floor-item / decoration object
slots; ~366 of the 500 are "active"). The record fields (positions, link words,
type) are partly visible but not yet fully labelled, and the array transitions
into further sections past the first block.

### `ITEMS.TMP` (the main save: party + state + item array, ~20 KB)

Despite the name this is the **main save file** (party, position, items). Offsets
below are from the goldbox.games thread (Ishad Nha, topic 3417), verified against
`SAVEGAME/ITEMS.TMP` (the "Quick Start Party" save) and `ITEMS_00.BIN`:

```
header / game state (0..676)
  +252  u8   party X        (0..255)        \ verified: dungeon 3 @ (7,24)
  +253  u8   party Y        (0..255)        |  facing 1
  +254  u8   facing                          |
  +255  u8   dungeon number (1..14)         /
  ... (much still unmapped) ...
+677  Character record [6], 627 bytes each   (PC1..PC4 then NPCs "Bug","Father Jon")
        PC1 @677  PC2 @1304  PC3 @1931  PC4 @2558  +627…
  within a record:
    +127  equipment slots (2-byte item refs): body, bracers, right hand, rings,
          boots, left hand, pouch A/B/C, necklace, helmet, arrows type/qty …
    +155  name (string)                       (verified: "Sir Mikeal", …)
    + ability scores / HP / class / level / XP follow the EOB1/2 char layout
      (see the moddingwiki char format) — to be field-mapped.
then  serialized item *objects* (`u16 class` like 1347 + static block, ~21-byte
      stride) — the live items `write_initial_tempfiles` wrote and
      `resume_items`/`restore_items` rebuild.
```

So the Quick Start Party = Sir Mikeal / Stonebeard / Salina / Lady Reeya on
dungeon level 3. The static maze is **not** in the save — it loads from EYE.RES
map resources (confirmed: ASE3's 32×32 `Maps/NN.dat` don't appear in `LVLNN.TMP`).

### Status / sequencing note

The save loading is done by EOB3's own native runtime functions
(`resume_items`/`resume_level`/`restore_items`/`restore_level_objects`/
`write_initial_tempfiles`) — **not** a separate program. They have **no released
source** (only named in `arun.map`); the released AESOP runtime has no generic
object-save. So the format is community-RE'd: the offsets above come from the
**goldbox.games thread** (topic 3417) + the **Synalysis `eobdata.grammar`** (EOB1/2
char record) + **ASE3** (automapper; memory-searches the running game), and are
verified here against the `SAVEGAME/` files.

The party/position layout is now usable; the per-record ability/HP/class fields
and the level-object/item-object records still need field-level mapping. A full
round-trip (load *and* save, reconciled with the live SOP object graph) is best
sequenced **after in-game movement / dungeon-view rendering** — the maze comes
from EYE.RES, the party is already loaded from the char-gen transfer, and the
save's gameplay-critical bit (position + dungeon level, `+252..255`) only matters
once there's a dungeon to place the party in.

### Sources
- ITEM.DAT (EOB1): https://moddingwiki.shikadi.net/wiki/Eye_of_the_Beholder_item.dat_Format
- EOB1/2 char record: https://moddingwiki.shikadi.net/wiki/Eye_of_the_Beholder_Save_Game_Format
- Synalysis grammar: https://github.com/synalysis/Grammars/blob/master/eobdata.grammar
- EOB3 save offsets: https://forums.goldbox.games/index.php?topic=3417.0
- ASE3 automapper (maps, item names): https://ase3.zorbus.net/
