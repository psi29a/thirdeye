#include <iostream>
#include <stdio.h>

#include <boost/filesystem.hpp>
#include <boost/iostreams/stream.hpp>
//#include <boost/interprocess/file_mapping.hpp>
//#include <boost/interprocess/mapped_region.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

namespace Utils
{

unsigned long createRGB(int r, int g, int b)
{
    return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

bool loadCPS(boost::filesystem3::path cpsPath, boost::filesystem3::path palPath, bool transparency=false, bool sprite=false )
{
	const uint PALETTE_FILE_SIZE = 768;
	unsigned long currentPalette[256];

	if (boost::filesystem::exists(boost::filesystem::path(cpsPath)) == false)
		return false;

	if (boost::filesystem::exists(boost::filesystem::path(palPath)) == false)
		return false;

	if(boost::filesystem::file_size(palPath) != PALETTE_FILE_SIZE)
	{
		std::cout << "Not a valid PAL file: " << palPath << std::endl;
		return false;
	}

	// open palette for reading
	//boost::interprocess::file_mapping mPAL(palPath.string().c_str(), boost::interprocess::read_only);
	//boost::interprocess::mapped_region rPAL(mPAL, boost::interprocess::read_only);
	boost::iostreams::mapped_file_source sPAL;
	sPAL.open(palPath.string().c_str(), PALETTE_FILE_SIZE);
	if (!sPAL.is_open()){
		std::cout << "Could not open PAL file: " << palPath << std::endl;
		return false;
	}
	uint8_t *bytePAL = (uint8_t *)sPAL.data();

	// open CPS for reading
	//boost::interprocess::file_mapping mCPS(palPath.string().c_str(), boost::interprocess::read_only);
	//boost::interprocess::mapped_region rCPS(mCPS, boost::interprocess::read_only);

	//uint8_t *byteCPS = static_cast<uint8_t*>(rCPS.get_address());

	//std::cout << byteCPS << std::endl;

	uint16_t colorcount = 0;
	for(uint i=0; i<PALETTE_FILE_SIZE; i+=3)
	{
		// Bitshift from 8 bits to 6 bits that is which is our palette size
		currentPalette[colorcount++] = createRGB(bytePAL[i]<<2, bytePAL[i+1]<<2, bytePAL[i+3]<<2);

		// handle transparency: when RGB is Black, set to White
		if(!sprite && transparency && currentPalette[colorcount] == 0)
			currentPalette[colorcount] = createRGB(255, 255, 255);

		// Debug information
		//uint8_t temp = 0;
		//temp = byteCPS[i]<<2;
		//std::cout << i << " <-> " << (uint16_t)temp << std::endl;
		//std::cout << colorcount-1 << " : " << currentPalette[colorcount-1] << std::endl;
	}

	// Close our connections to files
	sPAL.close();


	return true;
}

}
