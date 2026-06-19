# EOB3 equipment slots — body-part RE

How items get placed onto a character's body when the party is created (or
loaded). The short answer: **the engine currently places by slot *position*,
not by item *type*** — so a chainmail can end up in the weapon hand and a
dagger in the ring slot. This doc pins down the real layout so a typed
placement can replace the heuristic in [TransferState in
apps/thirdeye/engine.cpp](../apps/thirdeye/engine.cpp).

References:
- [docs/create_sav_and_item_format.md](create_sav_and_item_format.md) — the
  source-side chargen format (CREATE.SAV + ITEM.DAT) that we read.
- [docs/eob3_savegame_format.md](eob3_savegame_format.md) — the live save
  (ITEMS.TMP) that the running game writes — pins the EOB3 equipment layout.

---

## 1. EOB3 PC class — `W:inventory[26]`, slot → body part

Disassembled from `EYE.RES` resource 1369 (`PC`). The PC class exports
`W:inventory` as 26 `W` (word) slots at static byte offset **81** (so each
`inventory[i]` lives at PC-record byte `81 + i*2`). It also has separate
`W:quiver` (byte 133) and `W:arrows` (byte 135) — these are *not* part of
`W:inventory`.

The mapping below is established by reading three EOB3 handlers in
[/tmp/eyeres_1369_PC.ascii.txt] (and verified against the goldbox community
ITEMS.TMP RE; see §3):

- **`M:169 "free hand"`** (line 3350) — checks `inventory[16]` first, then
  `inventory[20]`. Returns 0 if both occupied. → slots **16** and **20** are
  the two weapon hands.
- **`M:26 "drop item"`** (line 5268) — when the dropped slot is `16`, clears
  bit 2 of `h_stat[1]`; when it's `20`, clears bit 2 of `h_stat[0]`. So slot
  16 = **right hand** (its wielding flag lives in `h_stat[1]`), and slot 20
  = **left hand** (`h_stat[0]`). The naming is consistent with the array
  exports `B:h_stat 67,2` (two bytes, one per hand).
- **`M:100 "get AC"`** (line 2856) — iterates `inventory[14]`, then `[15]`,
  `[25]`, `[17]`, `[18]`, then the two hand slots `[16]`/`[20]` only when a
  shield is held. Every slot in the first set contributes to AC, so each is
  a piece of *protective* worn gear: **body / bracers / helmet / two rings
  of protection** (any of those can have an AC bonus). Body and helmet are
  the only "obvious" pair; the rest are pinned in §3.

| `W:inventory[i]` | Body part | Why we know |
|---|---|---|
| 0–13 | Backpack slots | `M:192 "show equipment screen"` (PC.cls, line 6692) draws each `inventory[i]` at screen-cell `i+12`; the layout puts the 14 backpack cells first. |
| 14 | Body armor | First AC-contributing slot in `get AC`; matches ITEMS.TMP +127 "body" (§3). |
| 15 | Bracers | Second AC slot; matches ITEMS.TMP +129 "bracers". |
| 16 | **Right hand** (weapon / shield) | `free hand`, `drop item`, AC shield check. |
| 17 | Ring 1 | AC slot; matches ITEMS.TMP +133 "ring1". |
| 18 | Ring 2 | AC slot; matches ITEMS.TMP +135 "ring2". |
| 19 | Boots | Matches ITEMS.TMP +137 "boots". Boots don't contribute to AC by default (only special boots — like "feather falling" — affect anything, and they do it via magic effects, not the AC loop). |
| 20 | **Left hand** (weapon / shield) | `free hand`, `drop item`, AC shield check. |
| 21 | Pouch A | ITEMS.TMP +141. Quick-access slot (potions, scrolls, etc.). |
| 22 | Pouch B | ITEMS.TMP +143. |
| 23 | Pouch C | ITEMS.TMP +145. |
| 24 | Necklace / amulet | ITEMS.TMP +147. |
| 25 | Helmet | Last AC slot; matches ITEMS.TMP +149. |
| (separate `W:quiver`@133, `W:arrows`@135) | Quiver + arrow count | ITEMS.TMP +151 "arrows type/qty". |

