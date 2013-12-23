#include "font.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>

#include <stdio.h>
#include <string.h>

GRAPHICS::Font::Font(const std::vector<uint8_t> vec) :
		vec_(vec) {
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
