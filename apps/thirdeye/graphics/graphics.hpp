#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#define WIDTH 320
#define HEIGHT 200

#include "font.hpp"
#include "palette.hpp"
#include "bitmap.hpp"
#include "../resources/gffi.hpp"

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
	uint8_t mState;
	int16_t mAlpha;
	uint16_t mFrames;
	uint16_t mCounter;
	uint32_t mClock;
	uint32_t mRunningClock;
	uint32_t mVideoWait;
	std::vector<uint8_t> mBuffer;
	std::map<uint8_t, SDL_Surface*> mSurface;
	std::map<uint8_t, tuple<uint8_t, uint8_t, std::vector<uint8_t> > > mVideo;
	int zoomSurfaceRGBA(SDL_Surface * src, SDL_Surface * dst);

public:
	Graphics(uint16_t scale = 1, bool renderer = false);
	virtual ~Graphics();

	void drawImage(std::vector<uint8_t> &bmp, uint16_t index, uint16_t posX,
			uint16_t posY, bool transparency = false);
	void drawText(std::vector<uint8_t> &fnt, std::string text, uint16_t posX,
			uint16_t posY);

	void playVideo(RESOURCES::GFFI gffi);
	void stopVideo();
	bool isVideoPlaying();

	void fadeIn();
	void playAnimation(std::vector<uint8_t> video);
	void panDirection(uint8_t panDir, std::vector<uint8_t> bgRight,
			std::vector<uint8_t> bgLeft, std::vector<uint8_t> bgFarLeft,
			std::vector<uint8_t> fgRight, std::vector<uint8_t> fgLeft);
	void drawCurtain(std::vector<uint8_t> bmp);

	void loadPalette(std::vector<uint8_t> &basePal, bool isRes = true);
	void loadPalette(std::vector<uint8_t> &basePal,
			std::vector<uint8_t> &subPal, std::string index);

	void loadMouse(std::vector<uint8_t> &bitmap, uint16_t index);

	void update();
};

}
#endif //GRAPHICS_HPP
