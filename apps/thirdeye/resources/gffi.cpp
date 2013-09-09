/*
 * gffi.cpp
 *
 *  Created on: Sept 3, 2013
 *      Author: bcurtis
 */
#include "gffi.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/positioning.hpp>
#include <boost/serialization/vector.hpp>

using boost::iostreams::file_source;
using boost::iostreams::seek;
using boost::iostreams::stream_offset;

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

	seek(fResource, mGFFIHeader.directory_offset, BOOST_IOS::beg);
	fResource.read(reinterpret_cast<char*>(&mGFFIDirectoryHeader),
			sizeof(GFFIDirectoryHeader));

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

	for (uint16_t block = 0; block < mGFFIDirectoryHeader.number_of_tags;
			block++) {

		GFFIBlockHeader mGFFIBlockHeader;
		fResource.read(reinterpret_cast<char*>(&mGFFIBlockHeader), sizeof(GFFIBlockHeader));

		if (mGFFIBlockHeader.tag[3] == 0x20)
			mGFFIBlockHeader.tag[3] = '\0';

		for (uint16_t elements = 0; elements < mGFFIBlockHeader.number_of_elements;
				elements++) {

			GFFIBlock mGFFIBlock;

			fResource.read(reinterpret_cast<char*>(&mGFFIBlock),
					sizeof(mGFFIBlock));

			/*
			std::cout << std::hex << "    tag: " << std::string(mGFFIBlockHeader.tag)
					<< std::endl << "    elements: "
					<< mGFFIBlockHeader.number_of_elements << std::endl
					<< "    unique: " << mGFFIBlock.unique << std::endl
					<< "    offset: " << mGFFIBlock.offset << std::endl
					<< "    size: " << mGFFIBlock.size << std::endl << std::endl;
			*/
			mFiles[mGFFIBlockHeader.tag][mGFFIBlock.unique].offset = mGFFIBlock.offset;
			mFiles[mGFFIBlockHeader.tag][mGFFIBlock.unique].data.resize(mGFFIBlock.size);
		}
	}

	//std::cout << "Data dump:" << std::endl;
	std::map< std::string, std::map< uint32_t, File > >::iterator tag;
	for (tag = mFiles.begin(); tag != mFiles.end(); tag++) {
		std::map< uint32_t, File >::iterator file;
		for (file = tag->second.begin(); file != tag->second.end(); file++) {

			seek(fResource, file->second.offset, BOOST_IOS::beg);
			fResource.read(reinterpret_cast<char*>(&file->second.data[0]),
					file->second.data.size());
			/*
			std::string Path = "/tmp/"+boost::lexical_cast<std::string>(file->first)+"."+tag->first;
			std::ofstream FILE(Path.c_str(), std::ios::out | std::ofstream::binary);
			size_t sz = file->second.data.size();
			FILE.write(reinterpret_cast<const char*>(&file->second.data[0]), sz * sizeof(file->second.data[0]));
			std::cout << std::hex << "    tag: " << std::string(tag->first)
					<< std::endl << "    elements: " << tag->second.size() << std::endl
					<< "    unique: " << file->first << std::endl
					<< "    offset: " << file->second.offset << std::endl
					<< "    size: " << file->second.data.size() << std::endl << std::endl;
			*/
		}

	}
	//showResources();
}

RESOURCES::GFFI::~GFFI() {
	//cleanup
}
