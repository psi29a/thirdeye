#ifndef COMPONENTS_UTILS_FILEREAD_HPP
#define COMPONENTS_UTILS_FILEREAD_HPP

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include "color.hpp"

namespace Utils
{
	const uint16_t EOB2_IMAGE_SIZE = 64000; 		// A 320x200 image where 1 byte is 1 pixel on PC
	const uint16_t EOB2_PALETTE_FILE_SIZE = 768; 	// Palette is alwa

	bool getImageFromCPS(uint8_t *, boost::filesystem3::path, boost::filesystem3::path, bool=false, bool=false);
	bool getPaletteFromPAL(rgb *, boost::filesystem3::path, bool=false, bool=false);
}

#endif /* COMPONENTS_UTILS_FILEREAD_HPP */
