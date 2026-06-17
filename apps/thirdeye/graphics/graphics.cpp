#include "graphics.hpp"
#include <algorithm>
#include <cstdlib>

#include "SDL_syswm.h" // SDL_GetWindowWMInfo (kept out of headers: drags in X11)

#include <iostream>
#include <stdexcept>
#include <random>
#include <sstream>
#include <string>
#include <ctime>

#define SOFTWARE	0
#define HARDWARE	1

GRAPHICS::Graphics::Graphics(uint16_t scale, bool renderer) {
	std::cout << "Initializing SDL... ";
	mScale = scale;
	Uint32 flags = SDL_INIT_VIDEO;
	if (SDL_WasInit(flags) == 0) {
		//kindly ask SDL not to trash our OGL context
		//might this be related to http://bugzilla.libsdl.org/show_bug.cgi?id=748 ?
		//SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

		if (SDL_Init(flags) != 0) {
			throw std::runtime_error(
					"Could not initialize SDL! " + std::string(SDL_GetError()));
		}
	}

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version); // initialize info structure with SDL version info

	// Create a window.
	mWindow = SDL_CreateWindow("Thirdeye", SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED, WIDTH * mScale, HEIGHT * mScale, 0 //SDL_WINDOW_SHOWN
			);

	if (SDL_GetWindowWMInfo(mWindow, &info)) { // the call returns true on success
		std::cout << "done!" << std::endl << "  Version:	"
				<< (int) info.version.major << "." << (int) info.version.minor
				<< "." << (int) info.version.patch << std::endl
				<< "  Environment:	";
		switch (info.subsystem) {
		case SDL_SYSWM_UNKNOWN:
			std::cout << "an unknown system!";
			break;
		case SDL_SYSWM_WINDOWS:
			std::cout << "Microsoft Windows(TM)";
			break;
		case SDL_SYSWM_X11:
			std::cout << "X Window System";
			break;
		case SDL_SYSWM_DIRECTFB:
			std::cout << "DirectFB";
			break;
		case SDL_SYSWM_COCOA:
			std::cout << "Apple OS X";
			break;
		case SDL_SYSWM_UIKIT:
			std::cout << "UIKit";
			break;
		default:
			std::cout << "an unsupported or unknown system (" << info.subsystem << ")";
			break;
		}
		std::cout << std::endl;
	} else {
		throw std::runtime_error(
				"Couldn't get window information: "
						+ std::string(SDL_GetError()));
	}

	// Create the renderer driver to be used in window
	if (renderer == HARDWARE)
		mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_ACCELERATED);
	else
		mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_SOFTWARE);

	//SDL_RendererInfo* displayRendererInfo;
	//SDL_GetRendererInfo(mRenderer, displayRendererInfo);
	//std::cout << displayRendererInfo->name << std::endl;

	// Create our game screen that will be blitted to before renderering
	mScreen = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 32, 0, 0, 0, 0);

	SDL_SetColorKey(mScreen, SDL_TRUE, SDL_MapRGB(mScreen->format, 0, 0, 0));

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear"); // make the scaled rendering look smoother.
	SDL_RenderSetLogicalSize(mRenderer, WIDTH, HEIGHT);

	mPalette = SDL_AllocPalette(256);
	mCursor = NULL;
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
	SDL_FreeCursor(mCursor);
	SDL_FreeSurface(mScreen);
	SDL_FreeSurface(mBackdrop);   // lazily created in drawImage (nullptr-safe free)
	SDL_FreeSurface(mCompassSnap); // lazily created in snapshotCompass
	SDL_FreePalette(mPalette);
	SDL_DestroyRenderer(mRenderer);
	SDL_DestroyWindow(mWindow);
	SDL_Quit();
}

