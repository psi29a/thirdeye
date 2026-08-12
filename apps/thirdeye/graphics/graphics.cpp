#include "graphics.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <stdexcept>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>

// SDL3 makes 8 bpp indexed surfaces palette-backed via SDL_CreateSurfacePalette;
// helper centralises the setup so every "indexed source surface" path looks
// identical to the SDL2 flow we deleted.
static SDL_Surface *makeIndexedSurfaceFrom(void *pixels, int w, int h, int pitch,
                                           SDL_Palette *palette) {
	SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_INDEX8, pixels,
	                                       pitch);
	if (s != nullptr)
		SDL_SetSurfacePalette(s, palette);
	return s;
}

GRAPHICS::Graphics::Graphics(uint16_t scale, bool /*renderer*/) {
	std::cout << "Initializing SDL... ";
	mScale = scale;
	Uint32 flags = SDL_INIT_VIDEO;
	if (!SDL_WasInit(flags)) {
		if (!SDL_Init(flags)) {
			throw std::runtime_error(
					"Could not initialize SDL! " + std::string(SDL_GetError()));
		}
	}

	// SDL3 dropped SDL_SysWMinfo entirely; the version + driver chatter we
	// used to print here was diagnostic only. Drop it -- the SDL3 video
	// driver name is still queryable with SDL_GetCurrentVideoDriver() if
	// anyone misses it. ponytail: removed for simplicity, add back via
	// SDL_GetCurrentVideoDriver() if a user reports needing that info.
	int v = SDL_GetVersion();
	std::cout << "done! SDL " << SDL_VERSIONNUM_MAJOR(v) << "."
	          << SDL_VERSIONNUM_MINOR(v) << "." << SDL_VERSIONNUM_MICRO(v)
	          << " (" << (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?")
	          << ")" << std::endl;

	mWindow = SDL_CreateWindow("Thirdeye", WIDTH * mScale, HEIGHT * mScale, 0);
	if (mWindow == nullptr)
		throw std::runtime_error(
				"Could not create window: " + std::string(SDL_GetError()));

	// SDL3 picks the best renderer when name=nullptr; the SDL2 SOFTWARE
	// branch we used to honour was dead code (callers always took HARDWARE).
	mRenderer = SDL_CreateRenderer(mWindow, nullptr);
	if (mRenderer == nullptr)
		throw std::runtime_error(
				"Could not create renderer: " + std::string(SDL_GetError()));

	// Create our game screen that will be blitted to before rendering.
	mScreen = SDL_CreateSurface(WIDTH, HEIGHT, SDL_PIXELFORMAT_ARGB8888);
	SDL_SetSurfaceColorKey(mScreen, true, SDL_MapSurfaceRGB(mScreen, 0, 0, 0));

	// SDL3 replaces SDL_HINT_RENDER_SCALE_QUALITY with per-texture scale mode
	// (set on mPresentTex once it's created below in update()).
	SDL_SetRenderLogicalPresentation(mRenderer, WIDTH, HEIGHT,
	                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);

	mPalette = SDL_CreatePalette(256);
	// SDL3 seeds palette entries to WHITE; any bitmap using a palette index
	// the SOP hasn't loaded yet then renders as pure white. EOB3 hides this
	// by loading the full palette on boot, but DH's opening cinematic starts
	// with only a partial set_palette(0, 28) load -- silhouette pixels using
	// higher indices came out as white blobs on top of the throne-room art.
	for (int i = 0; i < 256; ++i)
		mPalette->colors[i] = SDL_Color{0, 0, 0, 255};
	mCursor = nullptr;
	mFrames = 0;
	mCounter = 0;
	mState = NOOP;
	mAlpha = 0;
	mClock = SDL_GetTicks();
	mVideoWait = 0;
	mRunningClock = 0;
	mSleep = 0;
}

GRAPHICS::Graphics::~Graphics() {
	// If a DH page was still the render target, put the real screen back first
	// so the mScreen free below doesn't hit a page surface (double free with
	// the mPages sweep).
	if (mScreenSaved != nullptr) {
		mScreen = mScreenSaved;
		mScreenSaved = nullptr;
	}
	SDL_DestroyCursor(mCursor);
	SDL_DestroySurface(mScreen);
	SDL_DestroySurface(mBackdrop);     // lazily created in drawImage (nullptr-safe)
	SDL_DestroySurface(mCompassSnap);  // lazily created in snapshotCompass
	SDL_DestroySurface(mCompassUnderlay);
	SDL_DestroySurface(mOverlaySave);  // lazily created in update (save-under)
	SDL_DestroySurface(mLastShown);    // lazily created in update
	// Offscreen DH pages. endPage() has already restored mScreen by now in any
	// sane shutdown; if a page were still the target, mScreen above would be a
	// page surface -- so restore first to avoid a double free of the same ptr.
	for (auto &kv : mPages)
		if (kv.second != nullptr && kv.second != mScreen)
			SDL_DestroySurface(kv.second);
	mPages.clear();
	if (mPresentTex != nullptr) SDL_DestroyTexture(mPresentTex);
	SDL_DestroyPalette(mPalette);
	SDL_DestroyRenderer(mRenderer);
	SDL_DestroyWindow(mWindow);
	SDL_Quit();
}

// The compass occupies the bottom-left of the HUD, left of the movement arrows
// (which start at x=117); the disc spans y~120-168. Capture/restore that rect.
static const SDL_Rect kCompassRect = { 0, 120, 116, 49 };
// The compass ART (gold ornaments) bleeds below the disc rect, down to ~y174,
// and the spell-book panel window spans (0,120)-(116,175). The pristine-
// backdrop underlay covers the full panel footprint so transparent panel
// pixels anywhere in it reveal clean frame art, not compass ornament.
static const SDL_Rect kMenuUnderlayRect = { 0, 120, 117, 56 };

const SDL_Rect &GRAPHICS::Graphics::menuUnderlayRect() {
	return kMenuUnderlayRect;
}

// --- Offscreen pages (Dungeon Hack copy_window compositing) ------------------
//
// The trick here is that we redirect by swapping the mScreen pointer rather
// than threading a render-target parameter through every draw routine: all the
// existing drawing code keeps writing to "mScreen" and lands in the page.

bool GRAPHICS::Graphics::beginPage(int32_t handle, int w, int h) {
	if (mScreenSaved != nullptr) return false;  // no nesting (DH never nests)
	if (w <= 0 || h <= 0) return false;
	// .find() rather than operator[]: the latter default-inserts a null entry
	// on every miss, so a failed SDL_CreateSurface would leave a null in the
	// table for a handle that has no page (see CLAUDE.md on operator[] as a
	// foot-gun for sparse-index containers).
	SDL_Surface *page = nullptr;
	auto it = mPages.find(handle);
	if (it != mPages.end()) page = it->second;
	if (page == nullptr) {
		page = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
		// A recycled address must not inherit the previous page's indices.
		if (page != nullptr) mIndexPlanes.erase(page);
		if (page == nullptr) return false;   // table untouched on failure
		// Start transparent-black so a page that only gets a partial draw
		// composites without a surprise opaque border.
		SDL_FillSurfaceRect(page, nullptr,
		                    SDL_MapSurfaceRGBA(page, 0, 0, 0, 0));
		mPages[handle] = page;
	}
	mScreenSaved = mScreen;
	mScreen = page;
	SDL_SetSurfaceClipRect(mScreen, nullptr);
	return true;
}

void GRAPHICS::Graphics::endPage() {
	if (mScreenSaved == nullptr) return;
	mScreen = mScreenSaved;
	mScreenSaved = nullptr;
}

bool GRAPHICS::Graphics::blitPage(int32_t src, int32_t dstPage, int dstX,
		int dstY) {
	auto it = mPages.find(src);
	if (it == mPages.end() || it->second == nullptr) return false;
	SDL_Surface *from = it->second;
	// Destination is another page when one exists, else the visible screen.
	SDL_Surface *to = mScreen;
	auto dit = mPages.find(dstPage);
	if (dit != mPages.end() && dit->second != nullptr) to = dit->second;
	SDL_Rect dst{ dstX, dstY, from->w, from->h };
	// Straight copy: the page already holds composited pixels, and DH relies on
	// the copy overwriting whatever the destination had.
	SDL_SetSurfaceBlendMode(from, SDL_BLENDMODE_NONE);
	SDL_Rect saved;
	bool hadClip = SDL_GetSurfaceClipRect(to, &saved);
	SDL_SetSurfaceClipRect(to, nullptr);
	bool ok = SDL_BlitSurface(from, nullptr, to, &dst);
	if (hadClip) SDL_SetSurfaceClipRect(to, &saved);
	// The page's pixels now own this rect. Carry the page's recorded indices
	// across so text composited from a page still recolours on a palette swap,
	// and so stale indices underneath cannot.
	if (std::vector<uint16_t> *dstPlane = indexPlaneFor(to)) {
		std::vector<uint16_t> *srcPlane = indexPlaneFor(from);
		for (int y = 0; y < from->h; ++y) {
			const int ty = dst.y + y;
			if (ty < 0 || ty >= to->h) continue;
			for (int x = 0; x < from->w; ++x) {
				const int tx = dst.x + x;
				if (tx < 0 || tx >= to->w) continue;
				(*dstPlane)[static_cast<size_t>(ty) * to->w + tx] =
				    srcPlane ? (*srcPlane)[static_cast<size_t>(y) * from->w + x]
				             : kNoIndex;
			}
		}
	}
	return ok;
}

void GRAPHICS::Graphics::drawImage(std::vector<uint8_t> &bmp, uint16_t index,
		int posX, int posY, bool transparency, int mirror, uint32_t cacheId,
		int scale) {

	// Cache the decoded shape (un-mirrored): the RLE VFX decode is the per-frame
	// hotspot for the in-game 3D view, which re-runs the draw bytecode every
	// frame and hits the same wall/floor/stair shapes dozens of times. The
	// caller's cacheId must be a stable identity for the source bytes (we use
	// the AESOP resource number); 0 means "uncacheable" -- transient buffers
	// (cinematic frames) whose allocator addresses can be recycled would alias
	// each other under any pointer-keyed scheme.
	DecodedShape transient;
	const DecodedShape *shape = nullptr;
	auto decodeInto = [&](DecodedShape &s) {
		Bitmap image(bmp);
		s.w = image.getWidth(index);
		s.h = image.getHeight(index);
		// Both AESOP bitmap formats encode transparency structurally, not by
		// colour: the VFX RLE has an explicit skip token, and the older
		// scanline format simply omits the pixels it does not paint. Either
		// way a run may legitimately paint palette index 0, and the original
		// draws that as solid black. So always decode with a coverage mask and
		// never colorkey -- keying index 0 punched a hole through every
		// painted-black pixel (DH's camp panel showed the dungeon through it;
		// the same class of bug the VFX path had before it was masked).
		s.pixels = image.decodeScanlineMasked(index, s.mask);
	};
	if (cacheId != 0) {
		ShapeKey key{cacheId, index};
		auto it = mShapeCache.find(key);
		if (it == mShapeCache.end()) {
			DecodedShape s;
			decodeInto(s);
			it = mShapeCache.emplace(key, std::move(s)).first;
		}
		shape = &it->second;
	} else {
		decodeInto(transient);
		shape = &transient;
	}
	int iw = shape->w, ih = shape->h;
	// Mirror operates on a per-draw copy (the cached buffer stays canonical).
	// Most draws have mirror=0 and skip the copy entirely.
	std::vector<uint8_t> imageData, maskData;
	const std::vector<uint8_t> *src = &shape->pixels;
	const std::vector<uint8_t> *msk = &shape->mask;
	if (mirror) {
		imageData = shape->pixels;
		maskData = shape->mask;
		src = &imageData;
		msk = &maskData;
	}

	// Apply the AESOP draw_bitmap mirror flag by flipping the indexed pixels in
	// place on the per-draw copy (the cached buffer stays canonical). 1=X flips
	// each row left-to-right (right-hand dungeon walls), 2=Y flips the row order.
	auto flipX = [iw, ih](std::vector<uint8_t> &v) {
		for (int row = 0; row < ih; ++row)
			std::reverse(v.begin() + static_cast<ptrdiff_t>(row) * iw,
			             v.begin() + static_cast<ptrdiff_t>(row + 1) * iw);
	};
	auto flipY = [iw, ih](std::vector<uint8_t> &v) {
		for (int row = 0; row < ih / 2; ++row)
			std::swap_ranges(v.begin() + static_cast<ptrdiff_t>(row) * iw,
			                 v.begin() + static_cast<ptrdiff_t>(row + 1) * iw,
			                 v.begin() + static_cast<ptrdiff_t>(ih - 1 - row) * iw);
	};
	if ((mirror & 1) && iw > 0) {
		flipX(imageData);
		if (!maskData.empty()) flipX(maskData);
	}
	if ((mirror & 2) && ih > 0) {
		flipY(imageData);
		if (!maskData.empty()) flipY(maskData);
	}

	// VFX shapes with a mask render through an ARGB surface with true
	// per-pixel alpha (transparency == the RLE's skip pixels); this keeps
	// painted-black pixels opaque, matching VFX_shape_draw. Non-VFX shapes
	// (empty mask) keep the legacy indexed + colorkey-0 path below.
	const bool masked = transparency && !msk->empty() &&
	                    msk->size() == static_cast<size_t>(iw) * ih;
	SDL_Surface *surface;
	if (masked) {
		surface = SDL_CreateSurface(iw, ih, SDL_PIXELFORMAT_ARGB8888);
		auto *out = static_cast<uint32_t *>(surface->pixels);
		int stride = surface->pitch / 4;
		for (int y = 0; y < ih; ++y)
			for (int x = 0; x < iw; ++x) {
				size_t at = static_cast<size_t>(y) * iw + x;
				if ((*msk)[at]) {
					SDL_Color c = mPalette->colors[(*src)[at]];
					out[y * stride + x] = 0xFF000000u | (c.r << 16) |
					                      (c.g << 8) | c.b;
				} else {
					out[y * stride + x] = 0;
				}
			}
	} else {
		surface = makeIndexedSurfaceFrom(
				const_cast<uint8_t*>(src->data()), iw, ih, iw, mPalette);
	}

	// Apply the AESOP depth-tier scale: shape shrinks to (scale/256) of native
	// size (0 = native). Anchor stays at the sprite's origin, so the offset
	// half-fills the width/height loss -- centred shrink, matching
	// GIL2VFX_draw_bitmap's `xp += bounds*(1-scale)/2` in the reference runtime.
	int dw = iw, dh = ih, ox = posX, oy = posY;
	if (scale > 0 && scale != 256) {
		dw = iw * scale / 256;
		dh = ih * scale / 256;
		ox += (iw - dw) / 2;
		oy += (ih - dh) / 2;
	}
	SDL_Rect dest = { ox, oy, dw, dh };

	// A bitmap draw covering the whole compass rect means the SOP is painting
	// a panel over the compass area (the spell-book "Auxiliary display", a
	// full HUD Backdrop). Two consequences, both mirroring what the original's
	// page compositor did for free:
	//  1. Restore the pristine backdrop under the rect FIRST -- the panel's
	//     transparent (index 0) pixels, e.g. the scroll-arrow glyph holes,
	//     must reveal clean HUD leather, not stale compass pixels ("garbage").
	//  2. Suspend the per-present compass re-stamp (same rule as fillRect) or
	//     the re-stamp paints the old compass back over the panel each frame.
	// Self-healing on close: the panel-close path redraws the compass (bitmap
	// 187) -> gCompassDirty -> snapshotCompass() re-arms the re-stamp.
	{
		SDL_Rect clip0, eff = dest;
		SDL_GetSurfaceClipRect(mScreen, &clip0);
		static const bool kDbg = std::getenv("THIRDEYE_COMPASSDBG") != nullptr;
		if (SDL_GetRectIntersection(&eff, &clip0, &eff) &&
		    eff.x <= kCompassRect.x && eff.y <= kCompassRect.y &&
		    eff.x + eff.w >= kCompassRect.x + kCompassRect.w &&
		    eff.y + eff.h >= kCompassRect.y + kCompassRect.h) {
			if (kDbg)
				fprintf(stderr, "[compass] cover eff=(%d,%d,%d,%d) underlay=%p\n",
				        eff.x, eff.y, eff.w, eff.h, (void*)mCompassUnderlay);
			// Full-screen frame art (HUD Backdrop 190, Mausoleum letterbox
			// 178) is drawn over an explicit BLACK underlay by the runtime --
			// restoring the compass underlay here would repaint HUD pixels
			// (possibly compass-poisoned on a --skip-menu boot, where 190
			// lands after the compass stamp) over that black, leaking the
			// compass arch into the transition dialog. Those resources paint
			// their own world; skip the underlay restore for them.
			bool fullScreenFrame = (cacheId == 190 || cacheId == 178);
			if (transparency && mCompassUnderlay != nullptr &&
			    !fullScreenFrame) {
				// Restore only what THIS draw is about to cover (its rect
				// clipped to the underlay's footprint) -- never repaint
				// beyond the incoming shape.
				SDL_Rect r;
				if (SDL_GetRectIntersection(&eff, &kMenuUnderlayRect, &r)) {
					SDL_Rect src = { r.x - kMenuUnderlayRect.x,
					                 r.y - kMenuUnderlayRect.y, r.w, r.h };
					SDL_SetSurfaceBlendMode(mCompassUnderlay,
					                        SDL_BLENDMODE_NONE);
					SDL_BlitSurface(mCompassUnderlay, &src, mScreen, &r);
				}
			}
			mCompassCovered = true;
		} else {
			// Loose match (see fillRect): the mausoleum-entry cutscene draws
			// its dialog panel as pieces that individually don't 100% cover
			// the compass but together clearly do. Any single bitmap that
			// takes >= half the compass area is treated as a cover, so the
			// compass restamp stops until the SOP explicitly redraws it.
			SDL_Rect ov;
			if (SDL_GetRectIntersection(&eff, &kCompassRect, &ov) &&
			    ov.w * ov.h * 2 >= kCompassRect.w * kCompassRect.h)
				mCompassCovered = true;
		}
	}

	if (masked) {
		SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
	} else if (transparency) {
		SDL_SetSurfaceColorKey(surface, true, 0);
	}
	if (dw == iw && dh == ih)
		SDL_BlitSurface(surface, NULL, mScreen, &dest);
	else
		SDL_BlitSurfaceScaled(surface, NULL, mScreen, &dest,
		                      SDL_SCALEMODE_NEAREST);
	SDL_DestroySurface(surface);
	// Art drawn over text invalidates whatever indices we recorded there, so a
	// later palette swap can't resurrect the old glyph colours. Conservative:
	// a transparent sprite clears more than it covers, which only ever means
	// we repaint less.
	clearIndexRect(mScreen, dest);

	// Keep the backdrop snapshot in sync with the bitmap art (used to restore a
	// text box's background so in-game text overlays the panel art). Text never
	// touches mBackdrop (it's drawn straight to mScreen by printText), so this
	// snapshot stays text-free regardless of draw order. Mirror only the *drawn
	// rect* (intersected with the active clip), not the whole 320x200 surface --
	// the previous full-screen blit was ~256 KB per draw, and the in-game view
	// does dozens of draws per frame (~10 MB/frame of needless memcpy, the main
	// cause of slow keypress->render latency). The blit rect is intersected with
	// SDL's current clip rect on mScreen so the backdrop matches what's visible.
	// Skip the backdrop mirror while mSuspendBackdrop is set (automap overlay
	// or similar): the SOP's text-erase source must never capture overlay
	// pixels, else the next scroll/print restores those overlay pixels
	// underneath fresh text and the same "You can't go" line, redrawn on
	// top of a subtly different backdrop, reads as bolder or thinner across
	// the M cycle.
	if (!mSuspendBackdrop) {
		if (mBackdrop == nullptr)
			mBackdrop = SDL_CreateSurface(WIDTH, HEIGHT, SDL_PIXELFORMAT_ARGB8888);
		SDL_Rect clip;
		SDL_GetSurfaceClipRect(mScreen, &clip);
		SDL_Rect mirror_rect = dest;
		if (SDL_GetRectIntersection(&mirror_rect, &clip, &mirror_rect)) {
			SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
			SDL_BlitSurface(mScreen, &mirror_rect, mBackdrop, &mirror_rect);
		}
	}
}

bool GRAPHICS::Graphics::visibleBitmapRect(std::vector<uint8_t> &bmp,
		uint16_t index, int posX, int posY, int mirror, int out[4]) {
	Bitmap image(bmp);
	int iw = image.getWidth(index), ih = image.getHeight(index);
	// Bound the box by COVERAGE, not by colour: a shape may paint palette
	// index 0, and testing `pixel != 0` would shrink the rect around those
	// pixels -- or report a shape that is entirely painted black as fully
	// transparent. Same reason drawImage stopped colorkeying.
	std::vector<uint8_t> mask;
	std::vector<uint8_t> pixels = image.decodeScanlineMasked(index, mask);
	if (iw <= 0 || ih <= 0 ||
	    pixels.size() < static_cast<size_t>(iw) * ih ||
	    mask.size() < static_cast<size_t>(iw) * ih)
		return false;
	int minX = iw, minY = ih, maxX = -1, maxY = -1;
	for (int y = 0; y < ih; ++y)
		for (int x = 0; x < iw; ++x)
			if (mask[static_cast<size_t>(y) * iw + x] != 0) {
				if (x < minX) minX = x;
				if (x > maxX) maxX = x;
				if (y < minY) minY = y;
				if (y > maxY) maxY = y;
			}
	if (maxX < 0)
		return false; // fully transparent shape
	// Mirroring flips the pixels within the same dest rect (see drawImage),
	// so the visible box reflects inside [0, iw) / [0, ih).
	if (mirror & 1) {
		int nMinX = iw - 1 - maxX;
		maxX = iw - 1 - minX;
		minX = nMinX;
	}
	if (mirror & 2) {
		int nMinY = ih - 1 - maxY;
		maxY = ih - 1 - minY;
		minY = nMinY;
	}
	out[0] = std::max(posX + minX, 0);
	out[1] = std::max(posY + minY, 0);
	out[2] = std::min(posX + maxX, WIDTH - 1);
	out[3] = std::min(posY + maxY, HEIGHT - 1);
	return out[0] <= out[2] && out[1] <= out[3];
}

void GRAPHICS::Graphics::setClip(int x, int y, int w, int h) {
	SDL_Rect r = { x, y, w, h };
	SDL_SetSurfaceClipRect(mScreen, &r);
}

void GRAPHICS::Graphics::clearClip() {
	SDL_SetSurfaceClipRect(mScreen, nullptr);
}

// Blit a flat width*height byte buffer (8 bpp indexed) onto the screen
// surface using the live palette. Used for CPS backdrops and any other art
// that comes as raw indexed pixels (no RLE container). Empty pixel buffers
// are a no-op so a missing/short backdrop doesn't crash the caller.
void GRAPHICS::Graphics::drawIndexed(const std::vector<uint8_t> &pixels,
                                      int width, int height, int posX, int posY,
                                      bool transparent) {
	if (pixels.empty() || width <= 0 || height <= 0) return;
	if (static_cast<size_t>(width) * height > pixels.size()) return;

	SDL_Surface *surface = makeIndexedSurfaceFrom(
		const_cast<uint8_t *>(pixels.data()), width, height, width, mPalette);
	if (!surface) return;
	if (transparent) {
		// Palette index 0 -> transparent. Sparkles use this so the slot
		// frame shows through the empty parts of the sprite.
		SDL_SetSurfaceColorKey(surface, true, 0);
	}

	SDL_Rect dest = { posX, posY, width, height };
	SDL_BlitSurface(surface, NULL, mScreen, &dest);
	SDL_DestroySurface(surface);

	// Keep mBackdrop in sync with the visible art so later text-window restores
	// don't ghost over the CPS. Matches drawImage's mirror-rect logic.
	// Skip the backdrop mirror while mSuspendBackdrop is set (automap overlay
	// or similar): the SOP's text-erase source must never capture overlay
	// pixels, else the next scroll/print restores those overlay pixels
	// underneath fresh text and the same "You can't go" line, redrawn on
	// top of a subtly different backdrop, reads as bolder or thinner across
	// the M cycle.
	if (!mSuspendBackdrop) {
		if (mBackdrop == nullptr)
			mBackdrop = SDL_CreateSurface(WIDTH, HEIGHT, SDL_PIXELFORMAT_ARGB8888);
		SDL_Rect clip;
		SDL_GetSurfaceClipRect(mScreen, &clip);
		SDL_Rect mirror_rect = dest;
		if (SDL_GetRectIntersection(&mirror_rect, &clip, &mirror_rect)) {
			SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
			SDL_BlitSurface(mScreen, &mirror_rect, mBackdrop, &mirror_rect);
		}
	}
}

void GRAPHICS::Graphics::snapshotCompass() {
	// Skip while an overlay covers mScreen -- otherwise we'd snapshot the
	// overlay pixels instead of the compass and restamp them on future frames.
	if (mSuppressCompassSnap) return;
	if (mCompassSnap == nullptr)
		mCompassSnap = SDL_CreateSurface(kCompassRect.w, kCompassRect.h,
		                                 SDL_PIXELFORMAT_ARGB8888);
	SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mScreen, &kCompassRect, mCompassSnap, nullptr);
	mCompassCovered = false; // freshly drawn: resume the per-present re-stamp
}

