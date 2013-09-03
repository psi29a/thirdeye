/*
 * gffi.cpp
 *
 *  Created on: Jul 9, 2013
 *      Author: bcurtis
 */
#include "gffi.hpp"

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/positioning.hpp>

using boost::iostreams::file_source;
using boost::iostreams::seek;

#include <boost/lexical_cast.hpp>

RESOURCES::GFFI::GFFI(boost::filesystem::path gffiPath) {
	mGFFIFile = gffiPath;

	// get our file
	file_source fResource(mGFFIFile.string(), BOOST_IOS::binary);

	// does resource exist
	if (boost::filesystem::exists(mGFFIFile) == false)
		throw std::runtime_error(mGFFIFile.string() + " does not exist!");
	else
		std::cout << "  Loading Cinematic: " << mGFFIFile;

	// how big is the file on disk?
	mGFFIFileSize = boost::filesystem::file_size(mGFFIFile);
	std::cout << " " << mGFFIFileSize << " bytes " << std::endl;

	// open our resource
	fResource.open(mGFFIFile.c_str());
	if (fResource.is_open() == false)
		throw std::runtime_error("Could not open file " + mGFFIFile.string());

	// reset to begin of file, read from file
	seek(fResource, 0, BOOST_IOS::beg);
	fResource.read(reinterpret_cast<char*>(&mGFFIHeader), sizeof(GFFIHeader));

	// is resource a valid GFFI
	if (std::string(mGFFIHeader.signature) != GFFI_ID)
		throw std::runtime_error(
				mGFFIFile.string() + " is not a valid GFFI resource");

	std::cout << std::hex << "    sig: " << mGFFIHeader.signature << std::endl
			<< "    unknown1: " << mGFFIHeader.unknown1 << std::endl
			<< "    unknown2: " << mGFFIHeader.unknown2 << std::endl
			<< "    start of body: " << mGFFIHeader.header << std::endl
			<< "    dir offset: " << mGFFIHeader.directory_offset << std::endl
			<< "    dir size: " << mGFFIHeader.directory_size << std::endl
			<< "    unknown3: " << mGFFIHeader.unknown3 << std::endl
			<< "    unknown4: " << mGFFIHeader.unknown4 << std::endl
			<< "    size offset: " << sizeof(GFFIDirectoryHeader) << std::endl
			<< std::endl;

	seek(fResource, mGFFIHeader.directory_offset, BOOST_IOS::beg);
	fResource.read(reinterpret_cast<char*>(&mGFFIDirectoryHeader),
			sizeof(GFFIDirectoryHeader));

	std::cout << std::hex << "    unknown1: " << mGFFIDirectoryHeader.unknown1
			<< std::endl << "    dir size: "
			<< mGFFIDirectoryHeader.directory_size << std::endl
			<< "    num of tags: " << mGFFIDirectoryHeader.number_of_tags
			<< std::endl << "    offset 1st tag: "
			<< mGFFIHeader.directory_offset + sizeof(GFFIDirectoryHeader)
			<< std::endl << std::endl;

	seek(fResource,
			mGFFIHeader.directory_offset + mGFFIDirectoryHeader.directory_size,
			BOOST_IOS::beg);
	uint16_t end_tag;
	fResource.read(reinterpret_cast<char*>(&end_tag), sizeof(char) * 4);
	if (end_tag != 0)
		throw std::runtime_error(
				"Could not find end of directory in GFFI resource.");

	seek(fResource, mGFFIHeader.directory_offset + sizeof(GFFIDirectoryHeader),
			BOOST_IOS::beg);
	char tag[4];
	fResource.read(reinterpret_cast<char*>(&tag), sizeof(tag));

	std::cout << std::hex << "    end_tag: " << end_tag << std::endl
			<< std::endl;

	seek(fResource, mGFFIHeader.directory_offset + sizeof(GFFIDirectoryHeader),
			BOOST_IOS::beg);

	for (uint16_t block = 0; block < mGFFIDirectoryHeader.number_of_tags;
			block++) {

		GFFIBlock mGFFIBlock;
		fResource.read(reinterpret_cast<char*>(&mGFFIBlock), sizeof(GFFIBlock));

		if (mGFFIBlock.tag[3] == 0x20)
			mGFFIBlock.tag[3] = '\0';

		for (uint16_t elements = 0; elements < mGFFIBlock.number_of_elements;
				elements++) {
			uint32_t element_unique;		// location of first element
			fResource.read(reinterpret_cast<char*>(&element_unique),
					sizeof(uint32_t));
			uint32_t element_offset;		// location of first element
			fResource.read(reinterpret_cast<char*>(&element_offset),
					sizeof(uint32_t));
			uint32_t element_size;		// size of first element
			fResource.read(reinterpret_cast<char*>(&element_size),
					sizeof(uint32_t));
			std::cout << std::hex << "    tag: " << std::string(mGFFIBlock.tag)
					<< std::endl << "    elements: "
					<< mGFFIBlock.number_of_elements << std::endl
					<< "    unique: " << element_unique << std::endl
					<< "    offset: " << element_offset << std::endl
					<< "    size: " << element_size << std::endl << std::endl;
		}
	}
	//showResources();
}

RESOURCES::GFFI::~GFFI() {
	//cleanup
}
