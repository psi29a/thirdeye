/*
 * resource.cpp
 *
 *  Created on: Jul 9, 2013
 *      Author: bcurtis
 */
#include "resource.hpp"

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

RESOURCE::Resource::Resource(boost::filesystem::path resourcePath) {
	mResFile = resourcePath;
	boost::iostreams::mapped_file_source mMapResource;
	uint32_t mMapFileSize = 0;
	GlobalHeader *mResFileHeader;

	std::cout << "Initializing Resources:" << std::endl;


	// does resource exist
	if ( boost::filesystem::exists(mResFile) == false )
		throw std::runtime_error(mResFile.string() + "' does not exist!");
	else
		std::cout << "  Loading: " << mResFile;

	mMapFileSize = boost::filesystem::file_size(mResFile);
	std::cout << " " << mMapFileSize << " bytes " << std::endl;
	mMapResource.open(mResFile.c_str(), mMapFileSize);

	if ( mMapResource.is_open() == false)
		throw std::runtime_error( "Could not open file " + mResFile.string() );

	mResFileHeader = (GlobalHeader*)mMapResource.data();

	// is resource a valid RES
	if ( std::string(mResFileHeader->signature) != AESOP_ID )
		throw std::runtime_error( mResFile.string() + " is not a valid AESOP resource" );

	std::cout << "    Signature:	" << mResFileHeader->signature << std::endl;
	std::cout << "    Size:	" << mResFileHeader->file_size << std::endl;
	std::cout << "    Created:	" << mResFileHeader->create_time << std::endl;
	std::cout << "    Modified:	" << mResFileHeader->modify_time << std::endl;

	mMapResource.close();
	std::cout << std::endl;
}


RESOURCE::Resource::~Resource() {
	//cleanup
}

