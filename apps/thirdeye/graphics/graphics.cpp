#include "graphics.hpp"

#include <iostream>
#include <stdexcept>

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

	//SDL_GetRendererInfo(mRenderer, &displayRendererInfo);

	// Create our game screen that will be blitted to before renderering
	mScreen = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 32, 0, 0, 0, 0);

	// set magenta as our transparent colour
	SDL_SetColorKey(mScreen, SDL_TRUE,
			SDL_MapRGB(mScreen->format, 255, 0, 255));

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear"); // make the scaled rendering look smoother.
	SDL_RenderSetLogicalSize(mRenderer, WIDTH, HEIGHT);

}

GRAPHICS::Graphics::~Graphics() {
	SDL_FreeSurface(mSurface[0]);
	SDL_FreeSurface(mScreen);
	SDL_FreePalette(mPalette[0]);
	SDL_DestroyRenderer(mRenderer);
	SDL_DestroyWindow(mWindow);
	SDL_Quit();
}

void GRAPHICS::Graphics::drawImage(uint16_t surfaceId, std::vector<uint8_t> bmp,
		std::vector<uint8_t> pal, uint16_t posX, uint16_t posY, uint16_t width,
		uint16_t height, bool sprite, bool transparency) {

	SDL_Color magenta = { 255, 0, 255, 0 };
	Bitmap image(bmp);
	Palette palette(pal);
	std::vector<uint8_t> sub;

	//return;
	mSurface[surfaceId] = SDL_CreateRGBSurfaceFrom((void*) &image[0],
			image.getWidth(0), image.getHeight(0), 8, image.getWidth(0), 0, 0,
			0, 0);
	mPalette[surfaceId] = SDL_AllocPalette(256);

	std::cout << "TEST: " << mSurface[surfaceId]->h << " "
			<< mSurface[surfaceId]->w << std::endl;

	// assign our game palette to a SDL palette
	for (uint i = 0; i < palette.getNumOfColours(); i++) {
		mPalette[surfaceId]->colors[i] = palette[i];

		// Handle our black transparency and replace it with magenta
		if (!sprite && transparency && palette[i].r == 0 && palette[i].g == 0
				&& palette[i].b == 0) {
			mPalette[surfaceId]->colors[i] = magenta;
		}
	}

	SDL_SetPaletteColors(mSurface[surfaceId]->format->palette,
			mPalette[surfaceId]->colors, 0, 256);

	SDL_BlitSurface(mSurface[surfaceId], NULL, mScreen, NULL);
}

SDL_Surface* GRAPHICS::Graphics::getSurface(uint16_t surfaceId) {
	return mSurface[surfaceId];
}

void GRAPHICS::Graphics::update() {

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

void GRAPHICS::Graphics::loadFont(std::vector<uint8_t> fnt) {
	Font font(fnt);
	SDL_Rect rect = { 8, 181, 8, 8 }; // start at 8x180 with 8x8 pixels
	std::string text = "Welcome to Thirdeye!";
	//std::string text = "ABDCEFGHIJKLMNOPQRSTUVWXYZ";
	//std::string text = "abcdefghijklmnopqrstuvwxyz";
	//std::string text = "0123456789!@#$%^&*()[]{}\\/?<>.,:;'\"-=_+";
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
