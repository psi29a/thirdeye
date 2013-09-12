#include "palette.hpp"

GRAPHICS::Palette::Palette(const std::vector<uint8_t> &pal, bool isRes) {

	if (isRes){
		mNumOfColours = *reinterpret_cast<const uint16_t*>(&pal[0]);
		mColorArray = *reinterpret_cast<const uint16_t*>(&pal[1]);
		mFadeIndexArray00 = *reinterpret_cast<const uint16_t*>(&pal[2]);

		std::cout << "Colours: " << mNumOfColours << std::endl;

		// Bitshift from 6 bits (64 colours) to 8 bits (256 colours that is in our palette
		for (uint16_t i = 0; i < mNumOfColours; i++) {
			uint16_t offset = (i * 3) + PALHEADEROFFSET;
			mPalette[i].r = pal[offset] << 2;
			mPalette[i].g = pal[offset + 1] << 2;
			mPalette[i].b = pal[offset + 2] << 2;
			mPalette[i].a = 0;

			//std::cout << "RGB: " << (int) i << " " << (int) mPalette[i].r << " "
			//		<< (int) mPalette[i].g << " " << (int) mPalette[i].b << std::endl;
		}
	} else {
		mNumOfColours = 256;
		for(uint i=0; i<768; i+=3){
			// Bitshift from 8 bits to 6 bits that is which is our palette size
			mPalette[i].r = pal[i] << 2;
			mPalette[i].g = pal[i+1] << 2;
			mPalette[i].b = pal[i+2] << 2;
			mPalette[i].a = 0;
		}
	}

}

uint16_t GRAPHICS::Palette::getNumOfColours() const {
	return mNumOfColours;
}

uint16_t GRAPHICS::Palette::getColorArray() const {
	return mColorArray;
}

uint16_t GRAPHICS::Palette::getFadeIndexArray00() const {
	return mFadeIndexArray00;
}

const SDL_Color& GRAPHICS::Palette::operator[](uint16_t index) {
	return mPalette[index];
}
