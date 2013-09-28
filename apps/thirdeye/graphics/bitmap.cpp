/*
 * RLE decoding technique from Andreas Larsson (Jackasser)
 */

#include "bitmap.hpp"

GRAPHICS::Bitmap::Bitmap(const std::vector<uint8_t> &vec) {
	mBitmapData = vec;
	mNumSubBitmaps = *reinterpret_cast<const uint16_t*>(&vec[2 * 2]);

	for (uint16_t i = 0; i < mNumSubBitmaps; i++) {
		mBitmapOffets[i] = *reinterpret_cast<const uint16_t*>(&vec[6 + i * 4]);
	}
}

GRAPHICS::Bitmap::~Bitmap() {

}

uint16_t GRAPHICS::Bitmap::getNumberOfBitmaps() {
	return mNumSubBitmaps;
}

uint16_t GRAPHICS::Bitmap::getWidth(uint16_t index) {
	return *reinterpret_cast<const uint16_t*>(&mBitmapData[mBitmapOffets[index]]);
}

uint16_t GRAPHICS::Bitmap::getHeight(uint16_t index) {
	return *reinterpret_cast<const uint16_t*>(&mBitmapData[mBitmapOffets[index]
			+ 2]);
}

std::vector<uint8_t> GRAPHICS::Bitmap::operator[](uint16_t index) {
	uint32_t pos = mBitmapOffets[index] + 4;	// skip over width and height
	std::vector<uint8_t> bitmap(getWidth(index) * getHeight(index));
	memset(&bitmap[0], 0, getWidth(index) * getHeight(index));

	while (true) {
		int32_t y = mBitmapData[pos];
		if (y == 0xff)
			break;

		if ((y < 0) || (y >= getHeight(index))) {
			std::cout << "Probably out of sync. Reported y-coord: " << y
					<< std::endl;
			throw;
		}
		pos++;

		while (true) {
			int32_t x = mBitmapData[pos + 0]
					| ((mBitmapData[pos + 1] & 0x7f) << 8);
			int32_t islast = mBitmapData[pos + 1] & 0x80;
			int32_t rle_width = mBitmapData[pos + 2];
			//int rle_bytes = vec[pos+3];
			pos += 4;

			while (rle_width > 0) {
				int32_t mode = mBitmapData[pos] & 1;
				int32_t amount = (mBitmapData[pos] >> 1) + 1;
				pos++;

				if (mode == 0) {		// Copy
					memcpy(&bitmap[0] + x + y * getWidth(index),
							&mBitmapData[0] + pos, amount);
					pos += amount;
				} else if (mode == 1)	// Fill
						{
					int value = mBitmapData[pos];
					pos++;
					memset(&bitmap[0] + x + y * getWidth(index), value, amount);
				}
				x += amount;
				rle_width -= amount;
			}

			if (rle_width != 0) {
				std::cout << "Out of sync while unpacking RLE: ( rle_width = "
						<< rle_width << " )." << std::endl;
				throw;
			}

			if (islast == 0x80)
				break;
		}
	}


	if (pos+1 == mBitmapData.size())
		std::cout << "We're at the end!" << std::endl;
	else
		std::cout << "Pos: " << pos << " size of file: " << mBitmapData.size() << std::endl;



	return bitmap;
}
