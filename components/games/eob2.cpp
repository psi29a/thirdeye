#include "eob2.hpp"

namespace Games
{

bool gameInit( SDL_Surface *sdlSurface[] )
{
	// load all images as surfaces
	SDL_Surface *surface = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);

	boost::filesystem3::path playfldCPSPath = "/opt/eob2/PLAYFLD.CPS";
	boost::filesystem3::path decorateCPSPath = "/opt/eob2/DECORATE.CPS";
	boost::filesystem3::path thrownCPSPath = "/opt/eob2/THROWN.CPS";
	boost::filesystem3::path silverPALPath = "/opt/eob2/SILVER.PAL";

	uint8_t playfldImage[Utils::EOB2_IMAGE_SIZE] = {};
	Utils::getImageFromCPS(playfldImage, playfldCPSPath);
	uint8_t decorateImage[Utils::EOB2_IMAGE_SIZE] = {};
	Utils::getImageFromCPS(decorateImage, decorateCPSPath);
	uint8_t thrownImage[Utils::EOB2_IMAGE_SIZE] = {};
	Utils::getImageFromCPS(thrownImage, thrownCPSPath);

	// Set palette for surface
    SDL_Palette* sdlPalette = SDL_AllocPalette(256);
	Utils::getPaletteFromPAL(sdlPalette, silverPALPath, true); // grab palette and convert to SDLPalette
	SDL_SetPaletteColors(surface->format->palette, sdlPalette->colors, 0, 256);
	drawImage(surface, sdlPalette, playfldImage);

    sdlSurface[0] = surface;
	return true;
}

bool drawImage(SDL_Surface *surface, SDL_Palette *palette, uint8_t *cpsImage, uint16_t posX, uint16_t posY, uint16_t width, uint16_t height, bool sprite)
{
	SDL_Rect dstrect;
	int count = 0;
	for(int h=0; h<height; h++)
	{
		for(int w=0; w<width; w++)
		{
			dstrect.h = 1; dstrect.w = 1; dstrect.x = w; dstrect.y = h;
			if(sprite && palette->colors[cpsImage[count]].r == 0 && palette->colors[cpsImage[count]].g == 0 && palette->colors[cpsImage[count]].b == 0)
			{
				bool rightfree = true, leftfree = true, topfree = true, bottomfree = true;

				for(int x=1; x<=8; x++)
				{
					if(count - x >= 0 && count - x <= 63999)
						if(palette->colors[cpsImage[count - x]].r != 0 || palette->colors[cpsImage[count - x]].g != 0 || palette->colors[cpsImage[count - x]].b != 0)
							leftfree = false;
					if(count + x >= 0 && count + x <= 63999)
						if(palette->colors[cpsImage[count + x]].r != 0 || palette->colors[cpsImage[count + x]].g != 0 || palette->colors[cpsImage[count + x]].b != 0)
							rightfree = false;
				}
				for(int x=1; x<=32; x++)
				{
					if(count - (320*x) >= 0 && count - (320*x) <= 63999)
						if(palette->colors[cpsImage[count - (320*x)]].r != 0 || palette->colors[cpsImage[count - (320*x)]].g != 0 || palette->colors[cpsImage[count - (320*x)]].b != 0)
							topfree = false;
					if(count + (320*x) >= 0 && count + (320*x) <= 63999)
						if(palette->colors[cpsImage[count + (320*x)]].r != 0 || palette->colors[cpsImage[count + (320*x)]].g != 0 || palette->colors[cpsImage[count + (320*x)]].b != 0)
							bottomfree = false;
				}

				if(!rightfree && !leftfree && !topfree && !bottomfree)
					SDL_FillRect(surface, &dstrect, 255);
				else
					SDL_FillRect(surface, &dstrect, cpsImage[count]);
			}
			else
				SDL_FillRect(surface, &dstrect, cpsImage[count]);
			count++;
		}
	}

	return true;
}

}
