// Regression tests for the runtime clip / windowing layer.
//
// These tests do NOT need EYE.RES (or any other game data). They construct an
// EventSystem in-memory and drive its pane-edge API directly -- that's the
// surface the SOP's set_x1/set_x2/set_y1/set_y2 runtime calls land on, and
// that draw_bitmap reads back when clipping every blit (mirroring the
// original GIL2VFX_draw_bitmap's use of panes[wnd].x0..y1).
//
// The bug these lock in: a single global gViewClipX1/X2 was tracking the
// "view's per-cell clip" across ALL pages, so set_x2(view_page, 175) leaked
// into subsequent draw_bitmap(hud_page, ...) calls, narrowing the HUD draw
// to x<=175 and erasing the right-hand portrait column.

#include "gtest/gtest.h"

#include "../thirdeye/vm/events.hpp"
#include "../thirdeye/vm/objects.hpp"

#include "SDL.h"

namespace {

using VM::EventSystem;
using VM::ObjectSystem;

// Initial state per EventSystem ctor: handles 0 and 1 are PAGE1/PAGE2,
// pre-seeded as the full 320x200 screen so HUD draws to them are unclipped.
TEST(EventClip, Pages0And1ArePreSeededFullScreen) {
	ObjectSystem objects;
	EventSystem events(objects);

	int32_t x0, y0, x1, y1;
	ASSERT_TRUE(events.windowRect(0, x0, y0, x1, y1));
	EXPECT_EQ(x0, 0);
	EXPECT_EQ(y0, 0);
	EXPECT_EQ(x1, 319);
	EXPECT_EQ(y1, 199);

	ASSERT_TRUE(events.windowRect(1, x0, y0, x1, y1));
	EXPECT_EQ(x0, 0);
	EXPECT_EQ(y0, 0);
	EXPECT_EQ(x1, 319);
	EXPECT_EQ(y1, 199);
}

// Setting an edge on one pane MUST NOT leak into another. This is the
// regression that broke the HUD: set_x2(view_page, 175) was treated as a
// global "next draw clips at x=175", so the next HUD draw to page 1 got
// narrowed to x<=175 and the right-hand portraits vanished.
TEST(EventClip, SetEdgePerPaneDoesNotLeakAcross) {
	ObjectSystem objects;
	EventSystem events(objects);

	// Allocate a subwindow at view-like coords (dungeon's 0..175 x 0..119).
	int32_t viewH = events.assignWindow(/*owner*/ 0, 0, 0, 175, 119);
	ASSERT_GE(viewH, 2); // pre-seeded handles 0/1 are reserved

	// Per-cell narrowing: set the view's right edge to 127 (one cell).
	events.setWindowEdge(viewH, 'r', 127);

	int32_t x0, y0, x1, y1;
	ASSERT_TRUE(events.windowRect(viewH, x0, y0, x1, y1));
	EXPECT_EQ(x1, 127) << "set_x2 on view should narrow ONLY the view's right edge";

	// Page 1 (HUD) must still be full screen -- this is the assert that
	// would have caught the portrait-erasure regression.
	ASSERT_TRUE(events.windowRect(1, x0, y0, x1, y1));
	EXPECT_EQ(x1, 319) << "set_x2 on view must not narrow PAGE1's right edge";
	EXPECT_EQ(x0, 0);
	EXPECT_EQ(y0, 0);
	EXPECT_EQ(y1, 199);
}

// All four edges round-trip, and 'l'/'t'/'r'/'b' map to x0/y0/x1/y1 in that
// order. (set_x1/set_y1/set_x2/set_y2 in the SOP-level API.)
TEST(EventClip, AllFourEdgesRoundTrip) {
	ObjectSystem objects;
	EventSystem events(objects);

	int32_t h = events.assignWindow(0, 10, 20, 200, 100);
	ASSERT_GE(h, 2);

	events.setWindowEdge(h, 'l', 11);
	events.setWindowEdge(h, 't', 22);
	events.setWindowEdge(h, 'r', 222);
	events.setWindowEdge(h, 'b', 122);

	int32_t x0, y0, x1, y1;
	ASSERT_TRUE(events.windowRect(h, x0, y0, x1, y1));
	EXPECT_EQ(x0, 11);
	EXPECT_EQ(y0, 22);
	EXPECT_EQ(x1, 222);
	EXPECT_EQ(y1, 122);
}

// Negative edge values are stored VERBATIM -- matching the original
// GIL2VFX_set_x1: `panes[wnd].x0 = val` (no sentinel semantics, no
// "restore natural edge" logic). The SOP's convention: pages 74..85
// (the per-cell aux windows reset on every party step) are flagged
// inactive by writing -1 to all four edges, producing a degenerate
// rect that any subsequent draw_bitmap clips away to nothing. Aux
// windows are never draw targets in EOB3's SOP (empirically: the
// intersection of "pages with set_x*(P, -1)" and "pages with
// draw_bitmap(P, ...)" is empty in the menu-load path), so the
// degenerate-clip suppression never fires in real gameplay. This
// test pins the storage contract; do NOT add "interpret -1 as
// 'use the pane's natural edge'" -- that would diverge from
// GIL2VFX and break the aux-window-disable convention.
TEST(EventClip, NegativeOneIsStoredVerbatim) {
	ObjectSystem objects;
	EventSystem events(objects);

	int32_t h = events.assignWindow(0, 0, 0, 175, 119);
	events.setWindowEdge(h, 'r', -1);

	int32_t x0, y0, x1, y1;
	ASSERT_TRUE(events.windowRect(h, x0, y0, x1, y1));
	EXPECT_EQ(x1, -1);
}

// Setting an edge on an unused handle is a no-op (matches GIL2VFX: setting
// panes[wnd] with no live pane writes garbage, but our impl is defensive --
// we silently ignore out-of-range / released handles).
TEST(EventClip, SetEdgeOnUnusedHandleIsNoOp) {
	ObjectSystem objects;
	EventSystem events(objects);

	// Handle 99 is unused (we haven't allocated anything yet).
	events.setWindowEdge(99, 'r', 175);
	int32_t x0, y0, x1, y1;
	EXPECT_FALSE(events.windowRect(99, x0, y0, x1, y1));

	// And it must not have accidentally promoted handle 99 into "used".
	int32_t fresh = events.assignWindow(0, 0, 0, 10, 10);
	EXPECT_EQ(fresh, 2) << "no-op setWindowEdge should leave the free list intact";
}

// --- SDL clip-blit primitive (the second half of the regression) ---------
//
// runtime/graphics.cpp's draw_bitmap derives a clip rect from
// events.windowRect(page) and calls SDL_SetClipRect on the screen surface,
// then SDL_BlitSurface to it. These tests pin the SDL behavior we depend on:
// pixels OUTSIDE the clip rect must not change during a blit, even when the
// source surface is full screen. If SDL changes that semantic or we
// accidentally apply the wrong rect, the portrait panel goes black again.
//
// SDL_CreateRGBSurface does NOT require SDL_Init(VIDEO) -- it's a pure
// memory allocation. So this test runs on a CI box with no display.

constexpr int kSurfW = 320;
constexpr int kSurfH = 200;

// Solid 32-bit ARGB sentinel; channels chosen so a per-channel diff is
// unambiguous when reading raw pixels (avoids 0x00000000 / 0xFFFFFFFF, which
// can be confused with uninitialised memory or a transparent fill).
constexpr uint32_t kColorBackground = 0xFF112233; // ARGB sentinel A
constexpr uint32_t kColorForeground = 0xFF445566; // ARGB sentinel B

// Allocate a 320x200 32-bit ARGB surface and fill every pixel with `c`.
SDL_Surface *makeFilled(uint32_t c) {
	SDL_Surface *s = SDL_CreateRGBSurface(0, kSurfW, kSurfH, 32,
	                                      0x00FF0000, 0x0000FF00, 0x000000FF,
	                                      0xFF000000);
	if (!s) return nullptr;
	SDL_FillRect(s, nullptr, c);
	return s;
}

// Read the ARGB pixel at (x, y). Caller must guarantee the surface is 32bpp.
uint32_t pixelAt(SDL_Surface *s, int x, int y) {
	auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(s->pixels) +
	                                         y * s->pitch);
	return row[x];
}

