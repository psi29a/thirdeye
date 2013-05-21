#ifndef COMPONENTS_UTILS_FILEREAD_HPP
#define COMPONENTS_UTILS_FILEREAD_HPP

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <SDL2/SDL.h>

namespace Utils
{
	const uint16_t EOB2_IMAGE_SIZE = 64000; 		// A 320x200 image where 1 byte is 1 pixel on PC
	const uint16_t EOB2_PALETTE_FILE_SIZE = 768; 	// Palette is alwa

	bool getImageFromCPS(uint8_t *, boost::filesystem3::path cpsPath);
	bool getPaletteFromPAL(SDL_Palette *, boost::filesystem3::path palPath, bool transparency=false, bool sprite=false);
}

#endif /* COMPONENTS_UTILS_FILEREAD_HPP */