void GRAPHICS::Graphics::snapshotCompassUnderlay() {
	if (mCompassUnderlay == nullptr)
		mCompassUnderlay = SDL_CreateSurface(kMenuUnderlayRect.w,
		                                     kMenuUnderlayRect.h,
		                                     SDL_PIXELFORMAT_ARGB8888);
	SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mScreen, &kMenuUnderlayRect, mCompassUnderlay, nullptr);
	if (std::getenv("THIRDEYE_COMPASSDBG")) {
		static int n = 0;
		char p[64];
		snprintf(p, sizeof(p), "/tmp/underlay_%d.bmp", n++);
		SDL_SaveBMP(mCompassUnderlay, p);
		fprintf(stderr, "[compass] underlay captured -> %s\n", p);
	}
}

void GRAPHICS::Graphics::restoreBackdrop() {
	if (mBackdrop == nullptr) return;
	SDL_SetSurfaceBlendMode(mBackdrop, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mBackdrop, nullptr, mScreen, nullptr);
}

void GRAPHICS::Graphics::restoreCompass() {
	// Only in-game (mTextRestoreBg marks the HUD; the title menu uses flat-fill
	// and has no compass), only once a snapshot exists, and only while the
	// adventure screen is up (mUiScreen -- the pump sets it from the SOP's
	// own screen state each tick; dialogs/menus must not get the adventure
	// compass stamped over them).
	if (mCompassSnap == nullptr || !mTextRestoreBg || mCompassCovered ||
	    mUiScreen)
		return;
	SDL_Rect dst = kCompassRect;
	SDL_SetSurfaceBlendMode(mCompassSnap, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mCompassSnap, nullptr, mScreen, &dst);
}

