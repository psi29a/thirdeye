#ifndef COMPONENTS_UTILS_FILEREAD_HPP
#define COMPONENTS_UTILS_FILEREAD_HPP

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include "color.hpp"

namespace Utils
{
	uint8_t * getImageFromCPS(boost::filesystem3::path, boost::filesystem3::path, bool=false, bool=false);
	rgb * getPaletteFromPAL(boost::filesystem3::path, bool=false, bool=false);
}

#endif /* COMPONENTS_UTILS_FILEREAD_HPP */
