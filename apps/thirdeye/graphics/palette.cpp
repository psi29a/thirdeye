#include "palette.hpp"

#include <stdexcept>

GRAPHICS::Palette::Palette(const std::vector<uint8_t> &pal, bool isRes) {

	if (isRes){
		// PAL_HEADER fields are little-endian u16 at offsets 0, 2, 4. The
		// earlier code read at 0, 1, 2 -- misaligned (UB on strict-alignment
		// CPUs) and wrong: mColorArray and mFadeIndexArray00 got smeared
		// bytes from adjacent fields.
		if (pal.size() < 6)
			throw std::runtime_error(
				"palette resource header is truncated (<6 bytes)");

		auto rd16 = [&](size_t off) -> uint16_t {
			return static_cast<uint16_t>(pal[off] | (pal[off + 1] << 8));
		};
		mNumOfColours = rd16(0);
		mColorArray = rd16(2);
		mFadeIndexArray00 = rd16(4);

		// RGB data starts at PALHEADEROFFSET (26) and is 3 bytes per colour;
		// catch a corrupt mNumOfColours before the loop walks off the end.
		const size_t paletteBytes = static_cast<size_t>(mNumOfColours) * 3;
		if (pal.size() < PALHEADEROFFSET + paletteBytes)
			throw std::runtime_error(
				"palette resource data is truncated for declared colour count");

		//std::cout << "Colours: " << mNumOfColours << std::endl;

		// Bitshift from 6 bits (64 colours) to 8 bits (256 colours that is in our palette
		for (uint16_t i = 0; i < mNumOfColours; i++) {
			uint16_t offset = (i * 3) + PALHEADEROFFSET;
			mPalette[i].r = pal[offset] << 2;
			mPalette[i].g = pal[offset + 1] << 2;
			mPalette[i].b = pal[offset + 2] << 2;
			// Opaque: SDL3 honours palette alpha when blitting INDEX8 -> ARGB8888,
			// so leaving a=0 produced fully transparent pixels (black screen,
			// invisible cursor). SDL2's default 32-bit screen format had no
			// alpha channel and silently ignored it.
			mPalette[i].a = SDL_ALPHA_OPAQUE;
			//std::cout << "RGB: " << (int) i << " " << (int) mPalette[i].r << " "
			//		<< (int) mPalette[i].g << " " << (int) mPalette[i].b << std::endl;
		}
	} else {
		mNumOfColours = static_cast<uint16_t>(pal.size()/3); // 3 is rgb
		uint16_t counter = 0;
		for(size_t i=0; i<pal.size(); i+=3){
			// Bitshift from 8 bits to 6 bits that is which is our palette size
			mPalette[counter].r = pal[i] << 2;
			mPalette[counter].g = pal[i+1] << 2;
			mPalette[counter].b = pal[i+2] << 2;
			mPalette[counter].a = SDL_ALPHA_OPAQUE;
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