void GRAPHICS::Graphics::fillRect(int x0, int y0, int x1, int y1, uint8_t color) {
	if (x1 < x0) std::swap(x0, x1);
	if (y1 < y0) std::swap(y0, y1);
	SDL_Rect r = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
	// A fill that touches most of the compass rect (a full cover, or the top
	// half of a cutscene dialog panel drawn in pieces) means the SOP intends
	// to paint over the compass -- stop re-stamping the snapshot on present
	// until the compass is redrawn (see restoreCompass). We use "any overlap
	// >= half of the compass rect area" instead of full-cover, because the
	// mausoleum-entry dialog draws its panel as several partial fills that
	// individually don't 100% cover the compass; the old strict rule left
	// each fill un-detected and the compass re-stamped over the dialog.
	{
		SDL_Rect ov;
		if (SDL_GetRectIntersection(&r, &kCompassRect, &ov) &&
		    ov.w * ov.h * 2 >= kCompassRect.w * kCompassRect.h)
			mCompassCovered = true;
	}
	SDL_Color c = mPalette->colors[color];
	Uint32 px = SDL_MapSurfaceRGB(mScreen, c.r, c.g, c.b);
	SDL_FillSurfaceRect(mScreen, &r, px);
	clearIndexRect(mScreen, r);
	// Keep the text-free backdrop snapshot in sync so a later text_window restore of
	// this box shows the cleared colour, not the art the clear replaced.
	if (mBackdrop != nullptr && !mSuspendBackdrop)
		SDL_FillSurfaceRect(mBackdrop, &r, px);
}

void GRAPHICS::Graphics::fillRectRGB(int x0, int y0, int x1, int y1,
                                     uint8_t r, uint8_t g, uint8_t b) {
	if (x1 < x0) std::swap(x0, x1);
	if (y1 < y0) std::swap(y0, y1);
	SDL_Rect rc = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
	Uint32 px = SDL_MapSurfaceRGB(mScreen, r, g, b);
	SDL_FillSurfaceRect(mScreen, &rc, px);
	clearIndexRect(mScreen, rc);
}

uint32_t GRAPHICS::Graphics::peekPixel(int x, int y) const {
	if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
		return 0;
	const uint8_t *row = static_cast<const uint8_t *>(mScreen->pixels) +
	                     static_cast<size_t>(y) * mScreen->pitch;
	return reinterpret_cast<const uint32_t *>(row)[x];
}

void GRAPHICS::Graphics::pokePixel(int x, int y, uint32_t raw) {
	if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
		return;
	uint8_t *row = static_cast<uint8_t *>(mScreen->pixels) +
	               static_cast<size_t>(y) * mScreen->pitch;
	reinterpret_cast<uint32_t *>(row)[x] = raw;
}

uint32_t GRAPHICS::Graphics::mapColor(uint8_t color) const {
	SDL_Color c = mPalette->colors[color];
	return SDL_MapSurfaceRGB(mScreen, c.r, c.g, c.b);
}

