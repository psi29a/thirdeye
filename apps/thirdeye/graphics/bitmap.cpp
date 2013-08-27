#include "bitmap.hpp"

GRAPHICS::Bitmap::Bitmap(std::vector<uint8_t> vec) :
	vec_(vec)
	{

	std::vector<uint8_t> indexedBitmap;

	std::cout << "BMP Info: " << std::endl << " " << getFilesize() << " "
			<< getWidth() << " " << getHeight() << " " << vec.size()
			//<< " " << (int) image[0]
			<< std::endl;

	std::cout << getNumberOfBitmaps() << " sub picture(s) found." << std::endl;

	std::map<uint16_t, uint32_t> offsets = getBitmapOffsets();
	for (uint16_t i = 0; i < offsets.size(); i++) {

		std::cout << "Sub picture " << i << " starts at offset " << offsets[i]
				<< std::endl;

		unsigned int pos = offsets[i];

		uint16_t width = *reinterpret_cast<const uint16_t*>(&vec[pos]);
		uint16_t height = *reinterpret_cast<const uint16_t*>(&vec[pos + 2]);
		pos += 4;

		std::cout << "   Size is " << width << " x " << height << std::endl;

		//unsigned char* indexedBitmap=new unsigned char[width*height];
		indexedBitmap.resize(width * height);

		memset(&indexedBitmap[0], 0, width * height); // Default bgcolor??? Probably defined in the header...

		while (true) {
			int y = vec[pos];
			if (y == 0xff)
				break;

			if ((y < 0) || (y >= height)) {
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
						memcpy(&indexedBitmap[0] + x + y * width, &vec[0] + pos,
								amount);
						pos += amount;
					} else if (mode == 1) // Fill
							{
						int value = vec[pos];
						pos++;
						memset(&indexedBitmap[0] + x + y * width, value,
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

uint16_t GRAPHICS::Bitmap::getFilesize() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[0]);
}

uint16_t GRAPHICS::Bitmap::getNumberOfBitmaps() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[2 * 2]);
}

std::map<uint16_t, uint32_t> GRAPHICS::Bitmap::getBitmapOffsets() const {
	std::map<uint16_t, uint32_t> offsets;
	for (uint16_t i = 0; i < getNumberOfBitmaps(); i++) {
		offsets[i] = *reinterpret_cast<const uint16_t*>(&vec_[6 + i * 4]);
	}

	return offsets;
}

std::vector<uint8_t> GRAPHICS::Bitmap::getBitmap(uint8_t number) {
	return subBitmap[number];
}

uint16_t GRAPHICS::Bitmap::getWidth() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[5 * 2]);
}

uint16_t GRAPHICS::Bitmap::getHeight() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[6 * 2]);
}

uint8_t& GRAPHICS::Bitmap::operator[](uint8_t number) {
	return subBitmap[number][0];

	/*
	if (off > vec_.size() - BMPHEADEROFFSET) {
		std::cerr << "Trying to access BMP data out of bounds." << std::endl;
		throw;
	}
//std::cout << "offset @: " << off << std::endl;
	return vec_[off + BMPHEADEROFFSET];
	*/

}
