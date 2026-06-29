#include "screen.hpp"

#include "../graphics/graphics.hpp"
#include "../graphics/bitmap.hpp"
#include "../graphics/cps.hpp"
#include "../runtime/internal.hpp" // QuitRequested

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> readFile(const std::filesystem::path &p) {
	std::ifstream f(p, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
}

// Slot frames are baked into the CPS backdrop. Both rows have the same
// layout: left slot at x=17, right slot at x=81, 32-px decorative gap
// between them. Frame lines at y=63/96 (top row) and y=127/160 (bottom row).
// 32x32 portraits centered on the frame line so the portrait's top-row
// stone-blend padding (palette 0xac) merges into the frame visually.
struct Slot { int x, y; };
constexpr Slot kSlots[4] = { {17, 64}, {81, 64}, {17, 128}, {81, 128} };

// 12 race-gender combinations, listed in the order the original chargen
// shows them. Names are uppercase to match FONT6/FONT8 menu rendering.
const std::array<const char *, 12> kRaceNames = { {
	"HUMAN MALE",    "HUMAN FEMALE",
	"ELF MALE",      "ELF FEMALE",
	"HALF-ELF MALE", "HALF-ELF FEMALE",
	"DWARF MALE",    "DWARF FEMALE",
	"GNOME MALE",    "GNOME FEMALE",
	"HALFLING MALE", "HALFLING FEMALE",
} };

// 15 chargen classes in the order EOB3's CHGEN.EXE shows them: 6 single
// classes (F/R/P/M/C/T) followed by 9 multi-class combos. The picker
// filters by race via kRaceClassAllowed (humans can't multi-class,
// dwarves only get F/T, half-elves get the widest combo list, etc. -- the
// AD&D 2e PHB chapter 2 table).
const std::array<const char *, 15> kClassNames = { {
	"FIGHTER", "RANGER", "PALADIN", "MAGE", "CLERIC", "THIEF",
	"FIGHTER/CLERIC", "FIGHTER/THIEF", "FIGHTER/MAGE",
	"FIGHTER/MAGE/THIEF", "THIEF/MAGE", "CLERIC/THIEF",
	"FIGHTER/CLERIC/MAGE", "RANGER/CLERIC", "CLERIC/MAGE",
} };

// Component single-classes that make up each chargen entry, terminated with
// -1. Single-class entries (0..5) carry just themselves; multi-class entries
// (6..14) list 2 or 3 component classes for HP/XP rolls and per-class level
// tracking. Components reference the chargen UI's single-class indices
// (0=F, 1=R, 2=P, 3=M, 4=C, 5=T).
constexpr int kClassComponents[15][3] = {
	{ 0, -1, -1 },   // FIGHTER
	{ 1, -1, -1 },   // RANGER
	{ 2, -1, -1 },   // PALADIN
	{ 3, -1, -1 },   // MAGE
	{ 4, -1, -1 },   // CLERIC
	{ 5, -1, -1 },   // THIEF
	{ 0,  4, -1 },   // FIGHTER/CLERIC
	{ 0,  5, -1 },   // FIGHTER/THIEF
	{ 0,  3, -1 },   // FIGHTER/MAGE
	{ 0,  3,  5 },   // FIGHTER/MAGE/THIEF
	{ 5,  3, -1 },   // THIEF/MAGE
	{ 4,  5, -1 },   // CLERIC/THIEF
	{ 0,  4,  3 },   // FIGHTER/CLERIC/MAGE
	{ 1,  4, -1 },   // RANGER/CLERIC
	{ 4,  3, -1 },   // CLERIC/MAGE
};

int classComponentCount(int chargenClass) {
	if (chargenClass < 0 || chargenClass >= 15) return 0;
	int n = 0;
	for (int i = 0; i < 3; ++i)
		if (kClassComponents[chargenClass][i] >= 0) ++n;
	return n;
}

// AD&D 2e race/class restrictions. Race index is the chargen UI race / 2
// (M/F pairs collapse): 0=Human, 1=Elf, 2=Half-Elf, 3=Dwarf, 4=Gnome,
// 5=Halfling. Each row is the 15 chargen-UI classes (6 single + 9 multi).
// True = race can be that class. Sourced from AD&D 2e PHB chapter 2 and
// the EOB3 chargen behaviour (humans never multi-class; demihumans get
// race-specific combos). EOB3's CHGEN.EXE has the exact table in its
// data segment -- this matches the canon and the bundled Quick Start
// Party (Alice = Dwarf F/C, Carol = Elf Mage, etc.).
constexpr bool kRaceClassAllowed[6][15] = {
	// F  R  P  M  C  T  F/C F/T F/M F/M/T T/M C/T F/C/M R/C C/M
	{  1, 1, 1, 1, 1, 1, 0,  0,  0,  0,    0,  0,  0,    0,  0  }, // Human
	{  1, 1, 0, 1, 0, 1, 0,  0,  1,  1,    1,  0,  0,    0,  0  }, // Elf
	{  1, 1, 0, 1, 1, 1, 1,  0,  1,  1,    1,  0,  1,    1,  1  }, // Half-Elf
	{  1, 0, 0, 0, 0, 1, 0,  1,  0,  0,    0,  0,  0,    0,  0  }, // Dwarf
	{  1, 0, 0, 1, 1, 1, 1,  1,  1,  0,    1,  1,  0,    0,  0  }, // Gnome
	{  1, 0, 0, 0, 0, 1, 0,  1,  0,  0,    0,  0,  0,    0,  0  }, // Halfling
};

int raceCategory(int chargenRace) {
	return (chargenRace >= 0 && chargenRace < 12) ? chargenRace / 2 : 0;
}

bool classAllowedForRace(int chargenRace, int chargenClass) {
	if (chargenClass < 0 || chargenClass >= 15) return false;
	return kRaceClassAllowed[raceCategory(chargenRace)][chargenClass];
}

// AD&D 2e single-class ability minimums in chargen ability order
// (STR, INT, WIS, DEX, CON, CHA -- the same order as enum Abil below; we
// can't use NUM_ABIL here because the enum is declared later). 0 = no
// minimum. Multi-class characters must satisfy ALL components' minimums
// (in classSatisfiesAbilityMinima).
constexpr int kSingleClassMinima[6][6] = {
	// STR INT WIS DEX CON CHA
	{   9,  0,  0,  0,  0,  0  }, // FIGHTER (S9)
	{  13,  0, 14, 13, 14,  0  }, // RANGER  (S13/W14/D13/C14)
	{  12,  0, 13,  0,  9, 17  }, // PALADIN (S12/W13/Con9/Cha17)
	{   0,  9,  0,  0,  0,  0  }, // MAGE    (I9)
	{   0,  0,  9,  0,  0,  0  }, // CLERIC  (W9)
	{   0,  0,  0,  9,  0,  0  }, // THIEF   (D9)
};

bool classSatisfiesAbilityMinima(int chargenClass, const int *abil) {
	int n = std::max(1, classComponentCount(chargenClass));
	for (int i = 0; i < n; ++i) {
		int comp = (n == 1) ? chargenClass : kClassComponents[chargenClass][i];
		if (comp < 0 || comp >= 6) continue;
		for (int a = 0; a < 6; ++a)
			if (abil[a] < kSingleClassMinima[comp][a]) return false;
	}
	return true;
}

// Starting equipment templates per chargen class. Each row is up to 6
// EOB1 item types (see TransferState::table123Lookup) the new character
// gets in CREATE.SAV. Types are routed to body parts by the transfer's
// categoryForClass (chainmail->body, long sword->weapon, shield->left
// hand, etc.) so slot order here doesn't matter -- the transfer remaps.
// Sentinel = -1.
//
// Type reference (see also docs/create_sav_and_item_format.md table123):
//   1 long sword, 2 short sword, 5 dagger, 11 mace, 20 chain mail,
//   22 leather armor, 27 shield, 28 lock picks, 29 spellbook,
//   30 holy symbol, 31 rations, 41 robe.
//
// Holy-symbol unid varies: 9 = Cleric Holy symbol, 23 = Paladin's Holy
// Symbol. Tracked separately via kClassHolySymbolUnid below.
struct ItemType { int type; };
constexpr int kClassKit[15][6] = {
	// FIGHTER:   chainmail + long sword + shield + rations
	{ 20,  1, 27, 31, -1, -1 },
	// RANGER:    leather + short sword + dagger + rations
	{ 22,  2,  5, 31, -1, -1 },
	// PALADIN:   chainmail + long sword + shield + paladin holy + rations
	{ 20,  1, 27, 30, 31, -1 },
	// MAGE:      robe + dagger + spellbook + rations
	{ 41,  5, 29, 31, -1, -1 },
	// CLERIC:    chainmail + mace + shield + cleric holy + rations
	{ 20, 11, 27, 30, 31, -1 },
	// THIEF:     leather + short sword + dagger + lock picks + rations
	{ 22,  2,  5, 28, 31, -1 },
	// F/C:       chainmail + long sword + shield + cleric holy + rations
	{ 20,  1, 27, 30, 31, -1 },
	// F/T:       chainmail + long sword + dagger + lock picks + rations
	{ 20,  1,  5, 28, 31, -1 },
	// F/M:       chainmail + long sword + spellbook + dagger + rations
	{ 20,  1, 29,  5, 31, -1 },
	// F/M/T:     chainmail + long sword + spellbook + lock picks + rations
	{ 20,  1, 29, 28, 31, -1 },
	// T/M:       leather + short sword + spellbook + lock picks + rations
	{ 22,  2, 29, 28, 31, -1 },
	// C/T:       leather + mace + cleric holy + lock picks + rations
	{ 22, 11, 30, 28, 31, -1 },
	// F/C/M:     chainmail + long sword + cleric holy + spellbook + rations
	{ 20,  1, 30, 29, 31, -1 },
	// R/C:       leather + long sword + cleric holy + rations
	{ 22,  1, 30, 31, -1, -1 },
	// C/M:       chainmail + mace + cleric holy + spellbook + rations
	{ 20, 11, 30, 29, 31, -1 },
};

// Which "holy symbol" name a class uses (only matters for entries whose
// kit includes type 30). Paladin (UI class 2) uses Paladin's Holy Symbol
// (ITEM.DAT name unid 23); everyone else gets the Cleric Holy symbol
// (unid 9). All other types map to a single canonical ITEM.DAT name.
int classHolySymbolUnid(int chargenClass) {
	return (chargenClass == 2) ? 23 : 9;
}

// ITEMTYPE.DAT runtime table. 64 × 16-byte records loaded once at chargen
// entry from CHARGEN/ITEMTYPE.DAT. We use byte +5 (class_use_mask) to
// validate each class kit at boot -- if a Fighter kit ever emits a type
// whose class_use_mask doesn't include the Fighter bit, we assert + log.
// Format documented in docs/item_dat_format.md.
struct ItemTypeRec {
	uint16_t mask_a, mask_b;
	int8_t   ac_bonus;
	uint8_t  class_use_mask;
	uint8_t  flag_x;
	uint8_t  sm_n, sm_d, sm_plus;
	uint8_t  lg_n, lg_d, lg_plus;
	uint8_t  pad;
	uint8_t  slot_hint;
	uint8_t  pad2;
};
constexpr int kItemTypeCount = 64;
ItemTypeRec gItemType[kItemTypeCount] = {};
bool        gItemTypeLoaded = false;

void loadItemTypeDat(const std::filesystem::path &chargenDir) {
	if (gItemTypeLoaded) return;
	auto path = chargenDir / "ITEMTYPE.DAT";
	auto bytes = readFile(path);
	if (bytes.size() < 2 + kItemTypeCount * 16) {
		std::cout << "  [chargen: ITEMTYPE.DAT missing or short ("
		          << bytes.size() << " B); class_use_mask validation disabled]"
		          << std::endl;
		return;
	}
	uint16_t hdr = bytes[0] | (bytes[1] << 8);
	if (hdr != kItemTypeCount) {
		std::cout << "  [chargen: ITEMTYPE.DAT header says " << hdr
		          << " types, expected " << kItemTypeCount
		          << "; reading anyway]" << std::endl;
	}
	for (int i = 0; i < kItemTypeCount; ++i) {
		const uint8_t *p = bytes.data() + 2 + i * 16;
		ItemTypeRec &r = gItemType[i];
		r.mask_a         = p[0] | (p[1] << 8);
		r.mask_b         = p[2] | (p[3] << 8);
		r.ac_bonus       = static_cast<int8_t>(p[4]);
		r.class_use_mask = p[5];
		r.flag_x         = p[6];
		r.sm_n           = p[7];
		r.sm_d           = p[8];
		r.sm_plus        = p[9];
		r.lg_n           = p[10];
		r.lg_d           = p[11];
		r.lg_plus        = p[12];
		r.pad            = p[13];
		r.slot_hint      = p[14];
		r.pad2           = p[15];
	}
	gItemTypeLoaded = true;
	std::cout << "  [chargen: ITEMTYPE.DAT loaded (" << kItemTypeCount
	          << " types, class_use_mask validation enabled)]" << std::endl;
}

// Bit position in ITEMTYPE.DAT.class_use_mask per SINGLE class
// (0=Fighter, 1=Mage, 2=Cleric, 3=Thief, 4=Paladin, 5=Ranger).
// Our chargen UI uses a different ORDER (F/R/P/M/C/T = 0..5), so map.
constexpr uint8_t kSingleClassUseBit[6] = {
	0x01, // 0 FIGHTER → bit 0
	0x20, // 1 RANGER  → bit 5
	0x10, // 2 PALADIN → bit 4
	0x02, // 3 MAGE    → bit 1
	0x04, // 4 CLERIC  → bit 2
	0x08, // 5 THIEF   → bit 3
};

// Class-use bitmask combining all component classes for a chargen-UI
// class (single or multi). A multi-class character can wield any item
// usable by ANY of their components (AD&D 2e rule).
uint8_t chargenClassUseMask(int chargenClass) {
	int n = std::max(1, classComponentCount(chargenClass));
	uint8_t mask = 0;
	for (int i = 0; i < n; ++i) {
		int comp = (n == 1) ? chargenClass : kClassComponents[chargenClass][i];
		if (comp >= 0 && comp < 6) mask |= kSingleClassUseBit[comp];
	}
	return mask;
}

// At chargen start, walk every kit and assert that every item type's
// class_use_mask includes at least one of the chargen class's component
// classes. Catches typos / class-mismatched kits at boot before any
// player rolls a "Fighter equipped with spellbook" surprise.
void validateClassKits() {
	if (!gItemTypeLoaded) return;
	int problems = 0;
	for (int cls = 0; cls < 15; ++cls) {
		uint8_t classMask = chargenClassUseMask(cls);
		for (int s = 0; s < 6; ++s) {
			int type = kClassKit[cls][s];
			if (type < 0) break;
			if (type >= kItemTypeCount) {
				std::cout << "  [chargen-validate: " << kClassNames[cls]
				          << " kit item " << s << " type " << type
				          << " out of ITEMTYPE.DAT bounds]" << std::endl;
				++problems; continue;
			}
			uint8_t typeMask = gItemType[type].class_use_mask;
			if ((typeMask & classMask) == 0) {
				std::cout << "  [chargen-validate: " << kClassNames[cls]
				          << " kit type " << type
				          << " (class_use_mask=0x" << std::hex
				          << static_cast<int>(typeMask)
				          << ") not usable by chargen class (mask=0x"
				          << static_cast<int>(classMask) << std::dec
				          << ")]" << std::endl;
				++problems;
			}
		}
	}
	std::cout << "  [chargen-validate: " << problems
	          << " kit/class mismatches across 15 classes]" << std::endl;
}

int kitItemCount(int chargenClass) {
	if (chargenClass < 0 || chargenClass >= 15) return 0;
	int n = 0;
	for (int i = 0; i < 6; ++i)
		if (kClassKit[chargenClass][i] >= 0) ++n;
	return n;
}

// Picker viewport: the list area between the header (y=82) and the BACK
// button (y=172) fits 11 rows at 8 px each. Lists shorter than this draw
// in place; the 15-entry class list scrolls to keep the cursor visible.
constexpr int kListMaxVisible = 11;

int listFirstVisible(int cursor, int numEntries) {
	if (numEntries <= kListMaxVisible) return 0;
	int first = cursor - kListMaxVisible / 2;
	if (first < 0) first = 0;
	int maxFirst = numEntries - kListMaxVisible;
	if (first > maxFirst) first = maxFirst;
	return first;
}

// 9 AD&D alignments in the chargen's listing order.
const std::array<const char *, 9> kAlignmentNames = { {
	"LAWFUL GOOD",    "NEUTRAL GOOD",    "CHAOTIC GOOD",
	"LAWFUL NEUTRAL", "TRUE NEUTRAL",    "CHAOTIC NEUTRAL",
	"LAWFUL EVIL",    "NEUTRAL EVIL",    "CHAOTIC EVIL",
} };

enum class Step {
	EntryScreen,    // 4-slot picker, "Select the box of..." prompt
	PickRace,       // race list in right panel
	PickClass,      // class list
	PickAlignment,  // 9 AD&D alignments
	ShowStats,      // rolled abilities + REROLL/MODIFY/FACES/KEEP
	ModifyStats,    // per-ability +/-, selected stat value shown red
	PickPortrait,   // 4x4 grid of CHARPICS portraits with paging
	EnterName,      // type a name (max 10 chars), Enter commits + writes CREATE.SAV
};

// Ability indices, in the order they appear on screen.
enum Abil { STR=0, INTEL, WIS, DEX, CON, CHA, NUM_ABIL };

struct CharSheet {
	bool filled    = false;  // true when a complete character is rolled
	int  race      = -1;     // 0..11
	int  klass     = -1;     // 0..5
	int  alignment = -1;     // 0..8
	int  abil[NUM_ABIL] = {0, 0, 0, 0, 0, 0};
	int  hp = 0, ac = 10, lvl = 1;
	int  portrait = 1;  // CHARPICS index; placeholder until picker lands
	char name[11] = {0}; // NUL-terminated, max 10 chars
};

struct State {
	Step step             = Step::EntryScreen;
	int  activeSlot       = -1;   // 0..3 when in a sub-step
	int  raceCursor       = 0;    // 0..11 in PickRace
	int  classCursor      = 0;    // 0..5  in PickClass
	int  alignmentCursor  = 0;    // 0..8  in PickAlignment
	int  statCursor       = 0;    // 0..5  in ModifyStats (STR..CHA)
	int  portraitCursor   = 0;    // 0..89 in PickPortrait (linear index)
	int  sparkleFrame     = 0;    // animation tick for active-slot sparkles
	CharSheet party[4];
	bool done             = false; // exit chargen entirely
	bool cancelled        = false; // true if user Esc'd out (return to title)
};

// Roll 3d6. Used for each ability score on the stats screen.
int roll3d6(std::mt19937 &rng) {
	std::uniform_int_distribution<int> d6(1, 6);
	return d6(rng) + d6(rng) + d6(rng);
}

// AD&D 1e CON bonus to HP per hit die. Fighters/Paladins/Rangers get the
// full bonus; other classes cap at +2 per traditional rules, but EOB-era
// simplified -- we use the warrior bonus across the board (the in-game
// HP cap will trim if it matters).
int conHpBonus(int con) {
	if (con <= 3)  return -2;
	if (con <= 5)  return -1;
	if (con <= 14) return  0;
	if (con == 15) return  1;
	if (con == 16) return  2;
	if (con == 17) return  3;
	return  4;  // 18+
}

// HP die per single class (chargen UI order: F, R, P, M, C, T).
int singleClassHpDie(int singleClass) {
	switch (singleClass) {
	case 0: return 10; // FIGHTER  -- d10
	case 1: return  8; // RANGER   -- d8
	case 2: return 10; // PALADIN  -- d10
	case 3: return  4; // MAGE     -- d4
	case 4: return  8; // CLERIC   -- d8
	case 5: return  6; // THIEF    -- d6
	}
	return 8;
}

// HP die for a chargen entry (single or multi). AD&D 2e rule: multi-class
// characters roll each class's HD and average them (rounded down). We
// approximate by averaging the dice sizes -- the rolled HP smooths over
// the difference well enough for HUD purposes.
int classHpDie(int chargenClass) {
	int n = classComponentCount(chargenClass);
	if (n <= 1) return singleClassHpDie(chargenClass);
	int sum = 0;
	for (int i = 0; i < n; ++i)
		sum += singleClassHpDie(kClassComponents[chargenClass][i]);
	return sum / n;
}

// Roll a fresh character sheet for the given race/class/alignment. EOB3's
// campaign opens at ~level 11; we roll N hit dice (one per level) plus the
// CON bonus per HD. Fixed level 11 for now -- the original CHGEN.EXE has
// a configurable start-level we haven't RE'd yet.
//
// AD&D 2e classes have ability minima (Paladin needs Cha 17, Ranger needs
// Str/Dex/Con/Wis, multi-class needs all components' minima). 3d6 ignoring
// minima would make a Paladin/Ranger almost impossible to roll, so we
// retry up to kMaxStatRolls times until the rolled stats satisfy
// classSatisfiesAbilityMinima. If we exhaust retries (very rare with
// modest classes), the last roll stands -- the user can then MODIFY to
// patch up the missing stat. This matches the original DOS behaviour
// where Paladins / Rangers reroll dozens of times under the hood.
constexpr int kMaxStatRolls = 200;
void rollStats(CharSheet &c, std::mt19937 &rng) {
	for (int attempt = 0; attempt < kMaxStatRolls; ++attempt) {
		for (int i = 0; i < NUM_ABIL; ++i)
			c.abil[i] = roll3d6(rng);
		if (classSatisfiesAbilityMinima(c.klass, c.abil)) break;
	}
	c.lvl = 11;
	int die = classHpDie(c.klass);
	int bonusPerHd = conHpBonus(c.abil[CON]);
	std::uniform_int_distribution<int> hpRoll(1, die);
	int hp = 0;
	for (int hd = 0; hd < c.lvl; ++hd)
		hp += hpRoll(rng) + bonusPerHd;
	c.hp = std::max(1, hp);  // never below 1
	c.ac = 10;
}

// Hit-test the four slot rectangles. Returns 0..3 on hit, -1 otherwise.
int slotAt(int x, int y) {
	for (int i = 0; i < 4; ++i) {
		if (x >= kSlots[i].x && x < kSlots[i].x + 32 &&
		    y >= kSlots[i].y && y < kSlots[i].y + 32) {
			return i;
		}
	}
	return -1;
}

// Read a logical (320x200) mouse position from an SDL mouse event.
// SDL3 event coordinates are floats; truncate to int for the integer-pixel API.
void mousePos(GRAPHICS::Graphics &gfx, const SDL_Event &e, int &lx, int &ly) {
	float fx = (e.type == SDL_EVENT_MOUSE_MOTION) ? e.motion.x : e.button.x;
	float fy = (e.type == SDL_EVENT_MOUSE_MOTION) ? e.motion.y : e.button.y;
	gfx.mouseToLogical(static_cast<int>(fx), static_cast<int>(fy), lx, ly);
}

// Draw text with a drop-shadow at (+1, +1) in a dark palette index, then the
// foreground glyphs over it. Matches the original chargen's text look --
// the engine drew every label this way to make text pop off the stone
// backdrop. paletteIndex==0xff means "default white" (use drawText).
void drawShadowed(GRAPHICS::Graphics &gfx, std::vector<uint8_t> &fnt,
                  const std::string &text, int x, int y,
                  uint8_t fgIndex = 0xff) {
	// Shadow: palette 0 (true black in the chargen palette).
	gfx.drawTextColored(fnt, text, x + 1, y + 1, 0x00);
	// Foreground: white via drawText if fgIndex is sentinel, else tinted.
	if (fgIndex == 0xff)
		gfx.drawText(fnt, text, x, y);
	else
		gfx.drawTextColored(fnt, text, x, y, fgIndex);
}

// Blit a sub-rect of an indexed (8 bpp) source buffer onto the screen.
// Used to grab individual button sprites out of CHARGENB.CPS without
// needing a full Bitmap container around it.
void drawSubIndexed(GRAPHICS::Graphics &gfx,
                    const std::vector<uint8_t> &src, int srcW,
                    int srcX, int srcY, int w, int h,
                    int destX, int destY, bool transparent = false) {
	if (src.empty() || srcW <= 0 || w <= 0 || h <= 0) return;
	if (srcX < 0 || srcY < 0) return;
	// Bounds-check the source rectangle so a short / malformed CPS buffer
	// can't trigger an OOB memcpy. CodeRabbit nit + correct: src.empty()
	// only guards against the all-empty case; a non-empty-but-short buffer
	// (e.g. a truncated CHARGENB.CPS) would otherwise let row+w sail past
	// src.size().
	size_t lastRow = static_cast<size_t>(srcY + h - 1) * srcW + srcX + w;
	if (lastRow > src.size()) return;
	std::vector<uint8_t> sub(static_cast<size_t>(w) * h);
	for (int y = 0; y < h; ++y) {
		size_t row = static_cast<size_t>(srcY + y) * srcW + srcX;
		std::memcpy(sub.data() + y * w, src.data() + row, w);
	}
	gfx.drawIndexed(sub, w, h, destX, destY, transparent);
}

// Source-rect of the blank blue button sprite inside CHARGENB.CPS (small
// 32x14 button, top-left at native 128,128). The wider PLAY/DELETE
// composite sprites sit next to it; we draw text on top of this blank
// to compose BACK/KEEP/REROLL etc.
constexpr int kButtonW = 32;
// 16 rows -- the sprite at (128,128) carries a stone-pattern border on
// both the top (row 0) and bottom (row 15). Slicing only 14 rows clipped
// the bottom border, which read as "cut off" on the wide OK button.
constexpr int kButtonH = 16;
constexpr int kButtonSrcX = 128;
constexpr int kButtonSrcY = 128;

// Where the BACK button lands on screen: bottom of the 16-tall sprite sits
// at y=190, flush against the right info panel's stone frame at y=191.
constexpr int kBackBtnX = 272;
constexpr int kBackBtnY = 175;

// Stats-screen button layout: 2x2 grid at the bottom-right of the right
// panel (REROLL/MODIFY in the top row, FACES/KEEP in the bottom). Per
// user feedback the buttons are tight against each other (no gap) -- 44
// wide each, side-by-side -- and the gold labels render with a tight
// 6-px pitch so 6-char labels fit centered. Synthesized from the 32-wide
// blank sprite by drawWideButton.
constexpr int kStatsBtnW = 44;
constexpr int kStatsBtnH = 16;
constexpr int kStatsBtnPitch = 6; // tight FONT6 pitch for labels
struct StatsBtn { int x, y; const char *label; };
constexpr StatsBtn kStatsBtns[4] = {
	{ 218, 159, "REROLL" },
	{ 262, 159, "MODIFY" },
	{ 218, 175, "FACES"  },  // = 159 + kStatsBtnH so rows touch; bottom row
	{ 262, 175, "KEEP"   },  // bottoms at y=190, flush with stone frame y=191
};
enum StatsBtnId { BTN_REROLL=0, BTN_MODIFY, BTN_FACES, BTN_KEEP };

int statsBtnAt(int lx, int ly) {
	for (int i = 0; i < 4; ++i) {
		const auto &b = kStatsBtns[i];
		if (lx >= b.x && lx < b.x + kStatsBtnW &&
		    ly >= b.y && ly < b.y + kStatsBtnH)
			return i;
	}
	return -1;
}

// PLAY button on the entry screen, shown only when all 4 slots are filled.
// Sits below the bottom slot row's name labels, narrower than the slot grid
// (8 px of slot frame visible on each side).
constexpr int kPlayBtnX = 25;
constexpr int kPlayBtnY = 181;
constexpr int kPlayBtnW = 82;

bool inPlayButton(int lx, int ly) {
	return lx >= kPlayBtnX && lx < kPlayBtnX + kPlayBtnW &&
	       ly >= kPlayBtnY && ly < kPlayBtnY + kButtonH;
}

bool partyComplete(const State &state) {
	for (int i = 0; i < 4; ++i)
		if (!state.party[i].filled) return false;
	return true;
}

// Modify-screen button row: +, -, OK at bottom-right of the right panel.
// Same y as the stats screen's bottom row -- flush with the stone frame.
constexpr int kModBtnY = 175;
struct ModBtn { int x, w; const char *label; };
constexpr ModBtn kModBtns[3] = {
	{ 220, 22, "+"  },
	{ 242, 22, "-"  },  // tight against +
	{ 264, 42, "OK" },  // tight against -
};
enum ModBtnId { BTN_PLUS=0, BTN_MINUS, BTN_OK };

int modBtnAt(int lx, int ly) {
	if (ly < kModBtnY || ly >= kModBtnY + kStatsBtnH) return -1;
	for (int i = 0; i < 3; ++i) {
		if (lx >= kModBtns[i].x && lx < kModBtns[i].x + kModBtns[i].w)
			return i;
	}
	return -1;
}

// Portrait carousel: horizontal strip of 4 portraits with two arrow buttons
// stacked vertically to the left (prev on top, next on bottom). The cursor
// portrait is the leftmost visible cell; the arrows scroll the visible
// window through all 90 portraits.
constexpr int kPortraitVisible = 4;
constexpr int kPortraitStride = 32;
// Arrows use the full 32-wide blank-blue sprite so their right border
// isn't cropped (a 16-wide crop showed only the left half). 16 tall each
// and stacked exactly touching so the strip reads as one tall button
// pair, matching the height of the 32-tall portrait row.
constexpr int kArrowX = 142;        // +2 right per user feedback
constexpr int kArrowYTop = 64;
constexpr int kArrowYBot = 80;      // = kArrowYTop + kArrowH (no gap)
constexpr int kArrowW = 32;
constexpr int kArrowH = 16;
// Strip starts after the arrows; 4 * 32 = 128 wide, fits in the panel.
// +1 right + 1 down nudge per user feedback for visual centering.
constexpr int kPortraitStripX = 177;
constexpr int kPortraitStripY = 65;

void portraitCellXY(int cellIdx, int &x, int &y) {
	x = kPortraitStripX + cellIdx * kPortraitStride;
	y = kPortraitStripY;
}

int portraitCellAt(int lx, int ly) {
	if (ly < kPortraitStripY || ly >= kPortraitStripY + 32) return -1;
	int col = (lx - kPortraitStripX) / kPortraitStride;
	if (col < 0 || col >= kPortraitVisible) return -1;
	return col;
}

bool inArrow(int lx, int ly, int y) {
	return lx >= kArrowX && lx < kArrowX + kArrowW &&
	       ly >= y && ly < y + kArrowH;
}

void drawBackButton(GRAPHICS::Graphics &gfx,
                    const std::vector<uint8_t> &chargenbPx,
                    std::vector<uint8_t> &fnt6Bytes,
                    std::vector<uint8_t> &fntBytes) {
	drawSubIndexed(gfx, chargenbPx, 320, kButtonSrcX, kButtonSrcY,
	               kButtonW, kButtonH, kBackBtnX, kBackBtnY);
	// "BACK" centered on the 32-wide button using a tight 6-px pitch (FONT6
	// glyphs are 8-wide but only ~5 cols visible). 4 chars * 6 = 24 px,
	// centred at (32 - 24) / 2 = 4. Gold (palette 0x1b).
	auto &fnt = fnt6Bytes.empty() ? fntBytes : fnt6Bytes;
	gfx.drawTextColored(fnt, "BACK", kBackBtnX + 4, kBackBtnY + 5, 0x1b, 6);
}

bool inBackButton(int lx, int ly) {
	return lx >= kBackBtnX && lx < kBackBtnX + kButtonW &&
	       ly >= kBackBtnY && ly < kBackBtnY + kButtonH;
}

// Synthesize a wider button by tiling the 32-wide blank sprite: left half
// (16 cols) + right half (16 cols) + the seam column repeated to fill the
// middle. Works because the blank blue button is a left-bordered + middle
// gradient + right-bordered design -- stretching the mid column reads as
// "wider button" without an obvious seam.
void drawWideButton(GRAPHICS::Graphics &gfx,
                    const std::vector<uint8_t> &src,
                    int destX, int destY, int destW) {
	if (src.empty() || destW <= 0) return;
	// Narrow buttons (e.g. +/- at 22 wide) just take a left slice + right
	// slice from the sprite — no middle stretch needed, the borders meet
	// in the centre.
	if (destW < kButtonW) {
		int leftW  = destW / 2;
		int rightW = destW - leftW;
		drawSubIndexed(gfx, src, 320, kButtonSrcX, kButtonSrcY,
		               leftW, kButtonH, destX, destY);
		drawSubIndexed(gfx, src, 320, kButtonSrcX + kButtonW - rightW,
		               kButtonSrcY, rightW, kButtonH,
		               destX + leftW, destY);
		return;
	}
	constexpr int halfW = kButtonW / 2; // 16
	// Left half from sprite cols 0..15
	drawSubIndexed(gfx, src, 320, kButtonSrcX, kButtonSrcY,
	               halfW, kButtonH, destX, destY);
	// Right half from sprite cols 16..31
	drawSubIndexed(gfx, src, 320, kButtonSrcX + halfW, kButtonSrcY,
	               halfW, kButtonH, destX + destW - halfW, destY);
	// Middle stretch: column-strip from the sprite's seam at col 15.
	int midSpan = destW - 2 * halfW;
	for (int dx = 0; dx < midSpan; ++dx) {
		drawSubIndexed(gfx, src, 320, kButtonSrcX + halfW - 1, kButtonSrcY,
		               1, kButtonH, destX + halfW + dx, destY);
	}
}

// --- rendering -------------------------------------------------------

// Repaint the whole screen from the CPS backdrop + party portraits +
// step-specific overlay. Cheaper than tracking dirty rects, and the
// chargen screen isn't perf-sensitive (one update per input event).
void render(GRAPHICS::Graphics &gfx,
            const std::vector<uint8_t> &picBytes,
            std::vector<uint8_t> &fntBytes,
            std::vector<uint8_t> &fnt6Bytes,
            const GRAPHICS::Cps &backdrop,
            const GRAPHICS::Cps &chargenb,
            const std::vector<uint8_t> &previewIdxPlaceholder,
            const State &state) {
	// Backdrop: full CPS or grey fallback.
	if (!backdrop.pixels.empty())
		gfx.drawIndexed(backdrop.pixels, backdrop.width, backdrop.height, 0, 0);
	else
		gfx.fillRect(0, 0, WIDTH - 1, HEIGHT - 1, 0);

	// Slot portraits. Only filled slots draw a portrait -- empty slots show
	// the bare backdrop. The active slot during a sub-step gets a sparkles
	// overlay (extracted from CHARGENB.CPS's top decorative strip) so the
	// user can see which slot they're working on.
	auto picCopy = picBytes;
	for (int i = 0; i < 4; ++i) {
		bool isActive = (state.step != Step::EntryScreen && state.activeSlot == i);
		if (isActive && !chargenb.pixels.empty()) {
			// Sparkles: CHARGENB.CPS top strip (y=0..31, x=0..319) holds a
			// row of star sprites. We slice a 32x32 window from it and
			// shift the source X over time (driven by state.sparkleFrame
			// in the main loop) so the stars appear to twinkle. Palette
			// index 0 is transparent so the slot frame shows through.
			constexpr int kStripFrames = 320 / 32; // 10 distinct windows
			int srcX = (state.sparkleFrame % kStripFrames) * 32;
			drawSubIndexed(gfx, chargenb.pixels, 320, srcX, 0, 32, 32,
			               kSlots[i].x, kSlots[i].y,
			               /*transparent=*/true);
			continue;
		}
		if (!state.party[i].filled) continue;
		uint16_t portraitIdx = static_cast<uint16_t>(state.party[i].portrait);
		gfx.drawImage(picCopy, portraitIdx,
		              kSlots[i].x, kSlots[i].y,
		              /*transparency=*/false, /*mirror=*/0, /*cacheId=*/0);
		// Name label below the portrait, centred in the 32-wide slot. Blue
		// (palette 0x90, the picker-header accent), drop-shadowed, in FONT6
		// at a tight 6-px pitch so longer names fit under the slot. Falls
		// back to FONT8 if FONT6 didn't load.
		if (state.party[i].name[0] != 0) {
			auto &lblFnt = fnt6Bytes.empty() ? fntBytes : fnt6Bytes;
			if (!lblFnt.empty()) {
				const char *nm = state.party[i].name;
				int pitch = fnt6Bytes.empty() ? 8 : 6;
				int textW = static_cast<int>(std::strlen(nm)) * pitch;
				int nx = kSlots[i].x + 16 - textW / 2;
				int ny = kSlots[i].y + 43;
				gfx.drawTextColored(lblFnt, nm, nx + 1, ny + 1, 0x00, pitch);
				gfx.drawTextColored(lblFnt, nm, nx,     ny,     0x90, pitch);
			}
		}
	}

	// Step-specific right-panel content.
	switch (state.step) {
	case Step::EntryScreen: {
		if (fntBytes.empty()) break;
		// Body text positioned to match dosbox: drop-shadowed (+1,+1 black
		// under white), starts at x=156, y=90, with 10-px line spacing.
		drawShadowed(gfx, fntBytes, "Select the box of",  156,  90);
		drawShadowed(gfx, fntBytes, "the character you",  156, 100);
		drawShadowed(gfx, fntBytes, "wish to create or",  156, 110);
		drawShadowed(gfx, fntBytes, "view.",               156, 120);
		// "Party is complete" prompt + PLAY button appear once all 4 slots
		// are filled. Matches the dosbox flow -- you can't start the game
		// until every PC has been rolled.
		if (partyComplete(state)) {
			drawShadowed(gfx, fntBytes, "Your party is",      156, 140);
			drawShadowed(gfx, fntBytes, "complete. Select",   156, 150);
			drawShadowed(gfx, fntBytes, "the PLAY button",    156, 160);
			drawShadowed(gfx, fntBytes, "or press 'P' to",    156, 170);
			drawShadowed(gfx, fntBytes, "start the game.",    156, 180);
			if (!chargenb.pixels.empty()) {
				drawWideButton(gfx, chargenb.pixels,
				               kPlayBtnX, kPlayBtnY, kPlayBtnW);
				auto &listFnt = fnt6Bytes.empty() ? fntBytes : fnt6Bytes;
				int textW = 4 * kStatsBtnPitch; // "PLAY"
				int tx = kPlayBtnX + (kPlayBtnW - textW) / 2;
				gfx.drawTextColored(listFnt, "PLAY", tx,
				                    kPlayBtnY + 5, 0x1b,
				                    kStatsBtnPitch);
			}
		}
		break;
	}
	case Step::PickRace:
	case Step::PickClass:
	case Step::PickAlignment: {
		if (fntBytes.empty()) break;
		// Header + list layout is shared across pickers; the per-step bits
		// are the header text, the entry array, and which cursor selects.
		const char *header;
		const char *const *entries;
		int numEntries, cursor;
		switch (state.step) {
		case Step::PickRace:
			header     = "SELECT RACE:";
			entries    = kRaceNames.data();
			numEntries = static_cast<int>(kRaceNames.size());
			cursor     = state.raceCursor;
			break;
		case Step::PickClass: {
			header     = "SELECT CLASS:";
			// Filter the class list to only those allowed for this slot's
			// race (per kRaceClassAllowed). The picker cursor indexes into
			// the filtered view, NOT the underlying kClassNames; the click
			// + commit handlers know to map back via filteredIndices.
			// thread_local buffers (size 15 max) so we don't allocate per
			// frame and the buffers stay valid through the switch's lifetime.
			thread_local const char *fNames[15];
			thread_local int        fIdx[15];
			int fCount = 0;
			int activeRace = (state.activeSlot >= 0)
				? state.party[state.activeSlot].race : -1;
			for (int i = 0; i < 15; ++i) {
				if (activeRace < 0 || classAllowedForRace(activeRace, i)) {
					fNames[fCount] = kClassNames[i];
					fIdx[fCount]   = i;
					++fCount;
				}
			}
			entries    = fNames;
			numEntries = fCount;
			cursor     = state.classCursor;
			break;
		}
		case Step::PickAlignment:
		default:
			header     = "SELECT ALIGNMENT:";
			entries    = kAlignmentNames.data();
			numEntries = static_cast<int>(kAlignmentNames.size());
			cursor     = state.alignmentCursor;
			break;
		}
		// Header in FONT8 (taller), drop-shadowed in light blue
		// (palette 0x90 = 80,196,252). Matches the dosbox accent colour
		// for picker titles.
		drawShadowed(gfx, fntBytes, header, 144, 70, 0x90);
		// Entries in FONT6 (uppercase, tighter). 8-px line spacing matches
		// dosbox exactly; the drop shadow adds 1 px, so going tighter
		// collides (FONT6 glyphs are 6 tall).
		auto &listFnt = fnt6Bytes.empty()
			? const_cast<std::vector<uint8_t> &>(fntBytes) : fnt6Bytes;
		const int kListX = 144;
		const int kListY0 = 82;
		const int kListStep = 8;
		int firstVis = listFirstVisible(cursor, numEntries);
		int lastVis  = std::min(numEntries, firstVis + kListMaxVisible);
		for (int i = firstVis; i < lastVis; ++i) {
			int y = kListY0 + (i - firstVis) * kListStep;
			// Selected row in red (palette 0x12 = 220,48,44). Header is
			// blue, selected entry is red -- the dosbox convention.
			uint8_t fg = (i == cursor) ? 0x12 : 0xff;
			drawShadowed(gfx, listFnt, entries[i], kListX, y, fg);
		}
		// BACK button at the bottom-right of the right panel. Sprite from
		// CHARGENB.CPS, gold "BACK" text overlaid. Click handler treats it
		// as Esc for the active picker.
		if (!chargenb.pixels.empty())
			drawBackButton(gfx, chargenb.pixels, fnt6Bytes, fntBytes);
		break;
	}
	case Step::PickPortrait: {
		// Horizontal carousel: 4 portraits visible, 2 arrow buttons stacked
		// to the left. Cursor scrolls through the 90 CHARPICS portraits;
		// the leftmost visible cell holds the cursor portrait.
		int total = static_cast<int>(GRAPHICS::Bitmap(picBytes).getNumberOfBitmaps());
		int strip0 = state.portraitCursor;
		// picCopy is already in scope from the outer slot-portrait block.
		// Draw 4 visible portraits (wrap if past the end).
		for (int i = 0; i < kPortraitVisible; ++i) {
			int idx = (strip0 + i) % total;
			int x, y; portraitCellXY(i, x, y);
			gfx.drawImage(picCopy, static_cast<uint16_t>(idx), x, y,
			              /*transparency=*/false, 0, 0);
		}
		// Cursor outline around cell 0 (the chosen portrait).
		{
			int x, y; portraitCellXY(0, x, y);
			gfx.fillRect(x - 1, y - 1, x + 32, y - 1, 0x90);
			gfx.fillRect(x - 1, y + 32, x + 32, y + 32, 0x90);
			gfx.fillRect(x - 1, y - 1, x - 1, y + 32, 0x90);
			gfx.fillRect(x + 32, y - 1, x + 32, y + 32, 0x90);
		}
		// Two arrow buttons stacked left: prev (top), next (bottom). Full
		// 32-wide blank-blue sprite (so the right border isn't cropped).
		// Gold "<" / ">" centered in the button.
		if (!chargenb.pixels.empty()) {
			drawSubIndexed(gfx, chargenb.pixels, 320, kButtonSrcX, kButtonSrcY,
			               kArrowW, kArrowH, kArrowX, kArrowYTop);
			drawSubIndexed(gfx, chargenb.pixels, 320, kButtonSrcX, kButtonSrcY,
			               kArrowW, kArrowH, kArrowX, kArrowYBot);
			if (!fntBytes.empty()) {
				// Glyph is ~5 px visually; centre in 32-wide button.
				gfx.drawTextColored(fntBytes, "<", kArrowX + 13, kArrowYTop + 5, 0x1b);
				gfx.drawTextColored(fntBytes, ">", kArrowX + 13, kArrowYBot + 5, 0x1b);
			}
		}
		// Race + class labels + LARGE stats below. All text shifted right
		// +16 px from previous layout per user feedback.
		if (!fntBytes.empty()) {
			const auto &c = state.party[state.activeSlot];
			const char *raceName  = (c.race  >= 0) ? kRaceNames[c.race]   : "";
			const char *className = (c.klass >= 0) ? kClassNames[c.klass] : "";
			// All carousel-page text moved up 2 px per user feedback.
			// Race/class/stat positions held identical to the stats page
			// (race y=110, class y=120, stat pitch 9 px) so switching
			// between pages doesn't shuffle the text.
			drawShadowed(gfx, fntBytes, raceName,  192, 110);
			drawShadowed(gfx, fntBytes, className, 204, 120);
			auto statLine = [&](int row, const char *name, int value) {
				char valBuf[8]; std::snprintf(valBuf, sizeof(valBuf), "%2d", value);
				int y = 130 + row * 9;
				drawShadowed(gfx, fntBytes, name, 156, y);
				drawShadowed(gfx, fntBytes, valBuf, 188, y);
			};
			auto rightLine = [&](int row, const char *name, int value) {
				char valBuf[8]; std::snprintf(valBuf, sizeof(valBuf), "%2d", value);
				int y = 130 + row * 9;
				drawShadowed(gfx, fntBytes, name, 240, y);
				drawShadowed(gfx, fntBytes, valBuf, 280, y);
			};
			statLine(0, "STR ", c.abil[STR]);
			statLine(1, "INT ", c.abil[INTEL]);
			statLine(2, "WIS ", c.abil[WIS]);
			statLine(3, "DEX ", c.abil[DEX]);
			statLine(4, "CON ", c.abil[CON]);
			statLine(5, "CHA ", c.abil[CHA]);
			rightLine(0, "AC ",  c.ac);
			rightLine(1, "HP ",  c.hp);
			rightLine(2, "LVL ", c.lvl);
		}
		break;
	}
	case Step::EnterName:
	case Step::ShowStats:
	case Step::ModifyStats: {
		if (fntBytes.empty()) break;
		const auto &c = state.party[state.activeSlot];
		const bool modifying = (state.step == Step::ModifyStats);
		// Layout (consistent across ShowStats / ModifyStats / EnterName).
		// Portrait sits INSIDE the stone window, then the Name row, race,
		// class, the 6-row stat block, and the button grid (original
		// position y=162). Everything below the portrait is shifted down
		// by 8 px from the previous layout per user request.
		//   portrait    y=66 .. 98
		//   Name row    y=100         (blank in ShowStats/Modify, prompt in EnterName)
		//   Race        y=110
		//   Class       y=120
		//   Stats       y=130..175    (6 rows, 9-px FONT8 pitch)
		//   Buttons     y=162         (rows 5-6 of stats overlap the button area)
		gfx.drawImage(const_cast<std::vector<uint8_t> &>(picBytes),
		              static_cast<uint16_t>(c.portrait),
		              220, 66, /*transparency=*/false, 0, 0);
		const char *raceName  = (c.race  >= 0) ? kRaceNames[c.race]   : "";
		const char *className = (c.klass >= 0) ? kClassNames[c.klass] : "";
		drawShadowed(gfx, fntBytes, raceName,  192, 110);
		drawShadowed(gfx, fntBytes, className, 204, 120);
		// Ability scores in FONT8 (larger), 2 columns. 10-px line height
		// keeps FONT8's 8-tall glyphs from colliding while still hugging
		// the class label tightly (first stat row at y=102, right under
		// the class label). Name + value at fixed X each so right-column
		// AC/HP/LVL values column-align despite name lengths differing.
		auto abilLine = [&](int row, const char *name, int value, bool sel) {
			char valBuf[8]; std::snprintf(valBuf, sizeof(valBuf), "%2d", value);
			int y = 130 + row * 9;
			drawShadowed(gfx, fntBytes, name, 156, y);
			uint8_t fg = sel ? 0x12 : 0xff;
			drawShadowed(gfx, fntBytes, valBuf, 188, y, fg);
		};
		auto rightLine = [&](int row, const char *name, int value) {
			char valBuf[8]; std::snprintf(valBuf, sizeof(valBuf), "%2d", value);
			int y = 130 + row * 9;
			drawShadowed(gfx, fntBytes, name, 240, y);
			drawShadowed(gfx, fntBytes, valBuf, 280, y);
		};
		auto &listFnt = fnt6Bytes.empty() ? fntBytes : fnt6Bytes;
		for (int i = 0; i < NUM_ABIL; ++i) {
			static const char *names[NUM_ABIL] = {
				"STR ", "INT ", "WIS ", "DEX ", "CON ", "CHA "
			};
			abilLine(i, names[i], c.abil[i],
			         modifying && state.statCursor == i);
		}
		rightLine(0, "AC ",  c.ac);
		rightLine(1, "HP ",  c.hp);
		rightLine(2, "LVL ", c.lvl);
		// Button row(s) -- different per sub-state.
		if (state.step == Step::EnterName) {
			// "Name:" label sits BETWEEN portrait and race (y=82, the
			// dedicated name slot in the shared layout). Blue label
			// (palette 0x90, matches picker headers), typed text + cursor
			// rendered shadowed white to match the rest of the chargen text.
			drawShadowed(gfx, fntBytes, "Name:", 156, 100, 0x90);
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%s_", c.name);
			drawShadowed(gfx, fntBytes, buf, 200, 100);
		} else if (modifying) {
			if (!chargenb.pixels.empty()) {
				for (const auto &b : kModBtns) {
					drawWideButton(gfx, chargenb.pixels, b.x, kModBtnY, b.w);
					int textW = static_cast<int>(std::strlen(b.label)) * kStatsBtnPitch;
					int tx = b.x + (b.w - textW) / 2;
					gfx.drawTextColored(listFnt, b.label,
					                    tx, kModBtnY + 5, 0x1b,
					                    kStatsBtnPitch);
				}
			}
		} else {
			if (!chargenb.pixels.empty()) {
				for (const auto &b : kStatsBtns) {
					drawWideButton(gfx, chargenb.pixels, b.x, b.y, kStatsBtnW);
					int textW = static_cast<int>(std::strlen(b.label)) * kStatsBtnPitch;
					int tx = b.x + (kStatsBtnW - textW) / 2;
					gfx.drawTextColored(listFnt, b.label, tx, b.y + 5, 0x1b,
					                    kStatsBtnPitch);
				}
			}
		}
		break;
	}
	}
}

// --- CREATE.SAV writer -----------------------------------------------
//
// Overlay our chargen-rolled fields onto the bundled CREATE.SAV template.
// Field offsets within each 345-byte PC record (rec_off = attr - 2, with
// the record starting at file 0x16 + pc*345). Verified live by tracing
// the xfer SOP's player_attrib calls in --debug mode:
//
//   rec  attr  size  field
//     0     2    11   name (NUL-padded)
//    12    14     1   STR
//    14    16     1   STR% (exceptional)
//    16    18     1   INT
//    18    20     1   WIS
//    20    22     1   DEX
//    22    24     1   CON
//    24    26     1   CHA
//    25-26 -      2   HP cur (unused by xfer -- set from hmax - lost_hp)
//    27    29     2   hmax (HP max)
//    31    33     1   race      (0..11 in our chargen order matches Bob=0)
//    32    34     1   classes   (single-class 0..5; multi-class uses higher)
//    33    35     1   alignment (0..8)
//    34    36     1   portrait  (CHARPICS index)
//    36    38     1   levels[0] (primary class level)
//    37    39     1   levels[1]
//    38    40     1   levels[2]
//    39    41     4   experience[0] (u32)
//    43    45     4   experience[1] (-1 if single-class)
//    47    49     4   experience[2] (-1 if single-class)
//
// Class remap: our chargen UI lists FIGHTER/RANGER/PALADIN/MAGE/CLERIC/
// THIEF, but the EOB1 byte uses F/M/C/T/P/R. Map at write time.
//
// HP / XP fall back to plausible level-11 fighter values (matching the
// default party's Bob); class-aware tables are a follow-up.
bool writeCreateSav(const std::filesystem::path &chargenDir,
                    const State &state) {
	auto path = chargenDir / "CREATE.SAV";
	auto bytes = readFile(path);
	if (bytes.size() < 0x894) {
		std::cout << "  [chargen: " << path
		          << " missing or too small to overlay]" << std::endl;
		return false;
	}
	constexpr size_t kPcBase = 0x16;
	constexpr size_t kPcStride = 345;
	// Chargen UI index -> EOB1 PC class byte. Single classes get reordered
	// (UI F/R/P/M/C/T -> EOB1 F/M/C/T/P/R = 0/1/2/3/4/5); multi-class entries
	// keep their chargen index (6..14 map identically because both lists use
	// the same multi-class ordering: F/C, F/T, F/M, F/M/T, T/M, C/T, F/C/M,
	// R/C, C/M -- confirmed against CHGEN's string table).
	constexpr int kClassToEob1[15] = {
		0, 5, 4, 1, 2, 3,
		6, 7, 8, 9, 10, 11, 12, 13, 14,
	};
	// Base XP values per EOB1 class @ level 11 (rough -- adequate for HUD
	// purposes; the game accepts a wide range here, won't reject the party).
	auto baseXp = [](int eob1Class) -> uint32_t {
		switch (eob1Class) {
		case 0: return 750000;  // FIGHTER
		case 1: return 600000;  // MAGE
		case 2: return 375000;  // CLERIC
		case 3: return 500000;  // THIEF
		case 4: return 750000;  // PALADIN
		case 5: return 750000;  // RANGER
		}
		return 750000;
	};
	auto patchU8 = [&](size_t off, uint8_t v) { bytes[off] = v; };
	auto patchU16 = [&](size_t off, uint16_t v) {
		bytes[off]     = v & 0xFF;
		bytes[off + 1] = (v >> 8) & 0xFF;
	};
	auto patchI32 = [&](size_t off, int32_t v) {
		bytes[off]     = v & 0xFF;
		bytes[off + 1] = (v >> 8) & 0xFF;
		bytes[off + 2] = (v >> 16) & 0xFF;
		bytes[off + 3] = (v >> 24) & 0xFF;
	};
	for (int pc = 0; pc < 4; ++pc) {
		const auto &c = state.party[pc];
		if (!c.filled) continue;
		size_t rec = kPcBase + pc * kPcStride;
		// Name (11 bytes NUL-padded).
		std::memset(bytes.data() + rec, 0, 11);
		std::memcpy(bytes.data() + rec, c.name,
		            std::min<size_t>(10, std::strlen(c.name)));
		// Ability pairs (max, current); write both bytes to the same value.
		auto writePair = [&](size_t pairOff, int val) {
			uint8_t v = static_cast<uint8_t>(std::max(0, std::min(99, val)));
			patchU8(rec + pairOff,     v);
			patchU8(rec + pairOff + 1, v);
		};
		writePair(11, c.abil[STR]);
		writePair(13, 0);              // STR%
		writePair(15, c.abil[INTEL]);
		writePair(17, c.abil[WIS]);
		writePair(19, c.abil[DEX]);
		writePair(21, c.abil[CON]);
		writePair(23, c.abil[CHA]);
		// HP: write both cur (rec+25) and max (rec+27) -- the xfer reads
		// only max, but Bob's template has them equal so let's match.
		uint16_t hp = static_cast<uint16_t>(std::max(0, c.hp));
		patchU16(rec + 25, hp);
		patchU16(rec + 27, hp);
		// race / class / alignment / portrait
		int eob1Class = (c.klass >= 0 && c.klass < 15) ? kClassToEob1[c.klass] : 0;
		patchU8(rec + 31, static_cast<uint8_t>(std::max(0, c.race)));
		patchU8(rec + 32, static_cast<uint8_t>(eob1Class));
		patchU8(rec + 33, static_cast<uint8_t>(std::max(0, c.alignment)));
		patchU8(rec + 34, static_cast<uint8_t>(c.portrait & 0xff));
		// levels[3] + experience[3]: one entry per component class for multi-
		// class characters. AD&D 2e splits earned XP evenly between active
		// classes, so a multi-class PC actually levels up slower per class.
		// We approximate at chargen time by dividing the single-class
		// baseline XP by component count -- a level-11 F/M ends up with
		// 375000 XP per class instead of 750000 each, which preserves the
		// HUD "appears as level 11" while keeping the next-level math sane.
		// Unused slots get level 0 and XP -1 to match the default party's
		// "no class" sentinel.
		int nComp = std::max(1, classComponentCount(c.klass));
		bool klassInRange = (c.klass >= 0 && c.klass < 15);
		for (int i = 0; i < 3; ++i) {
			if (i < nComp) {
				patchU8(rec + 36 + i, static_cast<uint8_t>(c.lvl));
				int compClass = klassInRange ? kClassComponents[c.klass][i] : -1;
				int compEob1  = (compClass >= 0 && compClass < 6)
				              ? kClassToEob1[compClass] : eob1Class;
				uint32_t xp = baseXp(compEob1) / static_cast<uint32_t>(nComp);
				patchI32(rec + 39 + i * 4, static_cast<int32_t>(xp));
			} else {
				patchU8(rec + 36 + i, 0);
				patchI32(rec + 39 + i * 4, -1);
			}
		}
	}

	// --- Starting equipment ---
	//
	// Write a class-appropriate kit to the CREATE.SAV item array
	// (offsets 0x894+, 14 bytes per record, ids running from 434) and
	// point each PC's inventory[26] (at PC record offset 219) at it. The
	// xfer SOP then routes each item to its EOB3 body-part slot via
	// table123 + TransferState::categoryForClass at boot.
	//
	// Layout: we partition ids 434..(434 + 4*kMaxKitItems - 1) into 4
	// equal-size blocks, one per PC. Unfilled PCs and unused slots leave
	// the corresponding records as the scaffold's, which is benign --
	// item_attrib only resolves slots that inventory[26] points at.
	//
	// Per-item template lookup: most types have an obvious ITEM.DAT entry
	// (Long Sword = unid 30; Chainmail = unid 19; etc.). Picture indices
	// come from ITEM.DAT for visual consistency in the equipment screen.
	constexpr size_t kItemArrayBase = 0x894;
	constexpr int    kItemBytes     = 14;
	constexpr int    kMaxKitItems   = 6; // matches kClassKit row width
	auto patchItemRec = [&](size_t off, uint8_t unid, uint8_t idn, uint8_t bits,
	                        uint8_t pic, uint8_t type, int8_t value) {
		if (off + kItemBytes > bytes.size()) return;
		bytes[off + 0] = unid;
		bytes[off + 1] = idn;
		bytes[off + 2] = bits;
		bytes[off + 3] = pic;
		bytes[off + 4] = type;
		bytes[off + 5] = 0;          // subpos = 0 (in inventory)
		bytes[off + 6] = 0; bytes[off + 7] = 0; // pos = 0
		bytes[off + 8] = 0; bytes[off + 9] = 0; // next = 0
		bytes[off + 10] = 0; bytes[off + 11] = 0; // prev = 0
		bytes[off + 12] = 0;         // level = 0 (in inventory)
		bytes[off + 13] = static_cast<uint8_t>(value);
	};
	// Per-type ITEM.DAT template: (unid name index, identified name index,
	// pic icon, default value). Picked from the canonical entries in
	// ITEM.DAT (dumped against the bundled install). value is the EOB1
	// "value/bonus" byte -- 0 for ordinary items, 50 for Iron Rations
	// (matches the scaffold's id 11 entry).
	struct ItemTpl { uint8_t unid, idn, pic; int8_t value; };
	auto itemTpl = [](int type, int chargenClass) -> ItemTpl {
		switch (type) {
		case  1: return { 30, 30,  1, 0 }; // Long Sword
		case  2: return {  6,  6,  2, 0 }; // Short sword
		case  5: return {  5,  5, 15, 0 }; // Dagger
		case 11: return { 22, 22,  4, 0 }; // Mace
		case 20: return { 19, 19, 29, 0 }; // Chainmail
		case 22: return {  2,  2, 31, 0 }; // Leather armor
		case 27: return { 27, 27, 23, 0 }; // Shield
		case 28: return {  7,  7, 56, 0 }; // Lock picks
		case 29: return {  8,  8, 35, 0 }; // Spellbook
		case 30: {
			uint8_t u = static_cast<uint8_t>(classHolySymbolUnid(chargenClass));
			uint8_t pic = (u == 23) ? 27 : 55; // Paladin (pic 27) vs Cleric (pic 55)
			return { u, u, pic, 0 };
		}
		case 31: return { 11, 11, 38, 50 }; // Iron Rations
		case 41: return {  3,  3, 32, 0 }; // Robe
		default: return {  0,  0,  0, 0 };
		}
	};
	// Categorise an EOB1 type into a CREATE.SAV raw-inventory slot.
	// Mirrors the original CHGEN.EXE layout so the bundled QSP's slot
	// pattern (body @ raw[17], weapon @ raw[0], shield @ raw[1], backpack
	// @ raw[2..]) carries through to ours. Behaviourally equivalent to a
	// sequential 0..N-1 write because the xfer SOP re-categorizes by item
	// class -- but a hex-dump comparison against a Westwood-rolled save
	// will line up byte for byte. See docs/arun_xfer_disassembly.md §2.
	enum class RawCat { BODY, WEAPON, SHIELD, CARRIED };
	auto rawCategory = [](int type) -> RawCat {
		switch (type) {
		case 19: case 20: case 22: case 24: case 25: case 41:
			return RawCat::BODY;     // banded/chain/leather/plate/scale/robe
		case 27: return RawCat::SHIELD;
		case  0: case  1: case  2: case  5: case  7: case  9:
		case 10: case 11: case 12: case 13: case 14: case 15:
		case 16: case 18: case 23: case 45: case 60:
			return RawCat::WEAPON;
		default: return RawCat::CARRIED;
		}
	};
	for (int pc = 0; pc < 4; ++pc) {
		const auto &c = state.party[pc];
		if (!c.filled) continue;
		int kitN = kitItemCount(c.klass);
		if (kitN <= 0) continue;
		// Zero out this PC's inventory[26] first so leftover scaffold
		// pointers (e.g. Bob's items at raw[0]/[2]/...) don't co-exist
		// with our new equipment and double up in the transfer.
		size_t invOff = kPcBase + pc * kPcStride + 219;
		for (int s = 0; s < 26; ++s) patchU16(invOff + s * 2, 0);
		uint16_t idBase = static_cast<uint16_t>(434 + pc * kMaxKitItems);
		int nextBackpack = 2;       // first backpack raw slot (per the
		                            // CHGEN.EXE layout; raw[0]=RH, raw[1]=LH)
		bool rhTaken = false, lhTaken = false, bodyTaken = false;
		for (int s = 0; s < kitN; ++s) {
			int type = kClassKit[c.klass][s];
			if (type < 0) break;
			uint16_t id = static_cast<uint16_t>(idBase + s);
			ItemTpl t = itemTpl(type, c.klass);
			size_t recOff = kItemArrayBase + (id - 434) * kItemBytes;
			patchItemRec(recOff, t.unid, t.idn, /*bits=*/0,
			             t.pic, static_cast<uint8_t>(type), t.value);
			int targetRaw = -1;
			switch (rawCategory(type)) {
			case RawCat::BODY:
				targetRaw = bodyTaken ? nextBackpack++ : 17;
				bodyTaken = true;
				break;
			case RawCat::WEAPON:
				if (!rhTaken)      { targetRaw = 0;  rhTaken = true; }
				else if (!lhTaken) { targetRaw = 1;  lhTaken = true; }
				else                targetRaw = nextBackpack++;
				break;
			case RawCat::SHIELD:
				if (!lhTaken)      { targetRaw = 1;  lhTaken = true; }
				else if (!rhTaken) { targetRaw = 0;  rhTaken = true; }
				else                targetRaw = nextBackpack++;
				break;
			case RawCat::CARRIED:
				targetRaw = nextBackpack++;
				break;
			}
			// Cap to inventory bounds; spill into the backpack range if
			// we somehow overflow (shouldn't, kits are <= 6 items).
			if (targetRaw < 0 || targetRaw >= 26) targetRaw = 2;
			patchU16(invOff + targetRaw * 2, id);
		}
	}
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) {
		std::cout << "  [chargen: failed to open " << path
		          << " for write]" << std::endl;
		return false;
	}
	f.write(reinterpret_cast<const char *>(bytes.data()),
	        static_cast<std::streamsize>(bytes.size()));
	// Flush before checking: write() buffers, and a disk-full / I/O failure
	// only surfaces in the stream state after the buffer's been drained. A
	// successful open is not enough -- the SOP transfer reads this file
	// byte-for-byte and a short / corrupt CREATE.SAV silently mis-rolls the
	// imported party.
	f.flush();
	if (!f) {
		std::cout << "  [chargen: write to " << path << " failed (disk full?)]"
		          << std::endl;
		return false;
	}
	std::cout << "  [chargen: wrote " << bytes.size() << " bytes to "
	          << path << "]" << std::endl;
	return true;
}

