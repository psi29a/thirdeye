#ifndef BITMAP_HPP
#define BITMAP_HPP

#define BMPHEADEROFFSET 14

#include <stdint.h>
#include <vector>
#include <stdio.h>
#include <iostream>

namespace GRAPHICS {

/*
 struct BMP
 {
 uint16_t fileSize;
 uint16_t unknown1;
 uint16_t unknown2;
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
	uint16_t getFilesize() const;
	uint16_t getWidth() const;
	uint16_t getHeight() const;
	uint8_t& operator[](size_t off);
private:
	std::vector<uint8_t> vec_;
};

}
#endif //BITMAP_HPP
