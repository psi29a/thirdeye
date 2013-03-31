#ifndef COMPONENTS_MEDIA_VIDEO_HPP
#define COMPONENTS_MEDIA_VIDEO_HPP

#include <SDL2/SDL.h>

namespace Utils
{
	extern bool Video_Init(void);
	extern void Video_Uninit(void);
	extern void Video_Tick(void);
	extern void Video_SetPalette(void *palette, int from, int length);
	extern void Video_Mouse_SetPosition(uint16_t x, uint16_t y);
	extern void Video_Mouse_SetRegion(uint16_t minX, uint16_t maxX, uint16_t minY, uint16_t maxY);
}

#endif /* COMPONENTS_MEDIA_VIDEO_HPP */