void GRAPHICS::Graphics::drawImage(std::vector<uint8_t> &bmp, uint16_t index,
		int posX, int posY, bool transparency, int mirror) {

	Bitmap image(bmp);
	std::vector<uint8_t> imageData = image[index];
	int iw = image.getWidth(index), ih = image.getHeight(index);

	// Apply the AESOP draw_bitmap mirror flag by flipping the indexed pixels in
	// place (the shape stays at posX/posY; only its content reflects). 1=X flips
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

	SDL_Surface *surface = SDL_CreateRGBSurfaceFrom((void*) &imageData[0],
			iw, ih, 8, iw, 0, 0, 0, 0);

	SDL_Rect dest = { posX, posY, iw, ih };

	//std::cout << "width: " << surface->w << " height: " << surface->h << std::endl;

	SDL_SetPaletteColors(surface->format->palette, mPalette->colors, 0, 256);

	if (transparency) {
		SDL_SetColorKey(surface, SDL_TRUE,
				SDL_MapRGB(surface->format, 0, 0, 0));
	}
	SDL_BlitSurface(surface, NULL, mScreen, &dest);
	SDL_FreeSurface(surface);

	// Keep the backdrop snapshot in sync with the bitmap art (used to restore a
	// text box's background so in-game text overlays the panel art). Text never
	// touches mBackdrop (it's drawn straight to mScreen by printText), so this
	// snapshot stays text-free regardless of draw order.
	if (mBackdrop == nullptr)
		mBackdrop = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 32, 0, 0, 0, 0);
	SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mScreen, NULL, mBackdrop, NULL);
}

void GRAPHICS::Graphics::setClip(int x, int y, int w, int h) {
	SDL_Rect r = { x, y, w, h };
	SDL_SetClipRect(mScreen, &r);
}

void GRAPHICS::Graphics::clearClip() {
	SDL_SetClipRect(mScreen, nullptr);
}

// The compass occupies the bottom-left of the HUD, left of the movement arrows
// (which start at x=117); the disc spans y~120-168. Capture/restore that rect.
static const SDL_Rect kCompassRect = { 0, 120, 116, 49 };

void GRAPHICS::Graphics::snapshotCompass() {
	if (mCompassSnap == nullptr)
		mCompassSnap = SDL_CreateRGBSurface(0, kCompassRect.w, kCompassRect.h, 32,
		                                    0, 0, 0, 0);
	SDL_SetSurfaceBlendMode(mScreen, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mScreen, &kCompassRect, mCompassSnap, nullptr);
}

