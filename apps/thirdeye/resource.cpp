/*
 * resource.cpp
 *
 *  Created on: Jul 9, 2013
 *      Author: bcurtis
 */
#include "resource.hpp"

#include <sstream>
#include <iomanip>

#include <boost/filesystem.hpp>

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/positioning.hpp>

using boost::iostreams::file_source;
using boost::iostreams::seek;

#include <boost/lexical_cast.hpp> // hack!

RESOURCE::Resource::Resource(boost::filesystem::path resourcePath) {
	mResFile = resourcePath;
	char buffer[sizeof(GlobalHeader)];

	std::cout << "Initializing Resources:" << std::endl;

	// get our file
	file_source fResource(mResFile.string(), BOOST_IOS::binary);

	// does resource exist
	if (boost::filesystem::exists(mResFile) == false)
		throw std::runtime_error(mResFile.string() + " does not exist!");
	else
		std::cout << "  Loading: " << mResFile;

	// how big is the file on disk?
	resourceFileSize = boost::filesystem::file_size(mResFile);
	std::cout << " " << resourceFileSize << " bytes " << std::endl;

	// open our resource
	fResource.open(mResFile.c_str());
	if (fResource.is_open() == false)
		throw std::runtime_error("Could not open file " + mResFile.string());

	// reset to begin of file, read from file
	seek(fResource, 0, BOOST_IOS::beg);
	fResource.read(buffer, sizeof(GlobalHeader));
	std::memcpy(&fileHeader, &buffer, sizeof(GlobalHeader));

	// is resource a valid RES
	if (std::string(fileHeader.signature) != AESOP_ID)
		throw std::runtime_error(
				mResFile.string() + " is not a valid AESOP resource");

	showFileHeader(fileHeader);

	getDirBlocks(fResource, fileHeader.first_directory_block);
	getEntries(fResource);

	fResource.close();
	std::cout << std::endl;
}

RESOURCE::Resource::~Resource() {
	//cleanup
}

std::string RESOURCE::Resource::getDate(uint32_t uiDate) {
	std::string months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
			"Aug", "Sep", "Oct", "Nov", "Dec" };

	std::ostringstream out;
	out << months[((uiDate >> 21) & 0x000f) - 1] << " " << std::setfill('0')
			<< std::setw(2) << ((uiDate >> 16) & 0x001f) << ", "
			<< (1980 + ((uiDate >> 25) & 0x003f)) << " "
			<< ((uiDate >> 11) & 0x001f) << ":" << std::setfill('0')
			<< std::setw(2) << ((uiDate >> 5) & 0x001f) << ":"
			<< std::setfill('0') << std::setw(2) << ((uiDate & 0x001f) << 1);

	return out.str();
}

void RESOURCE::Resource::showFileHeader(GlobalHeader fileHeader) {
	std::cout << "    Signature:		" << fileHeader.signature << std::endl;
	std::cout << "    Size:		" << fileHeader.file_size << std::endl;
	std::cout << "    Lost Space:		" << fileHeader.lost_space << std::endl;
	std::cout << "    Created:		" << getDate(fileHeader.create_time) << std::endl;
	std::cout << "    Modified:		" << getDate(fileHeader.modify_time) << std::endl;
}

uint8_t RESOURCE::Resource::getDirBlocks(file_source resourceFile, uint32_t firstBlock) {
	uint8_t blocks = 0;
	uint32_t currentBlock = firstBlock;

	// loop through all our blocks
	do {
		//std::cout << "block: " << (int) blocks << " @ " << currentBlock << std::endl;
		seek(resourceFile, currentBlock, BOOST_IOS::beg);
		DirectoryBlock block;
		resourceFile.read(reinterpret_cast<char*>(&block), sizeof(DirectoryBlock));
		currentBlock = block.next_directory_block;
		mDirBlocks[boost::lexical_cast<std::string>( blocks )] = block;
		blocks++;
	} while ( currentBlock != 0 );
	//std::cout << "blocks: " << mDirBlocks.size() << std::endl;

	return mDirBlocks.size();
}

uint8_t RESOURCE::Resource::getEntries(file_source resourceFile) {
	uint16_t entries = 0;
	std::map<std::string, DirectoryBlock>::iterator block;
	for (block = mDirBlocks.begin(); block != mDirBlocks.end(); block++){
		for (uint8_t i = 0; i < DIRECTORY_BLOCK_ITEMS; i++){
			if (block->second.entry_header_index[i] == 0) // ignore non-existing entries
				break;

			seek(resourceFile, block->second.entry_header_index[i], BOOST_IOS::beg);
			EntryHeader entry;
			resourceFile.read(reinterpret_cast<char*>(&entry), sizeof(EntryHeader));
			mEntryHeaders[boost::lexical_cast<std::string>( entries )] = entry;
			std::cout	<< " - " << block->second.entry_header_index[i]
			         	<< " : " << std::setw(5) << mEntryHeaders[boost::lexical_cast<std::string>( entries )].data_size
			         	<< " @ " << getDate(mEntryHeaders[boost::lexical_cast<std::string>( entries )].storage_time)
			         	<< std::endl;
			entries++;
		}
	}
}