**No dedicated cloak slot.** Cloak of protection (EOB3 class 1355) doesn't
appear in this layout — likely it shares the body or necklace slot, or only
ever takes effect via a magic-effect flag. To be confirmed when the cloak's
behaviour is exercised.

---

## 2. CREATE.SAV — EOB1 chargen's slot order

The EOB1 character generator (CHGEN.EXE) writes 26 `u16` "item id" slots
per PC at record offset **219** (so the same `[26]` shape as EOB3's
`W:inventory`, but a different layout). The default party uses these slots:

| PC | Filled CREATE.SAV slots → item ids |
|----|------------------------------------|
| Bob (fighter/mage) | 0=435 2=436 3=437 4=438 5=439 17=434 |
| Carol (cleric) | 0=441 2=442 3=443 4=444 17=440 |
| Ted (fighter) | 0=446 1=450 2=447 3=448 4=449 5=451 6=452 17=445 |
| Alice (fighter/cleric) | 0=455 1=454 2=456 3=457 4=458 5=459 6=460 17=453 |

Resolving each id through `table123` (see §4) gives:

| PC | Slot 0 | Slot 1 | Slot 2 | Slot 3 | Slot 4 | Slot 5 | Slot 6 | Slot 17 |
|----|--------|--------|--------|--------|--------|--------|--------|---------|
| Bob | robe (lthr armor) | — | staff | dagger | short sword | thieves' tools | — | leather armor |
| Carol | holy symbol | — | leather boots | rations | axe | — | — | spellbook |
| Ted | axe | rock | axe | axe | rock | rock | (key) | axe |
| Alice | axe | (bones) | chain mail | (bones) | rations | mace | holy symbol | (bones) |

**This is *not* "slot N = body-part X" in any consistent sense.** Slot 0
holds Bob's armor and Carol's holy symbol; slot 4 holds Bob's short sword
and Carol's axe and Alice's rations. The chargen's row is a free-form
inventory the player filled; it predates EOB3's slot scheme. The fact that
the default-party slots cluster at 0–6 + 17 is convention, not semantics.

So **any sensible placement has to look at item *type*, not slot index**.

---

## 3. ITEMS.TMP — live save's equipment layout (verified)

`docs/eob3_savegame_format.md` already documents the per-character record
at PC-record byte +127 as a sequence of 13 word equipment refs:

```
+127 body       +129 bracers    +131 right hand
+133 ring 1     +135 ring 2     +137 boots
+139 left hand  +141 pouch A    +143 pouch B    +145 pouch C
+147 necklace   +149 helmet     +151 arrows type/qty
```

Verified against `data/SAVEGAME/ITEMS.TMP` (the Quick Start Party save):

```
Sir Mikeal  @ 677: +127=994(body) +131=993(rhand) +139=992(lhand), rest 0xffff
Stonebeard @1304: +127=987(body) +131=986(rhand) +139=985(lhand), rest 0xffff
Salina     @1931: +127=979(body) +131=978(rhand) +139=977(lhand), rest 0xffff
Lady Reeya @2558: +127=971(body) +131=970(rhand) +139=969(lhand), rest 0xffff
Father Jon @3812: +129=584(bracers) +131=580(rhand) +133=582(ring1) +139=581(lhand)
```

All four PCs have body+rhand+lhand triples. Father Jon (NPC cleric) has
bracers + ring + both hands but no body, which is the only point in the
save that demonstrates the bracers / ring / hand positions are
distinguishable. The layout above is therefore correct as stated; no slot
was empirically refuted.

