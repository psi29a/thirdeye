#ifndef PALETTE_HPP
#define PALETTE_HPP

#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>

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
	Palette(std::vector<uint8_t> vec);

	uint16_t getNumOfColours() const;
	uint16_t getOffsetColorArray() const;
	uint16_t getOffsetFadeIndexArray00() const;
	uint8_t& operator[](size_t off);
private:
	std::vector<uint8_t> vec_;
};

}
#endif //PALETTE_HPP
