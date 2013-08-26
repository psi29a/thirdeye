#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#define WIDTH 320
#define HEIGHT 200

#include "font.hpp"
#include "palette.hpp"
#include "bitmap.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>

namespace GRAPHICS {

class Graphics {
private:
	std::map<uint16_t, SDL_Surface*> mSurface;
	std::map<uint16_t, SDL_Palette*> mPalette;
	SDL_Window *mWindow;
	SDL_Renderer *mRenderer;
	SDL_Surface *mScreen;
	uint16_t mScale;

public:
	Graphics(uint16_t scale);
	virtual ~Graphics();
	std::vector<uint8_t> uncompressBMP(std::vector<uint8_t> bmp);
	std::vector<uint8_t> uncompressPalette(std::vector<uint8_t> basePalette,
			std::vector<uint8_t> subPalette, uint8_t start = 0, uint8_t end = 0
			);
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