// --- input handling --------------------------------------------------

void handleEntryEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                      State &state) {
	if (e.type == SDL_EVENT_KEY_DOWN) {
		auto k = e.key.key;
		if ((k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_P)
		    && partyComplete(state)) {
			state.done = true;          // proceed to game
		} else if (k == SDLK_ESCAPE) {
			// Cancel: hand control back to the engine with a "go to title
			// menu" signal so the boot loop can reset mem[1264] = INTR.
			state.done      = true;
			state.cancelled = true;
		}
	} else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		if (partyComplete(state) && inPlayButton(lx, ly)) {
			state.done = true;
			return;
		}
		int s = slotAt(lx, ly);
		if (s >= 0) {
			state.step       = Step::PickRace;
			state.activeSlot = s;
			state.raceCursor = (state.party[s].race >= 0)
			                        ? state.party[s].race : 0;
		}
	}
}

// Shared list-picker input handler. Returns true if the user committed,
// false if cancelled (Esc) or no transition.
struct PickerResult { bool committed = false; bool cancelled = false; };

PickerResult handleListPicker(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                              int &cursor, int numEntries) {
	PickerResult res;
	if (e.type == SDL_EVENT_KEY_DOWN) {
		auto k = e.key.key;
		if (k == SDLK_UP)
			cursor = (cursor + numEntries - 1) % numEntries;
		else if (k == SDLK_DOWN)
			cursor = (cursor + 1) % numEntries;
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
			res.committed = true;
		else if (k == SDLK_ESCAPE)
			res.cancelled = true;
	} else if (e.type == SDL_EVENT_MOUSE_MOTION) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		// List rendered at y=82 + (i - firstVis)*8 (FONT6, 8-px line height).
		int firstVis = listFirstVisible(cursor, numEntries);
		int visCount = std::min(numEntries - firstVis, kListMaxVisible);
		if (lx >= 140 && lx < 305 && ly >= 82 && ly < 82 + 8 * visCount) {
			int idx = firstVis + (ly - 82) / 8;
			if (idx >= 0 && idx < numEntries)
				cursor = idx;
		}
	} else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	           e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		// BACK button takes priority — it's overlaid on the panel.
		if (inBackButton(lx, ly)) {
			res.cancelled = true;
		} else {
			int firstVis = listFirstVisible(cursor, numEntries);
			int visCount = std::min(numEntries - firstVis, kListMaxVisible);
			if (lx >= 140 && lx < 305 && ly >= 82 &&
			    ly < 82 + 8 * visCount) {
				int idx = firstVis + (ly - 82) / 8;
				if (idx >= 0 && idx < numEntries) {
					cursor = idx;
					res.committed = true;
				}
			}
		}
	}
	return res;
}

