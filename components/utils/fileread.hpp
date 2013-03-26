#ifndef COMPONENTS_UTILS_FILEREAD_HPP
#define COMPONENTS_UTILS_FILEREAD_HPP

namespace Utils
{
	bool getCPS(boost::filesystem3::path, boost::filesystem3::path, bool transparency=false, bool sprite=false);
	unsigned long* getPAL(boost::filesystem3::path);
	unsigned long createRGB(int, int, int);
}

#endif /* COMPONENTS_UTILS_FILEREAD_HPP */
