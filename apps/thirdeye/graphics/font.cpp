#include "font.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>

#include <stdio.h>
#include <string.h>

GRAPHICS::Font::Font(const std::vector<uint8_t> vec) :
		vec_(vec) {
	// AESOP/16 "2." VFX font (every EYE.RES font): a 4-byte version ("2.\0\0"),
	// then u32 char_count / char_height / font_background, a u32 offset table
	// (one entry per char), and per char a u32 pixel-width followed by
	// width*height bytes (1 byte per pixel, 0 = transparent). The older format
	// (no version string) is handled by the branch below.
	if (vec_.size() >= 16 && vec_[0] == '2' && vec_[1] == '.') {
		auto rd32 = [&](size_t o) -> uint32_t {
			return vec_[o] | (vec_[o + 1] << 8) | (vec_[o + 2] << 16) |
			       (static_cast<uint32_t>(vec_[o + 3]) << 24);
		};
		uint32_t count = rd32(4);
		uint32_t height = rd32(8);
		for (uint32_t i = 0; i < count && i < 256; i++) {
			uint32_t glyphOff = rd32(16 + i * 4);
			if (glyphOff + 4 > vec_.size())
				continue;
			uint32_t width = rd32(glyphOff);
			if (width == 0 || width > 512 || height == 0 || height > 512)
				continue;
			size_t data = glyphOff + 4;
			SDL_Surface *surf = SDL_CreateRGBSurface(0, width, height, 32, 0, 0,
			                                         0, 0);
			Uint32 white = SDL_MapRGB(surf->format, 255, 255, 255);
			for (uint32_t y = 0; y < height; y++)
				for (uint32_t x = 0; x < width; x++) {
					size_t p = data + static_cast<size_t>(y) * width + x;
					if (p < vec_.size() && vec_[p] != 0) {
						SDL_Rect r = { static_cast<int>(x), static_cast<int>(y),
						               1, 1 };
						SDL_FillRect(surf, &r, white);
					}
				}
			SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 0, 0, 0));
			character[static_cast<uint8_t>(i)] = surf;
		}
		return;
	}

	//uint16_t prev = 518;
	uint8_t characters = *reinterpret_cast<const uint16_t*>(&vec_[0]);
	uint8_t fontHeight = *reinterpret_cast<const uint16_t*>(&vec_[2]);

	for (uint16_t i = 0; i < characters; i++) {

		// find character information
		index[i] = *reinterpret_cast<const uint16_t*>(&vec_[264 + i * 2]);
		uint8_t charWidth = *reinterpret_cast<const uint16_t*>(&vec_[index[i]]);

		//std::cout << i << " " << index[i] << " " << index[i] - prev << " "
		//		<< (int) charWidth << std::endl;
		//prev = index[i];

		// create a surface to draw on
		character[i] = SDL_CreateRGBSurface(0, charWidth, fontHeight, 32, 0, 0,
				0, 0); // set to font dimensions
		Uint32 white = SDL_MapRGB(character[i]->format, 255, 255, 255); // set to white
		//Uint32 black = SDL_MapRGB(character[i]->format, 0, 0, 0); // set to black

		// set values before loop
		uint8_t counter = 2;
		uint16_t pixel = 0;

		// draw character to surface
		for (uint16_t x = 0; x < fontHeight; x++) {
			for (uint16_t y = 0; y < charWidth; y++) {
				SDL_Rect rect = { y, x, 1, 1 };
				pixel = vec_[index[i] + counter];

				/*
				if (i == 0x69)
					std::cout << "Offset: " << index[i] + counter << " Value: "
							<< pixel << "@" << rect.x << "x" << rect.y
							<< std::endl;
				*/

				if (pixel > 0) {
					SDL_FillRect(character[i], &rect, white);
				}
				counter++;
			}
		}
		// set black as our transparency colour
		SDL_SetColorKey(character[i], SDL_TRUE,
				SDL_MapRGB(character[i]->format, 0, 0, 0));
	}
}

GRAPHICS::Font::~Font() {
	std::map<uint8_t, SDL_Surface*>::iterator it;
	for (it = character.begin(); it != character.end(); it++) {
		SDL_FreeSurface(it->second);
	}
}
SDL_Surface* GRAPHICS::Font::getCharacter(uint8_t ascii) {
	return (character[ascii]);
}
