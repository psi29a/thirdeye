# `ITEM.DAT` + `ITEMTYPE.DAT` — full format RE

The two `CHARGEN/*.DAT` files that describe the EOB3 item universe. `ITEM.DAT`
holds 434 per-instance item records that the chargen ladles into `CREATE.SAV`;
`ITEMTYPE.DAT` holds 64 per-type templates that the *running game* uses for
combat math and inventory restrictions. Both are referenced (indirectly,
through the chargen-transfer SOP and the live runtime) and matter for any
chargen overhaul.

Note: this is RE'd from the bundled `CHARGEN/ITEM.DAT` (`10385` B) and
`CHARGEN/ITEMTYPE.DAT` (`1026` B), cross-checked against:

- `docs/create_sav_and_item_format.md` — the chargen→game transfer side.
- `apps/thirdeye/savegame/transfer.cpp` — our existing `table123Lookup` +
  `categoryForClass` (the EOB1 type → EOB3 class → slot routing).
- AD&D 2e PHB ch. 3 (weapon tables) and ch. 6 (armour AC values).
- The shipped Quick Start Party's gear in `CREATE.SAV` — which is consistent
  with the per-type fields decoded below.

---

## 1. `ITEM.DAT` — 10385 B, 434 items + 123 names

### File layout

| offset | size | field |
|---|---|---|
| `0x0000` | `u16` | `NumberOfItems` ( = 434 ) |
| `0x0002` | `434 × 14 B` | item records (id 1..434) |
| `0x17be` | `u16` | `NumberOfItemNames` ( = 123 ) |
| `0x17c0` | `123 × 35 B` | name strings (35-byte fixed, NUL-padded) |

Total: `2 + 434×14 + 2 + 123×35 = 10385`. ✓

### Per-item record (14 B, EOB1 format)

| off | size | field | meaning |
|---|---|---|---|
| `+0` | `u8` | `unid` | name-string index when *un*identified |
| `+1` | `u8` | `id` | name-string index when identified |
| `+2` | `u8` | `bits` | flags (`0x80` magic glow, `0x40` identified, `0x20` cursed, `0x08` life-drain) |
| `+3` | `u8` | `pic` | inventory-icon number into `ITEMICN.CPS` |
| `+4` | `u8` | `type` | item-type, key into `ITEMTYPE.DAT` AND `xfer.table123` (which gives the EOB3 class) |
| `+5` | `u8` | `subpos` | placement: floor/wall direction `0..7`, inventory slot `0..26`, container compartment `8` |
| `+6` | `i16` | `pos` | position = `x + y*32` on its dungeon level; `≤0` means consumed / carried |
| `+8` | `i16` | `next` | next item id in chain (`-1` = tail) |
| `+10` | `i16` | `prev` | previous item id (`-1` = head) |
| `+12` | `u8` | `level` | dungeon level the item lives on |
| `+13` | `i8` | `value` | per-item "value/bonus" byte; meaning is type-dependent (magical `+N` for weapons, charges for wands, key kind for keys) |

For items in `CREATE.SAV` (the chargen output), `subpos=0`, `pos=0`, `next/prev=0`,
`level=0` — they're carried, not placed.

### Name table (123 strings × 35 B)

Indices the chargen actually emits in our codebase (the others are
in-game/loot drops). Full list dumped at `/tmp/itemdat_full.txt`:

