#ifndef BITMAP_HPP
#define BITMAP_HPP

#define BMPHEADEROFFSET 14

#include <stdint.h>
#include <vector>
#include <map>
#include <stdio.h>
#include <iostream>
#include "SDL.h"

namespace GRAPHICS {

/*
 struct BMP
 {
 uint16_t fileSize;
 uint16_t unknown1;
 uint16_t numberOfSubImages;
 uint16_t unknown3;
 uint16_t unknown4;
 uint16_t width;
 uint16_t height;
 uint8_t compressedData[0];
 };
 */

struct SubBitmap {
	uint16_t width;
	uint16_t height;
	std::vector<uint8_t> subBitmap;
};

class Bitmap {
public:
	Bitmap(const std::vector<uint8_t> &vec);
	virtual ~Bitmap();
	std::vector<uint8_t> operator[](uint16_t index);
	uint16_t getWidth(uint16_t index) ;
	uint16_t getHeight(uint16_t index) ;
	uint16_t getNumberOfBitmaps();
	bool isMoreBitmap();
	uint32_t getNextBitmapPos();
private:
	// Decode one sub-shape of an AESOP/16 "1.10" VFX shape table (the format
	// every EYE.RES bitmap uses). See bitmap.cpp for the token grammar.
	std::vector<uint8_t> decodeVFXShape(uint16_t index);

	uint16_t mNumSubBitmaps;
	std::map<uint16_t, uint32_t> mBitmapOffets;
	std::map< uint16_t, SubBitmap > mSubBitmaps;
	std::vector<uint8_t> mBitmapData;
	uint32_t nextBitmapPos;
	// True when the resource is an AESOP/16 "1.10" VFX shape table (vs the older
	// row/span RLE format the intro's GFF frames use). Selects the decode path.
	bool mIsVFXShape = false;
};

}
#endif //BITMAP_HPP