// "Regression A" (the menu-load HUD path): a draw_bitmap to a page whose
// pane rect is the full 320x200 must hit every pixel -- nothing leaks back
// from a prior view-page narrowing. The earlier bug intersected a global
// `gViewClipX2 = 175` with PAGE1's natural 0..319 rect and clipped HUD draws
// to x<=175, blanking the right-hand portrait column.
TEST(ClipPrimitive, FullScreenBlitHitsEntireSurface) {
	SDL_Surface *dest = makeFilled(kColorBackground);
	ASSERT_NE(dest, nullptr);
	SDL_Surface *src = makeFilled(kColorForeground);
	ASSERT_NE(src, nullptr);

	// Pane rect = full screen (the HUD's PAGE1 case).
	SDL_Rect clip{0, 0, kSurfW, kSurfH};
	SDL_SetClipRect(dest, &clip);
	ASSERT_EQ(SDL_BlitSurface(src, nullptr, dest, nullptr), 0);
	SDL_SetClipRect(dest, nullptr);

	// Every pixel must now be the foreground sentinel -- the right-hand
	// portrait column included.
	for (int y : {0, 11, 99, 119, 150, 199}) {
		for (int x : {0, 100, 175, 176, 185, 257, 319}) {
			EXPECT_EQ(pixelAt(dest, x, y), kColorForeground)
				<< "pixel (" << x << "," << y << ") was not painted";
		}
	}

	SDL_FreeSurface(src);
	SDL_FreeSurface(dest);
}

