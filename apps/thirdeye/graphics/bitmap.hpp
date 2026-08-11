#ifndef BITMAP_HPP
#define BITMAP_HPP

#define BMPHEADEROFFSET 14

#include <stdint.h>
#include <vector>
#include <map>
#include <stdio.h>
#include <iostream>
#include <SDL3/SDL.h>

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
	// True for AESOP/16 "1.10" VFX shape tables -- the only format with a
	// per-pixel opacity mask (see decodeVFXShape).
	bool isVFXShape() const { return mIsVFXShape; }
	// Decode one VFX sub-shape, separating TRANSPARENCY from PAINTED BLACK.
	// In the VFX RLE the only transparent pixels are `skip` tokens; a shape
	// can paint palette index 0 (black) via run/string tokens and the original
	// VFX_shape_draw renders those as solid black (there is NO color keying).
	// `mask` gets 1 byte per pixel: 1 = painted (opaque, even if value 0),
	// 0 = skipped (transparent). Callers that colorkey index 0 instead lose
	// every painted-black pixel -- the spell-book arrows/checker bug.
	// Decode any shape with a coverage mask: 1 = painted (opaque even when the
	// value is 0), 0 = never covered by a run (transparent). Dispatches to the
	// VFX decoder for "1.10" tables. See the comment on the definition.
	std::vector<uint8_t> decodeScanlineMasked(uint16_t index,
	                                          std::vector<uint8_t> &mask);
	std::vector<uint8_t> decodeVFXShapeMasked(uint16_t index,
	                                          std::vector<uint8_t> &mask);
private:
	// Decode one sub-shape of an AESOP/16 "1.10" VFX shape table (the format
	// every EYE.RES bitmap uses). See bitmap.cpp for the token grammar.
	std::vector<uint8_t> decodeVFXShape(uint16_t index);

	// Defensive default-init: the constructor's three format branches all
	// assign mNumSubBitmaps explicitly today, but an early-return on a
	// malformed input would otherwise leave it garbage (this is the bug the
	// CHARPICS bring-up tripped on -- see the in-class test).
	uint16_t mNumSubBitmaps = 0;
	std::map<uint16_t, uint32_t> mBitmapOffets;
	std::map< uint16_t, SubBitmap > mSubBitmaps;
	std::vector<uint8_t> mBitmapData;
	uint32_t nextBitmapPos = 0;
	// True when the resource is an AESOP/16 "1.10" VFX shape table (vs the older
	// row/span RLE format the intro's GFF frames use). Selects the decode path.
	bool mIsVFXShape = false;
	// True when the resource is a CHARGEN/CHARPICS.BMP-style portrait sheet.
	// Mutually exclusive with mIsVFXShape. Selects the third decode path.
	bool mIsCharPics = false;
};

}
#endif //BITMAP_HPP
