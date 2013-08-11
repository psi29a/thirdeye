#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#define headerBMP 14

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
		std::cerr << "Trying to access BMP data out of bounds." << std::endl;
		throw;
	}
	std::cout << "offset @: " << off << std::endl;
    return vec_[off + headerBMP];
}

// provide accessors that retrieve the header fields by casting the vector entries
private:
   std::vector<uint8_t> vec_;
};

class Graphics {
private:

public:
	Graphics();
	virtual ~Graphics();
	std::vector<uint8_t> getBMP(std::vector<uint8_t> bmp);
	void getFont();
};

}
#endif //GRAPHICS_HPP
