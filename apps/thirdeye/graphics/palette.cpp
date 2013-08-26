#include "palette.hpp"

GRAPHICS::Palette::Palette(std::vector<uint8_t> vec) :
		vec_(vec) {
}

uint16_t GRAPHICS::Palette::getNumOfColours() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[0]);
}

uint16_t GRAPHICS::Palette::getOffsetColorArray() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[1]);
}

uint16_t GRAPHICS::Palette::getOffsetFadeIndexArray00() const {
	return *reinterpret_cast<const uint16_t*>(&vec_[2]);
}

uint8_t& GRAPHICS::Palette::operator[](size_t off) {
	if (off > vec_.size() - PALHEADEROFFSET) {
		std::cerr << "Trying to access PAL data out of bounds." << std::endl;
		throw;
	}
//std::cout << "offset @: " << off << std::endl;
	return vec_[off + PALHEADEROFFSET];
}
