#include <iostream>
#include <stdio.h>

#include "fileread.hpp"
#include "format80.hpp"

#include "intarray2bmp.hpp"

namespace Utils
{

unsigned long testing(uint8_t r, uint8_t g, uint8_t b){
	return (b << 24 | g << 16 | r << 8);
}

// Compression type based.
const uint8_t UNCOMPRESSED 	= 0x00;
const uint8_t CRUNCH_1 		= 0x01;
const uint8_t FORMAT_2 		= 0x02;
const uint8_t FORMAT_3 		= 0x03;
const uint8_t FORMAT_80 	= 0x04;

// CPS begins with an header, it is used for compressed file
struct CPSEOB1Header {
    unsigned short FileSize;
    unsigned short CompressionType;
    unsigned int   UncompressedSize;
    unsigned short PaletteSize;
};

bool getImageFromCPS(uint8_t *uImage, boost::filesystem3::path cpsPath, boost::filesystem3::path palPath, bool transparency, bool sprite)
{
	/*
	 * This kind of file contains images. Usually are 320x200 pixel in size,
	 * 256 colors for PC version, 32 colors for Amiga version.
	 * The images can be compressed with different compression method.
	 * They may or may not contain a palette. The palette in case that it
	 * exist is placed just after the header. The image data is placed after
	 * the header and the palette.
	 */

	uint8_t cImage[EOB2_IMAGE_SIZE] = {};
	uint16_t fileSize;
	uint16_t uImageSize;
	//unsigned long palette;

	if (boost::filesystem::exists(cpsPath) == false)
		return false;

	fileSize = boost::filesystem::file_size(cpsPath);

	boost::iostreams::mapped_file_source sCPS;
	sCPS.open(cpsPath.string().c_str(), fileSize);
	if (!sCPS.is_open()){
		std::cout << "Could not open CPS file: " << cpsPath << std::endl;
		return false;
	}
	uint8_t *byteCPS = (uint8_t *)sCPS.data();

	// Debug
	//if(byteCPS[2] != FORMAT_80)
	//	printf("no valid CPS file: %s\n", cpsPath.string().c_str());

	// What compression type is used
	// TODO: break this out to own function to get type of compression
	if (byteCPS[2] != FORMAT_80){
		std::cout << "Not a valid CPS file: " << cpsPath << std::endl;
		return false;
	}

	// Extract our image data which begins 10 bytes in
	for(int i=10; i<fileSize+10; i++)
		cImage[i-10] = byteCPS[i];

	// Decode Format80 data
	uImageSize = decodeFormat80(uImage, cImage, EOB2_IMAGE_SIZE);

//	std::cout << "Size: " << uImageSize << std::endl;
//	for(int i=1; i<IMAGE_SIZE; i++)
//		printf("@Byte: %i-- Compressed: %x to Uncompressed %x\n",i, cImage[i], uImage[i]);

	intarray2bmp<uint8_t>( cpsPath.filename().string()+".bmp", uImage, 200, 320, 0x00, 0xFF);
	return uImage;
}

bool getPaletteFromPAL(SDL_Palette *palette, boost::filesystem3::path palPath, bool transparency, bool sprite){

	if (boost::filesystem::exists(palPath) == false)
		return false;

	if(boost::filesystem::file_size(palPath) != EOB2_PALETTE_FILE_SIZE)
	{
		std::cout << "Not a valid PAL file: " << palPath << std::endl;
		return false;
	}

	// open palette for reading
	boost::iostreams::mapped_file_source sPAL;
	sPAL.open(palPath.string().c_str(), boost::filesystem::file_size(palPath));
	if (!sPAL.is_open()){
		std::cout << "Could not open PAL file: " << palPath << std::endl;
		return false;
	}
	uint16_t counter = 0;
	uint8_t *bytePAL = (uint8_t *)sPAL.data();
	for(uint i=0; i<EOB2_PALETTE_FILE_SIZE; i+=3)
	{
		// Bitshift from 8 bits to 6 bits that is which is our palette size
		palette->colors[counter].r = bytePAL[i]   << 2;
		palette->colors[counter].g = bytePAL[i+1] << 2;
		palette->colors[counter].b = bytePAL[i+3] << 2;
    	//printf("%u convert from ? to rgb: %u %u %u\n", counter, bytePAL[i]<<2, bytePAL[i+1]<<2, bytePAL[i+3]<<2);

		// Handle our black transparency and replace it with white
		if(!sprite && transparency && palette->colors[counter].r == 0 && palette->colors[counter].g == 0 && palette->colors[counter].b == 0){
			palette->colors[counter].r = 255;
			palette->colors[counter].g = 255;
			palette->colors[counter].b = 255;
		}

		// Debug information
		//printf(" Byte %d has this %x\n",i, bytePAL[i]);
		//std::cout << colorcount-1 << " : " << currentPalette[colorcount-1] << std::endl;
		counter++;
	}

	// Close our connections to files
	sPAL.close();

	return true;
}

}