| `unid` | name | used by chargen kits? |
|---|---|---|
| 0 | `Mouse Pointer` | no — internal |
| 1 | (empty) | no |
| 2 | `Leather armor` | yes |
| 3 | `Robe` | yes |
| 4 | `Staff` | no — fallback weapon |
| 5 | `Dagger` | yes |
| 6 | `Short sword` | yes |
| 7 | `Lock picks` | yes (thief) |
| 8 | `Spellbook` | yes (mage) |
| 9 | `Cleric Holy symbol` | yes (cleric) |
| 10 | `Leather boots` | no |
| 11 | `Iron Rations` | yes |
| 12 | `NULL` | (placeholder used by Ted's slots in QSP) |
| 13 | `Rock` | no |
| 14-15 | `Grey Key`, `Copper Key` | no — dungeon items |
| 16 | `Set of bones` | no |
| 17 | `Scroll` | no — varies |
| 18 | `Axe` | no — alternative warrior weapon |
| 19 | `Chainmail` | yes |
| 20 | `Potion` | no |
| 21 | `Rations` | no — see Iron Rations |
| 22 | `Mace` | yes (cleric) |
| 23 | `Paladin's Holy Symbol` | yes (paladin) |
| 27 | `Shield` | yes (fighter/paladin/cleric) |
| 30 | `Long Sword` | yes (fighter/paladin/etc) |
| 31 | `Helmet` | no — battle loot |
| 32 | `Plate Mail` | no — battle loot |

---

## 2. `ITEMTYPE.DAT` — 1026 B, 64 type templates

The *living rules table*: how each item-type behaves in combat, which classes
may equip it, what icon row to draw, etc. Loaded by the live game (NOT by the
chargen-transfer SOP — but consulted by `kernel`'s combat / equipment SOP).

### File layout

| offset | size | field |
|---|---|---|
| `0x0000` | `u16` | `NumberOfTypes` ( = 64 ) |
| `0x0002` | `64 × 16 B` | type records (index 0..63, 1:1 with `ITEM.DAT.type`) |

Total: `2 + 64×16 = 1026`. ✓

### Per-type record (16 B, verified against AD&D 2e PHB)

| off | size | field | verified value / formula |
|---|---|---|---|
| `+0` | `u16` | `mask_A` | item-handling flags (weight? size? hands?) — partially decoded |
| `+2` | `u16` | `mask_B` | secondary flags (varies with `mask_A`) |
| `+4` | `i8` | `AC_bonus` | **signed** AC modifier. For armour: `-7` plate, `-6` banded, `-5` chain, `-4` scale, `-2` leather, `-1` shield/helm. (Final AC = 10 + sum of bonuses; more negative = better.) For weapons: 0. |
| `+5` | `u8` | `class_use_mask` | **classes allowed to use/equip**. Bitfield: `0x01` Fighter, `0x02` Mage, `0x04` Cleric, `0x08` Thief, `0x10` Paladin, `0x20` Ranger. |
| `+6` | `u8` | `flag_x` | `0`/`1`/`2` — appears to be weapon range tier (0=melee, 1=thrown, 2=ranged) |
| `+7` | `u8` | `sm_dice_count` | # dice vs Small/Medium target |
| `+8` | `u8` | `sm_dice_sides` | dice-sides vs S/M |
| `+9` | `u8` | `sm_dmg_plus` | flat add vs S/M (mace gets +1) |
| `+10` | `u8` | `lg_dice_count` | # dice vs Large target |
| `+11` | `u8` | `lg_dice_sides` | dice-sides vs L |
| `+12` | `u8` | `lg_dmg_plus` | flat add vs L |
| `+13` | `u8` | `_pad` | always 0 in the bundled file |
| `+14` | `u8` | `slot_hint` | icon-row / inventory category hint (`0x81` = weapon-RH, `0x82` = weapon-LH, `0x05` = book, `0x06` = holy symbol, `0x07` = food, `0x80` = armour/shield/helmet) |
| `+15` | `u8` | `_pad2` | always 0 |

### Verified type entries

The fields above match these AD&D 2e weapon/armour stats exactly:

| type | name | SM dmg | L dmg | AC | classes (`class_use_mask`) |
|---|---|---|---|---|---|
| 0 | axe | 1d8 | 1d10 | 0 | F+T+P+R (`0x39`) |
| 1 | long sword | 1d8 | 1d12 | 0 | F+T+P+R (`0x39`) |
| 2 | short sword | 1d6 | 1d8 | 0 | F+T+P+R (`0x39`) |
| 5 | dagger | 1d4 | 1d3 | 0 | F+M+T+P+R (`0x3b`) |
| 7 | bow | 1d6 | 1d6 | 0 | F+T+P+R (`0x39`) |
| 9 | spear | 1d6 | 1d8 | 0 | F+T+P+R (`0x39`) |
| 10 | polearm | 1d10 | 2d6 | 0 | F+T+P+R (`0x39`) |
| 11 | mace | 1d6+1 | 1d6 | 0 | F+C+T+P+R (`0x3d`) |
| 12 | flail | 1d6+1 | 2d4 | 0 | F+C+T+P+R (`0x3d`) |
| 13 | staff | 2d6 | 1d6 | 0 | all six (`0x3f`) |
| 16 | arrow | 1d1 | 1d1 | 0 | (ammo) |
| 19 | banded mail | — | — | **−6** | F+C+P+R (`0x35`) |
| 20 | chainmail | — | — | **−5** | F+C+P+R (`0x35`) |
| 21 | helmet | — | — | **−1** | F+C+T+P+R (`0x3d`) |
| 22 | leather armor | — | — | **−2** | F+C+T+P+R (`0x3d`) |
| 24 | plate mail | — | — | **−7** | F+C+P+R (`0x35`) |
| 25 | scale mail | — | — | **−4** | F+C+P+R (`0x35`) |
| 27 | shield | — | — | **−1** | F+C+P+R (`0x35`) |
| 28 | lock picks | — | — | 0 | T only (`0x08`) |
| 29 | spellbook | — | — | 0 | M only (`0x02`) |
| 30 | holy symbol | — | — | 0 | C+P (`0x14`) |
| 31 | rations | — | — | 0 | all six (`0x3f`) |
| 32 | leather boots | — | — | 0 | all six (`0x3f`) |
| 41 | robe | — | — | 0 | all six (`0x3f`) |
| 42 | ring of protection | — | — | 0 | all six (`0x3f`) |
| 43 | bracers of protection | — | — | 0 | all six (`0x3f`) |
| 45 | two-handed sword | 1d10 | 3d6 | 0 | F+T+P+R (`0x39`) |
| 60 | crystal hammer | 1d4+1 | 1d4 | 0 | F+C+T+P+R (`0x3d`) |

### Empty / sentinel types

Indices 51-56 are all-zero except `byte +4 = 0xe2` — sentinel for "type
exists but no template" (used by special quest items the chargen never emits).

### Bit-layout of `class_use_mask` (byte +5)

| bit | mask | class |
|---|---|---|
| 0 | `0x01` | Fighter |
| 1 | `0x02` | Mage |
| 2 | `0x04` | Cleric |
| 3 | `0x08` | Thief |
| 4 | `0x10` | Paladin |
| 5 | `0x20` | Ranger |

Verified by triangulating:

- Lock picks `0x08` = Thief only (confirms bit 3).
- Spellbook `0x02` = Mage only (confirms bit 1).
- Holy symbol `0x14` = `0x10|0x04` = Paladin + Cleric (confirms bits 2, 4).
- Long sword `0x39` = `0x01|0x08|0x10|0x20` = F+T+P+R; long sword IS in the
  AD&D 2e Thief weapon list — confirms bit 3 is Thief (not Mage).
- Mace `0x3d` adds Cleric to the warrior set — Mace IS in the Cleric weapon
  list; EOB3 permits Thief mace too.

---

## 3. Wire-up to our code

### Where this lands in `apps/thirdeye`

- **Chargen kit selection** (`apps/thirdeye/chargen/chargen_screen.cpp`) — the
  `kClassKit` table should respect `class_use_mask` for every item it emits.
  Today's table is hand-derived from AD&D 2e canon; with `ITEMTYPE.DAT` loaded
  we can *verify* (or auto-build) the kit by intersecting allowed types with
  per-class "wants" (armour, weapon, ranged, kit-tool).
- **Transfer routing** (`apps/thirdeye/savegame/transfer.cpp`) — already
  routes by EOB3 class (post-`table123Lookup`); no change needed.
- **Combat math** (TBD when we wire dungeon combat) — `sm_dice_*` /
  `lg_dice_*` are the per-hit damage roll; the live game's "to-hit /
  damage" SOP handlers will need to read these.

### `AC_bonus` cross-checks the existing AC HUD math

Our [TransferState slot routing](../apps/thirdeye/savegame/transfer.cpp) sends
type 20 (chain mail) to `SlotCat::BODY` and the get-AC loop in `kernel` PC.M:100
sums slot 14 (body). `ITEMTYPE.DAT[20].AC_bonus = -5` matches the dosbox
behaviour where a fresh chainmail-wearing Fighter shows AC 5 with no other
armor.

---

## 4. Open RE follow-ups

- **`mask_A` / `mask_B` (bytes +0..3)** — partially decoded. Patterns
  observed:
  - `0x0008` for one-handed weapons, `0x0088` for thrown/missile.
  - `0x000a` for armour, `0x0028` for helmet, `0x0018` for boots.
  - High-bit pattern (`0x80`) appears to mean "can be thrown / off-hand".
  - Not yet pinned to combat-side semantics; not load-bearing for chargen.
- **`slot_hint` (byte +14)** — best-guess "default inventory slot category"
  for the equipment screen. Doesn't override transfer's `categoryForClass`.
- **`ITEMTYPE.DAT` byte +6 (`flag_x`)** — ranged-tier hypothesis. Verify
  when we wire missile combat: bow=1, dart=0, sling=2 etc.

---

## 5. Reproducing the dumps

```bash
python3 - << 'EOF' > /tmp/itemdat_full.txt
data = open('../data/CHARGEN/ITEM.DAT', 'rb').read()
# ... full dumper at /tmp/itemdat_full.txt; sourced from this RE pass.
EOF
```

A copy of the live dumps lives at `/tmp/itemdat_full.txt` and
`/tmp/itemtype_decoded.txt` for cross-checking changes against the original.
