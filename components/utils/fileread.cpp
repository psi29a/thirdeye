#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

namespace Utils
{

bool loadCPS(boost::filesystem3::path cpsPath, boost::filesystem3::path palPath )
{
	if (boost::filesystem::exists(boost::filesystem::path(cpsPath)) == false)
		return false;

	if (boost::filesystem::exists(boost::filesystem::path(palPath)) == false)
		return false;

	const uint PALETTE_FILE_SIZE = 768;
	uint cpsFilesize = 0;
	uint palFilesize = 0;
	uint currentPalette[256];

	// open palette for reading
	boost::interprocess::file_mapping mPAL(palPath.string().c_str(), boost::interprocess::read_only);
	boost::interprocess::mapped_region rPAL(mPAL, boost::interprocess::read_only);
	palFilesize = rPAL.get_size();

	// open CPS for reading
	boost::interprocess::file_mapping mCPS(palPath.string().c_str(), boost::interprocess::read_only);
	boost::interprocess::mapped_region rCPS(mCPS, boost::interprocess::read_only);
	cpsFilesize = rCPS.get_size();


	if(palFilesize != PALETTE_FILE_SIZE)
	{
		std::cout << "Not a valid PAL file: " << palPath << std::endl;
		return false;
	}

	int colorcount = 0;
	for(uint i=0; i<PALETTE_FILE_SIZE; i+=3)
	{	// Convert PAL 6-Bit palette to 8-bit
		//currentPalette[colorcount] =
/*		current_colors[colorcount].r=pal[i]<<2;
		current_colors[colorcount].g=pal[i+1]<<2;
		current_colors[colorcount].b=pal[i+2]<<2;
*/		colorcount++;
	}

	std::cout << cpsFilesize << " " << palFilesize << std::endl;
	return true;
}

unsigned long createRGB(int r, int g, int b)
{
    return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

}
