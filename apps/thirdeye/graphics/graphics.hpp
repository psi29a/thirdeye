#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#define WIDTH 320
#define HEIGHT 200

#include "font.hpp"
#include "palette.hpp"
#include "bitmap.hpp"

#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <unordered_map>
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
	bool mSuspendBackdrop = false;
	bool mSuppressCompassSnap = false;
	// SOP screen state, set by the pump each tick (see uiScreenActive):
	// true while a dialog/menu screen is up instead of the adventure view.
	// Gates the compass restamp AND switches text-box erases to flat-fill
	// (the HUD backdrop must not leak into dialog panels).
	bool mUiScreen = false;
	// Save-under buffer for the present-time overlay (see update()): the
	// overlay paints into mScreen for one present, then this restores the
	// SOP-pure pixels so the canvas and SOP state never drift apart.
	SDL_Surface *mOverlaySave = nullptr;
	std::function<void()> mPresentOverlay;
	std::function<bool()> mOverlayActive; // gates the save-under + paint
	// Offscreen AESOP pages -- Dungeon Hack's assign_window + copy_window model.
	// DH draws each HUD panel into its own page at page-local (0,0) and then
	// copy_window()s the page onto its screen rect; without real page buffers
	// every panel lands at screen (0,0) and stomps the previous one. Keyed by
	// window handle. EOB3 never creates these (it has no copy_window), so its
	// flattened single-surface path is untouched.
	std::map<int32_t, SDL_Surface *> mPages;
	SDL_Surface *mScreenSaved = nullptr; // real screen while a page is target
	// The compass widget lives on its own AESOP page (104) that the original
	// re-composites every frame, so it survives HUD redraws. We flatten pages onto
	// one surface, so we instead snapshot the compass region when it refreshes and
	// re-apply it each present -- otherwise the cardinal/facing indicator (only
	// redrawn by the bytecode on a turn) gets erased by an inventory-close redraw.
	SDL_Surface *mCompassSnap = nullptr;
	// True while the SOP has deliberately painted over the compass area (an
	// outtake/cutscene box fill covering the whole compass rect): suspend the
	// per-present re-stamp until the compass is redrawn (snapshotCompass).
	bool mCompassCovered = false;
	// Pristine HUD-backdrop pixels under the compass rect, captured right after
	// the Backdrop (190) draw and BEFORE any compass lands there. In the
	// original page model, panels drawn over the compass area (the spell-book
	// "Auxiliary display") sit on page 1, whose buffer holds the clean Backdrop
	// -- their transparent (index 0) pixels reveal dark leather. Our flattened
	// screen would reveal stale compass pixels instead (gold shrapnel in the
	// scroll-arrow glyph holes), so bitmap draws covering the compass rect
	// first restore this underlay. See drawImage.
	SDL_Surface *mCompassUnderlay = nullptr;
	// Copy of the last PRESENTED frame (refreshed at the end of update()).
	// pixelFade() dissolves from this (what the player currently sees) to the
	// current mScreen content -- the flattened-page stand-in for VFX_pixel_fade.
	SDL_Surface *mLastShown = nullptr;
	// Persistent streaming GPU texture for present (see update()): created
	// once, refreshed each frame via SDL_UpdateTexture. Avoids the
	// CreateTextureFromSurface + DestroyTexture per call that the original
	// loop did on every event pump (~30 Hz), the dominant per-present cost
	// at 320x200.
	SDL_Texture *mPresentTex = nullptr;
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

	// Decoded-shape cache (drawImage). Keyed by (cacheId, index) where cacheId
	// is the AESOP resource number of the bitmap table. The caller (engine.cpp
	// runtime hook) supplies it; cinematic playback (graphics.cpp playVideo)
	// works on transient by-value vectors whose addresses get recycled, so it
	// passes 0 to bypass the cache. Avoids redoing the RLE VFX decode on every
	// draw: the in-game view re-runs the draw bytecode every frame (~50 draws),
	// and many draws hit the same wall/floor/stair shape -- the per-frame cost
	// was dominated by re-decoding the same shapes (keypress->render latency).
	struct DecodedShape {
		int w, h;
		std::vector<uint8_t> pixels; // indexed (w*h bytes)
		// Per-pixel coverage: 1 = painted (opaque even when the value is
		// 0 = black), 0 = transparent. Both AESOP bitmap formats encode
		// transparency structurally -- the VFX RLE has an explicit skip
		// token, the older scanline format simply omits pixels no run
		// covers -- so neither colour-keys. Painted black is real black
		// (the spell-book arrows, and DH's UI panels).
		std::vector<uint8_t> mask;
	};
	struct ShapeKey {
		uint32_t cacheId;
		uint16_t index;
		bool operator==(const ShapeKey &o) const { return cacheId == o.cacheId && index == o.index; }
	};
	struct ShapeKeyHash {
		size_t operator()(const ShapeKey &k) const noexcept {
			return std::hash<uint32_t>{}(k.cacheId) ^ (static_cast<size_t>(k.index) << 1);
		}
	};
	std::unordered_map<ShapeKey, DecodedShape, ShapeKeyHash> mShapeCache;

	// Per-text-window state (font/cursor/colour), keyed by AESOP window number.
	struct TextWin {
		std::shared_ptr<Font> font;
		int htab = 0, vtab = 0;
		uint8_t fg = 15;
		int winX0 = 0, winX1 = WIDTH - 1; // bound window's horizontal extent
		int winY0 = 0, winY1 = HEIGHT - 1; // bound window's vertical extent
		int justify = 0;                  // 0=left, 1=right, 2=center (GIL2VFX)
		// Handle of the subwindow this text window is bound to (text_window(N,
		// M) -> boundHandle=M). When set_x1/x2/y1/y2 mutates the subwindow's
		// edges, the runtime calls Graphics::updateTextWindowsFor() to refresh
		// any bound text window's winX0/winX1/winY0/winY1.
		int boundHandle = -1;
	};
	std::map<int, TextWin> mTextWin;
	// Built glyph sets, cached by font resource id (text_style is called every
	// menu redraw -- we must not rebuild the font each frame).
	std::map<int, std::shared_ptr<Font>> mFontCache;

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
	// cacheId: a stable id for the bitmap source (use the AESOP resource number
	// for asset-backed vectors). 0 disables caching for transient buffers whose
	// storage address may be recycled (e.g. cinematic frames built by value).
	// scale is the AESOP draw_bitmap `scale` arg: 0 = native size, else the shape
	// is drawn at scale/256 (so 256 = 100%, 128 = 50%, 64 = 25%). Depth tiers
	// in the dungeon view rely on this to shrink monster sprites as they recede.
	// One live palette entry, for diagnostics (e.g. "does this art's index
	// range resolve to anything but black in the current palette?").
	SDL_Color paletteColor(uint8_t index) const {
		return mPalette != nullptr ? mPalette->colors[index] : SDL_Color{};
	}

	// --- Offscreen pages (Dungeon Hack's copy_window compositing) ---
	// beginPage redirects ALL subsequent drawing (drawImage, fills, text, …)
	// into page `handle`'s own w*h surface, creating it on first use; the page
	// is addressed in page-local coords, so the SOP's (0,0) means the page's
	// top-left rather than the screen's. endPage restores the visible screen as
	// the target. Implemented by swapping the mScreen pointer, so every existing
	// draw routine redirects without changes. Nesting is not supported (DH never
	// nests): a second beginPage before endPage is ignored and returns false.
	bool beginPage(int32_t handle, int w, int h);
	void endPage();
	// copy_window(src, dst): blit page `src`'s surface onto `dst` at (dstX,dstY).
	// dst is the visible screen when `dstPage` has no offscreen surface.
	// Returns false if `src` has no surface yet (nothing was ever drawn to it).
	bool blitPage(int32_t src, int32_t dstPage, int dstX, int dstY);

	void drawImage(std::vector<uint8_t> &bmp, uint16_t index, int posX,
			int posY, bool transparency = false, int mirror = 0,
			uint32_t cacheId = 0, int scale = 0);

	// Blit a raw indexed (8 bpp) pixel buffer onto the screen at (posX, posY),
	// using the live palette. Used for full-screen CPS backdrops and any other
	// art that's already a flat width*height byte stream (no RLE container).
	// transparent=true treats palette index 0 as see-through (skipped during
	// the blit), so sprite art over a backdrop reads cleanly.
	void drawIndexed(const std::vector<uint8_t> &pixels, int width, int height,
			int posX, int posY, bool transparent = false);
	// Bounding box of the non-transparent pixels of shape `index` drawn at
	// (posX, posY) with `mirror`, clamped to the screen; writes {x0,y0,x1,y1}
	// into out. Returns false if nothing would be visible
	// (GIL2VFX_visible_bitmap_rect -- the SOP hit-tests sprite clicks with it).
	bool visibleBitmapRect(std::vector<uint8_t> &bmp, uint16_t index, int posX,
			int posY, int mirror, int out[4]);

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
	// Capture the pristine backdrop under the compass rect (call right after
	// the HUD Backdrop bitmap draw, before the compass is stamped there).
	void snapshotCompassUnderlay();
	// Screen-space rect covered by the compass underlay (the spell-book menu
	// window footprint). Shared source of truth for callers deciding whether
	// a draw repaints the region (runtime's 190-draw capture gate).
	static const SDL_Rect &menuUnderlayRect();
	// fill_rectangle(x0,y0,x1,y1,color): flood an inclusive screen rectangle with a
	// palette colour. The SOP screens use it to clear a panel before redrawing (e.g.
	// the character-stats screen clears the equipment area first); also refreshes the
	// text-free backdrop snapshot so later text overlays the cleared box, not stale art.
	void fillRect(int x0, int y0, int x1, int y1, uint8_t color);
	// RGB fill (bypasses the palette). Used by overlays (automap) that need
	// stable UI colours across dungeon-specific palette swaps. Writes mScreen
	// only, so overlay footprints don't leak into mBackdrop.
	void fillRectRGB(int x0, int y0, int x1, int y1,
	                 uint8_t r, uint8_t g, uint8_t b);
	// Palette-indexed line (GIL2VFX_draw_line). Axis-aligned lines are one
	// fillRect; diagonals walk Bresenham. Writes the backdrop too (a drawn
	// line is panel state, same as fillRect).
	void drawLine(int x0, int y0, int x1, int y1, uint8_t color);
	// Every-other-pixel checkerboard fill (VFX_rectangle_hash) -- the SOP's
	// "grayed out" UI treatment.
	void hashRect(int x0, int y0, int x1, int y1, uint8_t color);
	// While true, fillRect/drawLine/hashRect write mScreen only, not mBackdrop.
	// ponytail: keeps overlays (automap) from corrupting the SOP's text-restore
	// snapshot; the overlay is redrawn each frame so mScreen churn is harmless.
	void suspendBackdrop(bool s) { mSuspendBackdrop = s; }
	// While true, snapshotCompass() is a no-op. Prevents the SOP's mid-frame
	// compass snapshot from capturing overlay pixels drawn on top of the
	// compass rect (would then get re-stamped as "compass" on future frames).
	void suppressCompassSnapshot(bool s) { mSuppressCompassSnap = s; }
	void setUiScreenActive(bool u) { mUiScreen = u; }
	// Raw single-pixel access for the particle effects (do_dots/do_ice port):
	// peek returns the mapped ARGB value (0 on OOB) so a particle can save and
	// restore the exact background it covers; poke writes one back. mapColor
	// maps a palette index to the same raw value. None of these touch the
	// backdrop -- particles are transient overlays erased by their own loop.
	uint32_t peekPixel(int x, int y) const;
	void pokePixel(int x, int y, uint32_t raw);
	uint32_t mapColor(uint8_t color) const;
	// VFX_pixel_fade for the flattened single-surface model: dissolve the
	// inclusive rect from the frame the player currently SEES (the copy kept
	// at each present) to the CURRENT mScreen content, revealing random
	// pixels over `intervals` presented frames. Covers both directions --
	// fade-to-black (SOP wipes then fades) and fade-in (SOP draws the new
	// scene then fades) -- without needing per-page surfaces.
	void pixelFade(int x0, int y0, int x1, int y1, int intervals);
	void drawText(std::vector<uint8_t> &fnt, std::string text, uint16_t posX,
			uint16_t posY);

	// drawText variant that tints the glyphs to a chosen palette index colour
	// (the font glyphs themselves are masks; SDL_SetSurfaceColorMod modulates
	// the white pixels to (r,g,b) drawn from the live palette). Used by
	// pickers that need a red "selected" entry over white siblings.
	// `pitch` (default 0 = use the glyph's full width) advances the cursor
	// by exactly that many pixels per character -- handy for FONT6 whose
	// glyphs are 8-wide but only ~5 cols of visible content, so a pitch of
	// 6 gives a tighter look for button labels.
	void drawTextColored(std::vector<uint8_t> &fnt, std::string text,
			uint16_t posX, uint16_t posY, uint8_t paletteIndex,
			int pitch = 0);

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
	// ponytail: hook fired at the end of update() -- after all state-machine
	// draws and the compass restamp, immediately before SDL_UpdateTexture and
	// present. The automap uses this so its overlay lands ON TOP of anything
	// the SOP drew during a recursive dispatch inside our pump.
	// `active` gates painting per present (e.g. automap::isOpen) so the
	// save-under copy is only taken while the overlay is actually up.
	void setPresentOverlay(std::function<void()> cb, std::function<bool()> active) {
		mPresentOverlay = std::move(cb);
		mOverlayActive = std::move(active);
	}
	// Blit the text-restore snapshot back onto mScreen. The automap calls this
	// on close so residual overlay pixels (legend column, title) that landed in
	// gaps the SOP doesn't redraw get erased in one shot.
	void restoreBackdrop();
	// Save the current screen surface to a BMP (debug / headless verification).
	void saveScreenshot(const std::string &path);

	// Map a window-pixel coordinate (from an SDL mouse event) to the 320x200
	// logical space the game's windows use. Accounts for the integer scale.
	void mouseToLogical(int wx, int wy, int &lx, int &ly) const;

	// Expose the SDL window. Needed by SDL3's SDL_StartTextInput / StopTextInput
	// (which now take the target window, no longer global).
	SDL_Window *getWindow() const { return mWindow; }
	// Expose the renderer so callers can call SDL_ConvertEventToRenderCoordinates
	// (SDL3 no longer auto-rescales event coords for logical presentation).
	SDL_Renderer *getRenderer() const { return mRenderer; }

	// --- AESOP text output (GRAPHICS.C text_window/style/color/xy + print) ---
	// AESOP addresses text into numbered "text windows", each with a font,
	// colour and cursor. The runtime feeds the font/string bytes (they're
	// resources); we hold the per-window state and render.
	void setTextFont(int wndnum, int fontId, std::vector<uint8_t> &fontRes);
	void setTextColor(int wndnum, uint8_t color);   // text_color's remap target
	void setTextXY(int wndnum, int x, int y);
	// Text-cursor + font-metric readbacks (GRAPHICS.C get_text_x/get_text_y,
	// char_width, font_height). Metric getters return 0 for an unknown
	// window/font; textCursor returns false and leaves x/y untouched.
	bool textCursor(int wndnum, int &x, int &y) const;
	int textCharWidth(int wndnum, uint8_t ch);
	int textFontHeight(int wndnum);
	// Bind a text window to a graphics-window rectangle: records the extent (for
	// centering) and clears the interior to its background, so previously-drawn
	// (or bitmap-baked) text there doesn't ghost under the fresh text.
	void setTextWindow(int wndnum, int x0, int y0, int x1, int y1);
	// Repaint a screen-coord rectangle from either the panel-art backdrop
	// (in-game) or sampled flat colour (menus). Callable directly from the
	// SOP's wipe_window runtime function -- matches GIL2VFX_wipe_window /
	// GIL2VFX_home in the original. Used to be a side effect of
	// setTextWindow; pulled out so the save-picker's many rebinds don't
	// wipe between draws.
	void wipeTextBox(int x0, int y0, int x1, int y1);
	// Variant that also records the source subwindow handle for live re-query
	// when set_x1/x2/y1/y2 mutates it. The runtime calls this from
	// text_window(N, M); the existing 4-coord overload is kept for callers
	// that bind to an ad-hoc rectangle (not a subwindow handle).
	void setTextWindow(int wndnum, int x0, int y0, int x1, int y1, int handle);
	// Update every text window currently bound to subwindow `handle` so their
	// cached edges match the new (x0, y0)-(x1, y1). Called by the runtime
	// after set_x1/x2/y1/y2 mutates the subwindow's bounds.
	void updateTextWindowsFor(int handle, int x0, int y0, int x1, int y1);
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
