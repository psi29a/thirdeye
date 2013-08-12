#include "graphics.hpp"

#include <iostream>
#include<fstream>
#include <stdio.h>
#include <string.h>

GRAPHICS::Graphics::Graphics() {

}

GRAPHICS::Graphics::~Graphics() {

}

std::vector<uint8_t> GRAPHICS::Graphics::uncompressBMP(std::vector<uint8_t> bmp){
	BMP image(bmp);
	std::vector<uint8_t> indexedBitmap;

	std::cout << "BMP Info: " << std::endl
		<< " " << image.getFilesize()
		<< " " << image.getWidth()
		<< " " << image.getHeight()
		<< " " << bmp.size()
		<< " " << (int) image[0]
		<< std::endl;

	uint16_t nrSubPictures = bmp[4] | (bmp[5]<<8);
	std::cout << nrSubPictures << " sub picture(s) found." << std::endl;

	uint32_t* startOffsets=new uint32_t[nrSubPictures];
	for (int32_t i=0; i<nrSubPictures; i++)
	{
		startOffsets[i]=bmp[6+i*4+0] | (bmp[6+i*4+1]<<8) | (bmp[6+i*4+2]<<16) | (bmp[6+i*4+3]<<24);
		std::cout << "Sub picture "<< i << " starts at offset " << startOffsets[i] << std::endl;

		unsigned int pos=startOffsets[i];

		int width=bmp[pos+0] | (bmp[pos+1]<<8);
		int height=bmp[pos+2] | (bmp[pos+3]<<8);
		pos+=4;

		std::cout << "   Size is " << width << " x " << height << std::endl;

		//unsigned char* indexedBitmap=new unsigned char[width*height];
		indexedBitmap.resize(width*height);

		memset(&indexedBitmap[0],0,width*height);	// Default bgcolor??? Probably defined in the header...

		while(true)
		{
			int y=bmp[pos];
			if (y==0xff)
				break;

			if ((y<0) || (y>=height))
			{
				printf("Probably out of sync. Reported y-coord: %d\n", y);
				throw;
			}
			pos++;

			while(true)
			{
				int x=bmp[pos];
				pos++;

				int islast=bmp[pos];
				pos++;

				int rle_width=bmp[pos];
				pos++;

				int rle_bytes=bmp[pos];
				pos++;

				while(rle_width>0)
				{
					int mode=bmp[pos]&1;
					int amount=(bmp[pos]>>1)+1;
					pos++;

					if (mode==0)	// Copy
					{
						memcpy(&indexedBitmap[0]+x+y*width, &bmp[0]+pos, amount);
						pos+=amount;
					}
					else if (mode==1) // Fill
					{
						int value=bmp[pos];
						pos++;
						memset(&indexedBitmap[0]+x+y*width,value, amount);
					}
					x+=amount;
					rle_width-=amount;
				}

				if (rle_width!=0)
				{
					printf("Out of sync while depacking RLE (rle_width=%d).\n", rle_width);
					throw;
				}

				if (islast==0x80)
					break;
			}
		}

		std::ofstream osss ("/tmp/backdrop_2.bmp", std::ios::binary);
		osss.write((const char*) &indexedBitmap[0], width*height);
		osss.close();
	}

	return indexedBitmap;
}


std::vector<uint8_t> GRAPHICS::Graphics::uncompressPalette(std::vector<uint8_t> pal){
	Palette palette(pal);
	std::vector<uint8_t> fullPalette(pal.size()-headerPalette);

	for(uint16_t i = 0; i<fullPalette.size(); i++){
		fullPalette[i] = palette[i];
	}

	return fullPalette;
}
