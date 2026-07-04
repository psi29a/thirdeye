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
	SDL_DestroyCursor(mCursor);
	SDL_DestroySurface(mScreen);
	SDL_DestroySurface(mBackdrop);     // lazily created in drawImage (nullptr-safe)
	SDL_DestroySurface(mCompassSnap);  // lazily created in snapshotCompass
	SDL_DestroySurface(mLastShown);    // lazily created in update
	if (mPresentTex != nullptr) SDL_DestroyTexture(mPresentTex);
	SDL_DestroyPalette(mPalette);
	SDL_DestroyRenderer(mRenderer);
	SDL_DestroyWindow(mWindow);
	SDL_Quit();
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
	if (cacheId != 0) {
		ShapeKey key{cacheId, index};
		auto it = mShapeCache.find(key);
		if (it == mShapeCache.end()) {
			Bitmap image(bmp);
			DecodedShape s;
			s.w = image.getWidth(index);
			s.h = image.getHeight(index);
			s.pixels = image[index];
			it = mShapeCache.emplace(key, std::move(s)).first;
		}
		shape = &it->second;
	} else {
		Bitmap image(bmp);
		transient.w = image.getWidth(index);
		transient.h = image.getHeight(index);
		transient.pixels = image[index];
		shape = &transient;
	}
	int iw = shape->w, ih = shape->h;
	// Mirror operates on a per-draw copy (the cached buffer stays canonical).
	// Most draws have mirror=0 and skip the copy entirely.
	std::vector<uint8_t> imageData;
	const std::vector<uint8_t> *src = &shape->pixels;
	if (mirror) {
		imageData = shape->pixels;
		src = &imageData;
	}

	// Apply the AESOP draw_bitmap mirror flag by flipping the indexed pixels in
	// place on the per-draw copy (the cached buffer stays canonical). 1=X flips
	// each row left-to-right (right-hand dungeon walls), 2=Y flips the row order.
	if ((mirror & 1) && iw > 0) {
		for (int row = 0; row < ih; ++row)
			std::reverse(imageData.begin() + static_cast<ptrdiff_t>(row) * iw,
			             imageData.begin() + static_cast<ptrdiff_t>(row + 1) * iw);
	}
	if ((mirror & 2) && ih > 0) {
		for (int row = 0; row < ih / 2; ++row)
			std::swap_ranges(imageData.begin() + static_cast<ptrdiff_t>(row) * iw,
			                 imageData.begin() + static_cast<ptrdiff_t>(row + 1) * iw,
			                 imageData.begin() + static_cast<ptrdiff_t>(ih - 1 - row) * iw);
	}

	SDL_Surface *surface = makeIndexedSurfaceFrom(
			const_cast<uint8_t*>(src->data()), iw, ih, iw, mPalette);

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

	if (transparency) {
		SDL_SetSurfaceColorKey(surface, true, 0);
	}
	if (dw == iw && dh == ih)
		SDL_BlitSurface(surface, NULL, mScreen, &dest);
	else
		SDL_BlitSurfaceScaled(surface, NULL, mScreen, &dest,
		                      SDL_SCALEMODE_NEAREST);
	SDL_DestroySurface(surface);

	// Keep the backdrop snapshot in sync with the bitmap art (used to restore a
	// text box's background so in-game text overlays the panel art). Text never
	// touches mBackdrop (it's drawn straight to mScreen by printText), so this
	// snapshot stays text-free regardless of draw order. Mirror only the *drawn
	// rect* (intersected with the active clip), not the whole 320x200 surface --
	// the previous full-screen blit was ~256 KB per draw, and the in-game view
	// does dozens of draws per frame (~10 MB/frame of needless memcpy, the main
	// cause of slow keypress->render latency). The blit rect is intersected with
	// SDL's current clip rect on mScreen so the backdrop matches what's visible.
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

bool GRAPHICS::Graphics::visibleBitmapRect(std::vector<uint8_t> &bmp,
		uint16_t index, int posX, int posY, int mirror, int out[4]) {
	Bitmap image(bmp);
	int iw = image.getWidth(index), ih = image.getHeight(index);
	std::vector<uint8_t> pixels = image[index];
	if (iw <= 0 || ih <= 0 ||
	    pixels.size() < static_cast<size_t>(iw) * ih)
		return false;
	int minX = iw, minY = ih, maxX = -1, maxY = -1;
	for (int y = 0; y < ih; ++y)
		for (int x = 0; x < iw; ++x)
			if (pixels[static_cast<size_t>(y) * iw + x] != 0) {
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

// The compass occupies the bottom-left of the HUD, left of the movement arrows
// (which start at x=117); the disc spans y~120-168. Capture/restore that rect.
static const SDL_Rect kCompassRect = { 0, 120, 116, 49 };

void GRAPHICS::Graphics::snapshotCompass() {
	if (mCompassSnap == nullptr)
		mCompassSnap = SDL_CreateSurface(kCompassRect.w, kCompassRect.h,
		                                 SDL_PIXELFORMAT_ARGB8888);
	SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mScreen, &kCompassRect, mCompassSnap, nullptr);
	mCompassCovered = false; // freshly drawn: resume the per-present re-stamp
}

void GRAPHICS::Graphics::restoreCompass() {
	// Only in-game (mTextRestoreBg marks the HUD; the title menu uses flat-fill and
	// has no compass), and only once a snapshot exists.
	if (mCompassSnap == nullptr || !mTextRestoreBg || mCompassCovered)
		return;
	SDL_Rect dst = kCompassRect;
	SDL_SetSurfaceBlendMode(mCompassSnap, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mCompassSnap, nullptr, mScreen, &dst);
}

void GRAPHICS::Graphics::fillRect(int x0, int y0, int x1, int y1, uint8_t color) {
	if (x1 < x0) std::swap(x0, x1);
	if (y1 < y0) std::swap(y0, y1);
	SDL_Rect r = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
	// A fill covering the whole compass rect (an outtake/cutscene box) means
	// the SOP intends to paint over the compass -- stop re-stamping the
	// snapshot on present until the compass is redrawn (see restoreCompass).
	if (r.x <= kCompassRect.x && r.y <= kCompassRect.y &&
	    r.x + r.w >= kCompassRect.x + kCompassRect.w &&
	    r.y + r.h >= kCompassRect.y + kCompassRect.h)
		mCompassCovered = true;
	SDL_Color c = mPalette->colors[color];
	Uint32 px = SDL_MapSurfaceRGB(mScreen, c.r, c.g, c.b);
	SDL_FillSurfaceRect(mScreen, &r, px);
	// Keep the text-free backdrop snapshot in sync so a later text_window restore of
	// this box shows the cleared colour, not the art the clear replaced.
	if (mBackdrop != nullptr)
		SDL_FillSurfaceRect(mBackdrop, &r, px);
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
		if (mBackdrop != nullptr)
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
			if (mBackdrop != nullptr)
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
	if (mTextRestoreBg && mBackdrop != nullptr) {
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