The 12 worn equipment refs (skipping the arrows pair at +151, which goes
into the separate `W:quiver`/`W:arrows` variables) correspond **in order**
to `W:inventory[14..25]`. That is the missing link the engine's transfer
needs.

---

## 4. `table123` — EOB1 item type → EOB3 item class

From `xfer` SOP (resource 1380), pairs of `LONG` at code offset 123 (read
with `LTDA L:table123`). Each pair is `[eob1_type, eob3_obj_id]`. The
*class* of the EOB3 object to instantiate is **`1280 + obj_id`**. (E.g.
type 22 → obj 60 → class 1340 "leather armor", which matches the EYE.RES
resource listing.)

Verified pairs:

| EOB1 `type` | `obj` | EOB3 class | EOB3 class name | What EOB1 type usually is |
|---|---|---|---|---|
| 0 | 43 | 1323 | axe | (rations placeholder uses this too) |
| 1 | 44 | 1324 | long sword | femur |
| 2 | 45 | 1325 | short sword | dagger |
| 5 | 46 | 1326 | dagger | staff |
| 7 | 47 | 1327 | bow | dart / bow |
| 9 | 48 | 1328 | spear | (rare) |
| 10 | 49 | 1329 | halberd | necklace / long sword |
| 11 | 50 | 1330 | mace | rations / polearm |
| 12 | 51 | 1331 | flail | tome |
| 13 | 52 | 1332 | staff | robe |
| 14 | 53 | 1333 | sling | plate mail |
| 15 | 54 | 1334 | dart | fire sphere |
| 16 | 55 | 1335 | arrow | composite bow |
| 18 | 56 | 1336 | rock | (NULL/arrow placeholder) |
| 19 | 57 | 1337 | banded mail | spear |
| 20 | 58 | 1338 | chain mail | axe |
| 21 | 59 | 1339 | helm | long sword |
| 22 | 60 | 1340 | leather armor | (icon 31; the EOB1 leather-armor base) |
| 24 | 61 | 1341 | plate mail | helmet |
| 25 | 62 | 1342 | scale mail | cloak |
| 27 | 63 | 1343 | shield | dark moon key |
| 28 | 64 | 1344 | thieves' tools | short sword |
| 29 | 65 | 1345 | spellbook | lock picks |
| 30 | 66 | 1346 | holy symbol | spellbook / mace |
| 31 | 67 | 1347 | rations | leather boots / potion |
| 32 | 68 | 1348 | leather boots | cleric holy symbol |
| 34 | 0 | — | (special, no object created) | set of bones |
| 35 | 0 | — | (special) | set of bones |
| 39 | 0 | — | (special) | chainmail |
| 41 | 69 | 1349 | robe | leather armor / amulet |
| 42 | 70 | 1350 | ring/protection | banded armor |
| 43 | 71 | 1351 | bracers of protection | arrow |
| 44 | 72 | 1352 | necklace/adornment | bracers / ring |
| 45 | 73 | 1353 | two-handed sword | wand |
| 47 | 0 | — | (special) | horn |
| 48 | 0 | — | (special) | flail |
| 50 | 74 | 1354 | sceptre of kingly might | (rare) |
| 59 | 75 | 1355 | cloak of protection | crystal hammer |
| 60 | 76 | 1356 | green crystal hammer | hilt of talon |
| 62 | 0 | — | (special) | flail variant |

> **The EOB1 name and the resulting EOB3 class don't always match.** A
> CREATE.SAV item whose `unid` name is "Leather armor" might be type 41 →
> class 1349 "robe" in EOB3. That's a deliberate game-data choice (EOB3
> renames or re-uses sprites) — *don't* try to reconcile the names; trust
> the type-to-class mapping.

The 4-byte cells with `obj = 0` are special-cased (the SOP path through
table123 hits `LBL_3727` and skips the slot entirely), so types 34, 35, 39,
47, 48, 62 in CREATE.SAV produce **no item object** on the EOB3 side. This
is why some of Alice's items vanish on transfer.

