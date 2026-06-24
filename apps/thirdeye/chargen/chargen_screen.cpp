#include "chargen_screen.hpp"

#include "../graphics/graphics.hpp"
#include "../graphics/bitmap.hpp"
#include "../graphics/cps.hpp"
#include "../runtime/internal.hpp" // QuitRequested

#include <SDL.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

std::vector<uint8_t> readFile(const std::filesystem::path &p) {
	std::ifstream f(p, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
}

} // namespace

void THIRDEYE::chargen::runChargenScreen(GRAPHICS::Graphics &gfx,
                                         const std::filesystem::path &chargenDir) {
	// Required chargen assets sit beside the game's .RES (e.g. ../data/CHARGEN/).
	// PALETTE.COL: 256-colour 6-bit VGA palette (768 bytes, no header).
	// CHARPICS.BMP: 90 portrait sheet (see graphics/bitmap.cpp).
	// FONT8.FNT: 8x8 bit-packed font, has real lowercase glyphs (vs FONT6
	//            which is uppercase-only and used for menu labels).
	auto palBytes = readFile(chargenDir / "PALETTE.COL");
	auto picBytes = readFile(chargenDir / "CHARPICS.BMP");
	auto fntBytes = readFile(chargenDir / "FONT8.FNT");
	if (palBytes.empty() || picBytes.empty()) {
		std::cout << "  [chargen: missing assets in " << chargenDir
		          << " (need PALETTE.COL + CHARPICS.BMP); skipping screen]"
		          << std::endl;
		return;
	}

	gfx.loadPalette(palBytes, /*isRes=*/false);
	// drawImage takes the raw container + sub-shape index (it builds its own
	// Bitmap internally for the cache key). The Bitmap below is only used to
	// bounds-check the indices before we ask for them.
	GRAPHICS::Bitmap portraits(picBytes);

	// CHARGEN.CPS is the full-screen 320x200 backdrop: stone-textured frame +
	// gold "Character Generation" title baked in + the empty slot/info panels.
	// The original program decompresses it (Westwood Format80/LCW) into video
	// memory before drawing anything else. Fall back to a flat grey if the
	// file is missing -- we still want a usable placeholder.
	GRAPHICS::Cps backdrop = GRAPHICS::loadCps(chargenDir / "CHARGEN.CPS");
	if (!backdrop.pixels.empty())
		gfx.drawIndexed(backdrop.pixels, backdrop.width, backdrop.height, 0, 0);
	else
		gfx.fillRect(0, 0, WIDTH - 1, HEIGHT - 1, 0);

	// Drop a few real portraits into the slots so the user can see the
	// decoder working. Portrait 0 is the bundled file's blank template, so
	// preview a meaningful sample: portrait 1 (the bundled wizard), 2, 3, 4.
	// Slot frames are baked into the CPS backdrop. Measured authoritatively
	// by scanning the rendered CHARGEN.CPS (byte-identical to source) for
	// solid frame lines that span the slot's full width.
	//
	// Both rows have the SAME layout: left slot at x=16, right slot at x=80,
	// with a 32-px decorative gap between them. Verified by dark-run scans
	// at the row frame Ys:
	//   y=63  (top row top frame):    [16..48], [80..112]
	//   y=96  (top row bottom frame): [17..48], [80..112]
	//   y=127 (bot row top frame):    [16..48], [80..112]
	//   y=160 (bot row bottom frame): [16..48], [80..104]
	// Portrait top sits at the frame line (y=63 / y=127) -- portrait rows
	// 0..N have palette index 0xac (stone-blend padding) that merges into
	// the frame visually, so the visible "content" starts a few rows lower.
	struct Slot { int x, y; };
	const Slot slots[4] = { {17, 64}, {81, 64}, {17, 128}, {81, 128} };
	const uint16_t previewIdx[4] = { 1, 2, 3, 4 };
	// Set THIRDEYE_CHARGEN_NO_PORTRAITS=1 to render JUST the CPS backdrop
	// (used to eyeball where the baked-in slot frames actually land).
	if (!std::getenv("THIRDEYE_CHARGEN_NO_PORTRAITS")) {
		for (int i = 0; i < 4; ++i) {
			if (previewIdx[i] < portraits.getNumberOfBitmaps()) {
				// slots[i] is the portrait top-left (inside the baked frame).
				gfx.drawImage(picBytes, previewIdx[i],
				              slots[i].x, slots[i].y,
				              /*transparency=*/true, /*mirror=*/0,
				              /*cacheId=*/0);
			}
		}
	}

	// Right-side info panel text. The original screenshot reads:
	//   "Select the box of the character you wish to create or view."
	// Right panel inner area sits at roughly x=128..308, y=50..180 in the
	// CPS backdrop. drawText uses the live palette; glyphs are masks tinted
	// to the current text colour (white by default).
	if (!fntBytes.empty()) {
		gfx.drawText(fntBytes, "Select the box of",   140,  60);
		gfx.drawText(fntBytes, "the character you",   140,  72);
		gfx.drawText(fntBytes, "wish to create or",   140,  84);
		gfx.drawText(fntBytes, "view.",                140,  96);
		gfx.drawText(fntBytes, "(per-slot flow WIP)", 140, 120);
		gfx.drawText(fntBytes, "P/Enter = play,",     140, 152);
		gfx.drawText(fntBytes, "Esc = cancel.",       140, 164);
	}

	gfx.update();
	std::cout << "  [chargen: entry screen up -- press P/Enter to play, "
	             "Esc to cancel]" << std::endl;
	// Headless / regression aid: drop a snapshot of the rendered screen when
	// the env var is set. Lets the autowalk + dump harness verify the
	// portraits land in the right slots without driving the live UI.
	if (const char *p = std::getenv("THIRDEYE_DUMP_CHARGEN"))
		gfx.saveScreenshot(p);

	// Wait for the user to commit or cancel. Cancel is best-effort: returning
	// without resetting the SOP's mem[1264] still re-enters `start` in mode
	// CHGN, so we currently always fall through to the game. A proper cancel
	// path needs to write mem[1264] = MODE_INTR -- deferred with the rest of
	// the chargen flow.
	SDL_Event event;
	bool done = false;
	while (!done) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT)
				throw THIRDEYE::runtime::QuitRequested{};
			if (event.type == SDL_KEYDOWN) {
				auto k = event.key.keysym.sym;
				if (k == SDLK_RETURN || k == SDLK_KP_ENTER ||
				    k == SDLK_p      || k == SDLK_ESCAPE)
					done = true;
			}
		}
		gfx.update();
		SDL_Delay(16);
	}
}