void GRAPHICS::Graphics::restoreCompass() {
	// Only in-game (mTextRestoreBg marks the HUD; the title menu uses flat-fill and
	// has no compass), and only once a snapshot exists.
	if (mCompassSnap == nullptr || !mTextRestoreBg)
		return;
	SDL_Rect dst = kCompassRect;
	SDL_SetSurfaceBlendMode(mCompassSnap, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(mCompassSnap, nullptr, mScreen, &dst);
}

void GRAPHICS::Graphics::fillRect(int x0, int y0, int x1, int y1, uint8_t color) {
	if (x1 < x0) std::swap(x0, x1);
	if (y1 < y0) std::swap(y0, y1);
	SDL_Rect r = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
	SDL_Color c = mPalette->colors[color];
	Uint32 px = SDL_MapRGB(mScreen->format, c.r, c.g, c.b);
	SDL_FillRect(mScreen, &r, px);
	// Keep the text-free backdrop snapshot in sync so a later text_window restore of
	// this box shows the cleared colour, not the art the clear replaced.
	if (mBackdrop != nullptr)
		SDL_FillRect(mBackdrop, &r, px);
}

void GRAPHICS::Graphics::zoomIntoImage(std::vector<uint8_t> &bmp) {
	mState = ZOOM_INTO;
	mCounter = 0;
	mBuffer = bmp;

	mSurface[0] = SDL_CreateRGBSurface(0, 320, 200, 32, 0, 0, 0, 0);
	SDL_BlitSurface(mSurface[0], NULL, mScreen, NULL);

	Bitmap image(bmp);
	std::vector<uint8_t> imageData = image[0];
	SDL_Surface *surface = SDL_CreateRGBSurfaceFrom((void*) &imageData[0],
			image.getWidth(0), image.getHeight(0), 8, image.getWidth(0), 0, 0,
			0, 0);
	SDL_SetPaletteColors(surface->format->palette, mPalette->colors, 0, 256);

	SDL_BlitSurface(surface, NULL, mSurface[0], NULL);
	SDL_FreeSurface(surface);
}

void GRAPHICS::Graphics::playVideo(sequence video) {
	mVideo = video;
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
	mBuffer = animationData;
	mState = DISP_BMA;
}

void GRAPHICS::Graphics::fadeIn() {
	mState = FADE_IN;
	mAlpha = 0;
}

void GRAPHICS::Graphics::drawCurtain(std::vector<uint8_t> bmp) {
	Bitmap bImage(bmp);
	std::vector<uint8_t> bImageD = bImage[0];
	SDL_Surface *sImage = SDL_CreateRGBSurfaceFrom((void*) &bImageD[0],
			bImage.getWidth(0), bImage.getHeight(0), 8, bImage.getWidth(0), 0,
			0, 0, 0);
	mSurface[0] = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
	SDL_BlitSurface(sImage, NULL, mSurface[0], NULL);
	SDL_SetPaletteColors(mSurface[0]->format->palette, mPalette->colors, 0,
			256);
	SDL_SetColorKey(mSurface[0], SDL_TRUE,
			SDL_MapRGB(mSurface[0]->format, 0, 0, 0));
	mState = DRAW_CURTAIN;
	mCounter = 1;
	SDL_FreeSurface(sImage);
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
		bgFarLeftSurface = SDL_CreateRGBSurfaceFrom((void*) &bBGFarLeftD[0],
				bBGFarLeft.getWidth(0), bBGFarLeft.getHeight(0), 8,
				bBGFarLeft.getWidth(0), 0, 0, 0, 0);
		SDL_SetPaletteColors(bgFarLeftSurface->format->palette,
				mPalette->colors, 0, 256);
	}

	// create temporary surfaces
	Bitmap bBGRight(bgRight);
	std::vector<uint8_t> bBGRightD = bBGRight[0];
	SDL_Surface *bgRightSurface = SDL_CreateRGBSurfaceFrom(
			(void*) &bBGRightD[0], bBGRight.getWidth(0), bBGRight.getHeight(0),
			8, bBGRight.getWidth(0), 0, 0, 0, 0);
	SDL_SetPaletteColors(bgRightSurface->format->palette, mPalette->colors, 0,
			256);

	Bitmap bBGLeft(bgLeft);
	std::vector<uint8_t> bBGLeftD = bBGLeft[0];
	SDL_Surface *bgLeftSurface = SDL_CreateRGBSurfaceFrom((void*) &bBGLeftD[0],
			bBGLeft.getWidth(0), bBGLeft.getHeight(0), 8, bBGLeft.getWidth(0),
			0, 0, 0, 0);
	SDL_SetPaletteColors(bgLeftSurface->format->palette, mPalette->colors, 0,
			256);

	Bitmap bFGRight(fgRight);
	std::vector<uint8_t> bFGRightD = bFGRight[0];
	SDL_Surface *fgRightSurface = SDL_CreateRGBSurfaceFrom(
			(void*) &bFGRightD[0], bFGRight.getWidth(0), bFGRight.getHeight(0),
			8, bFGRight.getWidth(0), 0, 0, 0, 0);
	SDL_SetPaletteColors(fgRightSurface->format->palette, mPalette->colors, 0,
			256);

	Bitmap bFGLeft(fgLeft);
	std::vector<uint8_t> bFGLeftD = bFGLeft[0];
	SDL_Surface *fgLeftSurface = SDL_CreateRGBSurfaceFrom((void*) &bFGLeftD[0],
			bFGLeft.getWidth(0), bFGLeft.getHeight(0), 8, bFGLeft.getWidth(0),
			0, 0, 0, 0);
	SDL_SetPaletteColors(fgLeftSurface->format->palette, mPalette->colors, 0,
			256);

	// create surfaces that will slide over each other
	mSurface[0] = SDL_CreateRGBSurface(0, 320 * bgPanels, 200, 8, 0, 0, 0, 0);
	SDL_SetPaletteColors(mSurface[0]->format->palette, mPalette->colors, 0,
			256);
	mSurface[1] = SDL_CreateRGBSurface(0, 320 * fgPanels, 200, 8, 0, 0, 0, 0);
	SDL_SetPaletteColors(mSurface[1]->format->palette, mPalette->colors, 0,
			256);

	SDL_Rect dest = { 0, 0, 0, 0 };

	if (!bgFarLeft.empty()) { // bonus texture?
		dest.x = 3;
		dest.y = -1;
		SDL_BlitSurface(bgFarLeftSurface, NULL, mSurface[0], &dest);
		dest.x -= 3;
		dest.y = 0;
		dest.x += 320;
		SDL_FreeSurface(bgFarLeftSurface);
	}
	SDL_BlitSurface(bgLeftSurface, NULL, mSurface[0], &dest);
	SDL_FreeSurface(bgLeftSurface);
	dest.x += 320;
	SDL_BlitSurface(bgRightSurface, NULL, mSurface[0], &dest);
	SDL_FreeSurface(bgRightSurface);
	SDL_SetColorKey(mSurface[0], SDL_TRUE,
			SDL_MapRGB(mSurface[0]->format, 0, 0, 0));

	SDL_BlitSurface(fgLeftSurface, NULL, mSurface[1], NULL);
	SDL_FreeSurface(fgLeftSurface);
	dest.x = 320;
	SDL_BlitSurface(fgRightSurface, NULL, mSurface[1], &dest);
	SDL_FreeSurface(fgRightSurface);
	SDL_SetColorKey(mSurface[1], SDL_TRUE,
			SDL_MapRGB(mSurface[1]->format, 0, 0, 0));

	mState = PAN_LEFT;
	mCounter = mSurface[0]->w - 320;
}