// Build the list of class indices allowed for `chargenRace`. Mirrors the
// filter in the render path so the picker view and the commit handler
// agree on what visible index N means.
int buildAllowedClasses(int chargenRace, int outIdx[15]) {
	int n = 0;
	for (int i = 0; i < 15; ++i)
		if (chargenRace < 0 || classAllowedForRace(chargenRace, i))
			outIdx[n++] = i;
	return n;
}

void handleRacePickEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                         State &state) {
	auto r = handleListPicker(e, gfx, state.raceCursor,
	                          static_cast<int>(kRaceNames.size()));
	if (r.committed) {
		int newRace = state.raceCursor;
		state.party[state.activeSlot].race = newRace;
		std::cout << "  [chargen: slot " << state.activeSlot
		          << " race set to " << kRaceNames[newRace] << "]" << std::endl;
		// Advance to class picker; the active slot stays the same. Reset
		// the class cursor to the previously-chosen class if it's still
		// allowed for the (possibly-new) race, else 0.
		int prevKlass = state.party[state.activeSlot].klass;
		int allowed[15];
		int n = buildAllowedClasses(newRace, allowed);
		state.classCursor = 0;
		if (prevKlass >= 0 && n > 0) {
			for (int i = 0; i < n; ++i)
				if (allowed[i] == prevKlass) { state.classCursor = i; break; }
		}
		state.step = Step::PickClass;
	} else if (r.cancelled) {
		state.step       = Step::EntryScreen;
		state.activeSlot = -1;
	}
}

void handleClassPickEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                          State &state) {
	int race = state.party[state.activeSlot].race;
	int allowed[15];
	int n = buildAllowedClasses(race, allowed);
	auto r = handleListPicker(e, gfx, state.classCursor, n);
	if (r.committed) {
		int realKlass = (state.classCursor >= 0 && state.classCursor < n)
		                    ? allowed[state.classCursor] : 0;
		state.party[state.activeSlot].klass = realKlass;
		std::cout << "  [chargen: slot " << state.activeSlot
		          << " class set to " << kClassNames[realKlass]
		          << "]" << std::endl;
		// Advance to alignment picker; active slot stays.
		state.alignmentCursor =
			(state.party[state.activeSlot].alignment >= 0)
			    ? state.party[state.activeSlot].alignment : 0;
		state.step = Step::PickAlignment;
	} else if (r.cancelled) {
		// Step back to race picker (so user can change race without
		// retracing everything). Active slot stays.
		state.step = Step::PickRace;
	}
}

void handleAlignmentPickEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                              State &state, std::mt19937 &rng) {
	auto r = handleListPicker(e, gfx, state.alignmentCursor,
	                          static_cast<int>(kAlignmentNames.size()));
	if (r.committed) {
		state.party[state.activeSlot].alignment = state.alignmentCursor;
		std::cout << "  [chargen: slot " << state.activeSlot
		          << " alignment set to "
		          << kAlignmentNames[state.alignmentCursor] << "]"
		          << std::endl;
		// Roll stats + advance to portrait carousel. After the user picks
		// a portrait, the flow lands on ShowStats with REROLL/MODIFY/
		// FACES/KEEP buttons.
		rollStats(state.party[state.activeSlot], rng);
		state.portraitCursor =
			(state.party[state.activeSlot].portrait >= 0)
				? state.party[state.activeSlot].portrait : 0;
		state.step = Step::PickPortrait;
	} else if (r.cancelled) {
		state.step = Step::PickClass;
	}
}

