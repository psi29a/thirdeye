#include "items_tmp.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace THIRDEYE::savegame {

namespace {

// All offsets verified against the Quick Start Party save (docs §2).
constexpr size_t kPosOff       = 252;
constexpr size_t kPosBytes     = 4;
constexpr size_t kPcRecordBase = 677;
constexpr size_t kPcStride     = 627;
// 10 PC-class record slots in EOB3: 4 party + 2 joined NPCs + 4 reserves.
// The reserves have name "" and class still 1369; the integration code gates
// on classNumber == 1369 *and* a populated name / non-zero HP to act on
// real records only.
constexpr size_t kMaxRecords   = 10;

// Within a PC record:
constexpr size_t kObjIndexOff  = 0;
constexpr size_t kClassOff     = 2;
constexpr size_t kBackpackOff  = 96;  // 14 words -> W:inventory[0..13]
constexpr size_t kEquipOff     = 127; // 12 words: body, bracers, rhand, lring,
                                      //           rring, boots, lhand, pouchA,
                                      //           pouchB, pouchC, necklace, helmet
constexpr size_t kArrowsTypeOff = 151;
constexpr size_t kArrowsQtyOff  = 153;
constexpr size_t kNameOff       = 155;
constexpr size_t kNameMaxLen    = 20;
constexpr size_t kRaceOff       = 175;
constexpr size_t kClassesOff    = 176;
constexpr size_t kPortraitOff   = 177;
constexpr size_t kPCstatOff     = 179;
constexpr size_t kAlignmentOff  = 180;
constexpr size_t kLevelsOff     = 181; // 3 bytes
constexpr size_t kLostLevelsOff = 184; // 3 bytes
constexpr size_t kLostHpOff     = 187;
constexpr size_t kHpCurOff      = 189;
constexpr size_t kHpMaxOff      = 191;
constexpr size_t kHbonOff       = 193;
constexpr size_t kFoodOff       = 195;
constexpr size_t kXpOff         = 197; // 3 longs (12 bytes)
constexpr size_t kStrOff        = 209;
constexpr size_t kStrPctOff     = 210;
constexpr size_t kIntOff        = 211;
constexpr size_t kWisOff        = 212;
constexpr size_t kDexOff        = 213;
constexpr size_t kConOff        = 214;
constexpr size_t kChaOff        = 215;
constexpr size_t kSparkleOff    = 216;
constexpr size_t kMagicEffOff   = 217;
constexpr size_t kTigerOff      = 221;
constexpr size_t kLostStrOff    = 222;
constexpr size_t kUnknownGapOff = 223;
constexpr size_t kSpellCntOff   = 227;
constexpr size_t kSpellStatOff  = 427;
constexpr size_t kSpellArrayLen = 200;
// Last byte we touch in a record. The PC class's static block ends at PC
// offset 609 = file +627; the record stride is 627, so we read everything
// the SOP cares about and stop at the record's natural end.
constexpr size_t kRecordFootprint = kSpellStatOff + kSpellArrayLen; // 627

uint16_t readU16(const std::vector<uint8_t> &d, size_t off) {
	return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
}

int16_t readI16(const std::vector<uint8_t> &d, size_t off) {
	return static_cast<int16_t>(readU16(d, off));
}

uint32_t readU32(const std::vector<uint8_t> &d, size_t off) {
	return static_cast<uint32_t>(d[off]) |
	       (static_cast<uint32_t>(d[off + 1]) << 8) |
	       (static_cast<uint32_t>(d[off + 2]) << 16) |
	       (static_cast<uint32_t>(d[off + 3]) << 24);
}

std::string readNulString(const std::vector<uint8_t> &d, size_t off,
                          size_t maxLen) {
	std::string s;
	for (size_t i = 0; i < maxLen && off + i < d.size(); ++i) {
		uint8_t b = d[off + i];
		if (b == 0) break;
		s.push_back(static_cast<char>(b));
	}
	return s;
}

// Map the equipment field (0xFFFF = empty) to a signed -1.
int16_t readEquipSlot(const std::vector<uint8_t> &d, size_t off) {
	uint16_t v = readU16(d, off);
	return v == 0xFFFF ? int16_t{-1} : static_cast<int16_t>(v);
}

} // namespace