void GRAPHICS::Graphics::pixelFade(int x0, int y0, int x1, int y1,
                                   int intervals) {
	if (x1 < x0) std::swap(x0, x1);
	if (y1 < y0) std::swap(y0, y1);
	x0 = std::max(0, x0); y0 = std::max(0, y0);
	x1 = std::min(WIDTH - 1, x1); y1 = std::min(HEIGHT - 1, y1);
	if (mLastShown == nullptr || intervals <= 0) { // nothing presented yet
		update();
		return;
	}
	int w = x1 - x0 + 1, h = y1 - y0 + 1;
	// Snapshot the NEW content (what the SOP just drew), show the OLD frame,
	// then reveal new pixels in shuffled order across `intervals` presents.
	SDL_Rect rect = { x0, y0, w, h };
	SDL_Surface *newSnap = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
	if (newSnap == nullptr) { update(); return; }
	SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
	SDL_SetSurfaceBlendMode(mLastShown, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mScreen, &rect, newSnap, nullptr);
	SDL_BlitSurface(mLastShown, &rect, mScreen, &rect);
	std::vector<uint32_t> order(static_cast<size_t>(w) * h);
	for (size_t i = 0; i < order.size(); ++i)
		order[i] = static_cast<uint32_t>(i);
	static std::minstd_rand rng{0x1D5EED};
	std::shuffle(order.begin(), order.end(), rng);
	size_t done = 0;
	for (int step = 0; step < intervals; ++step) {
		size_t upto = order.size() * (step + 1) / intervals;
		for (; done < upto; ++done) {
			int px = static_cast<int>(order[done] % w);
			int py = static_cast<int>(order[done] / w);
			const uint8_t *srow = static_cast<const uint8_t *>(newSnap->pixels) +
			                      static_cast<size_t>(py) * newSnap->pitch;
			pokePixel(x0 + px, y0 + py,
			          reinterpret_cast<const uint32_t *>(srow)[px]);
		}
		update();
		// pixelFade blocks for `intervals` frames (30-ish typical, ~500ms
		// wall time); pump so the OS doesn't beach-ball. SDL_EVENT_QUIT
		// stays in the queue and gets picked up on the next dispatch_event.
		SDL_PumpEvents();
		SDL_Delay(16); // ~1 vblank per interval, matching VFX_pixel_fade pacing
	}
	SDL_DestroySurface(newSnap);
}

void GRAPHICS::Graphics::drawLine(int x0, int y0, int x1, int y1,
		uint8_t color) {
	if (x0 == x1 || y0 == y1) { // axis-aligned: one rect fill
		fillRect(x0, y0, x1, y1, color);
		return;
	}
	SDL_Color c = mPalette->colors[color];
	Uint32 px = SDL_MapSurfaceRGB(mScreen, c.r, c.g, c.b);
	int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		SDL_Rect r = { x0, y0, 1, 1 };
		SDL_FillSurfaceRect(mScreen, &r, px);
	clearIndexRect(mScreen, r);
		clearIndexRect(mScreen, r);
		if (mBackdrop != nullptr && !mSuspendBackdrop)
			SDL_FillSurfaceRect(mBackdrop, &r, px);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

void GRAPHICS::Graphics::hashRect(int x0, int y0, int x1, int y1,
		uint8_t color) {
	if (x1 < x0) std::swap(x0, x1);
	if (y1 < y0) std::swap(y0, y1);
	SDL_Color c = mPalette->colors[color];
	Uint32 px = SDL_MapSurfaceRGB(mScreen, c.r, c.g, c.b);
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0 + ((x0 ^ y) & 1); x <= x1; x += 2) {
			SDL_Rect r = { x, y, 1, 1 };
			SDL_FillSurfaceRect(mScreen, &r, px);
			clearIndexRect(mScreen, r);
	clearIndexRect(mScreen, r);
		clearIndexRect(mScreen, r);
			if (mBackdrop != nullptr && !mSuspendBackdrop)
				SDL_FillSurfaceRect(mBackdrop, &r, px);
		}
	}
}

void GRAPHICS::Graphics::zoomIntoImage(std::vector<uint8_t> &bmp) {
	mState = ZOOM_INTO;
	mCounter = 0;
	mBuffer = bmp;

	mSurface[0] = SDL_CreateSurface(320, 200, SDL_PIXELFORMAT_ARGB8888);
	SDL_BlitSurface(mSurface[0], NULL, mScreen, NULL);

	Bitmap image(bmp);
	std::vector<uint8_t> imageData = image[0];
	SDL_Surface *surface = makeIndexedSurfaceFrom(&imageData[0],
			image.getWidth(0), image.getHeight(0), image.getWidth(0), mPalette);
	SDL_BlitSurface(surface, NULL, mSurface[0], NULL);
	SDL_DestroySurface(surface);
}

void GRAPHICS::Graphics::playVideo(sequence video) {
	// Reset the same fields stopVideo() clears. Otherwise a second cinematic
	// (e.g. FINALE/INTRO after in-game Abandon) inherits the game session's
	// mState — the advance guard at update()'s `mState == NOOP && ...` never
	// fires and no frames get drawn, even though isVideoPlaying() is true
	// (so the event loop still runs and the user has to press ESC to skip
	// an invisible playback).
	stopVideo();
	// Also wipe the framebuffer to black. Cinematics only paint the main play
	// rect; the compass / HUD region a prior game session drew stays visible
	// otherwise, bleeding through the cinematic. Cold-boot cinematics don't
	// hit this because the screen was already blank on first draw.
	fillRect(0, 0, WIDTH - 1, HEIGHT - 1, 0);
	mVideo = std::move(video);
}

bool GRAPHICS::Graphics::isVideoPlaying() {
	return (!mVideo.empty());
}

void GRAPHICS::Graphics::stopVideo() {
	mVideo.clear();
	mBuffer.clear();
	mBitmap.clear();
	mState = NOOP;
	mFrames = 0;
	mCounter = 0;
	mAlpha = 0;
	SDL_SetSurfaceAlphaMod(mScreen, SDL_ALPHA_OPAQUE);
}

void GRAPHICS::Graphics::playAnimation(std::vector<uint8_t> animationData) {
	Bitmap animation(animationData);
	mFrames = animation.getNumberOfBitmaps();
	mCounter = 0;
	mBuffer = std::move(animationData);
	mState = DISP_BMA;
}

void GRAPHICS::Graphics::fadeIn() {
	mState = FADE_IN;
	mAlpha = 0;
}

void GRAPHICS::Graphics::drawCurtain(std::vector<uint8_t> bmp) {
	Bitmap bImage(bmp);
	std::vector<uint8_t> bImageD = bImage[0];
	SDL_Surface *sImage = makeIndexedSurfaceFrom(&bImageD[0],
			bImage.getWidth(0), bImage.getHeight(0), bImage.getWidth(0), mPalette);
	mSurface[0] = SDL_CreateSurface(320, 200, SDL_PIXELFORMAT_INDEX8);
	SDL_SetSurfacePalette(mSurface[0], mPalette);
	SDL_BlitSurface(sImage, NULL, mSurface[0], NULL);
	SDL_SetSurfaceColorKey(mSurface[0], true, 0);
	mState = DRAW_CURTAIN;
	mCounter = 1;
	SDL_DestroySurface(sImage);
}

void GRAPHICS::Graphics::panDirection(uint8_t panDir,
		std::vector<uint8_t> bgRight, std::vector<uint8_t> bgLeft,
		std::vector<uint8_t> bgFarLeft, std::vector<uint8_t> fgRight,
		std::vector<uint8_t> fgLeft) {

	uint8_t bgPanels = 2;
	uint8_t fgPanels = 2;

	// bonus texture?
	SDL_Surface *bgFarLeftSurface = nullptr;
	std::vector<uint8_t> bBGFarLeftD;
	if (!bgFarLeft.empty()) {
		bgPanels = 3;
		Bitmap bBGFarLeft(bgFarLeft);
		bBGFarLeftD = bBGFarLeft[0];
		bgFarLeftSurface = makeIndexedSurfaceFrom(&bBGFarLeftD[0],
				bBGFarLeft.getWidth(0), bBGFarLeft.getHeight(0),
				bBGFarLeft.getWidth(0), mPalette);
	}

	// create temporary surfaces
	Bitmap bBGRight(bgRight);
	std::vector<uint8_t> bBGRightD = bBGRight[0];
	SDL_Surface *bgRightSurface = makeIndexedSurfaceFrom(&bBGRightD[0],
			bBGRight.getWidth(0), bBGRight.getHeight(0), bBGRight.getWidth(0),
			mPalette);

	Bitmap bBGLeft(bgLeft);
	std::vector<uint8_t> bBGLeftD = bBGLeft[0];
	SDL_Surface *bgLeftSurface = makeIndexedSurfaceFrom(&bBGLeftD[0],
			bBGLeft.getWidth(0), bBGLeft.getHeight(0), bBGLeft.getWidth(0),
			mPalette);

	Bitmap bFGRight(fgRight);
	std::vector<uint8_t> bFGRightD = bFGRight[0];
	SDL_Surface *fgRightSurface = makeIndexedSurfaceFrom(&bFGRightD[0],
			bFGRight.getWidth(0), bFGRight.getHeight(0), bFGRight.getWidth(0),
			mPalette);

	Bitmap bFGLeft(fgLeft);
	std::vector<uint8_t> bFGLeftD = bFGLeft[0];
	SDL_Surface *fgLeftSurface = makeIndexedSurfaceFrom(&bFGLeftD[0],
			bFGLeft.getWidth(0), bFGLeft.getHeight(0), bFGLeft.getWidth(0),
			mPalette);

	// create surfaces that will slide over each other
	mSurface[0] = SDL_CreateSurface(320 * bgPanels, 200, SDL_PIXELFORMAT_INDEX8);
	SDL_SetSurfacePalette(mSurface[0], mPalette);
	mSurface[1] = SDL_CreateSurface(320 * fgPanels, 200, SDL_PIXELFORMAT_INDEX8);
	SDL_SetSurfacePalette(mSurface[1], mPalette);

	SDL_Rect dest = { 0, 0, 0, 0 };

	if (!bgFarLeft.empty()) { // bonus texture?
		dest.x = 3;
		dest.y = -1;
		SDL_BlitSurface(bgFarLeftSurface, NULL, mSurface[0], &dest);
		dest.x -= 3;
		dest.y = 0;
		dest.x += 320;
		SDL_DestroySurface(bgFarLeftSurface);
	}
	SDL_BlitSurface(bgLeftSurface, NULL, mSurface[0], &dest);
	SDL_DestroySurface(bgLeftSurface);
	dest.x += 320;
	SDL_BlitSurface(bgRightSurface, NULL, mSurface[0], &dest);
	SDL_DestroySurface(bgRightSurface);
	SDL_SetSurfaceColorKey(mSurface[0], true, 0);

	SDL_BlitSurface(fgLeftSurface, NULL, mSurface[1], NULL);
	SDL_DestroySurface(fgLeftSurface);
	dest.x = 320;
	SDL_BlitSurface(fgRightSurface, NULL, mSurface[1], &dest);
	SDL_DestroySurface(fgRightSurface);
	SDL_SetSurfaceColorKey(mSurface[1], true, 0);

	mState = PAN_LEFT;
	mCounter = mSurface[0]->w - 320;
}

