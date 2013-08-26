#include "bitmap.hpp"

GRAPHICS::Bitmap::Bitmap(std::vector<uint8_t> vec) :
		vec_(vec) {
}

uint16_t GRAPHICS::Bitmap::getFilesize() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[0]);
}

uint16_t GRAPHICS::Bitmap::getWidth() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[5 * 2]);
}

uint16_t GRAPHICS::Bitmap::getHeight() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[6 * 2]);
}

uint8_t& GRAPHICS::Bitmap::operator[](size_t off) {
	if (off > vec_.size() - BMPHEADEROFFSET) {
		std::cerr << "Trying to access BMP data out of bounds." << std::endl;
		throw;
	}
//std::cout << "offset @: " << off << std::endl;
	return vec_[off + BMPHEADEROFFSET];
}
