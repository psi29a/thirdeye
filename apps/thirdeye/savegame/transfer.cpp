#include "transfer.hpp"

namespace THIRDEYE::savegame {

int TransferState::table123Lookup(int type) {
	switch (type) {
	case 0:  return 1323; // axe
	case 1:  return 1324; // long sword
	case 2:  return 1325; // short sword
	case 5:  return 1326; // dagger
	case 7:  return 1327; // bow
	case 9:  return 1328; // spear
	case 10: return 1329; // halberd
	case 11: return 1330; // mace
	case 12: return 1331; // flail
	case 13: return 1332; // staff
	case 14: return 1333; // sling
	case 15: return 1334; // dart
	case 16: return 1335; // arrow
	case 18: return 1336; // rock
	case 19: return 1337; // banded mail
	case 20: return 1338; // chain mail
	case 21: return 1339; // helm
	case 22: return 1340; // leather armor
	case 24: return 1341; // plate mail
	case 25: return 1342; // scale mail
	case 27: return 1343; // shield
	case 28: return 1344; // thieves' tools
	case 29: return 1345; // spellbook
	case 30: return 1346; // holy symbol
	case 31: return 1347; // rations
	case 32: return 1348; // leather boots
	case 41: return 1349; // robe
	case 42: return 1350; // ring of protection
	case 43: return 1351; // bracers of protection
	case 44: return 1352; // necklace
	case 45: return 1353; // two-handed sword
	case 50: return 1354; // sceptre
	case 59: return 1355; // cloak of protection
	case 60: return 1356; // hammer
	default: return 0;
	}
}

TransferState::SlotCat TransferState::categoryForClass(int cls) {
	switch (cls) {
	case 1323: case 1324: case 1325: case 1326:
	case 1328: case 1329: case 1330: case 1331:
	case 1332: case 1354: case 1356:
		return SlotCat::WEAPON;
	case 1353: return SlotCat::WEAPON_2H;
	case 1327: case 1333: return SlotCat::RANGED;
	case 1334: case 1335: case 1336: return SlotCat::AMMO;
	case 1337: case 1338: case 1340: case 1341:
	case 1342: case 1349:
		return SlotCat::BODY;
	case 1339: return SlotCat::HELMET;
	case 1343: return SlotCat::SHIELD;
	case 1348: return SlotCat::BOOTS;
	case 1350: return SlotCat::RING;
	case 1351: return SlotCat::BRACERS;
	case 1352: return SlotCat::NECKLACE;
	// ponytail: cloak (1355) has no dedicated body offset in ITEMS.TMP --
	// route to pouches/backpack until we RE its slot.
	case 1355:
	case 1344: case 1345: case 1346: case 1347:
	default: return SlotCat::CARRIED;
	}
}

int32_t TransferState::playerAttrib(int pc, int attr, int size) const {
	// `pc`/`size` come from VM args: reject a bad size (the `8*i` shift would be
	// >= 32 -- UB) and a negative pc before computing the offset.
	if (pc < 0 || (size != 1 && size != 2 && size != 4))
		return 0;
	size_t off = static_cast<size_t>(kPcBase + pc * kPcStride + attr);
	uint32_t v = 0;
	for (int i = 0; i < size; ++i) {
		if (off + i >= data.size()) break;
		v |= static_cast<uint32_t>(data[off + i]) << (8 * i);
	}
	return static_cast<int32_t>(v);
}

int TransferState::readItemTypeAtCsSlot(int pc, int csSlot) const {
	size_t slotOff = static_cast<size_t>(kPcBase + 2 + pc * kPcStride +
	                                     kInvRecOff + csSlot * 2);
	if (slotOff + 1 >= data.size()) return -1;
	int id = data[slotOff] | (data[slotOff + 1] << 8);
	if (id < kItemIdBase) return -1;
	size_t rec = static_cast<size_t>(kItemArrayBase +
	                                 (id - kItemIdBase) * kItemRecSize);
	if (rec + kItemRecSize > data.size()) return -1;
	return data[rec + 4];
}

void TransferState::rebuildPlacement() {
	for (int pc = 0; pc < 4; ++pc)
		for (int i = 0; i < kInvSlots; ++i) pcItemAtSlot[pc][i] = -1;
	if (data.empty()) return;
	for (int pc = 0; pc < 4; ++pc) {
		auto free = [&](int s) { return pcItemAtSlot[pc][s] < 0; };
		auto pickFrom = [&](std::initializer_list<int> opts) -> int {
			for (int s : opts) if (free(s)) return s;
			return -1;
		};
		for (int csSlot = 0; csSlot < kInvSlots; ++csSlot) {
			int type = readItemTypeAtCsSlot(pc, csSlot);
			if (type < 0) continue;
			int cls = table123Lookup(type);
			if (cls == 0) continue;
			int t = -1;
			switch (categoryForClass(cls)) {
			case SlotCat::WEAPON:
			case SlotCat::RANGED:    t = pickFrom({16, 20}); break;
			// ponytail: 2H sword takes only slot 16; blocking slot 20 (linked
			// flag in h_stat) is a runtime concern, add when combat uses it.
			case SlotCat::WEAPON_2H: t = pickFrom({16}); break;
			case SlotCat::BODY:      t = pickFrom({14}); break;
			case SlotCat::HELMET:    t = pickFrom({25}); break;
			case SlotCat::SHIELD:    t = pickFrom({20, 16}); break;
			case SlotCat::BOOTS:     t = pickFrom({19}); break;
			case SlotCat::RING:      t = pickFrom({17, 18}); break;
			case SlotCat::BRACERS:   t = pickFrom({15}); break;
			case SlotCat::NECKLACE:  t = pickFrom({24}); break;
			case SlotCat::AMMO:
			case SlotCat::CARRIED:   t = pickFrom({21, 22, 23}); break;
			default: break;
			}
			if (t < 0)
				for (int s = 0; s < 14; ++s) if (free(s)) { t = s; break; }
			if (t >= 0) pcItemAtSlot[pc][t] = static_cast<int8_t>(csSlot);
		}
	}
}

int32_t TransferState::itemAttrib(int pc, int slot, int attr) const {
	if (pc < 0 || pc >= 4 || slot < 0 || slot >= kInvSlots)
		return attr == 1 ? -1 : 0;
	int csSlot = pcItemAtSlot[pc][slot];
	if (csSlot < 0) return attr == 1 ? -1 : 0;
	size_t slotOff = static_cast<size_t>(kPcBase + 2 + pc * kPcStride +
	                                     kInvRecOff + csSlot * 2);
	if (slotOff + 1 >= data.size())
		return attr == 1 ? -1 : 0;
	int id = data[slotOff] | (data[slotOff + 1] << 8);
	if (id < kItemIdBase)
		return attr == 1 ? -1 : 0;
	size_t rec = static_cast<size_t>(kItemArrayBase +
	                                 (id - kItemIdBase) * kItemRecSize);
	if (rec + kItemRecSize > data.size())
		return attr == 1 ? -1 : 0;
	auto rb = [&](int off) -> int { return data[rec + off]; };
	switch (attr) {
	case 0: return rb(2);                                 // bits  -> itmflags
	case 1: return rb(4);                                 // type  (table123 key)
	case 2: return static_cast<int8_t>(rb(13));           // value -> bonus
	default: return 0;
	}
}

} // namespace THIRDEYE::savegame
