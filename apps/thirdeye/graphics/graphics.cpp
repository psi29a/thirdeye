#include "graphics.hpp"

GRAPHICS::Graphics::Graphics() {

}

GRAPHICS::Graphics::~Graphics() {

}

void GRAPHICS::Graphics::getBMP(std::vector<uint8_t> bmp){
	//BMP image;
	//std::vector<BMP> image;
	//image[0] = &bmp[0];

	Overlay ovr(bmp);
	BMP* image = reinterpret_cast<BMP*>(&bmp[0]);

	std::cout << "BMP Info: " << std::endl
		<< " " << image->fileSize
		<< " " << ovr.getFilesize()
		<< " " << image->width
		<< " " << ovr.getWidth()
		<< " " << image->height
		<< " " << ovr.getHeight()
		<< " " << bmp.size()
		<< " " << image->unknown1
		<< " " << image->unknown2
		<< " " << image->unknown3
		<< " " << image->unknown4
		<< " data: " << (int) image->compressedData[0]
		<< " " << (int) ovr[0]
		//<< " " << ovr.getData()
		<< std::endl;

}
