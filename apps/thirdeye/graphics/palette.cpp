#include "palette.hpp"

GRAPHICS::Palette::Palette(const std::vector<uint8_t> &pal, bool isRes) {

	if (isRes){
		mNumOfColours = *reinterpret_cast<const uint16_t*>(&pal[0]);
		mColorArray = *reinterpret_cast<const uint16_t*>(&pal[1]);
		mFadeIndexArray00 = *reinterpret_cast<const uint16_t*>(&pal[2]);

		//std::cout << "Colours: " << mNumOfColours << std::endl;

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
		mNumOfColours = pal.size()/3; // 3 is rgb
		uint16_t counter = 0;
		for(uint16_t i=0; i<pal.size(); i+=3){
			// Bitshift from 8 bits to 6 bits that is which is our palette size
			mPalette[counter].r = pal[i] << 2;
			mPalette[counter].g = pal[i+1] << 2;
			mPalette[counter].b = pal[i+2] << 2;
			mPalette[counter].a = 0;
			//std::cout << std::hex << "RGB: " << (int) i << " " << (int) mPalette[counter].r << " "
			//		<< (int) mPalette[counter].g << " " << (int) mPalette[counter].b << std::endl;
			counter++;
		}
	}

}

uint16_t GRAPHICS::Palette::getNumOfColours() const {
	return (mNumOfColours);
}

uint16_t GRAPHICS::Palette::getColorArray() const {
	return (mColorArray);
}

uint16_t GRAPHICS::Palette::getFadeIndexArray00() const {
	return (mFadeIndexArray00);
}

const SDL_Color& GRAPHICS::Palette::operator[](uint16_t index) {
	return (mPalette[index]);
}
