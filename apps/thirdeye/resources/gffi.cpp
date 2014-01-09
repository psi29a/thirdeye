/*
 * gffi.cpp
 *
 *  Created on: Sept 3, 2013
 *      Author: bcurtis
 */
#include "gffi.hpp"

#include <fstream>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/algorithm/string.hpp>

RESOURCES::GFFI::GFFI(boost::filesystem::path gffiPath) {
	mGFFIFile = gffiPath;

	// does resource exist
	if (boost::filesystem::exists(mGFFIFile) == false)
		throw std::runtime_error(mGFFIFile.string() + " does not exist!");
	else
		std::cout << "  Loading Cinematic: " << mGFFIFile;

	// how big is the file on disk?
	mGFFIFileSize = boost::filesystem::file_size(mGFFIFile);
	std::cout << " " << mGFFIFileSize << " bytes " << std::endl;

	// get our file
	std::ifstream fResource;
	fResource.open(mGFFIFile.c_str(), std::ios::in | std::ios::binary);
	if (fResource.is_open() == false)
		throw std::runtime_error("Could not open file " + mGFFIFile.string());

	//read from file
	fResource.read(reinterpret_cast<char*>(&mGFFIHeader), sizeof(GFFIHeader));

	// is resource a valid GFFI
	if (std::string(mGFFIHeader.signature) != GFFI_ID)
		throw std::runtime_error(
				mGFFIFile.string() + " is not a valid GFFI resource");

	fResource.seekg(mGFFIHeader.directory_offset, std::ios::beg);
	fResource.read(reinterpret_cast<char*>(&mGFFIDirectoryHeader),
			sizeof(GFFIDirectoryHeader));

	fResource.seekg(
			mGFFIHeader.directory_offset + mGFFIDirectoryHeader.directory_size,
			std::ios::beg);
	uint16_t gffi_trailer;
	fResource.read(reinterpret_cast<char*>(&gffi_trailer), sizeof(uint16_t));

	if (gffi_trailer != 0)
		throw std::runtime_error(
				"Could not find end of directory in GFFI resource.");

	// Now we read the header information
	fResource.seekg(mGFFIHeader.directory_offset + sizeof(GFFIDirectoryHeader),
			std::ios::beg);

	for (uint16_t block = 0; block < mGFFIDirectoryHeader.number_of_tags;
			block++) {

		GFFIBlockHeader mGFFIBlockHeader;
		fResource.read(reinterpret_cast<char*>(&mGFFIBlockHeader),
				sizeof(GFFIBlockHeader));

		//if (mGFFIBlockHeader.tag[3] == 0x20)
		mGFFIBlockHeader.tag[3] = '\0';

		for (uint16_t elements = 0;
				elements < mGFFIBlockHeader.number_of_elements; elements++) {

			GFFIBlock mGFFIBlock;

			fResource.read(reinterpret_cast<char*>(&mGFFIBlock),
					sizeof(mGFFIBlock));

			std::string tag = mGFFIBlockHeader.tag;
			//tag[3] =  '\0';	// needs null, but we loose last char

			/*
			 if (tag == "BMA") {
			 std::cout << std::hex << "    tag: " << tag << std::endl
			 << "    elements: "
			 << mGFFIBlockHeader.number_of_elements << std::endl
			 << "    unique: " << mGFFIBlock.unique << std::endl
			 << "    offset: " << mGFFIBlock.offset << std::endl
			 << "    size: " << mGFFIBlock.size << std::endl
			 << std::endl;
			 }
			 */

			mFiles[tag][mGFFIBlock.unique].offset = mGFFIBlock.offset;
			mFiles[tag][mGFFIBlock.unique].data[0].resize(mGFFIBlock.size);

		}
	}
	// Now we dump the data
	//std::cout << "Data dump:" << std::endl;
	std::map<std::string, std::map<uint32_t, File> >::iterator tag;
	for (tag = mFiles.begin(); tag != mFiles.end(); tag++) {
		std::map<uint32_t, File>::iterator file;
		for (file = tag->second.begin(); file != tag->second.end(); file++) {
			fResource.seekg(file->second.offset, std::ios::beg);
			fResource.read(reinterpret_cast<char*>(&file->second.data[0][0]),
					file->second.data[0].size());

			if (tag->first == "BMA" && file->first < 10) { // only process the original files
				std::vector<uint8_t> subBitmap = file->second.data[0];
				uint16_t counter = 1;
				while (true) {
					GRAPHICS::Bitmap anim(subBitmap);
					//uint32_t numOfBitmaps = *reinterpret_cast<const uint16_t*>(&subBitmap[2 * 2]);
					//uint32_t offset = *reinterpret_cast<const uint16_t*>(&subBitmap[6 + 0 * 4]);
					//printf("Current bitmap (%d) / (%d) has %x bitmaps @ offset: %x ", file->first, counter, numOfBitmaps, offset);
					anim[anim.getNumberOfBitmaps() - 1];
					bool isMore = anim.isMoreBitmap();
					//printf(" is there more: %x ", isMore);
					mFiles[tag->first][file->first].data[counter] = subBitmap;

					/* we write out the files
					 std::string Path = "/tmp/"+boost::lexical_cast<std::string>(file->first)+"_"+boost::lexical_cast<std::string>(counter)+".BMA";
					 std::ofstream FILE(Path.c_str(), std::ios::out | std::ofstream::binary);
					 size_t sz = mFiles[tag->first][file->first].data[counter].size();
					 FILE.write(reinterpret_cast<const char*>(&mFiles[tag->first][file->first].data[counter][0]), sz * sizeof(mFiles[tag->first][file->first].data[counter][0]));
					 */

					if (isMore) {
						//printf(" new bitmap found @ offset: %x", anim.getNextBitmapPos());
						uint32_t nextSize = subBitmap.size()
								- anim.getNextBitmapPos();
						std::vector<uint8_t> temp(
								subBitmap.size() - anim.getNextBitmapPos());
						memcpy(&temp[0],
								&subBitmap[0] + anim.getNextBitmapPos(),
								nextSize);
						//printf("\n");
						subBitmap = temp;
					} else {
						//printf("\n");
						break;
					}
					counter++;

				}
			}
			/*
			 std::string Path = "/tmp/"+boost::lexical_cast<std::string>(file->first)+"."+tag->first;
			 std::ofstream FILE(Path.c_str(), std::ios::out | std::ofstream::binary);
			 size_t sz = file->second.data[0].size();
			 FILE.write(reinterpret_cast<const char*>(&file->second.data[0][0]), sz * sizeof(file->second.data[0][0]));

			 std::cout << std::hex << "    tag: " << std::string(tag->first)
			 << std::endl << "    elements: " << tag->second.size() << std::endl
			 << "    unique: " << file->first << std::endl
			 << "    offset: " << file->second.offset << std::endl
			 << "    size: " << file->second.data[0].size() << std::endl << std::endl;
			 */
		}
	}
	//showResources();
	fResource.close();
}

