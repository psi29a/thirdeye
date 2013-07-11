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
	fResource.read(reinterpret_cast<char*>(&fileHeader), sizeof(GlobalHeader));

	// is resource a valid RES
	if (std::string(fileHeader.signature) != AESOP_ID)
		throw std::runtime_error(
				mResFile.string() + " is not a valid AESOP resource");

	showFileHeader(fileHeader);

	std::cout << "    Number of blocks:	"
			<< getDirBlocks(fResource, fileHeader.first_directory_block)
			<< std::endl;

	std::cout << "    Number of entries:	"
			<< getEntries(fResource)
			<< std::endl;

	std::cout << "    Dictionary size:	"
			<< getDictionary(fResource)
			<< std::endl;

	fResource.close();
	std::cout << std::endl;

	showFiles();
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

uint16_t RESOURCE::Resource::getDirBlocks(file_source resourceFile, uint32_t firstBlock) {
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

uint16_t RESOURCE::Resource::getEntries(file_source resourceFile) {
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
			/*
			std::cout	<< " Entry at position: " << block->second.entry_header_index[i]
			         	<< " of " << std::setw(5) << mEntryHeaders[boost::lexical_cast<std::string>( entries )].data_size << " bytes"
			         	<< " @ " << getDate(mEntryHeaders[boost::lexical_cast<std::string>( entries )].storage_time)
			         	<< std::endl;
			 */
			entries++;
		}
	}
	return mEntryHeaders.size();
}

uint16_t RESOURCE::Resource::getDictionary(file_source resourceFile) {
	DirectoryBlock dictionary = mDirBlocks.begin()->second;
	//EntryHeader entry;
	uint64_t dictOffset;
	uint16_t dictStringListsNumber;
	uint32_t stringListIndex;
	uint16_t stringLength;
	char string[256];
	char prevString[256];
	uint16_t counter = 1;

	for (uint8_t i = 0; i < DIRECTORY_BLOCK_ITEMS; i++){
		//seek(resourceFile, dictionary.entry_header_index[i], BOOST_IOS::beg);
		//resourceFile.read(reinterpret_cast<char*>(&entry), sizeof(EntryHeader));

		dictOffset = dictionary.entry_header_index[i]
					+ sizeof(struct EntryHeader);

		seek(resourceFile, dictOffset, BOOST_IOS::beg);
		resourceFile.read(reinterpret_cast<char*>(&dictStringListsNumber), sizeof(uint16_t));

		break;
	}

	for (uint16_t i = 0; i < dictStringListsNumber; i++) {
		seek(resourceFile, dictOffset + sizeof(uint16_t) + i * sizeof(uint32_t), BOOST_IOS::beg);
		resourceFile.read(reinterpret_cast<char*>(&stringListIndex), sizeof(uint32_t));

		if (stringListIndex == 0) // end of list index
			continue;

		seek(resourceFile, stringListIndex + dictOffset, BOOST_IOS::beg);
		for (;;counter++) {

			resourceFile.read(reinterpret_cast<char*>(&stringLength), sizeof(uint16_t));

			if (stringLength == 0) // end of string list
				break;

			resourceFile.read(reinterpret_cast<char*>(&string), stringLength);
			string[stringLength] = '\0'; // terminate our string

			if (counter % 2 == 0){
				mDictionary[boost::lexical_cast<std::string>( string )] = Dictionary(string, prevString);
				//std::cout << mDictionary[boost::lexical_cast<std::string>( string )].first << " " << mDictionary[boost::lexical_cast<std::string>( string )].second << std::endl;
			} else {
				strcpy(prevString, string);
			}
		}
	}
	return mDictionary.size();
}

void RESOURCE::Resource::showFiles(){
	/*
	std::map<std::string, Dictionary>::iterator block;
	for (block = mDictionary.begin(); block != mDictionary.end(); block++){
		std::cout << block->second.first << "	" << block->second.second << std::endl;
	}

	return;
	*/
	std::cout << "NUMBER	START	REAL_START	SIZE			DATE	ATTRIB	NAME" << std::endl;
	for (uint16_t i = 5; i < mEntryHeaders.size(); i++){
		std::cout << i
				<< "	"
				<< "	"
				<< "	"
				<< "	" << mEntryHeaders[boost::lexical_cast<std::string>(i)].data_size
				<< "	" << getDate(mEntryHeaders[boost::lexical_cast<std::string>(i)].storage_time)
				<< "	"
				<< "	" << mDictionary[boost::lexical_cast<std::string>(i)].second
				<< std::endl;
	}
}