void GRAPHICS::Graphics::scrollLeft(std::vector<uint8_t> bmp) {
	Bitmap bImage(bmp);
	std::vector<uint8_t> bImageD = bImage[0];
	SDL_Surface *sImage = SDL_CreateRGBSurfaceFrom((void*) &bImageD[0],
			bImage.getWidth(0), bImage.getHeight(0), 8, bImage.getWidth(0), 0,
			0, 0, 0);
	mSurface[0] = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
	SDL_BlitSurface(sImage, NULL, mSurface[0], NULL);
	SDL_SetPaletteColors(mSurface[0]->format->palette, mPalette->colors, 0,
			256);
	SDL_SetColorKey(mSurface[0], SDL_TRUE,
			SDL_MapRGB(mSurface[0]->format, 0, 0, 0));

	mCounter = mSurface[0]->w;
	mState = SCROLL_LEFT;
}

void GRAPHICS::Graphics::materializeImage(std::vector<uint8_t> bmp) {
	Bitmap bImage(bmp);
	std::vector<uint8_t> bImageD = bImage[0];
	SDL_Surface *sImage = SDL_CreateRGBSurfaceFrom((void*) &bImageD[0],
			bImage.getWidth(0), bImage.getHeight(0), 8, bImage.getWidth(0), 0,
			0, 0, 0);
	mSurface[0] = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
	SDL_BlitSurface(sImage, NULL, mSurface[0], NULL);
	SDL_SetPaletteColors(mSurface[0]->format->palette, mPalette->colors, 0,
			256);
	SDL_SetColorKey(mSurface[0], SDL_TRUE,
			SDL_MapRGB(mSurface[0]->format, 0, 0, 0));

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

			std::vector<uint8_t> bgRight = std::get<2>(scene);
			mVideo.erase(index++);
			scene = mVideo.begin()->second;
			std::vector<uint8_t> bgLeft = std::get<2>(scene);
			mVideo.erase(index++);
			std::vector<uint8_t> bgFarLeft;
			if (bgPanels == 3) {
				scene = mVideo.begin()->second;
				bgFarLeft = std::get<2>(scene);
				mVideo.erase(index++);
			}

			scene = mVideo.begin()->second;
			//uint8_t fgPanels = std::get<1>(scene);
			std::vector<uint8_t> fgRight = std::get<2>(scene);
			mVideo.erase(index++);
			scene = mVideo.begin()->second;
			std::vector<uint8_t> fgLeft = std::get<2>(scene);
			mVideoWait = std::get<1>(scene);

			panDirection(0, bgRight, bgLeft, bgFarLeft, fgRight, fgLeft);
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
			SDL_FreeSurface(mSurface[0]);
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
			SDL_FreeSurface(mSurface[0]);
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
			SDL_FreeSurface(mSurface[0]);
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
			SDL_FreeSurface(mSurface[0]);
			SDL_FreeSurface(mSurface[1]);
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

		SDL_Surface *scaledImage = SDL_CreateRGBSurface(0, width, height, 32, 0,
				0, 0, 0);
		zoomSurfaceRGBA(mSurface[0], scaledImage);
		SDL_BlitSurface(scaledImage, NULL, mScreen, &rect);
		SDL_FreeSurface(scaledImage);
		if (mCounter == 100) {
			mState = NOOP;
			SDL_FreeSurface(mSurface[0]);
		} else
			mCounter++;
		mSleep = 10;
	}

	// Re-apply the persistent compass (in-game) so HUD/inventory redraws can't erase
	// the facing indicator the bytecode only redraws on a turn.
	restoreCompass();

	// generate texture from our screen surface
	SDL_Texture *texture = SDL_CreateTextureFromSurface(mRenderer, mScreen);

	// Clear the entire screen to our selected color.
	SDL_RenderClear(mRenderer);

	// blit texture to display
	SDL_RenderCopy(mRenderer, texture, NULL, NULL);

	// Up until now everything was drawn behind the scenes.
	SDL_RenderPresent(mRenderer);

	// cleanup
	SDL_DestroyTexture(texture);

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