ItemsTmp parseItemsTmp(const std::vector<uint8_t> &data) {
	ItemsTmp out;
	if (data.size() < kPosOff + kPosBytes)
		return out; // header truncated; no party position
	out.position.x      = data[kPosOff + 0];
	out.position.y      = data[kPosOff + 1];
	out.position.facing = data[kPosOff + 2];
	out.position.level  = data[kPosOff + 3];

	for (size_t i = 0; i < kMaxRecords; ++i) {
		size_t base = kPcRecordBase + i * kPcStride;
		if (base + kRecordFootprint > data.size())
			break; // truncated -- stop on the first record that doesn't fit.

		ItemsTmp::Character c;
		c.objectIndex = readI16(data, base + kObjIndexOff);
		c.classNumber = readI16(data, base + kClassOff);
		c.name        = readNulString(data, base + kNameOff, kNameMaxLen);
		c.race        = data[base + kRaceOff];
		c.classes     = data[base + kClassesOff];
		c.portrait    = readI16(data, base + kPortraitOff);
		c.PCstat      = data[base + kPCstatOff];
		c.alignment   = data[base + kAlignmentOff];
		for (int k = 0; k < 3; ++k) {
			c.levels[k]     = data[base + kLevelsOff + k];
			c.lostLevels[k] = data[base + kLostLevelsOff + k];
			c.xp[k]         = static_cast<int32_t>(readU32(data, base + kXpOff + k * 4));
		}
		c.lostHp      = readI16(data, base + kLostHpOff);
		c.hpCurrent   = readI16(data, base + kHpCurOff);
		c.hpMax       = readI16(data, base + kHpMaxOff);
		c.hbon        = readI16(data, base + kHbonOff);
		c.foodPct     = readI16(data, base + kFoodOff);
		c.str         = data[base + kStrOff];
		c.strPct      = data[base + kStrPctOff];
		c.intel       = data[base + kIntOff];
		c.wis         = data[base + kWisOff];
		c.dex         = data[base + kDexOff];
		c.con         = data[base + kConOff];
		c.cha         = data[base + kChaOff];

		for (int s = 0; s < ItemsTmp::Character::kBackpackSlots; ++s)
			c.backpack[s] = readEquipSlot(data, base + kBackpackOff + s * 2);
		for (int s = 0; s < ItemsTmp::Character::kEquipSlots; ++s)
			c.equip[s] = readEquipSlot(data, base + kEquipOff + s * 2);
		c.arrowsType = readI16(data, base + kArrowsTypeOff);
		c.arrowsQty  = readI16(data, base + kArrowsQtyOff);

		// Tail (spell state + active magic effects + a few smaller fields).
		c.sparkle      = data[base + kSparkleOff];
		c.magicEffects = static_cast<int32_t>(readU32(data, base + kMagicEffOff));
		c.tiger        = data[base + kTigerOff];
		c.lostStr      = data[base + kLostStrOff];
		c.unknownGap   = readU32(data, base + kUnknownGapOff);
		c.spellCnt.assign(data.begin() + base + kSpellCntOff,
		                  data.begin() + base + kSpellCntOff + kSpellArrayLen);
		c.spellStat.assign(data.begin() + base + kSpellStatOff,
		                   data.begin() + base + kSpellStatOff + kSpellArrayLen);

		// An unused NPC slot is recognizable by class != 1369 (the live save
		// only marks the joined slots; the rest may be 0xFFFF / zeroed). Keep
		// them in the output so the caller can decide; the integration code
		// will gate on classNumber == 1369.
		out.characters.push_back(std::move(c));
	}
	out.itemStreamOff = kPcRecordBase + out.characters.size() * kPcStride;
	return out;
}

ItemsTmp loadItemsTmp(const std::filesystem::path &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
	                          std::istreambuf_iterator<char>());
	return parseItemsTmp(data);
}

std::vector<ItemRecord> parseItemStream(const std::vector<uint8_t> &data,
                                        size_t streamOff,
                                        const ClassStaticSize &lookup) {
	std::vector<ItemRecord> out;
	// ponytail: env-var-gated trailer probe. Set THIRDEYE_DUMP_TRAILERS=1 to
	// print every parsed record's (id, cls, trailer) for RE; off by default
	// so the normal trace stays clean.
	const bool dumpTrailers =
	    std::getenv("THIRDEYE_DUMP_TRAILERS") != nullptr;
	constexpr uint16_t kEmptyCls = 0xFFFF;
	// Per-record layout (RE'd against the Quick Start Party save):
	//   +0  u16  id
	//   +2  u16  class (0xFFFF = empty/dead slot)
	//   +4  N    static block (= instanceStaticSize(class); 0 for empty slots)
	//   +4+N  4  trailer (always present, even on empty slots). Captured into
	//           ItemRecord::trailer for round-trip; partial RE in
	//           items_tmp.hpp. Common values: 0x0000FFFF = not placed,
	//           0x010000FF = equipped on a PC, others = container/dungeon
	//           placement metadata not yet decoded.
	// Empty slots therefore total 8 bytes; real items total 8 + N.
	constexpr uint32_t kTrailerSize = 4;
	size_t o = streamOff;
	while (o + 4 + kTrailerSize <= data.size()) {
		uint16_t id  = static_cast<uint16_t>(data[o]     | (data[o + 1] << 8));
		uint16_t cls = static_cast<uint16_t>(data[o + 2] | (data[o + 3] << 8));
		if (cls == kEmptyCls) {
			o += 4 + kTrailerSize;
			continue;
		}
		// A non-empty record we can't size means desync (unknown class, or
		// we wandered past the live item region into the level/object trailer).
		// Stop rather than mis-stride and corrupt downstream parses.
		uint32_t blockSize = lookup(cls);
		if (blockSize == 0) break;
		if (o + 4 + blockSize + kTrailerSize > data.size()) break;
		ItemRecord r;
		r.id = id;
		r.cls = cls;
		r.staticBlock.assign(data.begin() + o + 4,
		                     data.begin() + o + 4 + blockSize);
		size_t t = o + 4 + blockSize;
		r.trailer =  static_cast<uint32_t>(data[t])
		          | (static_cast<uint32_t>(data[t + 1]) << 8)
		          | (static_cast<uint32_t>(data[t + 2]) << 16)
		          | (static_cast<uint32_t>(data[t + 3]) << 24);
		if (dumpTrailers)
			std::fprintf(stderr,
			    "[trailer] off=0x%05zx id=%u cls=%u block=%u trailer=0x%08x"
			    " (%u %u %u %u)\n",
			    o, id, cls, blockSize, r.trailer,
			    data[t], data[t + 1], data[t + 2], data[t + 3]);
		out.push_back(std::move(r));
		o += 4 + blockSize + kTrailerSize;
	}
	return out;
}

