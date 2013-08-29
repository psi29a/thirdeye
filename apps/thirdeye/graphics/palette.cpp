#include "palette.hpp"

GRAPHICS::Palette::Palette(std::vector<uint8_t> base) {
	mNumOfColours = *reinterpret_cast<const uint16_t*>(&base[0]);
	mColorArray = *reinterpret_cast<const uint16_t*>(&base[1]);
	mFadeIndexArray00 = *reinterpret_cast<const uint16_t*>(&base[2]);

	std::cout << "Colours: " << mNumOfColours << std::endl;

	// Bitshift from 6 bits (64 colours) to 8 bits (256 colours that is in our palette
	for (uint16_t i = 0; i < mNumOfColours; i++) {
		uint16_t offset = (i * 3) + PALHEADEROFFSET;
		mPalette[i].r = base[offset] << 2;
		mPalette[i].g = base[offset + 1] << 2;
		mPalette[i].b = base[offset + 2] << 2;
		mPalette[i].a = 0;

		//std::cout << "RGB: " << (int) i << " " << (int) mPalette[i].r << " "
		//		<< (int) mPalette[i].g << " " << (int) mPalette[i].b << std::endl;
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