void enterNameStep(State &state, SDL_Window *window) {
	// Pre-seed name input with the previous name (if any) so KEEP -> name
	// retains what was typed; otherwise blank.
	state.step = Step::EnterName;
	// SDL3 routes text-input events to the focused window, so we pass ours.
	SDL_StartTextInput(window);
}

void handleStatsEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                      State &state, std::mt19937 &rng) {
	if (e.type == SDL_EVENT_KEY_DOWN) {
		auto k = e.key.key;
		if (k == SDLK_R)
			rollStats(state.party[state.activeSlot], rng);
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER ||
		         k == SDLK_K) {
			// KEEP -> proceed to name entry. The slot only flips to
			// `filled` once a name is committed and CREATE.SAV is written.
			enterNameStep(state, gfx.getWindow());
		} else if (k == SDLK_ESCAPE) {
			// Back to alignment.
			state.step = Step::PickAlignment;
		}
	} else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	           e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		int btn = statsBtnAt(lx, ly);
		switch (btn) {
		case BTN_REROLL:
			rollStats(state.party[state.activeSlot], rng);
			break;
		case BTN_MODIFY:
			state.statCursor = 0;
			state.step = Step::ModifyStats;
			break;
		case BTN_FACES:
			state.portraitCursor = state.party[state.activeSlot].portrait;
			state.step = Step::PickPortrait;
			break;
		case BTN_KEEP:
			enterNameStep(state, gfx.getWindow());
			break;
		default: break;
		}
	}
}