RESOURCES::GFFI::~GFFI() {
//cleanup
}

std::vector<uint8_t> RESOURCES::GFFI::getMusic() {
	return (mFiles["LSE"][1].data[0]);
}

sequence RESOURCES::GFFI::getSequence() {
	sequence sequences;
	uint16_t i = 0;

	std::string filename = mGFFIFile.leaf().string();
	boost::to_upper(filename);

	if (filename == "INTRO.GFF") { // Intro sequence
		sequences[i++] = boost::make_tuple(SET_PAL, 0,
				mFiles["PAL"][1].data[0]);
		sequences[i++] = boost::make_tuple(FADE_IN, 12,
				mFiles["BMP"][1].data[0]);
		sequences[i++] = boost::make_tuple(SCROLL_LEFT, 8,
				mFiles["BMP"][2].data[0]);
		sequences[i++] = boost::make_tuple(SCROLL_LEFT, 2,
				mFiles["BMP"][1].data[0]);
		sequences[i++] = boost::make_tuple(SCROLL_LEFT, 8,
				mFiles["BMP"][3].data[0]);
		sequences[i++] = boost::make_tuple(SCROLL_LEFT, 2,
				mFiles["BMP"][1].data[0]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][1].data[0]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][2].data[1]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][2].data[2]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][2].data[3]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][2].data[4]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][2].data[5]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][2].data[6]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][3].data[1]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][3].data[2]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][3].data[3]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][3].data[4]);
		sequences[i++] = boost::make_tuple(PAN_LEFT, 3,
				mFiles["BMP"][4].data[0]);	// 3 background panels
		sequences[i++] = boost::make_tuple(PAN_LEFT, 0,
				mFiles["BMP"][5].data[0]);
		sequences[i++] = boost::make_tuple(PAN_LEFT, 0,
				mFiles["BMP"][16].data[0]);	// bonus panel
		sequences[i++] = boost::make_tuple(PAN_LEFT, 2,
				mFiles["BMP"][6].data[0]);	// 2 forground panels
		sequences[i++] = boost::make_tuple(PAN_LEFT, 12,
				mFiles["BMP"][7].data[0]);	// wait 10 seconds
		sequences[i++] = boost::make_tuple(DRAW_CURTAIN, 2,
				mFiles["BMP"][8].data[0]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][4].data[1]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][4].data[2]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][5].data[1]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][5].data[2]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][5].data[3]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][5].data[4]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 2,
				mFiles["BMA"][5].data[5]);
		sequences[i++] = boost::make_tuple(DISP_BMP, 0,
				mFiles["BMP"][9].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 4,
				mFiles["BMP"][19].data[0]);
		sequences[i++] = boost::make_tuple(MATERIALIZE, 4,
				mFiles["BMP"][10].data[0]);
		sequences[i++] = boost::make_tuple(MATERIALIZE, 4,
				mFiles["BMP"][11].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 4,
				mFiles["BMP"][20].data[0]);
		sequences[i++] = boost::make_tuple(MATERIALIZE, 4,
				mFiles["BMP"][12].data[0]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][6].data[1]);
		sequences[i++] = boost::make_tuple(DISP_BMA, 0,
				mFiles["BMA"][6].data[2]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][13].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][14].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][15].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][13].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][14].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][15].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][13].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][14].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][15].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][13].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][14].data[0]);
		sequences[i++] = boost::make_tuple(DISP_OVERLAY, 1,
				mFiles["BMP"][15].data[0]);
	} else {
		;
	}

	return (sequences);
}
