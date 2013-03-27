#ifndef COMPONENTS_UTILS_FILEREAD_HPP
#define COMPONENTS_UTILS_FILEREAD_HPP

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

namespace Utils
{
	bool getCPS(boost::filesystem3::path, boost::filesystem3::path, bool=false, bool=false);
	unsigned long getPAL(boost::filesystem3::path, bool=false, bool=false);
	unsigned long createRGB(int, int, int);
}

#endif /* COMPONENTS_UTILS_FILEREAD_HPP */