---

## 5. EOB3 item class → body part (the table the engine needs)

Combining EYE.RES resource names with the slot-meaning table from §1:

| EOB3 class | Name | Goes in slot | Notes |
|---|---|---|---|
| 1323 axe, 1324 long sword, 1325 short sword, 1326 dagger, 1328 spear, 1329 halberd, 1330 mace, 1331 flail, 1332 staff, 1353 two-handed sword, 1354 sceptre, 1356 hammer | (weapon) | 16 (right hand), spill to 20 | Two-handed sword has to occupy both 16+20 |
| 1327 bow, 1333 sling | (ranged weapon) | 16 or 20 | Pair with arrows in quiver |
| 1334 dart, 1335 arrow, 1336 rock | (ammunition) | quiver (`W:quiver`/`W:arrows`), or 21–23 pouch | Stackable |
| 1337 banded mail, 1338 chain mail, 1340 leather armor, 1341 plate mail, 1342 scale mail, 1349 robe | (body armor) | 14 | Only one |
| 1339 helm | (helmet) | 25 | Only one |
| 1343 shield | (shield) | 20 (left hand) | Conflicts with a left-hand weapon |
| 1348 leather boots | (boots) | 19 | Only one |
| 1350 ring/protection, 1365 ring/adornment, 1366 ring/wizardry, 1367 ring/sustenance, 1368 ring/feather falling, 1425 ring/fire resistance, 1434 ring/trobriand | (ring) | 17, spill to 18 | Two ring slots |
| 1351 bracers of protection | (bracers) | 15 | Only one |
| 1352 necklace/adornment, 1442 amulet of life, 1445 amulet of death, 1439 medallion | (necklace) | 24 | Only one |
| 1344 thieves' tools, 1345 spellbook, 1346 holy symbol, 1375 paladin holy symbol, 1397 blessed holy symbol | (class equipment) | 21–23 (pouch) or backpack | Carried, not worn |
| 1347 rations, 1451 apple, 1456 iron rations | (food) | 21–23 (pouch) or backpack | |
| 1357–1364 potions, 1377 scroll, 1376 wand, 1400 torch, 1417 nameplate, 1420 talisman, 1448 hag's eye, 1459 statue arm, 1462 fountain spout, 1465 golden cup, 1408 rod fragment, 1411 orb fragment, 1414 rod of restoration | (consumable / quest) | 21–23 or backpack | |
| 1355 cloak of protection | (cloak) | TBD | No dedicated slot — likely 24 (necklace) or 14 (body). Needs further RE. |
| 1403 helm/underwater breathing | (helmet — magic) | 25 | Same as helm |

---

## 6. Why the current engine is wrong

