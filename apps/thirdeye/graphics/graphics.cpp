#include "graphics.hpp"

#include <iostream>
#include <stdexcept>

#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

GRAPHICS::Graphics::Graphics(uint16_t scale) {
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
		}
		std::cout << std::endl;
	} else {
		throw std::runtime_error(
				"Couldn't get window information: "
						+ std::string(SDL_GetError()));
	}

	// Create the renderer driver to be used in window
	//mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_ACCELERATED);
	mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_SOFTWARE);

	//SDL_RendererInfo* displayRendererInfo;
	//SDL_GetRendererInfo(mRenderer, displayRendererInfo);
	//std::cout << displayRendererInfo->name << std::endl;

	// Create our game screen that will be blitted to before renderering
	mScreen = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 32, 0, 0, 0, 0);

	SDL_SetColorKey(mScreen, SDL_TRUE,
			SDL_MapRGB(mScreen->format, 0, 0, 0));

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear"); // make the scaled rendering look smoother.
	SDL_RenderSetLogicalSize(mRenderer, WIDTH, HEIGHT);

	mPalette = SDL_AllocPalette(256);
	mCursor = NULL;
	mFrames = 0;
	mCounter = 0;
	mFadeIn = false;
	mFadeOut = false;
	mAlpha = 0;

}

GRAPHICS::Graphics::~Graphics() {
	SDL_FreeCursor(mCursor);
	SDL_FreeSurface(mScreen);
	SDL_FreePalette(mPalette);
	SDL_DestroyRenderer(mRenderer);
	SDL_DestroyWindow(mWindow);
	SDL_Quit();
}

void GRAPHICS::Graphics::drawImage(std::vector<uint8_t> &bmp, uint16_t index,
		uint16_t posX, uint16_t posY, bool transparency) {

	Bitmap image(bmp);
	std::vector<uint8_t> imageData = image[index];

	SDL_Surface *surface = SDL_CreateRGBSurfaceFrom((void*) &imageData[0],
			image.getWidth(index), image.getHeight(index), 8,
			image.getWidth(index), 0, 0, 0, 0);

	SDL_Rect dest =
			{ posX, posY, image.getWidth(index), image.getHeight(index) };

	//std::cout << "width: " << surface->w << " height: " << surface->h << std::endl;

	SDL_SetPaletteColors(surface->format->palette, mPalette->colors, 0, 256);

	if (transparency) {
		SDL_SetColorKey(surface, SDL_TRUE,
				SDL_MapRGB(surface->format, 0, 0, 0));
	}
	SDL_BlitSurface(surface, NULL, mScreen, &dest);
	SDL_FreeSurface(surface);
}

void GRAPHICS::Graphics::playAnimation(std::vector<uint8_t> animationData) {
	Bitmap animation(animationData);
	mFrames = animation.getNumberOfBitmaps();
	mCounter = 0;
	mVideo = animationData;
}

void GRAPHICS::Graphics::fadeIn() {
	mFadeIn = True;
	mAlpha = 0;
}

void GRAPHICS::Graphics::update() {

	// anything in our animation queue to display?
	if (mFrames > 0 and mFrames > mCounter) {
		bool transparency = true;
		//if (mCounter == 0)	// only first frame is is not transparent
		//	transparency = false;

		drawImage(mVideo, mCounter, 0, 0, transparency);
		std::cout << "  Frame: " << (int) mCounter << std::endl;
		mCounter++;
	}

	// are we fading in or out?
	if (mFadeIn or mFadeOut){
		if (mFadeIn and mAlpha > SDL_ALPHA_OPAQUE){
			mFadeIn = false;
			mAlpha = SDL_ALPHA_OPAQUE;
		}
		if (mFadeOut and mAlpha < SDL_ALPHA_TRANSPARENT){
			mFadeOut = false;
			mAlpha = SDL_ALPHA_TRANSPARENT;
		}
		int value = SDL_SetSurfaceAlphaMod(mScreen, mAlpha);
		printf("Sleeping: %d - %d  \n", value, mAlpha);
		(mFadeIn) ? mAlpha+=10 : mAlpha-=10;
	}

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

	boost::char_separator<char> sep(",");
	boost::tokenizer<boost::char_separator<char> > tokens(index, sep);

	boost::tokenizer<boost::char_separator<char> >::iterator it =
			tokens.begin();
	it++;
	uint16_t start = boost::lexical_cast<uint16_t>(it.current_token());
	it++;
	uint16_t end = boost::lexical_cast<uint16_t>(it.current_token());

	uint8_t counter = 0;
	std::cout << "text: " << start << " " << end << std::endl;

	SDL_FreePalette(mPalette);
	mPalette = SDL_AllocPalette(256);

	Palette basePalette(basePal);
	Palette subPalette(subPal);

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
