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
 uint16_t numberOfCharacters;
 uint16_t charHeight;
 }
 */

class Font {
public:
	Font(std::vector<uint8_t> vec);
	virtual ~Font();
	SDL_Surface* getCharacter(uint8_t ascii);
private:
	std::vector<uint8_t> vec_;
	std::map<uint8_t, SDL_Surface*> character;
	std::map<uint8_t, uint16_t> index;
};

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

class Graphics {
private:
	std::map<uint16_t, SDL_Surface*> surface;
	std::map<uint16_t, SDL_Palette*> surfacePalette;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Surface *screen;

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
	void loadFont(std::vector<uint8_t>);
};

}
#endif //GRAPHICS_HPP