[apps/thirdeye/engine.cpp:211-216](../apps/thirdeye/engine.cpp#L211-L216)
maps CREATE.SAV slot → EOB3 slot by *position*:

```cpp
static const int eob3ToCreate[26] = {
    7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 21,   // EOB3 backpack 0-13
    0, 1, 2, 3, 4, 5, 6, 17, 22, 23, 24, 25,                // EOB3 worn 14-25
};
```

With Alice this becomes:

| EOB3 slot (body part) | CREATE.SAV slot | Item placed |
|---|---|---|
| 14 (body) | 0 | axe |
| 15 (bracers) | 1 | (set of bones → no object) |
| 16 (right hand) | 2 | **chain mail** ← user-visible bug |
| 17 (ring 1) | 3 | (no object) |
| 18 (ring 2) | 4 | rations |
| 19 (boots) | 5 | mace |
| 20 (left hand) | 6 | holy symbol |
| 21 (pouch A) | 17 | (no object) |

Bob fares no better: a dagger lands in his ring slot.

Real EOB3 `xfer` (resource 1380) does **no remap at all** — `slot N` of
CREATE.SAV is copied verbatim into `W:inventory[N]`. With no remap,
*everything* lands in the backpack range (because CHARGEN fills slots 0–6
+ 17, all backpack slots in EOB3). The current heuristic was a "make
something show up in the worn row" workaround.

---

## 7. The right placement

```
for slot in 0..25:
    type = item_attrib(pc, slot, 1)         # CREATE.SAV item type
    if type < 0:                             # empty
        continue
    cls = table123_lookup(type)              # EOB3 class, or 0 for skip
    if cls == 0:
        continue
    bits  = item_attrib(pc, slot, 0)         # itmflags
    bonus = item_attrib(pc, slot, 2)         # signed value
    obj   = create_item(cls, bits, bonus)

    bodyslot = category_to_slot(class_category(cls), PC.inventory)
    PC.inventory[bodyslot] = obj
```

Where:

- `class_category(cls)` returns one of `WEAPON / RANGED / AMMO / SHIELD /
  BODY / HELMET / BOOTS / BRACERS / RING / NECKLACE / CLOAK / POUCH /
  BACKPACK`. The table in §5 is the source.
- `category_to_slot(cat, inv)` returns the EOB3 slot index from §1, with
  spill rules: rings 17→18, weapons 16→20, ammo to quiver if empty else
  pouches A/B/C, anything that has no free slot falls into the first empty
  backpack slot 0..13.
- Two-handed weapons (currently just class 1353) need to claim both 16 and
  20 — the second hand stores -1 with a "linked" flag so neither can be
  filled independently. Mark this in `h_stat`.

---

## 8. Open questions

1. **Cloak slot.** Class 1355 has no dedicated body-part offset in
   ITEMS.TMP. Needs an in-game test (equip a cloak of protection, save,
   inspect ITEMS.TMP).
2. **Quiver / arrow type code.** ITEMS.TMP +151 is two bytes; we have
   `W:quiver` (134) and `W:arrows` (135). The wire format vs. the runtime
   field naming hasn't been cross-checked.
3. **The `xfer` SOP loop variable `locVar_W:18` and the inner item-create
   path** ([eyeres_1380_xfer.ascii.txt line ~3920 onward]) skips items
   when the type isn't in `table123` — but it also has three different
   create paths (`LBL_3920`, `LBL_4017`, `LBL_4094`). Which one fires
   depends on the `locVar_W:18` switch — that switch encodes a
   "two-handed", "ranged", "regular" distinction we haven't decoded yet.
   May affect how the engine should mark two-handed items.
4. **EOB1 chargen worn-slot semantics.** We've verified the chargen's row
   is *not* a fixed layout (slot 0 isn't always "body"). But it's
   possible the chargen *does* sort by item-id or by some "wear order"
   internal to CHGEN.EXE. Worth a pass to be sure we're not missing a
   simpler positional rule.

## 9. Source citations

- [apps/thirdeye/engine.cpp:211-216](../apps/thirdeye/engine.cpp#L211-L216) — current (wrong) remap.
- `EYE.RES` resource 1369 `PC` — disassembled with `daesop -k EYE.RES 1369`.
  - `"free hand"` handler — `inventory[16]`/`[20]` are the hands.
  - `"drop item"` handler — confirms hand assignments via `h_stat` clearing.
  - `"get AC"` handler — slots 14, 15, 17, 18, 25 contribute to AC; 16/20 only when a shield is held.
  - `"show equipment screen"` handler — draws each `inventory[i]` at screen-cell `i+12`.
- `EYE.RES` resource 1380 `xfer` — `L:table123` (offset 123, 40 pairs of `LONG`).
- `data/SAVEGAME/ITEMS.TMP` — confirms the equipment-block layout at PC
  record offset +127 (12 worn slots + arrows trailer).
- `data/CHARGEN/CREATE.SAV` and `data/CHARGEN/ITEM.DAT` — type→name
  cross-check across all 434 ITEM.DAT base records.
