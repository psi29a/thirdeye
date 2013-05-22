#ifndef COMPONENTS_GAME_EOB2_HPP
#define COMPONENTS_GAME_EOB2_HPP

#include <SDL2/SDL.h> // temp
#include <components/utils/fileread.hpp>

namespace Games
{
	const uint16_t EOB2_WIDTH = 320;
	const uint16_t EOB2_HEIGHT = 200;

	bool gameInit( SDL_Surface *sdlSurface[] );
	bool drawImage(SDL_Surface *surface, SDL_Palette *palette, uint8_t *cpsFile, uint16_t posX = 0, uint16_t posY = 0, uint16_t width = EOB2_WIDTH, uint16_t height = EOB2_HEIGHT, bool sprite = false);
}

#endif /* COMPONENTS_GAME_EOB2_HPP */
