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
	//SDL_SetColorKey(mScreen, SDL_TRUE,
	//		SDL_MapRGB(mScreen->format, 255, 0, 255));

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear"); // make the scaled rendering look smoother.
	SDL_RenderSetLogicalSize(mRenderer, WIDTH, HEIGHT);

	mPalette = SDL_AllocPalette(256);
	mCursor = NULL;

}

GRAPHICS::Graphics::~Graphics() {
	SDL_FreeCursor(mCursor);
	SDL_FreeSurface(mScreen);
	SDL_FreePalette(mPalette);
	SDL_DestroyRenderer(mRenderer);
	SDL_DestroyWindow(mWindow);
	SDL_Quit();
}

void GRAPHICS::Graphics::drawImage(std::vector<uint8_t> bmp, uint16_t index,
		uint16_t posX, uint16_t posY, bool transparency) {

	Bitmap image(bmp);

	SDL_Surface *surface = SDL_CreateRGBSurfaceFrom((void*) &image[index],
			image.getWidth(index), image.getHeight(index), 8,
			image.getWidth(index), 0, 0, 0, 0);

	SDL_Rect dest =
			{ posX, posY, image.getWidth(index), image.getHeight(index) };

	SDL_SetPaletteColors(surface->format->palette, mPalette->colors, 0, 256);

	if (transparency){
		SDL_SetColorKey(surface, SDL_TRUE,
				SDL_MapRGB(surface->format, 0, 0, 0));
	}
	SDL_BlitSurface(surface, NULL, mScreen, &dest);
	SDL_FreeSurface(surface);
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

void GRAPHICS::Graphics::drawText(std::vector<uint8_t> fnt, std::string text,
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

void GRAPHICS::Graphics::loadMouse(std::vector<uint8_t> bitmap,
		uint16_t index) {
	SDL_ClearError();
	Bitmap image(bitmap);

	SDL_Surface *cursor = SDL_CreateRGBSurface(0, image.getWidth(index), image.getHeight(index), 32,
                                   0,
                                   0,
                                   0,
                                   0);

	SDL_Surface *bmCursor = SDL_CreateRGBSurfaceFrom((void*) &image[index],
			image.getWidth(index), image.getHeight(index), 8,
			image.getWidth(index), 0, 0, 0, 0);

	SDL_SetPaletteColors(bmCursor->format->palette, mPalette->colors, 0, 256);

	SDL_SetColorKey(bmCursor, SDL_TRUE,
			SDL_MapRGB(bmCursor->format, 0, 0, 0));

	SDL_Surface *update = SDL_ConvertSurfaceFormat(bmCursor, SDL_PIXELFORMAT_ARGB8888, 0);
	if (!update){
		std::cout << "WINNER" << std::endl;
	}

	SDL_BlitSurface(bmCursor, NULL, cursor, NULL);
	printf("Check if failed: %s\n", SDL_GetError());
	SDL_BlitSurface(cursor, NULL, mScreen, NULL);
	printf("Check if failed: %s\n", SDL_GetError());

	SDL_SetColorKey(cursor, SDL_TRUE,
			SDL_MapRGB(cursor->format, 0, 0, 0));

	//mCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

	printf("Check if failed: %s\n", SDL_GetError());
	mCursor = SDL_CreateColorCursor(cursor, 0, 0);

	if (mCursor == NULL)
		std::cout << "We have a loser!" << std::endl;

	printf("Check if failed: %s\n", SDL_GetError());
	SDL_SetCursor(mCursor);
	//SDL_ShowCursor(1);

	printf("Check if failed: %s\n", SDL_GetError());
	std::cout << "refcount: " << bmCursor->refcount << std::endl;

	SDL_FreeSurface(bmCursor);
}

void GRAPHICS::Graphics::loadPalette(std::vector<uint8_t> basePal,
		std::vector<uint8_t> subPal, std::string index) {

	SDL_FreePalette(mPalette);
	mPalette = SDL_AllocPalette(256);

	Palette palette(basePal);

	// assign our game palette to a SDL palette
	for (uint i = 0; i < palette.getNumOfColours(); i++) {
		mPalette->colors[i] = palette[i];
	}
}