void handleNameEvent(const SDL_Event &e, State &state,
                     const std::filesystem::path &chargenDir,
                     SDL_Window *window) {
	auto &c = state.party[state.activeSlot];
	if (e.type == SDL_EVENT_TEXT_INPUT) {
		// Accept any byte SDL feeds us -- UTF-8 multi-byte sequences and
		// emoji included. Cap by name buffer size (10 bytes + NUL); a
		// multi-byte char may not fit, in which case we skip the whole
		// SDL_EVENT_TEXT_INPUT to avoid splitting a code point mid-sequence.
		// ASCII letters are folded to uppercase to match the original game's
		// name display; multi-byte sequences pass through unchanged (toupper
		// is a byte op and would corrupt UTF-8 trail bytes).
		size_t curLen = std::strlen(c.name);
		size_t inLen  = std::strlen(e.text.text);
		if (curLen + inLen <= 10) {
			for (size_t i = 0; i < inLen; ++i) {
				unsigned char b = static_cast<unsigned char>(e.text.text[i]);
				c.name[curLen + i] = (b < 0x80)
					? static_cast<char>(std::toupper(b))
					: static_cast<char>(b);
			}
			c.name[curLen + inLen] = '\0';
		}
	} else if (e.type == SDL_EVENT_KEY_DOWN) {
		auto k = e.key.key;
		if (k == SDLK_BACKSPACE) {
			// Drop the last UTF-8 code point (trail bytes start with 10xxxxxx).
			size_t len = std::strlen(c.name);
			while (len > 0) {
				unsigned char b = static_cast<unsigned char>(c.name[len - 1]);
				c.name[--len] = '\0';
				if ((b & 0xC0) != 0x80) break; // stop on lead/single byte
			}
		} else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
			// Name cannot be empty: ignore Enter until the user types
			// something. (Esc backs out instead.)
			if (std::strlen(c.name) == 0)
				return;
			SDL_StopTextInput(window);
			std::cout << "  [chargen: slot " << state.activeSlot
			          << " name=\"" << c.name << "\" -- writing CREATE.SAV]"
			          << std::endl;
			// Flip `filled` BEFORE writing so the write loop actually
			// includes the slot we just finished -- otherwise the last
			// character to be made gets skipped and the default party's
			// stale Alice record persists in CREATE.SAV. Roll back on I/O
			// failure so the UI doesn't show a finished slot for a save
			// that didn't make it to disk.
			c.filled = true;
			if (writeCreateSav(chargenDir, state)) {
				state.step       = Step::EntryScreen;
				state.activeSlot = -1;
			} else {
				c.filled = false;
				std::cout << "  [chargen: keeping slot " << state.activeSlot
				          << " open -- save failed, character not finalised]"
				          << std::endl;
			}
		} else if (k == SDLK_ESCAPE) {
			SDL_StopTextInput(window);
			state.step = Step::ShowStats; // back to stats
		}
	}
}

void handlePortraitPickEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                             State &state,
                             const std::vector<uint8_t> &picBytes) {
	int total = static_cast<int>(GRAPHICS::Bitmap(const_cast<std::vector<uint8_t> &>(picBytes)).getNumberOfBitmaps());
	if (total <= 0) total = 1;
	auto commit = [&]() {
		state.party[state.activeSlot].portrait = state.portraitCursor;
		std::cout << "  [chargen: slot " << state.activeSlot
		          << " portrait set to " << state.portraitCursor << "]"
		          << std::endl;
		state.step = Step::ShowStats;
	};
	auto scrollPrev = [&]() {
		state.portraitCursor = (state.portraitCursor + total - 1) % total;
	};
	auto scrollNext = [&]() {
		state.portraitCursor = (state.portraitCursor + 1) % total;
	};
	if (e.type == SDL_EVENT_KEY_DOWN) {
		auto k = e.key.key;
		if (k == SDLK_LEFT || k == SDLK_UP) scrollPrev();
		else if (k == SDLK_RIGHT || k == SDLK_DOWN) scrollNext();
		else if (k == SDLK_PAGEDOWN)
			state.portraitCursor = std::min(total - 1,
			                                state.portraitCursor + kPortraitVisible);
		else if (k == SDLK_PAGEUP)
			state.portraitCursor = std::max(0,
			                                state.portraitCursor - kPortraitVisible);
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) commit();
		else if (k == SDLK_ESCAPE) state.step = Step::ShowStats;
	} else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	           e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		if (inArrow(lx, ly, kArrowYTop)) scrollPrev();
		else if (inArrow(lx, ly, kArrowYBot)) scrollNext();
		else {
			int cell = portraitCellAt(lx, ly);
			if (cell >= 0) {
				// Clicking any visible portrait sets the cursor to it and
				// commits in one step.
				state.portraitCursor = (state.portraitCursor + cell) % total;
				commit();
			}
		}
	}
}

void handleModifyEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                       State &state) {
	auto &c = state.party[state.activeSlot];
	auto bump = [&](int delta) {
		int &v = c.abil[state.statCursor];
		v = std::max(3, std::min(18, v + delta));
	};
	if (e.type == SDL_EVENT_KEY_DOWN) {
		auto k = e.key.key;
		if (k == SDLK_UP)
			state.statCursor = (state.statCursor + NUM_ABIL - 1) % NUM_ABIL;
		else if (k == SDLK_DOWN)
			state.statCursor = (state.statCursor + 1) % NUM_ABIL;
		else if (k == SDLK_RIGHT || k == SDLK_PLUS || k == SDLK_EQUALS)
			bump(+1);
		else if (k == SDLK_LEFT || k == SDLK_MINUS)
			bump(-1);
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE)
			state.step = Step::ShowStats;
	} else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	           e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		int btn = modBtnAt(lx, ly);
		if (btn == BTN_PLUS)  bump(+1);
		else if (btn == BTN_MINUS) bump(-1);
		else if (btn == BTN_OK)    state.step = Step::ShowStats;
		else {
			// Click on a stat row picks that stat as cursor.
			// Rows at y = 108..108+8*5; left column starts at x=144.
			if (lx >= 144 && lx < 232 && ly >= 108 && ly < 108 + 8 * NUM_ABIL) {
				int row = (ly - 108) / 8;
				if (row >= 0 && row < NUM_ABIL) state.statCursor = row;
			}
		}
	}
}

// --- chargen autopilot for regression tests -----------------------------
//
// THIRDEYE_CHARGEN_AUTO drives the chargen UI deterministically by pushing
// synthetic SDL events into the queue, one action per tick. Same pump that
// THIRDEYE_AUTOWALK uses for the SOP, but routed to SDL since the chargen
// reads SDL_PollEvent directly (not via the VM event system).
//
// Action grammar (semicolons separate; whitespace ignored):
//   s<N>       click party slot N (0..3)        -> opens race picker
//   r<N>      pick race N (set cursor + Enter)  -> advances to class
//   c<N>      pick class N                       -> advances to alignment
//   a<N>      pick alignment N                   -> rolls stats + shows
//   K          press KEEP                        -> advances to portrait
//   R          press REROLL                      -> re-rolls in place
//   M          press MODIFY                      -> opens ModifyStats
//   F          press FACES                       -> opens portrait picker
//   O          press OK in ModifyStats           -> back to ShowStats
//   +<I>       MODIFY: +1 ability I (0..5)
//   -<I>       MODIFY: -1 ability I
//   p<N>       portrait carousel select slot N (0..89)
//   n<NAME>   enter NAME via SDL_EVENT_TEXT_INPUT + Enter (finalises slot)
//   P          PLAY (only fires if partyComplete)
//
// Full party recipe (4 chars, all single-class Fighter Human Male LG, then
// PLAY):
//   "s0;r0;c0;a0;K;F;p0;nAlice;
//    s1;r0;c0;a0;K;F;p1;nBob;
//    s2;r0;c0;a0;K;F;p2;nCarol;
//    s3;r0;c0;a0;K;F;p3;nDoe;
//    P"
//
// A regression run:
//   THIRDEYE_CHARGEN_AUTO="..." build/.../thirdeye EYE.RES --chargen-test
//                                                  --skip-intro --vm
// then hex-diff the resulting CREATE.SAV against a known-good golden file.

struct AutoAction {
	enum Kind { SLOT, RACE, CLASS, ALIGN, KEEP, REROLL, MODIFY, FACES, OK_BTN,
	            ABIL_UP, ABIL_DOWN, PORTRAIT, NAME, PLAY } kind;
	int  arg = 0;       // numeric arg
	std::string text;   // for NAME
};

std::vector<AutoAction> parseChargenAuto(const char *src) {
	std::vector<AutoAction> out;
	if (!src) return out;
	std::string s = src;
	size_t i = 0;
	auto skipWs = [&]() {
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
		                        s[i] == '\r' || s[i] == ';' || s[i] == ','))
			++i;
	};
	auto parseNum = [&]() -> int {
		int v = 0; bool any = false;
		while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
			v = v * 10 + (s[i] - '0'); ++i; any = true;
		}
		return any ? v : -1;
	};
	while (i < s.size()) {
		skipWs();
		if (i >= s.size()) break;
		char c = s[i++];
		AutoAction a{};
		switch (c) {
		case 's': a.kind = AutoAction::SLOT;    a.arg = parseNum(); break;
		case 'r': a.kind = AutoAction::RACE;    a.arg = parseNum(); break;
		case 'c': a.kind = AutoAction::CLASS;   a.arg = parseNum(); break;
		case 'a': a.kind = AutoAction::ALIGN;   a.arg = parseNum(); break;
		case 'K': a.kind = AutoAction::KEEP;    break;
		case 'R': a.kind = AutoAction::REROLL;  break;
		case 'M': a.kind = AutoAction::MODIFY;  break;
		case 'F': a.kind = AutoAction::FACES;   break;
		case 'O': a.kind = AutoAction::OK_BTN;  break;
		case '+': a.kind = AutoAction::ABIL_UP;   a.arg = parseNum(); break;
		case '-': a.kind = AutoAction::ABIL_DOWN; a.arg = parseNum(); break;
		case 'p': a.kind = AutoAction::PORTRAIT;  a.arg = parseNum(); break;
		case 'n': {
			a.kind = AutoAction::NAME;
			// Read until ';' or end. Strip trailing whitespace.
			size_t end = s.find_first_of(",;\n\r", i);
			if (end == std::string::npos) end = s.size();
			a.text = s.substr(i, end - i);
			i = end;
			break;
		}
		case 'P': a.kind = AutoAction::PLAY; break;
		default: continue; // skip unknown
		}
		out.push_back(std::move(a));
	}
	return out;
}

// Apply one action by mutating chargen State directly. Returns true if
// applied; false if the action doesn't fit the current step (= caller can
// retry / skip). Direct state-mutation rather than SDL-event injection
// because it bypasses event timing entirely -- the autopilot completes
// in N pumps for N actions, no race conditions.
bool applyChargenAction(const AutoAction &a, State &state, std::mt19937 &rng,
                        const std::filesystem::path &chargenDir,
                        const std::vector<uint8_t> &picBytes,
                        SDL_Window *window) {
	using K = AutoAction;
	switch (a.kind) {
	case K::SLOT:
		if (state.step != Step::EntryScreen) return false;
		if (a.arg < 0 || a.arg > 3) return false;
		state.step       = Step::PickRace;
		state.activeSlot = a.arg;
		state.raceCursor = state.party[a.arg].race >= 0
		                       ? state.party[a.arg].race : 0;
		return true;
	case K::RACE:
		if (state.step != Step::PickRace || state.activeSlot < 0) return false;
		state.raceCursor = std::clamp(a.arg, 0, 11);
		// Commit (same as Enter in handleRacePickEvent).
		state.party[state.activeSlot].race = state.raceCursor;
		{
			int allowed[15];
			int n = buildAllowedClasses(state.raceCursor, allowed);
			state.classCursor = 0;
			int prev = state.party[state.activeSlot].klass;
			if (prev >= 0 && n > 0)
				for (int i = 0; i < n; ++i)
					if (allowed[i] == prev) { state.classCursor = i; break; }
		}
		state.step = Step::PickClass;
		return true;
	case K::CLASS: {
		if (state.step != Step::PickClass || state.activeSlot < 0) return false;
		int allowed[15];
		int n = buildAllowedClasses(state.party[state.activeSlot].race, allowed);
		// arg here is the REAL class index (kClassNames index) -- map back
		// to the filtered cursor position so the commit logic finds it.
		int realIdx = std::clamp(a.arg, 0, 14);
		int visIdx = 0;
		for (int i = 0; i < n; ++i) if (allowed[i] == realIdx) { visIdx = i; break; }
		state.classCursor = visIdx;
		state.party[state.activeSlot].klass = (n > 0) ? allowed[visIdx] : 0;
		state.alignmentCursor = state.party[state.activeSlot].alignment >= 0
		                          ? state.party[state.activeSlot].alignment : 0;
		state.step = Step::PickAlignment;
		return true;
	}
	case K::ALIGN:
		if (state.step != Step::PickAlignment || state.activeSlot < 0) return false;
		state.alignmentCursor = std::clamp(a.arg, 0, 8);
		state.party[state.activeSlot].alignment = state.alignmentCursor;
		rollStats(state.party[state.activeSlot], rng);
		state.step = Step::ShowStats;
		return true;
	case K::KEEP:
		if (state.step != Step::ShowStats || state.activeSlot < 0) return false;
		state.portraitCursor = state.party[state.activeSlot].portrait;
		state.step = Step::PickPortrait;
		return true;
	case K::REROLL:
		if (state.step != Step::ShowStats || state.activeSlot < 0) return false;
		rollStats(state.party[state.activeSlot], rng);
		return true;
	case K::MODIFY:
		if (state.step != Step::ShowStats || state.activeSlot < 0) return false;
		state.statCursor = 0;
		state.step = Step::ModifyStats;
		return true;
	case K::FACES:
		// Same as KEEP -- opens portrait picker without committing the slot.
		if (state.step != Step::ShowStats || state.activeSlot < 0) return false;
		state.portraitCursor = state.party[state.activeSlot].portrait;
		state.step = Step::PickPortrait;
		return true;
	case K::OK_BTN:
		if (state.step != Step::ModifyStats) return false;
		state.step = Step::ShowStats;
		return true;
	case K::ABIL_UP:
	case K::ABIL_DOWN: {
		if (state.step != Step::ModifyStats || state.activeSlot < 0) return false;
		int i = std::clamp(a.arg, 0, NUM_ABIL - 1);
		auto &v = state.party[state.activeSlot].abil[i];
		v += (a.kind == K::ABIL_UP) ? 1 : -1;
		v = std::clamp(v, 3, 18);
		return true;
	}
	case K::PORTRAIT: {
		if (state.step != Step::PickPortrait || state.activeSlot < 0) return false;
		int total = static_cast<int>(GRAPHICS::Bitmap(picBytes).getNumberOfBitmaps());
		state.portraitCursor = std::clamp(a.arg, 0, std::max(0, total - 1));
		state.party[state.activeSlot].portrait = state.portraitCursor;
		// Match handlePortraitPickEvent's Enter path: go to EnterName.
		state.step = Step::EnterName;
		SDL_StartTextInput(window);
		return true;
	}
	case K::NAME: {
		if (state.step != Step::EnterName || state.activeSlot < 0) return false;
		auto &c = state.party[state.activeSlot];
		std::strncpy(c.name, a.text.c_str(), sizeof(c.name) - 1);
		c.name[sizeof(c.name) - 1] = 0;
		// Uppercase ASCII (matches the SDL_EVENT_TEXT_INPUT handler's behaviour).
		for (auto &ch : c.name)
			if (ch && static_cast<unsigned char>(ch) < 0x80)
				ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		if (std::strlen(c.name) == 0) return false; // chargen rejects empty
		SDL_StopTextInput(window);
		c.filled = true;
		if (writeCreateSav(chargenDir, state)) {
			state.step       = Step::EntryScreen;
			state.activeSlot = -1;
		} else {
			c.filled = false;
		}
		return true;
	}
	case K::PLAY:
		if (state.step != Step::EntryScreen) return false;
		if (!partyComplete(state)) return false;
		state.done = true;
		return true;
	}
	return false;
}

} // namespace