/*
 * Set the hardware cursor with a specified bitmap.
 * Warning: Possible bug in SDL2, as we cannot use 8-bit RGB image.
 * We create first a 32 ARGB8888 canvas that we paint our cursor onto,
 * then we use that canvas as our hardware cursor. Otherwise we get a
 * blank image.
 */
void GRAPHICS::Graphics::loadMouse(std::vector<uint8_t> &bitmap,
		uint16_t index) {
	Bitmap image(bitmap);
	std::vector<uint8_t> cursorData = image[index];

	SDL_Surface *cursor = SDL_CreateRGBSurface(0,
			image.getWidth(index) * mScale, image.getHeight(index) * mScale, 32,
			0, 0, 0, 0);

	SDL_Surface *cImage = SDL_CreateRGBSurfaceFrom((void*) &cursorData[0],
			image.getWidth(index), image.getHeight(index), 8,
			image.getWidth(index), 0, 0, 0, 0);
	SDL_SetPaletteColors(cImage->format->palette, mPalette->colors, 0, 256);

	SDL_Surface *cImage32 = SDL_CreateRGBSurface(0, image.getWidth(index),
			image.getHeight(index), 32, 0, 0, 0, 0);
	SDL_BlitSurface(cImage, NULL, cImage32, NULL);

	zoomSurfaceRGBA(cImage32, cursor);

	SDL_SetColorKey(cursor, SDL_TRUE, SDL_MapRGB(cursor->format, 0, 0, 0));

	mCursor = SDL_CreateColorCursor(cursor, 0, 0);

	SDL_SetCursor(mCursor);

	SDL_FreeSurface(cursor);
	SDL_FreeSurface(cImage);
	SDL_FreeSurface(cImage32);
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

	SDL_FreePalette(mPalette);
	mPalette = SDL_AllocPalette(256);

	Palette basePalette(basePal);
	Palette subPalette(subPal);
	uint16_t counter = 0;

	// assign our game palette to a SDL palette
	for (uint16_t i = 0; i < basePalette.getNumOfColours(); i++) {
		if (i >= start && i <= end)
			mPalette->colors[i] = subPalette[counter++];
		else
			mPalette->colors[i] = basePalette[i];
	}
}

