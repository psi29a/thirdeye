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
	uint16_t mScale;
	SDL_Window *mWindow;
	SDL_Renderer *mRenderer;
	SDL_Surface *mScreen;
	SDL_Cursor *mCursor;
	SDL_Palette *mPalette;

public:
	Graphics(uint16_t scale);
	virtual ~Graphics();

	void drawImage(std::vector<uint8_t> bmp, uint16_t index, uint16_t posX,
			uint16_t posY, bool transparency = false);
	void drawText(std::vector<uint8_t> fnt, std::string text, uint16_t posX,
			uint16_t posY);

	void loadPalette(std::vector<uint8_t> basePal, std::vector<uint8_t> subPal,
			std::string index);
	void loadMouse(std::vector<uint8_t> bitmap, uint16_t index);

	void update();
};

}
#endif //GRAPHICS_HPP