void GRAPHICS::Graphics::scrollLeft(std::vector<uint8_t> bmp) {
	Bitmap bImage(bmp);
	std::vector<uint8_t> bImageD = bImage[0];
	SDL_Surface *sImage = makeIndexedSurfaceFrom(&bImageD[0],
			bImage.getWidth(0), bImage.getHeight(0), bImage.getWidth(0), mPalette);
	mSurface[0] = SDL_CreateSurface(320, 200, SDL_PIXELFORMAT_INDEX8);
	SDL_SetSurfacePalette(mSurface[0], mPalette);
	SDL_BlitSurface(sImage, NULL, mSurface[0], NULL);
	SDL_SetSurfaceColorKey(mSurface[0], true, 0);
	SDL_DestroySurface(sImage);

	mCounter = mSurface[0]->w;
	mState = SCROLL_LEFT;
}

void GRAPHICS::Graphics::materializeImage(std::vector<uint8_t> bmp) {
	Bitmap bImage(bmp);
	std::vector<uint8_t> bImageD = bImage[0];
	SDL_Surface *sImage = makeIndexedSurfaceFrom(&bImageD[0],
			bImage.getWidth(0), bImage.getHeight(0), bImage.getWidth(0), mPalette);
	mSurface[0] = SDL_CreateSurface(320, 200, SDL_PIXELFORMAT_INDEX8);
	SDL_SetSurfacePalette(mSurface[0], mPalette);
	SDL_BlitSurface(sImage, NULL, mSurface[0], NULL);
	SDL_SetSurfaceColorKey(mSurface[0], true, 0);
	SDL_DestroySurface(sImage);

	uint16_t size = mSurface[0]->w * mSurface[0]->h;
	mBuffer.resize(size);
	mBuffer.clear();
	mBuffer.assign(size, 0x00);
	mCounter = 100;
	mState = MATERIALIZE;
}