// "Regression B" (the view-page wall-leak path): a draw_bitmap to the
// dungeon-view page (natural rect 0..175 x 0..119) must NOT touch pixels at
// x>=176 -- those are the portrait panel. The earlier bug had no clip at
// all when the view page number wasn't the hardcoded 92, so wide wall
// shapes painted brown across THELMA and LOUISE.
TEST(ClipPrimitive, ViewRectBlitLeavesHudPixelsAlone) {
	SDL_Surface *dest = makeFilled(kColorBackground);
	ASSERT_NE(dest, nullptr);
	SDL_Surface *src = makeFilled(kColorForeground);
	ASSERT_NE(src, nullptr);

	// Pane rect = dungeon view (0,0)-(175,119), supplied here by hand the
	// way windowRect() would return it after assign_subwindow + set_x*.
	SDL_Rect clip{0, 0, 176, 120};
	SDL_SetClipRect(dest, &clip);
	ASSERT_EQ(SDL_BlitSurface(src, nullptr, dest, nullptr), 0);
	SDL_SetClipRect(dest, nullptr);

	// Inside the view: painted.
	for (int y : {0, 11, 60, 119}) {
		for (int x : {0, 50, 100, 175}) {
			EXPECT_EQ(pixelAt(dest, x, y), kColorForeground)
				<< "view pixel (" << x << "," << y << ") was NOT painted";
		}
	}
	// Outside the view (the HUD region): untouched -- still background.
	for (int y : {0, 11, 60, 119, 199}) {
		for (int x : {176, 200, 257, 319}) {
			EXPECT_EQ(pixelAt(dest, x, y), kColorBackground)
				<< "HUD pixel (" << x << "," << y << ") was clobbered";
		}
	}
	// And rows below y=119 are entirely untouched.
	for (int y : {120, 150, 199}) {
		for (int x : {0, 100, 175, 200, 319}) {
			EXPECT_EQ(pixelAt(dest, x, y), kColorBackground);
		}
	}

	SDL_FreeSurface(src);
	SDL_FreeSurface(dest);
}

// "Per-cell narrowing" works the same way: set_x1/set_x2 mid-frame narrow
// the view's pane to a single column for one wall shape, then restore it
// to 0..175. This test pins that the clip rect we'd derive from a narrowed
// windowRect (0,0)-(127,119) cleanly contains the blit.
TEST(ClipPrimitive, NarrowedClipConfinesBlit) {
	SDL_Surface *dest = makeFilled(kColorBackground);
	SDL_Surface *src = makeFilled(kColorForeground);
	ASSERT_NE(dest, nullptr);
	ASSERT_NE(src, nullptr);

	// Narrow to columns 0..127 only.
	SDL_Rect clip{0, 0, 128, 120};
	SDL_SetClipRect(dest, &clip);
	ASSERT_EQ(SDL_BlitSurface(src, nullptr, dest, nullptr), 0);
	SDL_SetClipRect(dest, nullptr);

	EXPECT_EQ(pixelAt(dest, 0, 0), kColorForeground);
	EXPECT_EQ(pixelAt(dest, 127, 119), kColorForeground);
	EXPECT_EQ(pixelAt(dest, 128, 0), kColorBackground);
	EXPECT_EQ(pixelAt(dest, 200, 50), kColorBackground);
	EXPECT_EQ(pixelAt(dest, 0, 120), kColorBackground);

	SDL_FreeSurface(src);
	SDL_FreeSurface(dest);
}