bool THIRDEYE::chargen::runChargenScreen(GRAPHICS::Graphics &gfx,
                                         const std::filesystem::path &chargenDir) {
	// Required chargen assets sit beside the game's .RES (e.g. ../data/CHARGEN/).
	// PALETTE.COL: 256-colour 6-bit VGA palette (768 bytes, no header).
	// CHARPICS.BMP: 90 portrait sheet (see graphics/bitmap.cpp).
	// FONT8.FNT: 8x8 bit-packed font with lowercase glyphs (FONT6 is
	//            uppercase-only). The race-picker uses uppercase, but
	//            FONT8 handles both -- one font for the whole flow.
	auto palBytes = readFile(chargenDir / "PALETTE.COL");
	auto picBytes = readFile(chargenDir / "CHARPICS.BMP");
	auto fntBytes = readFile(chargenDir / "FONT8.FNT");  // body text (mixed case)
	auto fnt6Bytes = readFile(chargenDir / "FONT6.FNT"); // menu list (uppercase, tighter)
	if (palBytes.empty() || picBytes.empty()) {
		std::cout << "  [chargen: missing assets in " << chargenDir
		          << " (need PALETTE.COL + CHARPICS.BMP); skipping screen]"
		          << std::endl;
		// Caller proceeds with whatever CREATE.SAV is on disk (accept).
		return true;
	}

	gfx.loadPalette(palBytes, /*isRes=*/false);

	// Load ITEMTYPE.DAT once + validate every class kit against it. If
	// validation fails the chargen still runs (the kit produces playable
	// items either way) but we log so a future kit edit doesn't silently
	// give the Mage a long sword.
	loadItemTypeDat(chargenDir);
	validateClassKits();

	GRAPHICS::Bitmap portraits(picBytes);
	// `picBytes.empty()` (above) doesn't catch a malformed-but-nonzero
	// CHARPICS.BMP -- the parser may decline every header path and leave
	// `numberOfBitmaps == 0`. The portrait picker later does
	// `(strip0 + i) % total`, which divides by zero if we don't bail here.
	if (portraits.getNumberOfBitmaps() == 0) {
		std::cout << "  [chargen: CHARPICS.BMP at " << chargenDir
		          << " parsed to 0 portraits (truncated / unknown format);"
		          << " skipping screen]" << std::endl;
		return true;
	}
	GRAPHICS::Cps backdrop = GRAPHICS::loadCps(chargenDir / "CHARGEN.CPS");
	// CHARGENB.CPS holds the chargen UI sprites (blank blue/red buttons,
	// PLAY/DELETE composites, stone separators, sparkles). We grab the
	// blank blue button at (128,128,32,14) and overlay text to compose
	// BACK / KEEP / REROLL / etc.
	GRAPHICS::Cps chargenb = GRAPHICS::loadCps(chargenDir / "CHARGENB.CPS");

	// Sample portraits shown in empty slots until the per-slot flow finishes.
	// (Once a real character is rolled, we'll use its chosen portrait instead.)
	std::vector<uint8_t> previewIdx = { 1, 2, 3, 4 };

	State state;
	std::mt19937 rng{std::random_device{}()};
	// Headless verification: skip straight into a sub-step to snapshot it.
	if (const char *s = std::getenv("THIRDEYE_CHARGEN_STEP")) {
		std::string step = s;
		if (step == "race") {
			state.step = Step::PickRace; state.activeSlot = 0;
		} else if (step == "class") {
			state.step = Step::PickClass; state.activeSlot = 0;
		} else if (step == "alignment") {
			state.step = Step::PickAlignment; state.activeSlot = 0;
		} else if (step == "stats") {
			state.activeSlot = 0;
			state.party[0].race  = 0;   // HUMAN MALE
			state.party[0].klass = 0;   // FIGHTER
			state.party[0].alignment = 0;
			rollStats(state.party[0], rng);
			state.step = Step::ShowStats;
		} else if (step == "modify") {
			state.activeSlot = 0;
			state.party[0].race  = 0;
			state.party[0].klass = 0;
			state.party[0].alignment = 0;
			rollStats(state.party[0], rng);
			state.step = Step::ModifyStats;
		} else if (step == "portrait" || step == "faces") {
			state.activeSlot = 0;
			state.party[0].race  = 0;
			state.party[0].klass = 0;
			state.party[0].alignment = 0;
			state.step = Step::PickPortrait;
		} else if (step == "name") {
			state.activeSlot = 0;
			state.party[0].race  = 0;
			state.party[0].klass = 0;
			state.party[0].alignment = 0;
			rollStats(state.party[0], rng);
			enterNameStep(state, gfx.getWindow());
		} else if (step == "write") {
			// Headless: roll char + write CREATE.SAV in one shot. `done` only
			// flips on a successful save so a failing chargen-test exits with
			// the loop still running and the operator sees the [chargen: ...
			// failed] log.
			for (int pc = 0; pc < 4; ++pc) {
				state.party[pc].race  = 0;
				state.party[pc].klass = 0;
				state.party[pc].alignment = 0;
				rollStats(state.party[pc], rng);
				std::snprintf(state.party[pc].name,
				              sizeof(state.party[pc].name),
				              "Foo%d", pc + 1);
				state.party[pc].filled = true;
			}
			state.done = writeCreateSav(chargenDir, state);
		}
	}
	render(gfx, picBytes, fntBytes, fnt6Bytes, backdrop, chargenb, previewIdx, state);
	gfx.update();
	std::cout << "  [chargen: entry screen up -- click a slot to pick a "
	             "race, P/Enter to play, Esc to cancel]" << std::endl;
	if (const char *p = std::getenv("THIRDEYE_DUMP_CHARGEN"))
		gfx.saveScreenshot(p);

	// Chargen autopilot for regression testing. Parsed once from the env var
	// at chargen entry; advanced one action per outer-loop iteration (we
	// don't go through SDL events because the chargen state-mutation logic
	// is fast enough and we want determinism across SDL versions / platforms).
	auto autoActions = parseChargenAuto(std::getenv("THIRDEYE_CHARGEN_AUTO"));
	size_t autoIdx = 0;
	if (!autoActions.empty())
		std::cout << "  [chargen: autopilot loaded with "
		          << autoActions.size() << " actions]" << std::endl;

	// Event loop. Re-render after any input that mutates state. Cancel and
	// accept both currently fall through to whichever CREATE.SAV is on disk;
	// a proper accept path will write the rolled party out.
	SDL_Event event;
	while (!state.done) {
		// Drive the autopilot one step per outer pump. Each successful
		// action moves the state machine; if an action doesn't fit the
		// current step (programmer error in the script) we log + skip so
		// the loop doesn't hang.
		if (autoIdx < autoActions.size()) {
			Step beforeAuto = state.step;
			bool ok = applyChargenAction(autoActions[autoIdx], state, rng,
			                             chargenDir, picBytes, gfx.getWindow());
			if (ok) {
				++autoIdx;
				std::cout << "  [chargen autopilot: action " << autoIdx
				          << "/" << autoActions.size()
				          << " applied; step now=" << static_cast<int>(state.step)
				          << "]" << std::endl;
				// Re-render after an auto action so DUMP captures it.
				render(gfx, picBytes, fntBytes, fnt6Bytes, backdrop, chargenb,
				       previewIdx, state);
				gfx.update();
				continue;
			}
			std::cout << "  [chargen autopilot: action " << (autoIdx + 1)
			          << " (kind=" << static_cast<int>(autoActions[autoIdx].kind)
			          << " arg=" << autoActions[autoIdx].arg
			          << ") didn't fit current step "
			          << static_cast<int>(beforeAuto) << "; skipping]"
			          << std::endl;
			++autoIdx;
		}
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT)
				throw THIRDEYE::runtime::QuitRequested{};
			Step before = state.step;
			int beforeRace = state.raceCursor;
			int beforeClass = state.classCursor;
			int beforeAlign = state.alignmentCursor;
			int beforeStat = state.statCursor;
			int beforePortrait = state.portraitCursor;
			int beforeSlot = state.activeSlot;
			// Snapshot the active char's name length so typed chars / backspace
			// drive a redraw.
			size_t beforeNameLen = (state.activeSlot >= 0)
				? std::strlen(state.party[state.activeSlot].name) : 0;
			int beforeStr = state.party[std::max(0, state.activeSlot)].abil[STR];
			// Snapshot the whole ability vector so MODIFY +/- triggers a redraw
			// even when only one ability changes (and especially when the
			// "STR" element happens to be the same after a +1/-1 round-trip).
			int beforeAbilSum = 0;
			for (int i = 0; i < NUM_ABIL; ++i)
				beforeAbilSum += state.party[std::max(0, state.activeSlot)].abil[i];
			switch (state.step) {
			case Step::EntryScreen:   handleEntryEvent(event, gfx, state); break;
			case Step::PickRace:      handleRacePickEvent(event, gfx, state); break;
			case Step::PickClass:     handleClassPickEvent(event, gfx, state); break;
			case Step::PickAlignment: handleAlignmentPickEvent(event, gfx, state, rng); break;
			case Step::ShowStats:     handleStatsEvent(event, gfx, state, rng); break;
			case Step::ModifyStats:   handleModifyEvent(event, gfx, state); break;
			case Step::PickPortrait:  handlePortraitPickEvent(event, gfx, state, picBytes); break;
			case Step::EnterName:     handleNameEvent(event, state, chargenDir, gfx.getWindow()); break;
			}
			int afterStr = state.party[std::max(0, state.activeSlot)].abil[STR];
			int afterAbilSum = 0;
			for (int i = 0; i < NUM_ABIL; ++i)
				afterAbilSum += state.party[std::max(0, state.activeSlot)].abil[i];
			size_t afterNameLen = (state.activeSlot >= 0)
				? std::strlen(state.party[state.activeSlot].name) : 0;
			if (state.step != before || state.raceCursor != beforeRace ||
			    state.classCursor != beforeClass ||
			    state.alignmentCursor != beforeAlign ||
			    state.statCursor != beforeStat ||
			    state.portraitCursor != beforePortrait ||
			    state.activeSlot != beforeSlot ||
			    afterStr != beforeStr ||
			    afterAbilSum != beforeAbilSum ||
			    afterNameLen != beforeNameLen)
				render(gfx, picBytes, fntBytes, fnt6Bytes, backdrop, chargenb, previewIdx, state);
		}
		// Sparkle animation. Advance the strip every ~150 ms (every 9 pumps
		// at 16 ms each) so the stars twinkle visibly without strobing.
		// Only re-render when we're in a sub-step (the entry screen has no
		// active slot, so nothing to animate).
		static int pumpTick = 0;
		++pumpTick;
		if (state.activeSlot >= 0 && pumpTick % 9 == 0) {
			++state.sparkleFrame;
			render(gfx, picBytes, fntBytes, fnt6Bytes, backdrop, chargenb,
			       previewIdx, state);
		}
		gfx.update();
		SDL_Delay(16);
	}
	return !state.cancelled;
}
