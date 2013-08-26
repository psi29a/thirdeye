#ifndef BITMAP_HPP
#define BITMAP_HPP

#define BMPHEADEROFFSET 14

#include <stdint.h>
#include <vector>
#include <map>
#include <stdio.h>
#include <iostream>
#include <SDL2/SDL.h>

namespace GRAPHICS {

/*
 struct BMP
 {
 uint16_t fileSize;
 uint16_t unknown1;
 uint16_t numberOfSubImages;
 uint16_t unknown3;
 uint16_t unknown4;
 uint16_t width;
 uint16_t height;
 uint8_t compressedData[0];
 };
 */

class Bitmap {
public:
	Bitmap(std::vector<uint8_t> vec);
	uint8_t& operator[](size_t off);
	uint16_t getFilesize() const;
	uint16_t getWidth() const;
	uint16_t getHeight() const;
	uint16_t getNumberOfBitmaps() const;
	std::map<uint16_t, uint32_t> getBitmapOffsets() const;
	std::vector<uint8_t> getBitmap(uint8_t);
private:
	std::vector<uint8_t> vec_;
	std::map<uint16_t, SDL_Surface*> subBitmap;
};

}
#endif //BITMAP_HPP
