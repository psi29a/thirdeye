#include "internal.hpp"

#include "../graphics/graphics.hpp"
#include "../resources/res.hpp"
#include "../vm/objects.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <streambuf>
#include <string>

namespace THIRDEYE::runtime {

bool gRtTrace = false;
bool gCompassDirty = false;
bool gPerf = false;
int gDrawCount = 0;
long gDrawNanos = 0;
std::chrono::steady_clock::time_point gLastPresent;
std::chrono::steady_clock::time_point gBootStart;
bool gFirstPresentLogged = false;

std::ostream &rt() {
	struct NullBuf : std::streambuf { int overflow(int c) override { return c; } };
	static NullBuf nb;
	static std::ostream null(&nb);
	return gRtTrace ? std::cout : null;
}

std::string formatSop(const std::string &fmt, const std::vector<VM::Value> &args,
                      size_t start, VM::Interpreter &vm) {
	std::string out;
	size_t ai = start;
	for (size_t i = 0; i < fmt.size(); ++i) {
		if (fmt[i] != '%' || i + 1 >= fmt.size()) { out += fmt[i]; continue; }
		char conv = fmt[++i];
		if (conv == '%') { out += '%'; continue; }
		VM::Value a = ai < args.size() ? args[ai++] : 0;
		if (conv == 'd' || conv == 'u' || conv == 'i')
			out += std::to_string(a);
		else if (conv == 's')
			out += vm.readString(a);
		else { out += '%'; out += conv; }
	}
	return out;
}

bool loadDungeonLevel(int level, VM::ObjectSystem &objects,
                      RESOURCES::Resource &res, GRAPHICS::Graphics *gfx) {
	constexpr uint16_t kDungeonClass = 1381;
	constexpr uint32_t kLvlmapOff = 0, kWallsetOff = 7168;
	int dungeon = objects.firstObjectOfClass(kDungeonClass);
	int firstMap = res.getResourceNumber("Mausoleum 1"); // level 1's map
	int map = firstMap >= 0 ? firstMap + (level - 1) : -1;
	if (dungeon < 0 || map < 0)
		return false;
	std::string mapName = res.getResourceName(static_cast<uint16_t>(map));
	// Pick one of the four tilesets by the map's area keyword (best-effort
	// reconstruction of native change_level's level->tileset table).
	std::string area = "Mausoleum";
	if (mapName.find("Forest") != std::string::npos) area = "Forest";
	else if (mapName.find("Graveyard") != std::string::npos) area = "Ruins";
	else if (mapName.find("Guild") != std::string::npos ||
	         mapName.find("Mages") != std::string::npos ||
	         mapName.find("Temple") != std::string::npos) area = "Marble";
	int walls = res.getResourceNumber(area + " walls");
	int pal = res.getResourceNumber(area + " palette");
	try {
		std::vector<uint8_t> &maze = res.getAsset(static_cast<uint16_t>(map));
		if (uint8_t *p = objects.classStaticPtr(dungeon, kDungeonClass, kLvlmapOff,
		                                        static_cast<uint32_t>(maze.size())))
			std::memcpy(p, maze.data(), maze.size());
		// Debug: dump the maze cells around (15,15) (the new-game start) to inspect
		// wall layout. lvlmap is 32x32, 1 byte/cell (0x00=wall, 0xff=floor).
		if (const char *mz = std::getenv("THIRDEYE_MAZE")) {
			int x0 = 11, y0 = 11, x1 = 19, y1 = 19; // default: the (15,15) start area
			std::sscanf(mz, "%d,%d,%d,%d", &x0, &y0, &x1, &y1);
			std::cerr << "[maze] x=" << x0 << ".." << x1 << " y=" << y0 << ".." << y1
			          << " (row=y; 0=wall 255=floor):\n";
			for (int y = y0; y <= y1; ++y) {
				std::cerr << "  y" << y << ":";
				for (int x = x0; x <= x1; ++x) {
					int idx = y * 32 + x;
					std::cerr << "\t" << (idx >= 0 && idx < (int)maze.size()
					    ? (int)maze[idx] : -1);
				}
				std::cerr << "\n";
			}
		}
		if (walls >= 0)
			if (uint8_t *w = objects.classStaticPtr(dungeon, kDungeonClass,
			                                        kWallsetOff, 4)) {
				w[0] = walls & 0xFF;
				w[1] = (walls >> 8) & 0xFF;
				w[2] = w[3] = 0;
			}
		// The wall shapes use palette indices 176-190 (the fixed palette leaves them
		// white as level placeholders); the area palette is the 16-colour stone
		// gradient (near->far wall shading) for 176-191, loaded at base 0xB0.
		if (gfx && pal >= 0)
			gfx->setPaletteRange(res.getAsset(static_cast<uint16_t>(pal)), 0xB0);
		// Creature palettes (THIRDEYE_TESTMON bring-up): each monster sprite has its
		// own palette loaded into a high range -- e.g. "Sword wraith palette" (337)
		// at indices 192-223. The native change_level loads these per level; load the
		// wraith's here so the rendered creature is coloured, not placeholder-white.
		// (TODO: derive per-creature from the level's monster set instead of hard-coding.)
		if (gfx && std::getenv("THIRDEYE_TESTMON")) {
			int cp = res.getResourceNumber("Sword wraith palette");
			if (cp >= 0)
				gfx->setPaletteRange(res.getAsset(static_cast<uint16_t>(cp)), 0xC0);
		}
		std::cout << "  [loadDungeonLevel " << level << " = \"" << mapName << "\" ("
		          << area << ": map " << map << ", walls " << walls << ", palette "
		          << pal << ")]" << std::endl;
	} catch (const std::exception &e) {
		std::cout << "  [loadDungeonLevel " << level << " failed: " << e.what() << "]"
		          << std::endl;
		return false;
	}
	return true;
}

} // namespace THIRDEYE::runtime