void GRAPHICS::Graphics::update() {
	bool updateScene = false;
	mClock = SDL_GetTicks();
	mSleep = 0;

	if (mClock / 1000 > mRunningClock) {
		mRunningClock = mClock / 1000;
		updateScene = true;
	}

	if (mVideoWait > 0 && updateScene)
		mVideoWait--;

	// what are we playing now?
	if (mState == NOOP && !mVideo.empty() && mVideoWait == 0) {
		uint8_t index = mVideo.begin()->first;
		//std::cout << "Playing mVideo: " << std::dec << (int) index << std::endl;
		tuple<uint8_t, uint8_t, std::vector<uint8_t> > scene =
				mVideo.begin()->second;
		mVideoWait = std::get<1>(scene);
		switch (std::get<0>(scene)) {
		case SET_PAL:
			loadPalette(std::get<2>(scene), false);
			break;
		case PAN_LEFT: {
			uint8_t bgPanels = std::get<1>(scene);

			std::vector<uint8_t> bgRight = std::move(std::get<2>(scene));
			mVideo.erase(index++);
			scene = mVideo.begin()->second;
			std::vector<uint8_t> bgLeft = std::move(std::get<2>(scene));
			mVideo.erase(index++);
			std::vector<uint8_t> bgFarLeft;
			if (bgPanels == 3) {
				scene = mVideo.begin()->second;
				bgFarLeft = std::move(std::get<2>(scene));
				mVideo.erase(index++);
			}

			scene = mVideo.begin()->second;
			//uint8_t fgPanels = std::get<1>(scene);
			std::vector<uint8_t> fgRight = std::move(std::get<2>(scene));
			mVideo.erase(index++);
			scene = mVideo.begin()->second;
			std::vector<uint8_t> fgLeft = std::move(std::get<2>(scene));
			mVideoWait = std::get<1>(scene);

			panDirection(0, std::move(bgRight), std::move(bgLeft),
					std::move(bgFarLeft), std::move(fgRight), std::move(fgLeft));
		}
			break;
		case DISP_BMP:
			drawImage(std::get<2>(scene), 0, 0, 0, false);
			break;
		case DISP_OVERLAY:
			drawImage(std::get<2>(scene), 0, 0, 0, true);
			break;
		case DISP_BMA:
			playAnimation(std::get<2>(scene));
			break;
		case FADE_IN:
			drawImage(std::get<2>(scene), 0, 0, 0, false);
			fadeIn();
			break;
		case SCROLL_LEFT:
			scrollLeft(std::get<2>(scene));
			break;
		case DRAW_CURTAIN:
			drawCurtain(std::get<2>(scene));
			break;
		case MATERIALIZE:
			materializeImage(std::get<2>(scene));
			break;
		default:
			throw std::runtime_error(
					"Video scene type not yet implemented: " +
					std::to_string(static_cast<int>(std::get<0>(scene))));
		}
		mVideo.erase(index);
	}

	// materialize image on to screen
	if (mState == MATERIALIZE) {
		uint16_t size = mSurface[0]->w * mSurface[0]->h;
		std::mt19937 localRng(static_cast<unsigned int>(std::clock()));
		std::uniform_int_distribution<uint16_t> rngRange(1, size);

		for (uint16_t i = 0; i < size / 10; i++) {
			uint16_t randomNumber = rngRange(localRng);
			SDL_Rect rect = {
				static_cast<int>(randomNumber % mSurface[0]->w),
				static_cast<int>(randomNumber / mSurface[0]->w),
				1,
				1
			};

			SDL_BlitSurface(mSurface[0], &rect, mScreen, &rect);
		}

		if (mCounter == 0) {
			mState = NOOP;
			SDL_DestroySurface(mSurface[0]);
		} else
			mCounter--;
	}

	// scroll to the left
	if (mState == SCROLL_LEFT) {
		uint8_t speed = 5;
		SDL_Rect rect = { mCounter, 0, speed, 115 };
		SDL_BlitSurface(mSurface[0], &rect, mScreen, &rect);

		if (mCounter == 0) {
			mState = NOOP;
			SDL_DestroySurface(mSurface[0]);
		} else
			mCounter -= speed;
	}

	// are we curtain-ing to another image?
	if (mState == DRAW_CURTAIN) {
		uint16_t width = mCounter;
		uint16_t lines = 10;

		for (uint16_t line = 0; line <= lines; line++) {
			// going right
			SDL_Rect rectRight =
					{ mSurface[0]->w / lines * line, 0, width, 200 };
			SDL_BlitSurface(mSurface[0], &rectRight, mScreen, &rectRight);

			// going left
			SDL_Rect rectLeft = { mSurface[0]->w / lines * line - mCounter, 0,
					width, 200 };
			SDL_BlitSurface(mSurface[0], &rectLeft, mScreen, &rectLeft);
		}
		if (mCounter == mSurface[0]->w / lines / 2) {
			mState = NOOP;
			SDL_DestroySurface(mSurface[0]);
		} else
			mCounter++;

		mSleep = 150;
	}

	// panning are we panning?
	if (mState == PAN_LEFT) {
		//std::cout << "width: " << mSurface[0]->w << std::endl;
		SDL_Rect sRect = { mCounter, 0, mSurface[0]->w - 6, 115 };
		SDL_Rect dRect = { 3, 3, 0, 0 }; // last 2 are ignored
		SDL_BlitSurface(mSurface[0], &sRect, mScreen, &dRect);
		sRect.x = mSurface[0]->w - ((mSurface[0]->w - mCounter) * 2);
		SDL_BlitSurface(mSurface[1], &sRect, mScreen, &dRect);
		if (mCounter == 0) {
			mState = NOOP;
			SDL_DestroySurface(mSurface[0]);
			SDL_DestroySurface(mSurface[1]);
		} else
			mCounter--;

		mSleep = 5;
	}

	// anything in our animation queue to display?
	if (mState == DISP_BMA) {
		if (mFrames > mCounter) {
			drawImage(mBuffer, mCounter, 0, 0, true);
			//std::cout << "  Frame: " << (int) mCounter << std::endl;
			mCounter++;
		} else {
			//std::cout << "   Finished playing @ " << (int) mCounter << std::endl;
			mFrames = 0;
			mCounter = 0;
			mState = NOOP;
		}

		mSleep = 150;
	}

	// are we fading in or out?
	if (mState == FADE_IN) {
		if (mAlpha > SDL_ALPHA_OPAQUE) {
			mState = NOOP;
			mAlpha = SDL_ALPHA_OPAQUE;
		}
		SDL_SetSurfaceAlphaMod(mScreen, mAlpha);
		//printf("Applying alpha: %d  \n", mAlpha);
		mAlpha += 10;
		mSleep = 100;
	}

	/*
	 if (mState == FADE_OUT) {
	 if (mAlpha < SDL_ALPHA_TRANSPARENT) {
	 mState = NOOP;
	 mAlpha = SDL_ALPHA_TRANSPARENT;
	 }
	 int value = SDL_SetSurfaceAlphaMod(mScreen, mAlpha);
	 printf("Sleeping: %d - %d  \n", value, mAlpha);
	 mAlpha -= 10;
	 }
	 */

	// zoom into a image
	if (mState == ZOOM_INTO) {
		float percentage = (float) mCounter / 100;
		uint16_t width = static_cast<float>(mSurface[0]->w) * percentage;
		uint16_t height = static_cast<float>(mSurface[0]->h) * percentage;
		uint8_t x = static_cast<float>(mScreen->w) / 2 - percentage * static_cast<float>(mScreen->w) / 2;
		uint8_t y = static_cast<float>(mScreen->h) / 2 - percentage * static_cast<float>(mScreen->h) / 2;
		SDL_Rect rect = { x, y, width, height };

		// mCounter starts at 0, which would produce a 0x0 source -- SDL3
		// refuses to create it and the blit becomes a null deref. Skip the
		// first frame so the zoom starts at the first non-degenerate step.
		if (width > 0 && height > 0) {
			SDL_Surface *scaledImage = SDL_CreateSurface(width, height,
					SDL_PIXELFORMAT_ARGB8888);
			SDL_BlitSurfaceScaled(mSurface[0], nullptr, scaledImage, nullptr,
					SDL_SCALEMODE_NEAREST);
			SDL_BlitSurface(scaledImage, NULL, mScreen, &rect);
			SDL_DestroySurface(scaledImage);
		}
		if (mCounter == 100) {
			mState = NOOP;
			SDL_DestroySurface(mSurface[0]);
		} else
			mCounter++;
		mSleep = 10;
	}

	// Re-apply the persistent compass (in-game) so HUD/inventory redraws can't erase
	// the facing indicator the bytecode only redraws on a turn.
	restoreCompass();

	// Present-time overlay: runs after every SOP draw + compass restamp so
	// the overlay (automap) lands on top of anything drawn during a recursive
	// dispatch inside our pump. Set once from engine.cpp.
	//
	// Save-under: the overlay paints into mScreen, but mScreen is the SOP's
	// persistent canvas -- overlay pixels must never survive past this
	// present, or SOP state (text-window cursor, backdrop) and pixels drift
	// apart. We save the frame before painting and restore it after the
	// upload + THIRDEYE_DUMP below, so mScreen stays SOP-pure while the
	// player still sees (and dumps still capture) the overlay. This replaces
	// the old open/close snapshotScreen()/restoreScreenSnapshot() pair, which
	// rolled back every legitimate SOP draw made while the map was open
	// (garbling the message window on the next print/scroll).
	bool overlayPainted = false;
	SDL_Rect savedClip{};
	bool     savedClipValid = false;
	if (mPresentOverlay && (!mOverlayActive || mOverlayActive())) {
		if (mOverlaySave == nullptr)
			mOverlaySave = SDL_CreateSurface(WIDTH, HEIGHT,
			                                 SDL_PIXELFORMAT_ARGB8888);
		// SDL_BlitSurface honours the SOURCE surface's clip rect. If the
		// SOP had set a partial clip on mScreen (dungeon view, text-window
		// draw), our save would capture only that region -- and on restore
		// the un-captured pixels came from mOverlaySave's PREVIOUS frame,
		// leaving stale message-window text ghosted underneath fresh SOP
		// prints (two prints, two colours, overlapping across the M cycle).
		// Save the SOP's clip, drop it for a full-screen save, then hand
		// the clip back at the tail after restore.
		SDL_GetSurfaceClipRect(mScreen, &savedClip);
		savedClipValid = savedClip.w > 0 && savedClip.h > 0 &&
		                 !(savedClip.x == 0 && savedClip.y == 0 &&
		                   savedClip.w == WIDTH && savedClip.h == HEIGHT);
		SDL_SetSurfaceClipRect(mScreen, nullptr);
		SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(mScreen, nullptr, mOverlaySave, nullptr);
		mPresentOverlay();
		overlayPainted = true;
	}

	// Persistent streaming texture. The previous loop did
	// SDL_CreateTextureFromSurface + SDL_DestroyTexture every present, which
	// re-allocates a GPU texture and copies/converts the full 320x200 pixels
	// each call -- a fixed cost on every event pump (~30 Hz) even when nothing
	// has changed. SDL_UpdateTexture reuses the same texture and uploads only
	// the current frame's pixels; it dominates the present time at this
	// resolution.
	if (mPresentTex == nullptr) {
		mPresentTex = SDL_CreateTexture(mRenderer, SDL_PIXELFORMAT_ARGB8888,
		                                SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
		// Linear filter on upscale (the SDL2 hint we used to set globally).
		SDL_SetTextureScaleMode(mPresentTex, SDL_SCALEMODE_LINEAR);
	}
	SDL_UpdateTexture(mPresentTex, nullptr, mScreen->pixels, mScreen->pitch);
	SDL_RenderClear(mRenderer);
	SDL_RenderTexture(mRenderer, mPresentTex, nullptr, nullptr);
	SDL_RenderPresent(mRenderer);

	// Keep a copy of what the player now sees -- pixelFade()'s dissolve source.
	if (mLastShown == nullptr)
		mLastShown = SDL_CreateSurface(WIDTH, HEIGHT, SDL_PIXELFORMAT_ARGB8888);
	SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mScreen, nullptr, mLastShown, nullptr);

	// Debug aid: with THIRDEYE_DUMP set, snapshot every presented frame so a
	// headless run can be inspected as BMPs. Lives here (not in the runtime
	// present handler) so fades/particle effects presented mid-CALL are
	// captured too. "%d" in the path -> numbered frames; substituted by hand,
	// never passed as a printf format (env input is data, not a format spec).
	if (const char *d = std::getenv("THIRDEYE_DUMP")) {
		static int frameNo = 0;
		std::string path = d;
		auto pos = path.find("%d");
		if (pos != std::string::npos)
			path.replace(pos, 2, std::to_string(frameNo++));
		saveScreenshot(path.c_str());
	}

	// Save-under restore (see the overlay block above): wipe the overlay's
	// pixels off the SOP canvas now that they've been shown and dumped, and
	// hand the SOP's clip back. The dest blit runs with mScreen's clip open
	// (the overlay's clearClip() left it that way) so full-screen restore.
	if (overlayPainted) {
		SDL_SetSurfaceBlendMode(mOverlaySave, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(mOverlaySave, nullptr, mScreen, nullptr);
		if (savedClipValid)
			SDL_SetSurfaceClipRect(mScreen, &savedClip);
		else
			SDL_SetSurfaceClipRect(mScreen, nullptr);
	}
}

uint32_t GRAPHICS::Graphics::getSleep() {
	return (mSleep);
}

void GRAPHICS::Graphics::drawText(std::vector<uint8_t> &fnt, std::string text,
		uint16_t posX, uint16_t posY) {
	Font font(fnt);

	// set start positions and default width and height
	SDL_Rect rect = { posX, posY, font.getCharacter((unsigned char) text[0])->w,
			font.getCharacter((unsigned char) text[0])->h };

	std::string::iterator it = text.begin();

	/*
	 std::cout << std::endl;
	 int a = (unsigned char) 'i';

	 for (uint8_t x = 0; x < 8; x++) {
	 for (uint8_t y = 0; y < 8; y++) {
	 unsigned int pixel =
	 ((unsigned int*) font.getCharacter(a)->pixels)[x
	 * (font.getCharacter(a)->pitch
	 / sizeof(unsigned int)) + y];
	 if (pixel > 0)
	 std::cout << std::hex << 0xf;
	 else
	 std::cout << std::hex << 0x0;
	 }
	 std::cout << std::endl;
	 }
	 std::cout << std::dec << std::endl;
	 //return;
	 */

	while (it != text.end()) {
		int ascii = (unsigned char) *it;
		SDL_BlitSurface(font.getCharacter(ascii), NULL, mScreen, &rect);
		it++;
		rect.x += font.getCharacter(ascii)->w;
	}
}

void GRAPHICS::Graphics::drawTextColored(std::vector<uint8_t> &fnt,
		std::string text, uint16_t posX, uint16_t posY, uint8_t paletteIndex,
		int pitch) {
	Font font(fnt);
	SDL_Color c = mPalette->colors[paletteIndex];
	SDL_Rect rect = { posX, posY, 0, 0 };
	for (char ch : text) {
		int ascii = (unsigned char) ch;
		SDL_Surface *glyph = font.getCharacter(ascii);
		if (!glyph) continue;
		// Modulate (255,255,255) -> (c.r,c.g,c.b) at blit time. Font glyphs
		// are SDL_DestroySurface'd by ~Font(), so we don't need to restore.
		SDL_SetSurfaceColorMod(glyph, c.r, c.g, c.b);
		rect.w = glyph->w; rect.h = glyph->h;
		SDL_BlitSurface(glyph, NULL, mScreen, &rect);
		rect.x += pitch > 0 ? pitch : glyph->w;
	}
}

// SDL_CreateColorCursor wants an ARGB surface (it uses the alpha channel
// as the transparency mask), so we paint the indexed source onto a 32 bpp
// ARGB canvas first, then nearest-neighbour scale it to the desired cursor
// size for the current --scale.
void GRAPHICS::Graphics::loadMouse(std::vector<uint8_t> &bitmap,
		uint16_t index) {
	Bitmap image(bitmap);
	if (index >= image.getNumberOfBitmaps()) {
		// The kernel's `set_pointer` (M:153) passes `kernel.report(2000)` as the
		// shape index. The default report returns its arg verbatim (2000), and
		// Icons (~100 shapes) can't satisfy that -- without this guard, we'd
		// decode a ~155 MB garbage shape and burn 15 seconds in the inner loop.
		// (See bitmap.cpp; the underlying decoder now also throws, this is the
		// quiet early-return so the bytecode keeps going without a stale cursor.)
		return;
	}
	std::vector<uint8_t> cursorData = image[index];

	SDL_Surface *cursor = SDL_CreateSurface(
			image.getWidth(index) * mScale, image.getHeight(index) * mScale,
			SDL_PIXELFORMAT_ARGB8888);

	SDL_Surface *cImage = makeIndexedSurfaceFrom(&cursorData[0],
			image.getWidth(index), image.getHeight(index),
			image.getWidth(index), mPalette);
	// Palette index 0 is the transparent colour (same key every draw path
	// uses). The key must sit on the INDEXED source: the blit below then skips
	// those pixels, leaving the ARGB canvas's alpha at 0 there -- and the alpha
	// channel is the only transparency SDL_CreateColorCursor honours. (Keying
	// the ARGB surface after the blit happened to work on macOS's cursor
	// backend but wayland ignores it: the sword showed on an opaque black box.)
	SDL_SetSurfaceColorKey(cImage, true, 0);
	// Make the alpha-copy blit semantics explicit rather than relying on the
	// SDL default: RGB->RGBA copy with source in BLENDMODE_NONE sets dest
	// alpha to the source's per-surface alpha, which is what populates
	// cImage32's alpha channel from the keyed source.
	SDL_SetSurfaceBlendMode(cImage, SDL_BLENDMODE_NONE);

	SDL_Surface *cImage32 = SDL_CreateSurface(image.getWidth(index),
			image.getHeight(index), SDL_PIXELFORMAT_ARGB8888);
	SDL_FillSurfaceRect(cImage32, nullptr, 0); // start fully transparent
	SDL_BlitSurface(cImage, NULL, cImage32, NULL);

	// Copy the alpha channel verbatim through the scale (the default BLEND
	// mode would re-blend it against the uninitialised dest).
	SDL_SetSurfaceBlendMode(cImage32, SDL_BLENDMODE_NONE);
	SDL_BlitSurfaceScaled(cImage32, nullptr, cursor, nullptr, SDL_SCALEMODE_NEAREST);

	// loadMouse is called every time the SOP swaps the cursor sprite; free
	// the previous one (SDL doesn't take ownership through SDL_SetCursor).
	SDL_Cursor *next = SDL_CreateColorCursor(cursor, 0, 0);
	SDL_SetCursor(next);
	if (mCursor != nullptr) SDL_DestroyCursor(mCursor);
	mCursor = next;

	SDL_DestroySurface(cursor);
	SDL_DestroySurface(cImage);
	SDL_DestroySurface(cImage32);
}

void GRAPHICS::Graphics::loadPalette(std::vector<uint8_t> &basePal,
		std::vector<uint8_t> &subPal, std::string index) {

	std::vector<std::string> tokens;
	std::stringstream stream(index);
	std::string token;
	while (std::getline(stream, token, ',')) {
		tokens.push_back(token);
	}

	if (tokens.size() < 2) {
		return;
	}

	uint16_t start = static_cast<uint16_t>(std::stoi(tokens[0]));
	uint16_t end = static_cast<uint16_t>(std::stoi(tokens[1]));

	// SDL3: keep the same palette handle so any surface holding a ref
	// (via SDL_SetSurfacePalette) follows along; just overwrite colours
	// in place. SDL2's free+alloc would have orphaned every transient
	// indexed surface; under SDL3's refcounted palette, swapping out the
	// pointer breaks the ref instead of being a no-op, so reuse is safer.
	Palette basePalette(basePal);
	Palette subPalette(subPal);
	uint16_t counter = 0;

	for (uint16_t i = 0; i < basePalette.getNumOfColours(); i++) {
		if (i >= start && i <= end)
			mPalette->colors[i] = subPalette[counter++];
		else
			mPalette->colors[i] = basePalette[i];
	}
}

void GRAPHICS::Graphics::loadPalette(std::vector<uint8_t> &basePal,
		bool isRes) {
	Palette basePalette(basePal, isRes);

	for (uint16_t i = 0; i < basePalette.getNumOfColours(); i++) {
		mPalette->colors[i] = basePalette[i];
	}
}

std::vector<uint16_t> *GRAPHICS::Graphics::indexPlaneFor(SDL_Surface *s) {
	if (s == nullptr) return nullptr;
	auto it = mIndexPlanes.find(s);
	if (it == mIndexPlanes.end()) {
		const size_t n = static_cast<size_t>(s->w) * s->h;
		it = mIndexPlanes.emplace(s, std::vector<uint16_t>(n, kNoIndex)).first;
	}
	return &it->second;
}

void GRAPHICS::Graphics::clearIndexRect(SDL_Surface *s, const SDL_Rect &r) {
	std::vector<uint16_t> *plane = indexPlaneFor(s);
	if (plane == nullptr) return;
	const int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
	const int x1 = std::min(s->w, r.x + r.w), y1 = std::min(s->h, r.y + r.h);
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x)
			(*plane)[static_cast<size_t>(y) * s->w + x] = kNoIndex;
}