void GRAPHICS::Graphics::loadPalette(std::vector<uint8_t> &basePal,
		bool isRes) {
	SDL_FreePalette(mPalette);
	mPalette = SDL_AllocPalette(256);
	Palette basePalette(basePal, isRes);

	// assign our game palette to a SDL palette
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
	auto &cached = mFontCache[fontId];
	if (!cached)
		cached = std::make_shared<Font>(fontRes);
	mTextWin[wndnum].font = cached;
}

void GRAPHICS::Graphics::setTextColor(int wndnum, uint8_t color) {
	mTextWin[wndnum].fg = color;
}

void GRAPHICS::Graphics::setTextXY(int wndnum, int x, int y) {
	mTextWin[wndnum].htab = x;
	mTextWin[wndnum].vtab = y;
}

void GRAPHICS::Graphics::setTextWindow(int wndnum, int x0, int y0, int x1,
		int y1) {
	mTextWin[wndnum].winX0 = x0;
	mTextWin[wndnum].winX1 = x1;
	mTextWin[wndnum].winY0 = y0;
	mTextWin[wndnum].winY1 = y1;
	// Clear the box before text (re)draws, so old/baked text doesn't ghost.
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
		SDL_FillRect(mScreen, &r, bg);
	}
}

void GRAPHICS::Graphics::setTextJustify(int wndnum, int justify) {
	mTextWin[wndnum].justify = justify;
}

void GRAPHICS::Graphics::printText(int wndnum, const std::string &text) {
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
	auto runWidth = [&](const std::string &s) -> int {
		int w = 0;
		for (unsigned char ch : s) w += glyphW(ch);
		return w;
	};
	int lineH = 8; // these fonts are fixed-height; sample a glyph
	if (SDL_Surface *g = font->getCharacter('A')) lineH = g->h;

	const int winW = tw.winX1 - tw.winX0 + 1;

	// Greedy word-wrap into lines that fit the window width. The original AESOP
	// `print` flows text inside the bound window; we wrap on whitespace (and clip
	// to the box below). Single short strings (menu options) stay one line.
	std::vector<std::string> lines;
	{
		std::string cur, word;
		auto flushWord = [&]() {
			if (word.empty()) return;
			int wW = runWidth(word);
			int spW = cur.empty() ? 0 : glyphW(' ');
			if (!cur.empty() && runWidth(cur) + spW + wW > winW) {
				lines.push_back(cur);
				cur.clear();
			}
			if (!cur.empty()) cur += ' ';
			cur += word;
			word.clear();
		};
		for (char ch : text) {
			if (ch == ' ' || ch == '\n') {
				flushWord();
				if (ch == '\n') { lines.push_back(cur); cur.clear(); }
			} else {
				word.push_back(ch);
			}
		}
		flushWord();
		if (!cur.empty() || lines.empty()) lines.push_back(cur);
	}

	int y = tw.vtab;
	int lastX = tw.htab;
	for (size_t li = 0; li < lines.size(); ++li) {
		const std::string &line = lines[li];
		int lineW = runWidth(line);
		int x;
		if (tw.justify == 2)      // center each line in the window
			x = tw.winX0 + (winW - lineW) / 2;
		else if (tw.justify == 1) // right-align
			x = tw.winX1 - lineW;
		else                      // left: first line honours the cursor, wraps to winX0
			x = (li == 0 ? tw.htab : tw.winX0);

		if (y + lineH - 1 > tw.winY1) break; // clip below the window: stop drawing
		if (y < tw.winY0) { y += lineH; continue; } // clip above: skip this line
		for (unsigned char ch : line) {
			SDL_Surface *glyph = font->getCharacter(ch);
			if (glyph == nullptr) continue;
			if (x + glyph->w - 1 > tw.winX1) break; // clip past the right edge
			// Clip past the left edge: the original draws glyphs at (htab - x0)
			// relative to the window and VFX clips negatives, so text whose cursor
			// sits left of the window (e.g. an equipment-screen print with a
			// page-local htab) is clipped, not bled onto the screen at that x.
			if (x < tw.winX0) { x += glyph->w; continue; }
			// Glyphs are white masks (black = transparent via colour key); tint to
			// the text colour with a colour-mod multiply (white * colour = colour).
			SDL_SetSurfaceColorMod(glyph, c.r, c.g, c.b);
			SDL_Rect dst = { x, y, glyph->w, glyph->h };
			SDL_BlitSurface(glyph, NULL, mScreen, &dst);
			x += glyph->w;
		}
		lastX = x;
		y += lineH;
	}
	tw.htab = lastX;                       // cursor past the last line's text
	tw.vtab = y > tw.vtab ? y - lineH : y; // cursor on the last drawn line
}

