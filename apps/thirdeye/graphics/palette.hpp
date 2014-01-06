#ifndef PALETTE_HPP
#define PALETTE_HPP

#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>

#include "SDL.h"

#define PALHEADEROFFSET 26

namespace GRAPHICS {

/*
 struct PAL_HEADER
 {
 uint16_t numColors;
 uint16_t offsetColorArray;
 uint16_t offsetFadeIndexArray00;
 uint16_t offsetFadeIndexArray10;
 uint16_t offsetFadeIndexArray20;
 uint16_t offsetFadeIndexArray30;
 uint16_t offsetFadeIndexArray40;
 uint16_t offsetFadeIndexArray50;
 uint16_t offsetFadeIndexArray60;
 uint16_t offsetFadeIndexArray70;
 uint16_t offsetFadeIndexArray80;
 uint16_t offsetFadeIndexArray90;
 uint16_t offsetFadeIndexArray100;
 uint8_t paletteData[0];
 }
 */

class Palette {
public:
	Palette(const std::vector<uint8_t> &pal, bool isRes = true);
	uint16_t getNumOfColours() const;
	uint16_t getColorArray() const;
	uint16_t getFadeIndexArray00() const;
	const SDL_Color& operator[](uint16_t index);
private:
	uint16_t mNumOfColours;
	uint16_t mColorArray;
	uint16_t mFadeIndexArray00;
	std::map<uint8_t,SDL_Color> mPalette;
};

}
#endif //PALETTE_HPP