// The DAC change. Walk every tracked surface and rewrite the pixels we know
// came from an index in the range that just moved.
void GRAPHICS::Graphics::repaintPaletteRange(int first, int count) {
	if (count <= 0) return;
	const int last = first + count;
	for (auto &[surf, plane] : mIndexPlanes) {
		if (surf == nullptr || surf->format != SDL_PIXELFORMAT_ARGB8888)
			continue;
		if (plane.size() != static_cast<size_t>(surf->w) * surf->h) continue;
		auto *px = static_cast<uint32_t *>(surf->pixels);
		const int stride = surf->pitch / 4;
		for (int y = 0; y < surf->h; ++y)
			for (int x = 0; x < surf->w; ++x) {
				const uint16_t idx = plane[static_cast<size_t>(y) * surf->w + x];
				if (idx == kNoIndex || idx < first || idx >= last) continue;
				const SDL_Color c = mPalette->colors[idx];
				px[y * stride + x] =
				    0xFF000000u | (c.r << 16) | (c.g << 8) | c.b;
			}
	}
}

void GRAPHICS::Graphics::setPaletteRange(std::vector<uint8_t> &palRes,
		uint16_t firstColor, bool skipMarker) {
	Palette pal(palRes);
	for (uint16_t i = 0; i < pal.getNumOfColours() && (firstColor + i) < 256; i++) {
		SDL_Color c = pal[i];
		// Dungeon view palettes reserve their leading entries with a pure-red
		// (252,0,0) sentinel meaning "leave the existing colour" -- writing it
		// would paint those indices red. Skip it when asked.
		if (skipMarker && c.r == 252 && c.g == 0 && c.b == 0)
			continue;
		mPalette->colors[firstColor + i] = c;
	}
	// Now that the DAC holds the new colours, recolour anything already drawn
	// from those indices -- the whole point of a palette swap on real VGA.
	repaintPaletteRange(firstColor, pal.getNumOfColours());
}

void GRAPHICS::Graphics::setTextFont(int wndnum, int fontId,
		std::vector<uint8_t> &fontRes) {
	// Build the glyph set once per font and cache it: text_style is called on
	// every menu redraw, and rebuilding 128 glyphs each frame would be wasteful.
	auto fontIt = mFontCache.find(fontId);
	if (fontIt == mFontCache.end())
		fontIt = mFontCache.emplace(fontId,
		                            std::make_shared<Font>(fontRes)).first;
	auto [it, _] = mTextWin.try_emplace(wndnum);
	it->second.font = fontIt->second;
}

void GRAPHICS::Graphics::setTextColor(int wndnum, uint8_t color) {
	auto [it, _] = mTextWin.try_emplace(wndnum);
	it->second.fg = color;
}

void GRAPHICS::Graphics::setTextXY(int wndnum, int x, int y) {
	auto [it, _] = mTextWin.try_emplace(wndnum);
	it->second.htab = x;
	it->second.vtab = y;
}

void GRAPHICS::Graphics::setTextWindow(int wndnum, int x0, int y0, int x1,
		int y1, int handle) {
	// Delegate to the 4-coord overload (which resets boundHandle to -1) then
	// install the real handle. Order matters: doing it the other way would
	// have the 4-coord call clobber the handle we just set.
	setTextWindow(wndnum, x0, y0, x1, y1);
	// setTextWindow guaranteed the entry exists; .at() is a side-effect-free
	// read suitable for the now-known key.
	mTextWin.at(wndnum).boundHandle = handle;
}

void GRAPHICS::Graphics::updateTextWindowsFor(int handle, int x0, int y0,
		int x1, int y1) {
	for (auto &kv : mTextWin) {
		if (kv.second.boundHandle == handle) {
			kv.second.winX0 = x0;
			kv.second.winY0 = y0;
			kv.second.winX1 = x1;
			kv.second.winY1 = y1;
		}
	}
}

