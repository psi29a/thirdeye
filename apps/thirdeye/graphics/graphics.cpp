#include "graphics.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>

#include <stdio.h>
#include <string.h>

GRAPHICS::Graphics::Graphics() {
	std::cout << "Initializing SDL... ";
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
	window = SDL_CreateWindow("Thirdeye", SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED, 320, 200, 0    //SDL_WINDOW_SHOWN
			);

	if (SDL_GetWindowWMInfo(window, &info)) { // the call returns true on success
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
	//renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

	//SDL_GetRendererInfo(renderer, &displayRendererInfo);

	// Create our game screen that will be blitted to before renderering
	screen = SDL_CreateRGBSurface(0, 320, 200, 32, 0, 0, 0, 0);

	// set magenta as our transparent colour
	SDL_SetColorKey(screen, SDL_TRUE, SDL_MapRGB(screen->format, 255, 0, 255));

}

GRAPHICS::Graphics::~Graphics() {
	SDL_FreeSurface(surface[0]);
	SDL_FreeSurface(screen);
	SDL_FreePalette(surfacePalette[0]);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

std::vector<uint8_t> GRAPHICS::Graphics::uncompressBMP(
		std::vector<uint8_t> bmp) {
	Bitmap image(bmp);
	std::vector<uint8_t> indexedBitmap;

	std::cout << "BMP Info: " << std::endl << " " << image.getFilesize() << " "
			<< image.getWidth() << " " << image.getHeight() << " " << bmp.size()
			<< " " << (int) image[0] << std::endl;

	uint16_t nrSubPictures = bmp[4] | (bmp[5] << 8);
	std::cout << nrSubPictures << " sub picture(s) found." << std::endl;

	std::map<uint16_t, uint32_t> startOffsets;
	for (uint32_t i = 0; i < nrSubPictures; i++) {
		startOffsets[i] = bmp[6 + i * 4 + 0] | (bmp[6 + i * 4 + 1] << 8)
				| (bmp[6 + i * 4 + 2] << 16) | (bmp[6 + i * 4 + 3] << 24);
		std::cout << "Sub picture " << i << " starts at offset "
				<< startOffsets[i] << std::endl;

		unsigned int pos = startOffsets[i];

		int width = bmp[pos + 0] | (bmp[pos + 1] << 8);
		int height = bmp[pos + 2] | (bmp[pos + 3] << 8);
		pos += 4;

		std::cout << "   Size is " << width << " x " << height << std::endl;

		//unsigned char* indexedBitmap=new unsigned char[width*height];
		indexedBitmap.resize(width * height);

		memset(&indexedBitmap[0], 0, width * height);// Default bgcolor??? Probably defined in the header...

		while (true) {
			int y = bmp[pos];
			if (y == 0xff)
				break;

			if ((y < 0) || (y >= height)) {
				printf("Probably out of sync. Reported y-coord: %d\n", y);
				throw;
			}
			pos++;

			while (true) {
				int x = bmp[pos];
				pos++;

				int islast = bmp[pos];
				pos++;

				int rle_width = bmp[pos];
				pos++;

				//int rle_bytes=bmp[pos];
				pos++;

				while (rle_width > 0) {
					int mode = bmp[pos] & 1;
					int amount = (bmp[pos] >> 1) + 1;
					pos++;

					if (mode == 0)	// Copy
							{
						memcpy(&indexedBitmap[0] + x + y * width, &bmp[0] + pos,
								amount);
						pos += amount;
					} else if (mode == 1) // Fill
							{
						int value = bmp[pos];
						pos++;
						memset(&indexedBitmap[0] + x + y * width, value,
								amount);
					}
					x += amount;
					rle_width -= amount;
				}

				if (rle_width != 0) {
					printf("Out of sync while depacking RLE (rle_width=%d).\n",
							rle_width);
					throw;
				}

				if (islast == 0x80)
					break;
			}
		}

		std::ofstream osss("/tmp/backdrop_2.bmp", std::ios::binary);
		osss.write((const char*) &indexedBitmap[0], width * height);
		osss.close();
	}

	return indexedBitmap;
}

std::vector<uint8_t> GRAPHICS::Graphics::uncompressPalette(
		std::vector<uint8_t> pal) {
	Palette palette(pal);
	std::vector<uint8_t> fullPalette(pal.size() - headerPalette);

	for (uint16_t i = 0; i < fullPalette.size(); i++) {
		fullPalette[i] = palette[i];
	}

	return fullPalette;
}

void GRAPHICS::Graphics::drawImage(uint16_t surfaceId, std::vector<uint8_t> bmp,
		std::vector<uint8_t> pal, uint16_t posX, uint16_t posY, uint16_t width,
		uint16_t height, bool sprite = true, bool transparency = true) {

	std::vector<uint8_t> bitmap = uncompressBMP(bmp);
	std::vector<uint8_t> palette = uncompressPalette(pal);

	surface[surfaceId] = SDL_CreateRGBSurfaceFrom(&bitmap[0], 320, 200, 8, 320,
			0, 0, 0, 0);
	surfacePalette[surfaceId] = SDL_AllocPalette(256);

	// assign our game palette to a SDL palette
	uint16_t counter = 0;
	uint16_t size = 768;
	for (uint i = 0; i < size; i += 3) {
		// Bitshift from 6 bits (64 colours) to 8 bits (256 colours that is in our palette
		surfacePalette[surfaceId]->colors[counter].r = palette[i] << 2;
		surfacePalette[surfaceId]->colors[counter].g = palette[i + 1] << 2;
		surfacePalette[surfaceId]->colors[counter].b = palette[i + 2] << 2;

		// Handle our black transparency and replace it with magenta
		if (!sprite && transparency
				&& surfacePalette[surfaceId]->colors[counter].r == 0
				&& surfacePalette[surfaceId]->colors[counter].g == 0
				&& surfacePalette[surfaceId]->colors[counter].b == 0) {
			surfacePalette[surfaceId]->colors[counter].r = 255;
			surfacePalette[surfaceId]->colors[counter].g = 0;
			surfacePalette[surfaceId]->colors[counter].b = 255;
		}

		// Debug information
		//printf(" Byte %d has this %x\n",i, bytePAL[i]);
		//printf("%u colour: %u,%u,%u\n", counter, palette->colors[counter].r, palette->colors[counter].g, palette->colors[counter].b);
		counter++;
	}

	SDL_SetPaletteColors(surface[surfaceId]->format->palette,
			surfacePalette[surfaceId]->colors, 0, 256);

	SDL_BlitSurface(surface[surfaceId], NULL, screen, NULL);
}

SDL_Surface* GRAPHICS::Graphics::getSurface(uint16_t surfaceId) {
	return surface[surfaceId];
}

void GRAPHICS::Graphics::update() {

	// generate texture from our screen surface
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, screen);

	// Clear the entire screen to our selected color.
	SDL_RenderClear(renderer);

	// blit texture to display
	SDL_RenderCopy(renderer, texture, NULL, NULL);

	// Up until now everything was drawn behind the scenes.
	SDL_RenderPresent(renderer);

	// cleanup
	SDL_DestroyTexture(texture);
}

void GRAPHICS::Graphics::loadFont(std::vector<uint8_t> fnt) {
	Font font(fnt);
	SDL_Rect rect = { 8, 181, 8, 8 }; // start at 8x180 with 8x8 pixels
	//std::string text = "Welcome to Thirdeye!";
	//std::string text = "ABDCEFGHIJKLMNOPQRSTUVWXYZ";
	std::string text = "abcdefghijklmnopqrstuvwxyz";
	//std::string text = "0123456789!@#$%^&*()[]{}\\/?<>.,:;'\"-=_+";
	//std::string text = "hij1";
	std::string::iterator it = text.begin();

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

	while (it != text.end()) {
		int ascii = (unsigned char) *it;
		SDL_BlitSurface(font.getCharacter(ascii), NULL, screen, &rect);
		it++;
		rect.x += 8;
	}
}

GRAPHICS::Font::Font(std::vector<uint8_t> vec) :
		vec_(vec) {
	uint16_t prev = 518;
	uint8_t characters = *reinterpret_cast<const uint16_t*>(&vec_[0]);
	uint8_t fontHeight = *reinterpret_cast<const uint16_t*>(&vec_[2]);
	uint8_t fontWidth = 8;
	uint8_t charWidth = 0;

	for (uint16_t i = 0; i < characters; i++) {

		// find character information
		index[i] = *reinterpret_cast<const uint16_t*>(&vec_[264 + i * 2]);
		charWidth = *reinterpret_cast<const uint16_t*>(&vec_[index[i]]);

		//std::cout << i << " " << index[i] << " " << index[i] - prev << " "
		//		<< (int) charWidth << std::endl;

		prev = index[i];

		// create a surface to draw on
		character[i] = SDL_CreateRGBSurface(0, fontWidth, fontHeight, 32, 0, 0,
				0, 0); // set to font dimensions
		Uint32 white = SDL_MapRGB(character[i]->format, 255, 255, 255); // set to white
		Uint32 black = SDL_MapRGB(character[i]->format, 0, 0, 0); // set to black

		// set values before loop
		uint8_t counter = 2;
		uint8_t counterX = 2;
		uint8_t counterY = 0;
		uint16_t pixel = 0;
		SDL_Rect rect = { 0, 0, 1, 1 }; // start at 0x0 with 1 pixel

		// draw character to surface
		for (uint16_t x = 0; x < fontHeight; x++) {
			for (uint16_t y = 0; y < charWidth; y++) {
				SDL_Rect rect = { y, x, 1, 1 };
				pixel = vec_[index[i] + counter];

				if (i == 0x69)
					std::cout << "Offset: " << index[i] + counter << " Value: "
							<< pixel << "@" << rect.x << "x" << rect.y
							<< std::endl;

				if (pixel > 0) {
					SDL_FillRect(character[i], &rect, white);
				}
				counter++;
			}
		}

		/*

		 while (counterX - 2 < charWidth * fontHeight) {
		 rect.x = (counterX - 2) % charWidth;
		 rect.y = counterY % fontHeight;

		 //if (rect.x == 0 && (counterX - 2))
		 //std::cout << std::endl;

		 pixel = vec_[index[i] + counterX];

		 if (i == 0x69)
		 std::cout << "Value: " << pixel << "@" << rect.x << "x" << rect.y << std::endl;

		 if (pixel > 0){
		 SDL_FillRect(character[i], &rect, white);
		 }
		 //std::cout << std::hex << pixel;
		 counterX++;

		 if (rect.x == 0 && (counterX) >= fontHeight)
		 counterY++;
		 }
		 //std::cout << std::endl;

		 */
	}
}

GRAPHICS::Font::~Font() {
	std::map<uint8_t, SDL_Surface*>::iterator it;
	for (it = character.begin(); it != character.end(); it++) {
		SDL_FreeSurface(it->second);
	}
}
SDL_Surface* GRAPHICS::Font::getCharacter(uint8_t ascii) {
	return character[ascii];
}

GRAPHICS::Palette::Palette(std::vector<uint8_t> vec) :
		vec_(vec) {
}

uint16_t GRAPHICS::Palette::getNumOfColours() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[0]);
}

uint16_t GRAPHICS::Palette::getOffsetColorArray() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[1]);
}

uint16_t GRAPHICS::Palette::getOffsetFadeIndexArray00() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[2]);
}

uint8_t& GRAPHICS::Palette::operator[](size_t off) {
	if (off > vec_.size() - headerPalette) {
		std::cerr << "Trying to access PAL data out of bounds." << std::endl;
		throw;
	}
//std::cout << "offset @: " << off << std::endl;
	return vec_[off + headerPalette];
}

GRAPHICS::Bitmap::Bitmap(std::vector<uint8_t> vec) :
		vec_(vec) {
}

uint16_t GRAPHICS::Bitmap::getFilesize() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[0]);
}

uint16_t GRAPHICS::Bitmap::getWidth() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[5 * 2]);
}

uint16_t GRAPHICS::Bitmap::getHeight() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[6 * 2]);
}

uint8_t& GRAPHICS::Bitmap::operator[](size_t off) {
	if (off > vec_.size() - headerBMP) {
		std::cerr << "Trying to access BMP data out of bounds." << std::endl;
		throw;
	}
//std::cout << "offset @: " << off << std::endl;
	return vec_[off + headerBMP];
}