// Aux-window disable contract (mirrors original GIL2VFX): if a window's
// edges are set to -1 (which the SOP does on pages 74..85 after every
// party step), and *somehow* a draw_bitmap to that page slips through,
// the resulting clip rect from `px1 - px0 + 1` is degenerate and the
// blit is suppressed. This locks in the intent so a "helpful" patch
// that special-cases negative edges (e.g. "treat -1 as 'use the
// surface's natural edge'") will fail this test -- such a patch would
// turn the SOP's disable convention into an accidental full-screen
// draw and reproduce the wall-leak we just fixed.
TEST(ClipPrimitive, NegativeEdgesProduceDegenerateClipAndSuppressBlit) {
	SDL_Surface *dest = makeFilled(kColorBackground);
	SDL_Surface *src = makeFilled(kColorForeground);
	ASSERT_NE(dest, nullptr);
	ASSERT_NE(src, nullptr);

	// Simulate windowRect returning (-1, -1, -1, -1) for an aux page the
	// SOP has disabled. Apply the exact arithmetic draw_bitmap uses.
	int32_t px0 = -1, py0 = -1, px1 = -1, py1 = -1;
	SDL_Rect clip{px0, py0, px1 - px0 + 1, py1 - py0 + 1};
	ASSERT_EQ(clip.w, 1);
	ASSERT_EQ(clip.h, 1);
	SDL_SetClipRect(dest, &clip);
	ASSERT_EQ(SDL_BlitSurface(src, nullptr, dest, nullptr), 0);
	SDL_SetClipRect(dest, nullptr);

	// SDL intersects the clip rect with the surface's bounds, so an
	// at-most-1-pixel clip starting at (-1,-1) lands entirely off-surface;
	// nothing changes. (Even if it did land 1 pixel inside, the user-
	// visible effect is "draw was effectively suppressed", which is what
	// the SOP convention asks for.)
	int dirty = 0;
	for (int y = 0; y < kSurfH; ++y)
		for (int x = 0; x < kSurfW; ++x)
			if (pixelAt(dest, x, y) != kColorBackground) ++dirty;
	EXPECT_LE(dirty, 1)
		<< "Negative-edge pane must suppress (or near-suppress) the blit. "
		   "If you're tempted to special-case -1 in draw_bitmap, READ THIS: "
		   "the original GIL2VFX_set_x1 stores -1 verbatim; the SOP uses "
		   "this to mark aux pages disabled. Re-interpreting -1 as 'use "
		   "natural edge' breaks that convention.";

	SDL_FreeSurface(src);
	SDL_FreeSurface(dest);
}

// Bridge test: the rect runtime/graphics.cpp computes from windowRect MUST
// match what SDL_SetClipRect actually clips to. This wires the two halves
// together -- if either side drifts, we catch it without needing the game.
TEST(ClipPrimitive, WindowRectFeedsSdlClipRectCorrectly) {
	ObjectSystem objects;
	EventSystem events(objects);
	int32_t viewH = events.assignWindow(0, 0, 0, 175, 119);
	ASSERT_GE(viewH, 2);

	// Mid-frame narrowing for one cell.
	events.setWindowEdge(viewH, 'r', 127);

	int32_t px0, py0, px1, py1;
	ASSERT_TRUE(events.windowRect(viewH, px0, py0, px1, py1));

	// The exact arithmetic from runtime/graphics.cpp's draw_bitmap clip block:
	SDL_Rect clip{px0, py0, px1 - px0 + 1, py1 - py0 + 1};
	EXPECT_EQ(clip.x, 0);
	EXPECT_EQ(clip.y, 0);
	EXPECT_EQ(clip.w, 128);
	EXPECT_EQ(clip.h, 120);

	// And the rect must produce the same exclusive HUD region as
	// ClipPrimitive.NarrowedClipConfinesBlit above -- i.e. pixels x>=128
	// must NOT change under this clip.
	SDL_Surface *dest = makeFilled(kColorBackground);
	SDL_Surface *src = makeFilled(kColorForeground);
	ASSERT_NE(dest, nullptr);
	ASSERT_NE(src, nullptr);
	SDL_SetClipRect(dest, &clip);
	ASSERT_EQ(SDL_BlitSurface(src, nullptr, dest, nullptr), 0);
	SDL_SetClipRect(dest, nullptr);
	EXPECT_EQ(pixelAt(dest, 127, 0), kColorForeground);
	EXPECT_EQ(pixelAt(dest, 128, 0), kColorBackground);
	SDL_FreeSurface(src);
	SDL_FreeSurface(dest);
}

} // namespace