void GRAPHICS::Graphics::wipeTextBox(int x0, int y0, int x1, int y1) {
	// Repaint the rectangle from whichever source matches the current mode:
	// in-game = restore the panel-art backdrop; menus = sampled flat colour.
	// Extracted so the SOP's explicit wipe_window can call it without going
	// through setTextWindow (which used to wipe as a side effect, matching
	// GIL2VFX_select_text_window's intent of just rebinding -- the original
	// only wipes inside GIL2VFX_home / GIL2VFX_wipe_window).
	if (x0 < 0 || y0 < 0 || x1 >= WIDTH || y1 >= HEIGHT || x1 < x0 || y1 < y0)
		return;
	SDL_Rect r = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
	if (mTextRestoreBg && mBackdrop != nullptr && !mUiScreen) {
		// In-game: restore the real bitmap backdrop (panel art) for this box so
		// the text overlays it -- no flat rectangle.
		SDL_SetSurfaceBlendMode(mBackdrop, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(mBackdrop, &r, mScreen, &r);
	} else {
		// Title menu (flat box bg, options baked into the backdrop bitmap): erase
		// to the sampled background colour so the baked text doesn't show through.
		// Sample just inside the box, clamped to the surface so a window flush
		// against the right/bottom edge can't read past the buffer.
		Uint32 *pixels = static_cast<Uint32 *>(mScreen->pixels);
		int pitch = mScreen->pitch / 4;
		int sx = (x0 + 1 < WIDTH) ? x0 + 1 : x0;
		int sy = (y0 + 1 < HEIGHT) ? y0 + 1 : y0;
		Uint32 bg = pixels[sy * pitch + sx];
		SDL_FillSurfaceRect(mScreen, &r, bg);
		clearIndexRect(mScreen, r);
	}
}

void GRAPHICS::Graphics::setTextWindow(int wndnum, int x0, int y0, int x1,
		int y1) {
	// Pure bind -- matches GIL2VFX_select_text_window. Callers that need to
	// erase the box first should call wipeTextBox (the SOP does this through
	// wipe_window; our HUD redraw path can call it directly).
	//
	// `text_window` is the canonical "first touch" for a wndnum (SOP binds
	// it before setting font/colour/cursor), so we explicitly create-or-
	// update via try_emplace + per-field assign rather than operator[],
	// matching the project guideline on sparse-map writes.
	auto [it, _] = mTextWin.try_emplace(wndnum);
	TextWin &tw = it->second;
	tw.winX0 = x0;
	tw.winX1 = x1;
	tw.winY0 = y0;
	tw.winY1 = y1;
	// Ad-hoc rect (no subwindow handle): clear any stale binding, otherwise
	// a subsequent set_x/y on the previously-bound handle would unexpectedly
	// move this window's edges.
	tw.boundHandle = -1;
}

void GRAPHICS::Graphics::setTextJustify(int wndnum, int justify) {
	auto [it, _] = mTextWin.try_emplace(wndnum);
	it->second.justify = justify;
}

bool GRAPHICS::Graphics::textCursor(int wndnum, int &x, int &y) const {
	auto it = mTextWin.find(wndnum);
	if (it == mTextWin.end())
		return false;
	x = it->second.htab;
	y = it->second.vtab;
	return true;
}

int GRAPHICS::Graphics::textCharWidth(int wndnum, uint8_t ch) {
	auto it = mTextWin.find(wndnum);
	if (it == mTextWin.end() || !it->second.font)
		return 0;
	SDL_Surface *g = it->second.font->getCharacter(ch);
	return g ? g->w : 0;
}

int GRAPHICS::Graphics::textFontHeight(int wndnum) {
	auto it = mTextWin.find(wndnum);
	if (it == mTextWin.end() || !it->second.font)
		return 0;
	SDL_Surface *g = it->second.font->getCharacter('A');
	return g ? g->h : 0;
}

void GRAPHICS::Graphics::printText(int wndnum, const std::string &text) {
	// Faithful port of GIL2VFXA_print_buffer + GIL2VFX_cout (arun/asm/
	// GIL2VFXA.ASM, arun/src/GIL2VFX.C): word-wrap is computed against the
	// space REMAINING from the current cursor, boundary spaces are eaten on a
	// wrap (mid-line spaces render verbatim), justification offsets htab per
	// line, and a '\n' on the bottom line SCROLLS the window content up one
	// row (the cursor stays on the last line) instead of clipping. The
	// scroll is what keeps the message log left-flowing: every log message
	// starts with '\n', so a full log scrolls and the text restarts at the
	// left margin -- the old clip-only logic left the cursor stuck mid-line
	// and new messages continued from there.
	auto it = mTextWin.find(wndnum);
	if (it == mTextWin.end() || !it->second.font)
		return; // no font set for this window yet
	TextWin &tw = it->second;
	SDL_Color c = mPalette->colors[tw.fg];
	Font *font = tw.font.get();
	if (std::getenv("THIRDEYE_TEXTDBG"))
		std::cerr << "[txt] wnd " << wndnum << " win(" << tw.winX0 << ","
		          << tw.winY0 << ")-(" << tw.winX1 << "," << tw.winY1
		          << ") xy(" << tw.htab << "," << tw.vtab << ") just"
		          << tw.justify << " \"" << text << "\"" << std::endl;

	auto glyphW = [&](unsigned char ch) -> int {
		SDL_Surface *g = font->getCharacter(ch);
		return g ? g->w : 0;
	};
	int lineH = 8; // these fonts are fixed-height; sample a glyph
	if (SDL_Surface *g = font->getCharacter('A')) lineH = g->h;

	// VFX_pane_scroll(0, -lineH): move the window's pixels up one text row,
	// then repaint the vacated bottom strip from the text-free backdrop
	// (panel art in-game / sampled flat colour on menus -- wipeTextBox).
	auto scrollUp = [&]() {
		int w = tw.winX1 - tw.winX0 + 1;
		int h = tw.winY1 - tw.winY0 + 1;
		if (w <= 0 || h <= 0 || tw.winX0 < 0 || tw.winY0 < 0 ||
		    tw.winX1 >= WIDTH || tw.winY1 >= HEIGHT)
			return;
		if (h > lineH) {
			Uint8 *px = static_cast<Uint8 *>(mScreen->pixels);
			int pitch = mScreen->pitch;
			for (int y = tw.winY0; y <= tw.winY1 - lineH; ++y)
				std::memmove(px + y * pitch + tw.winX0 * 4,
				             px + (y + lineH) * pitch + tw.winX0 * 4,
				             static_cast<size_t>(w) * 4);
		}
		wipeTextBox(tw.winX0, std::max(tw.winY0, tw.winY1 - lineH + 1),
		            tw.winX1, tw.winY1);
	};

	// GIL2VFX_cout: one char at the cursor. '\n' = CR + advance-or-scroll
	// (scroll when another full line wouldn't fit below); '\r' = CR; else
	// draw the glyph clipped to the window rect and advance htab.
	auto coutChar = [&](unsigned char ch) {
		if (ch == '\n') {
			tw.htab = tw.winX0;
			// GIL2VFXA.ASM lfout: eax = vtab + 2*charH - 1 (bottom row of
			// the would-be new line), `jle __set_vtab` vs y2 -- advance on
			// exact fit, scroll only when the new line's bottom row passes y1.
			if (tw.vtab + 2 * lineH - 1 > tw.winY1)
				scrollUp();
			else
				tw.vtab += lineH;
			return;
		}
		if (ch == '\r') {
			tw.htab = tw.winX0;
			return;
		}
		SDL_Surface *glyph = font->getCharacter(ch);
		if (glyph == nullptr)
			return;
		SDL_SetSurfaceColorMod(glyph, c.r, c.g, c.b);
		// Pixel-precise clip to the window (matches VFX_character_draw):
		// intersect the surface clip with the window rect for this blit.
		SDL_Rect prevClip;
		SDL_GetSurfaceClipRect(mScreen, &prevClip);
		SDL_Rect winR = { tw.winX0, tw.winY0, tw.winX1 - tw.winX0 + 1,
		                  tw.winY1 - tw.winY0 + 1 };
		SDL_Rect clip;
		if (SDL_GetRectIntersection(&winR, &prevClip, &clip)) {
			SDL_SetSurfaceClipRect(mScreen, &clip);
			SDL_Rect dst = { tw.htab, tw.vtab, glyph->w, glyph->h };
			SDL_BlitSurface(glyph, NULL, mScreen, &dst);
			SDL_SetSurfaceClipRect(mScreen, &prevClip);
			// Remember that these pixels came from palette index tw.fg, so a
			// later set_palette can recolour them (see mIndexPlanes). Glyphs
			// are ARGB with a black colour key, so a pixel is painted iff its
			// RGB is non-zero. Mirror the blit's own clip exactly.
			if (std::vector<uint16_t> *plane = indexPlaneFor(mScreen)) {
				const auto *gp = static_cast<const uint32_t *>(glyph->pixels);
				const int gstride = glyph->pitch / 4;
				for (int gy = 0; gy < glyph->h; ++gy)
					for (int gx = 0; gx < glyph->w; ++gx) {
						if ((gp[gy * gstride + gx] & 0x00FFFFFFu) == 0) continue;
						const int sx = dst.x + gx, sy = dst.y + gy;
						if (sx < clip.x || sy < clip.y ||
						    sx >= clip.x + clip.w || sy >= clip.y + clip.h)
							continue;
						if (sx < 0 || sy < 0 || sx >= mScreen->w ||
						    sy >= mScreen->h) continue;
						(*plane)[static_cast<size_t>(sy) * mScreen->w + sx] =
						    tw.fg;
					}
			}
		}
		tw.htab += glyph->w; // advances even past the edge, like the original
	};

	const size_t len = text.size();
	size_t pos = 0;
	while (pos < len) {
		// Line-break scan (print_buffer __find_eol/__lscan): the line ends at
		// the last space that still fits from the current htab, at a '\n'
		// (included -- coutChar processes it), or at end-of-string. If not
		// even the first word fits, it prints anyway (clipped at the edge).
		size_t firstWordEnd = pos;
		while (firstWordEnd < len && text[firstWordEnd] != ' ' &&
		       text[firstWordEnd] != '\n')
			++firstWordEnd;
		if (firstWordEnd < len && text[firstWordEnd] == '\n')
			++firstWordEnd; // '\n' belongs to the line
		size_t lineEnd = firstWordEnd;
		{
			int wAcc = tw.htab;
			for (size_t i = pos; i < len; ++i) {
				unsigned char ch = text[i];
				if (ch == '\n') { lineEnd = i + 1; break; }
				if (ch == ' ')
					lineEnd = i; // last break candidate so far
				wAcc += glyphW(ch);
				if (wAcc > tw.winX1 + 1)
					break; // past the right edge: keep last candidate
				if (i + 1 == len) { lineEnd = len; break; }
			}
		}
		// A boundary space at a cursor already past the right edge leaves
		// lineEnd == pos: force one char of progress or text[lineEnd - 1]
		// below reads before the buffer and the loop never advances.
		if (lineEnd == pos)
			lineEnd = std::min(pos + 1, len);

		// Justification mutates htab for this line (out_r / out_c); width of
		// the line vs the space remaining from the cursor to the right edge.
		int wLin = 0;
		for (size_t i = pos; i < lineEnd; ++i)
			wLin += glyphW(text[i]);
		int wWin = tw.winX1 - tw.htab + 1;
		if (tw.justify == 1) { // right
			tw.htab = (wLin <= wWin) ? tw.winX1 - wLin + 1 : tw.winX0;
		} else if (tw.justify == 2) { // center (within the REMAINING space)
			if (wLin < wWin)
				tw.htab += (wWin - wLin + 1) / 2;
		} // 0 = left, 3 = fill (unused by EOB3) -> print from htab as-is

		for (size_t i = pos; i < lineEnd; ++i)
			coutChar(text[i]);

		if (lineEnd >= len)
			break; // end of buffer: no implicit newline, cursor stays
		pos = lineEnd;
		if (text[lineEnd - 1] != '\n') {
			coutChar('\n'); // wrapped: emit the line break ourselves
			while (pos < len && text[pos] == ' ')
				++pos; // ...and eat the boundary spaces the wrap consumed
		}
	}
}

void GRAPHICS::Graphics::saveScreenshot(const std::string &path) {
	if (!SDL_SaveBMP(mScreen, path.c_str()))
		std::cerr << "saveScreenshot failed: " << SDL_GetError() << std::endl;
}

void GRAPHICS::Graphics::mouseToLogical(int wx, int wy, int &lx, int &ly) const {
	// The coords we get here come from SDL mouse *events*, and SDL's renderer
	// event-watch ALREADY scales event coordinates into the logical (320x200) space
	// whenever SDL_RenderSetLogicalSize is set -- verified: at --scale 2, a warp to
	// window (292,266) arrives as event (146,133). So the input is already logical;
	// we just clamp it. Do NOT re-scale by the window/--scale here: that double-
	// converts and makes every click land at the wrong spot for --scale > 1 (the
	// clicks-miss-when-scaled bug), while looking fine at --scale 1 (identity).
	// (NB: SDL_GetMouseState, unlike events, returns RAW window coords -- so it must
	// NOT be used for position here; we only use it for the button-state bits.)
	lx = wx;
	ly = wy;
	if (lx < 0) lx = 0; else if (lx >= WIDTH) lx = WIDTH - 1;
	if (ly < 0) ly = 0; else if (ly >= HEIGHT) ly = HEIGHT - 1;
	if (std::getenv("THIRDEYE_MOUSE"))
		std::cout << "[mouse] event(" << wx << "," << wy << ") -> logical(" << lx
		          << "," << ly << ")" << std::endl;
}