void GRAPHICS::Graphics::saveScreenshot(const std::string &path) {
	if (SDL_SaveBMP(mScreen, path.c_str()) != 0)
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

/*!
 \brief Stripped down SDL2_gfx zoom function, if we need more we'll
 just use the library instead.

 Zooms 32 bit RGBA/ABGR 'src' surface to 'dst' surface.
 Assumes src and dst surfaces are of 32 bit depth.
 Assumes dst surface was allocated with the correct dimensions.

 \param src The surface to zoom (input).
 \param dst The zoomed surface (output).

 \return 0 for success or -1 for error.
 */
int GRAPHICS::Graphics::zoomSurfaceRGBA(SDL_Surface * src, SDL_Surface * dst) {
	/*!
	 \brief A 32 bit RGBA pixel.
	 */
	typedef struct tColorRGBA {
		Uint8 r;
		Uint8 g;
		Uint8 b;
		Uint8 a;
	} tColorRGBA;

	int x, y, sx, sy, ssx, ssy, *sax, *say, *csax, *csay, *salast, csx, csy,
			sstep, spixelgap, dgap;
	tColorRGBA *sp, *csp, *dp;

	/*
	 * Allocate memory for row/column increments
	 */
	if ((sax = (int *) malloc((dst->w + 1) * sizeof(Uint32))) == NULL) {
		return (-1);
	}
	if ((say = (int *) malloc((dst->h + 1) * sizeof(Uint32))) == NULL) {
		free(sax);
		return (-1);
	}

	/*
	 * Precalculate row increments
	 */

	sx = (int) (65536.0 * (float) (src->w) / (float) (dst->w));
	sy = (int) (65536.0 * (float) (src->h) / (float) (dst->h));

	/* Maximum scaled source size */
	ssx = (src->w << 16) - 1;
	ssy = (src->h << 16) - 1;

	/* Precalculate horizontal row increments */
	csx = 0;
	csax = sax;
	for (x = 0; x <= dst->w; x++) {
		*csax = csx;
		csax++;
		csx += sx;

		/* Guard from overflows */
		if (csx > ssx) {
			csx = ssx;
		}
	}

	/* Precalculate vertical row increments */
	csy = 0;
	csay = say;
	for (y = 0; y <= dst->h; y++) {
		*csay = csy;
		csay++;
		csy += sy;

		/* Guard from overflows */
		if (csy > ssy) {
			csy = ssy;
		}
	}

	sp = (tColorRGBA *) src->pixels;
	dp = (tColorRGBA *) dst->pixels;
	dgap = dst->pitch - dst->w * 4;
	spixelgap = src->pitch / 4;

	/*
	 * Non-Interpolating Zoom
	 */
	csay = say;
	for (y = 0; y < dst->h; y++) {
		csp = sp;
		csax = sax;
		for (x = 0; x < dst->w; x++) {
			/*
			 * Draw
			 */
			*dp = *sp;

			/*
			 * Advance source pointer x
			 */
			salast = csax;
			csax++;
			sstep = (*csax >> 16) - (*salast >> 16);
			sp += sstep;

			/*
			 * Advance destination pointer x
			 */
			dp++;
		}
		/*
		 * Advance source pointer y
		 */
		salast = csay;
		csay++;
		sstep = (*csay >> 16) - (*salast >> 16);
		sstep *= spixelgap;
		sp = csp + sstep;

		/*
		 * Advance destination pointer y
		 */
		dp = (tColorRGBA *) ((Uint8 *) dp + dgap);
	}

	/*
	 * Remove temp arrays
	 */
	free(sax);
	free(say);

	return (0);
}