bool restoreItems(const std::filesystem::path &saveDir, int slotIdx) {
	if (slotIdx < 0) return false;
	char src[24];
	std::snprintf(src, sizeof(src), "ITEMS_%02d.BIN", slotIdx);
	std::error_code ec;
	if (!std::filesystem::exists(saveDir / src, ec))
		return false; // fail fast: leave ITEMS.TMP untouched
	std::filesystem::copy_file(
	    saveDir / src, saveDir / "ITEMS.TMP",
	    std::filesystem::copy_options::overwrite_existing, ec);
	return !ec;
}

int restoreLevels(const std::filesystem::path &saveDir, int slotIdx) {
	if (slotIdx < 0) return 0;
	// Gate on the matching ITEMS_NN.BIN: the picker always calls
	// restoreItems immediately before us, so if that source doesn't exist
	// this slot has no save and we must not wipe LVL??.TMP.
	char itemsSrc[24];
	std::snprintf(itemsSrc, sizeof(itemsSrc), "ITEMS_%02d.BIN", slotIdx);
	std::error_code ec;
	if (!std::filesystem::exists(saveDir / itemsSrc, ec))
		return 0;
	int copied = 0;
	for (int lvl = 1; lvl <= 14; ++lvl) {
		char src[24], dst[24];
		std::snprintf(src, sizeof(src), "LVL%02d_%02d.BIN", lvl, slotIdx);
		std::snprintf(dst, sizeof(dst), "LVL%02d.TMP", lvl);
		if (!std::filesystem::exists(saveDir / src, ec)) continue;
		std::filesystem::copy_file(
		    saveDir / src, saveDir / dst,
		    std::filesystem::copy_options::overwrite_existing, ec);
		if (!ec) ++copied;
	}
	return copied;
}

int loadAreaInstances(const std::filesystem::path &saveDir,
                      const CdescCreate &create) {
	// Read ITEMS_00.BIN (the slot-0 scaffold that ships with the install),
	// parse the native CDESC stream, and hand off the first 15 slots
	// (0..14: kernel + 14 area-class singletons). Larger slots (item objects,
	// PCs) are restored elsewhere by our own ITEMS.TMP path -- the SOP
	// equivalent would be `restore_items` over the full 0..999 range, but we
	// only need the area instances to make the SOP "enter level" cascade fire.
	auto path = saveDir / "ITEMS_00.BIN";
	std::ifstream f(path, std::ios::binary);
	if (!f) return 0;
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
	                       std::istreambuf_iterator<char>());
	if (d.empty() || d[0] != 0x1a) return 0; // not the native binary format
	size_t off = 1;
	int created = 0;
	constexpr int kFirstAreaSlot = 1, kLastAreaSlot = 14;
	while (off + 8 <= d.size()) {
		uint16_t slot = d[off] | (uint16_t(d[off + 1]) << 8);
		uint32_t cls  = d[off + 2] | (uint32_t(d[off + 3]) << 8) |
		                (uint32_t(d[off + 4]) << 16) |
		                (uint32_t(d[off + 5]) << 24);
		uint16_t size = d[off + 6] | (uint16_t(d[off + 7]) << 8);
		off += 8;
		if (off + size > d.size()) break;
		// 0xFFFFFFFFu = empty slot sentinel; > 0xFFFF = corrupt / future-format
		// (every real EOB3 class number fits in 16 bits, and the VM's class
		// table is keyed by uint16_t -- the on-disk u32 is just the AESOP
		// container's natural width). Skip rather than silently truncate.
		if (cls != 0xFFFFFFFFu && cls <= 0xFFFFu &&
		    slot >= kFirstAreaSlot && slot <= kLastAreaSlot) {
			std::vector<uint8_t> data(d.begin() + off, d.begin() + off + size);
			create(static_cast<int>(slot), static_cast<uint16_t>(cls), data);
			++created;
		}
		off += size;
		if (slot >= kLastAreaSlot) break; // done -- no need to scan further
	}
	return created;
}

} // namespace THIRDEYE::savegame
