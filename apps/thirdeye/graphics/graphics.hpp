#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#define WIDTH 320
#define HEIGHT 200

#include "font.hpp"
#include "palette.hpp"
#include "bitmap.hpp"

#include "SDL.h"
// NB: SDL_syswm.h is intentionally NOT included here. On Linux it pulls in
// X11's <X.h>, which #defines None/Status/Bool/... into the global namespace and
// breaks any downstream identifier with those names (e.g. VM::AddrSpace). It's
// only needed by graphics.cpp, which includes it directly.

#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>
#include <tuple>
#include <random>
#include <memory>
#include <string>

using std::tuple;
using RNGType = std::mt19937;

#define NOOP	 		0
#define SET_PAL			1
#define DISP_BMP		2
#define DISP_BMA		3
#define FADE_IN			4
#define SCROLL_LEFT		5
#define PAN_LEFT		6
#define DRAW_CURTAIN	7
#define MATERIALIZE 	8
#define DISP_OVERLAY	9
#define ZOOM_INTO		10

typedef std::map<uint8_t, tuple<uint8_t, uint8_t, std::vector<uint8_t> > > sequence;

namespace GRAPHICS {

class Graphics {
private:
	uint16_t mScale;
	SDL_Window *mWindow;
	SDL_Renderer *mRenderer;
	SDL_Surface *mScreen;
	// A snapshot of the screen holding only the bitmap art (no dynamic text --
	// text is drawn via printText, never drawImage). setTextWindow restores a
	// text box from this so text overlays the real background (panel art) instead
	// of a flat fill. mTextRestoreBg selects that behaviour (in-game HUD) vs the
	// flat-fill clear (the title menu, whose "Menu shapes" bakes the options in).
	SDL_Surface *mBackdrop = nullptr;
	bool mTextRestoreBg = false;
	// The compass widget lives on its own AESOP page (104) that the original
	// re-composites every frame, so it survives HUD redraws. We flatten pages onto
	// one surface, so we instead snapshot the compass region when it refreshes and
	// re-apply it each present -- otherwise the cardinal/facing indicator (only
	// redrawn by the bytecode on a turn) gets erased by an inventory-close redraw.
	SDL_Surface *mCompassSnap = nullptr;
	SDL_Cursor *mCursor;
	SDL_Palette *mPalette;
	uint8_t mState;
	int16_t mAlpha;
	uint16_t mFrames;
	uint16_t mCounter;
	uint32_t mClock;
	uint32_t mRunningClock;
	uint32_t mVideoWait;
	uint32_t mSleep;
	std::vector<uint8_t> mBuffer;
	std::map<uint8_t, SDL_Surface*> mSurface;
	std::map<uint8_t, tuple<uint8_t, uint8_t, std::vector<uint8_t> > > mVideo;
	std::map<uint16_t, uint16_t> mBitmap;
	RNGType rng;

	// Per-text-window state (font/cursor/colour), keyed by AESOP window number.
	struct TextWin {
		std::shared_ptr<Font> font;
		int htab = 0, vtab = 0;
		uint8_t fg = 15;
		int winX0 = 0, winX1 = WIDTH - 1; // bound window's horizontal extent
		int winY0 = 0, winY1 = HEIGHT - 1; // bound window's vertical extent
		int justify = 0;                  // 0=left, 1=right, 2=center (GIL2VFX)
	};
	std::map<int, TextWin> mTextWin;
	// Built glyph sets, cached by font resource id (text_style is called every
	// menu redraw -- we must not rebuild the font each frame).
	std::map<int, std::shared_ptr<Font>> mFontCache;

	int zoomSurfaceRGBA(SDL_Surface * src, SDL_Surface * dst);

	void fadeIn();
	void fadeOut();

	void scrollLeft(std::vector<uint8_t> bmp);

	void materializeImage(std::vector<uint8_t> bmp);

	void playAnimation(std::vector<uint8_t> video);
	void panDirection(uint8_t panDir, std::vector<uint8_t> bgRight,
			std::vector<uint8_t> bgLeft, std::vector<uint8_t> bgFarLeft,
			std::vector<uint8_t> fgRight, std::vector<uint8_t> fgLeft);
	void drawCurtain(std::vector<uint8_t> bmp);

public:
	Graphics(uint16_t scale = 1, bool renderer = false);
	virtual ~Graphics();

