#include "bitmap.hpp"

GRAPHICS::Bitmap::Bitmap(const std::vector<uint8_t> &vec){

	mNumSubBitmaps = *reinterpret_cast<const uint16_t*>(&vec[2 * 2]);

	std::cout << mNumSubBitmaps << " sub picture(s) found." << std::endl;

	std::map<uint16_t, uint32_t> offsets;
	for (uint16_t i = 0; i < mNumSubBitmaps; i++) {
		offsets[i] = *reinterpret_cast<const uint16_t*>(&vec[6 + i * 4]);
	}

	for (uint16_t i = 0; i < offsets.size(); i++) {

		std::cout << "Sub picture " << i << " @ offset " << offsets[i]
				<< std::endl;

		unsigned int pos = offsets[i];

		mSubBitmaps[i].width = *reinterpret_cast<const uint16_t*>(&vec[pos]);
		mSubBitmaps[i].height =
				*reinterpret_cast<const uint16_t*>(&vec[pos + 2]);
		pos += 4;

		std::cout << "   Size is " << mSubBitmaps[i].width << "x"
				<< mSubBitmaps[i].height << std::endl;

		mSubBitmaps[i].subBitmap = std::vector<uint8_t>(
				mSubBitmaps[i].width * mSubBitmaps[i].height);

		memset(&mSubBitmaps[i].subBitmap[0], 0,
				mSubBitmaps[i].width * mSubBitmaps[i].height); // Default bgcolor??? Probably defined in the header...

		while (true) {
			int y = vec[pos];
			if (y == 0xff)
				break;

			if ((y < 0) || (y >= mSubBitmaps[i].height)) {
				printf("Probably out of sync. Reported y-coord: %d\n", y);
				throw;
			}
			pos++;

			while (true) {
				int x = vec[pos];
				pos++;

				int islast = vec[pos];
				pos++;

				int rle_width = vec[pos];
				pos++;

				//int rle_bytes=bmp[pos];
				pos++;

				while (rle_width > 0) {
					int mode = vec[pos] & 1;
					int amount = (vec[pos] >> 1) + 1;
					pos++;

					if (mode == 0)	// Copy
							{
						memcpy(
								&mSubBitmaps[i].subBitmap[0] + x
										+ y * mSubBitmaps[i].width,
								&vec[0] + pos, amount);
						pos += amount;
					} else if (mode == 1) // Fill
							{
						int value = vec[pos];
						pos++;
						memset(
								&mSubBitmaps[i].subBitmap[0] + x
										+ y * mSubBitmaps[i].width, value,
								amount);
					}
					x += amount;
					rle_width -= amount;
				}

				if (rle_width != 0) {
					printf("Out of sync while depacking RLE (rle_width=%d).\n",
							rle_width);
					throw;
				}

				if (islast == 0x80)
					break;
			}
		}
	}
}

GRAPHICS::Bitmap::~Bitmap() {

}

uint16_t GRAPHICS::Bitmap::getNumberOfBitmaps() {
	return mNumSubBitmaps;
}

uint16_t GRAPHICS::Bitmap::getWidth(uint16_t index) {
	return mSubBitmaps[index].width;
}

uint16_t GRAPHICS::Bitmap::getHeight(uint16_t index) {
	return mSubBitmaps[index].height;
}

const uint8_t& GRAPHICS::Bitmap::operator[](uint16_t index) {
	return mSubBitmaps[index].subBitmap[0];
}
