#include "eob2.hpp"

#include <map>
#include <vector>
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

namespace Games
{

bool gameInit( SDL_Surface *sdlSurface[] )
{
	boost::filesystem::path playfldCPSPath = "/opt/eob2/PLAYFLD.CPS";
	boost::filesystem::path decorateCPSPath = "/opt/eob2/DECORATE.CPS";
	boost::filesystem::path thrownCPSPath = "/opt/eob2/THROWN.CPS";
	boost::filesystem::path silverPALPath = "/opt/eob2/SILVER.PAL";

	std::map<char*, SDL_Surface*> surfaces;
	boost::filesystem::path path = "/opt/eob2";

	// get our palette
    SDL_Palette* sdlPalette = SDL_AllocPalette(256);
	Utils::getPaletteFromPAL(sdlPalette, silverPALPath, true); // grab palette and convert to SDLPalette

	uint8_t cpsImage[Utils::EOB2_IMAGE_SIZE] = {};
	uint16_t counter = 0;
	if ( boost::filesystem::exists(path) && boost::filesystem::is_directory(path)){
	    std::cout << "\nIn directory: "
	              << path << "\n\n";


	    boost::filesystem::directory_iterator end_iter;
    	std::string filename = "";
    	std::string ext = "";
        typedef std::vector<boost::filesystem::path> vec;	// store paths,
        vec v;                       						// so we can sort them later

        std::copy(boost::filesystem::directory_iterator(path), boost::filesystem::directory_iterator(), back_inserter(v));

        std::sort(v.begin(), v.end());	// sort, since directory iteration
                                        // is not ordered on some file systems

        for (vec::const_iterator it(v.begin()), it_end(v.end()); it != it_end; ++it)
        {
        	filename = boost::to_upper_copy(it->stem().string());
        	ext = boost::to_upper_copy(it->extension().string());

        	if ( !it->extension().empty() && ext == ".CPS"){
        		std::cout << "Loading: " << *it << " - " << counter <<std::endl;
        		Utils::getImageFromCPS(cpsImage, *it);
        		SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(cpsImage, 320, 200, 8, 320, 0, 0, 0, 0);
        		//surface = new SDL_Surface;
        		//surface = SDL_CreateRGBSurfaceFrom(cpsImage, 320, 200, 8, 320, 0, 0, 0, 0);
        		sdlSurface[counter] = surface;
        		//sdlSurface[counter] = SDL_CreateRGBSurfaceFrom(cpsImage, 320, 200, 8, 320, 0, 0, 0, 0);
        		//surface = SDL_CreateRGBSurfaceFrom(cpsImage, 320, 200, 8, 320, 0, 0, 0, 0);
        		SDL_SetPaletteColors(sdlSurface[counter]->format->palette, sdlPalette->colors, 0, 256);

        		//surfaces.insert("asdf",*surface);
        		//surfaces[it->stem().string()] = image;
        		//std::cout << surfaces[it->stem().string()] << std::endl;
        		std::fill(cpsImage, cpsImage+256, 0); // clear buffer
        		counter++;
        		if (counter == 4)
        			break;
        	}
        }

	} else {
		printf("Couldn't find directory: '%s'\n" , path.c_str());
		return false;
	}

	/*
	std::pair<std::string, SDL_Surface*> surface;
	BOOST_FOREACH(surface, surfaces)
	    std::cout << surface.first << "\n";
	*/

	//Utils::getImageFromCPS(playfldImage, playfldCPSPath);
	//uint8_t decorateImage[Utils::EOB2_IMAGE_SIZE] = {};
	//Utils::getImageFromCPS(decorateImage, decorateCPSPath);
	//uint8_t thrownImage[Utils::EOB2_IMAGE_SIZE] = {};
	//Utils::getImageFromCPS(thrownImage, thrownCPSPath);

	//SDL_Surface *surface = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
	//SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(cpsImage, 320, 200, 8, 320, 0, 0, 0, 0);


	// Set palette for surface
	//SDL_SetPaletteColors(surfaces["BEHOLDER"]->format->palette, sdlPalette->colors, 0, 256);
	//SDL_SetPaletteColors(surfaces["PLAYFLD"]->format->palette, sdlPalette->colors, 0, 256);
	//drawImage(surface, sdlPalette, playfldImage);

	/*
	if (surfaces["PLAYFLD"] == surfaces["BEHOLDER"])
		printf("HELP");
	else
		printf("NOTHELP");
	*/

	//sdlSurface[0] = surface;
	//sdlSurface[1] = surfaces["BEHOLDER"];

	//sdlSurface[115] = SDL_CreateRGBSurfaceFrom(cpsImage, 320, 200, 8, 320, 0, 0, 0, 0);
	//SDL_SetPaletteColors(sdlSurface[115]->format->palette, sdlPalette->colors, 0, 256);
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