	// mirror: AESOP draw_bitmap mirror flag (GIL2VFX) -- 1=X (horizontal flip),
	// 2=Y (vertical), 3=both. The dungeon view draws right-hand walls as the
	// X-mirror of the same shape used on the left.
	// posX/posY are signed: the dungeon view legitimately draws shapes at negative
	// coordinates (a left-edge wall at x=-32 that SDL then clips), so they must NOT
	// be unsigned -- a uint16_t wraps -32 to 65504 and the shape vanishes off-screen.
	void drawImage(std::vector<uint8_t> &bmp, uint16_t index, int posX,
			int posY, bool transparency = false, int mirror = 0);

	// Constrain subsequent blits to a rectangle on the screen surface (used to
	// clip the dungeon 3D view to its window so wide wall shapes don't bleed into
	// the character panels). clearClip() removes the constraint.
	void setClip(int x, int y, int w, int h);
	void clearClip();
	// Compass persistence (see mCompassSnap): snapshotCompass() captures the compass
	// region from the screen (called when AESOP refreshes the compass page);
	// restoreCompass() re-applies it (called each present while in-game) so the
	// facing indicator isn't erased by other redraws.
	void snapshotCompass();
	void restoreCompass();
	// fill_rectangle(x0,y0,x1,y1,color): flood an inclusive screen rectangle with a
	// palette colour. The SOP screens use it to clear a panel before redrawing (e.g.
	// the character-stats screen clears the equipment area first); also refreshes the
	// text-free backdrop snapshot so later text overlays the cleared box, not stale art.
	void fillRect(int x0, int y0, int x1, int y1, uint8_t color);
	void drawText(std::vector<uint8_t> &fnt, std::string text, uint16_t posX,
			uint16_t posY);

	void playVideo(sequence);
	void stopVideo();
	bool isVideoPlaying();

	void zoomIntoImage(std::vector<uint8_t> &bmp);

	void loadPalette(std::vector<uint8_t> &basePal, bool isRes = true);
	void loadPalette(std::vector<uint8_t> &basePal,
			std::vector<uint8_t> &subPal, std::string index);

	// Write a palette resource's colours into the live palette starting at
	// `firstColor`, leaving the rest intact (AESOP set_palette writes a region:
	// PAL_FIXED at 0x00, PAL_WALLS at 0xB0, ...). Used by the SOP runtime.
	void setPaletteRange(std::vector<uint8_t> &palRes, uint16_t firstColor,
			bool skipMarker = false);

	void loadMouse(std::vector<uint8_t> &bitmap, uint16_t index);

	uint32_t getSleep();
	void update();

	// Save the current screen surface to a BMP (debug / headless verification).
	void saveScreenshot(const std::string &path);

	// Map a window-pixel coordinate (from an SDL mouse event) to the 320x200
	// logical space the game's windows use. Accounts for the integer scale.
	void mouseToLogical(int wx, int wy, int &lx, int &ly) const;

	// --- AESOP text output (GRAPHICS.C text_window/style/color/xy + print) ---
	// AESOP addresses text into numbered "text windows", each with a font,
	// colour and cursor. The runtime feeds the font/string bytes (they're
	// resources); we hold the per-window state and render.
	void setTextFont(int wndnum, int fontId, std::vector<uint8_t> &fontRes);
	void setTextColor(int wndnum, uint8_t color);   // text_color's remap target
	void setTextXY(int wndnum, int x, int y);
	// Bind a text window to a graphics-window rectangle: records the extent (for
	// centering) and clears the interior to its background, so previously-drawn
	// (or bitmap-baked) text there doesn't ghost under the fresh text.
	void setTextWindow(int wndnum, int x0, int y0, int x1, int y1);
	// Choose how setTextWindow clears a box before (re)drawing text: true =
	// restore the bitmap backdrop (text overlays panel art, in-game); false =
	// flat-fill the sampled background (the title menu, which bakes its options).
	void setTextRestoreBackground(bool restore) { mTextRestoreBg = restore; }
	void setTextJustify(int wndnum, int justify);    // text_style's justify mode
	// Draw `text` at the window's cursor in its colour/font (glyphs are masks,
	// tinted to the colour), advancing the cursor.
	void printText(int wndnum, const std::string &text);
};

}
#endif //GRAPHICS_HPP
