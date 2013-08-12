#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#define headerBMP 14
#define headerPalette 26

#include <SDL2/SDL.h>

#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>

namespace GRAPHICS {

/*
 //
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
}
 */

class Palette
{
public:
	Palette(std::vector<uint8_t> vec)
      : vec_(vec)
{}

uint16_t getNumOfColours() const {
   return *reinterpret_cast<const uint16_t*>(&vec_[0]);
}

uint16_t getOffsetColorArray() const {
   return *reinterpret_cast<const uint16_t*>(&vec_[1]);
}

uint16_t getOffsetFadeIndexArray00() const {
   return *reinterpret_cast<const uint16_t*>(&vec_[2]);
}

uint8_t& operator[](size_t off){
	if (off > vec_.size() - headerPalette){
		std::cerr << "Trying to access BMP data out of bounds." << std::endl;
		throw;
	}
	//std::cout << "offset @: " << off << std::endl;
    return vec_[off + headerPalette];
}
private:
   std::vector<uint8_t> vec_;
};


class BMP
{
public:
   BMP(std::vector<uint8_t> vec)
      : vec_(vec)
{}

uint16_t getFilesize() const {
   return *reinterpret_cast<const uint16_t*>(&vec_[0]);
}

uint16_t getWidth() const {
   return *reinterpret_cast<const uint16_t*>(&vec_[5*2]);
}

uint16_t getHeight() const {
   return *reinterpret_cast<const uint16_t*>(&vec_[6*2]);
}

uint8_t& operator[](size_t off){
	if (off > vec_.size() - headerBMP){
		std::cerr << "Trying to access PAL data out of bounds." << std::endl;
		throw;
	}
	//std::cout << "offset @: " << off << std::endl;
    return vec_[off + headerBMP];
}

private:
   std::vector<uint8_t> vec_;
};

class Graphics {
private:

public:
	Graphics();
	virtual ~Graphics();
	std::vector<uint8_t> uncompressBMP(std::vector<uint8_t> bmp);
	std::vector<uint8_t> uncompressPalette(std::vector<uint8_t> pal);
	void getFont();
};

}
#endif //GRAPHICS_HPP
