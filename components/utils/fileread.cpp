#include <iostream>
#include <stdio.h>

#include <boost/filesystem.hpp>
#include <boost/iostreams/stream.hpp>
//#include <boost/interprocess/file_mapping.hpp>
//#include <boost/interprocess/mapped_region.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

namespace Utils
{

// Compression type based.
const uint8_t UNCOMPRESSED 	= 0x00;
const uint8_t CRUNCH_1 		= 0x01;
const uint8_t FORMAT_2 		= 0x02;
const uint8_t FORMAT_3 		= 0x03;
const uint8_t FORMAT_80 	= 0x04;

// A 320x200 image where 1 byte is 1 pixel on PC
const uint16_t IMAGE_SIZE = 64000;

// CPS begins with an header, it is used for compressed file
struct CPSEOB1Header {
    unsigned short FileSize;
    unsigned short CompressionType;
    unsigned int   UncompressedSize;
    unsigned short PaletteSize;
};

unsigned long createRGB(int r, int g, int b)
{
    return ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

bool getCPS(boost::filesystem3::path cpsPath, boost::filesystem3::path palPath, bool transparency=false, bool sprite=false )
{
	/*
	 * This kind of file contains images. Usually are 320x200 pixel in size,
	 * 256 colors for PC version, 32 colors for Amiga version.
	 * The images can be compressed with different compression method.
	 * They may or may not contain a palette. The palette in case that it
	 * exist is placed just after the header. The image data is placed after
	 * the header and the palette.
	 */

	uint16_t image[IMAGE_SIZE];
	uint16_t test[IMAGE_SIZE];

	if (boost::filesystem::exists(boost::filesystem::path(cpsPath)) == false)
		return false;

	boost::iostreams::mapped_file_source sCPS;
	sCPS.open(cpsPath.string().c_str(), boost::filesystem::file_size(cpsPath));
	if (!sCPS.is_open()){
		std::cout << "Could not open CPS file: " << cpsPath << std::endl;
		return false;
	}
	uint8_t *byteCPS = (uint8_t *)sCPS.data();

	if (byteCPS[2] == FORMAT_80)
		std::cout << "I'm format 80" << std::endl;

	if(byteCPS[2] != 0x04)
		printf("no valid CPS file: %s\n", cpsPath.string().c_str());

	// What compression type is used
	// TODO: break this out to own function to get type of compression
	if (byteCPS[2] != FORMAT_80){
		std::cout << "Not a valid CPS file: " << cpsPath << std::endl;
		return false;
	}

	// Extract our image data which begins 10 bytes in
	for(int i=10; i<IMAGE_SIZE; i++)
		image[i-10] = byteCPS[i];

	// try to bit shift it instead?
	 //http://stackoverflow.com/questions/29437/whats-the-best-way-to-shift-an-array-of-bytes-by-12-bits
	// *test = *byteCPS << 20;

	printf(" image has this %x\n", image[0]);
	printf(" test has this %x\n", test[0]);


	// handle transparency: when RGB is Black, set to White
//	if(!sprite && transparency && palette[counter] == 0)
//		palette[counter] = createRGB(255, 255, 255);


	return true;
}

unsigned long* getPAL(boost::filesystem3::path palPath ){
	const uint PALETTE_FILE_SIZE = 768;
	unsigned long palette[256];

	if (boost::filesystem::exists(boost::filesystem::path(palPath)) == false)
		return NULL;

	if(boost::filesystem::file_size(palPath) != PALETTE_FILE_SIZE)
	{
		std::cout << "Not a valid PAL file: " << palPath << std::endl;
		return NULL;
	}

	// open palette for reading
	boost::iostreams::mapped_file_source sPAL;
	sPAL.open(palPath.string().c_str(), boost::filesystem::file_size(palPath));
	if (!sPAL.is_open()){
		std::cout << "Could not open PAL file: " << palPath << std::endl;
		return NULL;
	}
	uint16_t counter = 0;
	uint8_t *bytePAL = (uint8_t *)sPAL.data();
	for(uint i=0; i<PALETTE_FILE_SIZE; i+=3)
	{
		// Bitshift from 8 bits to 6 bits that is which is our palette size
		palette[counter++] = createRGB(bytePAL[i]<<2, bytePAL[i+1]<<2, bytePAL[i+3]<<2);

		// Debug information
		//uint8_t temp = 0;
		//temp = bytePAL[i];
		//temp = bytePAL[i]<<2;
		//printf(" Byte %d has this %x\n",i, bytePAL[i]);
		//std::cout << i << " <-> " << (uint16_t)temp << std::endl;
		//std::cout << colorcount-1 << " : " << currentPalette[colorcount-1] << std::endl;
	}

	// Close our connections to files
	sPAL.close();

	return palette;
}

}
