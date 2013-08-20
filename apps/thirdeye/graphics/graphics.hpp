#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#define headerBMP 14
#define headerPalette 26

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <map>
#include <vector>
#include <stdint.h>
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

/*
struct Font
{
   int version;
   int char_count;
   int char_height;
   int font_background;
}
 */

class Font {
public:
	Font(std::vector<uint8_t> vec) :
		vec_(vec) {
		uint16_t prev = 518;
		uint8_t width = 0;
		for (uint16_t i = 0; i < 128; i++){
			index[i] = *reinterpret_cast<const uint16_t*>(&vec_[264+i*2]);
			width = *reinterpret_cast<const uint16_t*>(&vec_[index[i]]);
			std::cout <<  i << " " << index[i]
			     << " " << index[i] - prev
			     << " " << (int) width
			     << std::endl;
			prev = index[i];

			uint8_t counter = 2;
			uint16_t pixel = 0;
			while (counter-2 < width * 8){
				if ( (counter-2) % width == 0)
					std::cout << std::endl;
				pixel = vec_[index[i]+counter];
				std::cout << std::hex << pixel;
				counter++;

			}
			std::cout << std::endl;
		}
	}

	uint8_t& operator[](size_t off) {
		if (off > vec_.size() - 264) {
			std::cerr << "Trying to access FONT data out of bounds."
					<< std::endl;
			throw;
		}
		return vec_[off];
	}

	uint16_t getOffset() const {
		return index[0];
		//return *reinterpret_cast<const uint16_t*>(&vec_[264]);
	}
private:
	std::vector<uint8_t> vec_;
	uint16_t index[128];
};

class Palette {
public:
	Palette(std::vector<uint8_t> vec) :
			vec_(vec) {
	}

	uint16_t getNumOfColours() const {
		return *reinterpret_cast<const uint16_t*>(&vec_[0]);
	}

	uint16_t getOffsetColorArray() const {
		return *reinterpret_cast<const uint16_t*>(&vec_[1]);
	}

	uint16_t getOffsetFadeIndexArray00() const {
		return *reinterpret_cast<const uint16_t*>(&vec_[2]);
	}

	uint8_t& operator[](size_t off) {
		if (off > vec_.size() - headerPalette) {
			std::cerr << "Trying to access PAL data out of bounds."
					<< std::endl;
			throw;
		}
		//std::cout << "offset @: " << off << std::endl;
		return vec_[off + headerPalette];
	}
private:
	std::vector<uint8_t> vec_;
};

class Bitmap {
public:
	Bitmap(std::vector<uint8_t> vec) :
			vec_(vec) {
	}

	uint16_t getFilesize() const {
		return *reinterpret_cast<const uint16_t*>(&vec_[0]);
	}

	uint16_t getWidth() const {
		return *reinterpret_cast<const uint16_t*>(&vec_[5 * 2]);
	}

	uint16_t getHeight() const {
		return *reinterpret_cast<const uint16_t*>(&vec_[6 * 2]);
	}

	uint8_t& operator[](size_t off) {
		if (off > vec_.size() - headerBMP) {
			std::cerr << "Trying to access BMP data out of bounds."
					<< std::endl;
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
	std::map<uint16_t, SDL_Surface*> surface;
	std::map<uint16_t, SDL_Palette*> surfacePalette;
	SDL_Window 		*window;
	SDL_Renderer 	*renderer;
	SDL_Surface 	*screen;

public:
	Graphics();
	virtual ~Graphics();
	std::vector<uint8_t> uncompressBMP(std::vector<uint8_t> bmp);
	std::vector<uint8_t> uncompressPalette(std::vector<uint8_t> pal);
	void drawImage(uint16_t surfaceId, std::vector<uint8_t> bmp,
			std::vector<uint8_t> pal, uint16_t posX, uint16_t posY,
			uint16_t width, uint16_t height, bool sprite, bool transparency);
	void getFont();
	SDL_Surface* getSurface(uint16_t);
	void update();
	void testFont(std::vector<uint8_t>);
};

}
#endif //GRAPHICS_HPP
