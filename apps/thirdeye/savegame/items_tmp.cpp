#include "items_tmp.hpp"

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
		for (int i = 0; i < 3; ++i) {
			c.levels[i]     = data[base + kLevelsOff + i];
			c.lostLevels[i] = data[base + kLostLevelsOff + i];
			c.xp[i]         = static_cast<int32_t>(readU32(data, base + kXpOff + i * 4));
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
	constexpr uint16_t kEmptyCls = 0xFFFF;
	// Per-record layout (RE'd against the Quick Start Party save):
	//   +0  u16  id
	//   +2  u16  class (0xFFFF = empty/dead slot)
	//   +4  N    static block (= instanceStaticSize(class); 0 for empty slots)
	//   +4+N  4  trailer (always present, even on empty slots). For empty
	//           slots the trailer is the entire 4-byte payload (0xFFFFFFFF
	//           seen consistently); for real items it varies per record --
	//           plausibly a free-list/link pointer the original writer keeps
	//           outside the SOP static block. We don't decode it; just skip.
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
		out.push_back(std::move(r));
		o += 4 + blockSize + kTrailerSize;
	}
	return out;
}

} // namespace THIRDEYE::savegame
