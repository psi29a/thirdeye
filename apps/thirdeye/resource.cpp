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

	std::cout << "    Entries in Table0:	"
			<< getTable(fResource, 0, mTable0)
			<< std::endl;

	std::cout << "    Entries in Table1:	"
			<< getTable(fResource, 1, mTable1)
			<< std::endl;

	std::cout << "    Entries in Table2:	"
			<< getTable(fResource, 2, mTable2)
			<< std::endl;

	std::cout << "    Entries in Table3:	"
			<< getTable(fResource, 3, mTable3)
			<< std::endl;

	std::cout << "    Entries in Table4:	"
			<< getTable(fResource, 4, mTable4)
			<< std::endl;

	getAssets(fResource);

	fResource.close();
	std::cout << std::endl;

	//showResources();
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
	uint16_t blocks = 0;
	uint32_t currentBlock = firstBlock;

	if (mDirBlocks.size() > 0)	// if already initialized, return how big it is
		return mDirBlocks.size();

	// loop through all our blocks
	do {
		//std::cout << "block: " << (int) blocks << " @ " << currentBlock << std::endl;
		seek(resourceFile, currentBlock, BOOST_IOS::beg);
		DirectoryBlock block;
		resourceFile.read(reinterpret_cast<char*>(&block), sizeof(DirectoryBlock));
		currentBlock = block.next_directory_block;
		mDirBlocks[blocks] = block;
		blocks++;
	} while ( currentBlock != 0 );
	//std::cout << "blocks: " << mDirBlocks.size() << std::endl;

	return mDirBlocks.size();
}

uint16_t RESOURCE::Resource::getEntries(file_source resourceFile) {
	uint16_t entries = 0;

	if (mEntryHeaders.size() > 0)	// if already initialized, return how big it is
		return mEntryHeaders.size();

	std::map<uint16_t, DirectoryBlock>::iterator block;
	for (block = mDirBlocks.begin(); block != mDirBlocks.end(); block++){
		for (uint8_t i = 0; i < DIRECTORY_BLOCK_ITEMS; i++){
			if (block->second.entry_header_index[i] == 0) // ignore non-existing entries
				break;

			seek(resourceFile, block->second.entry_header_index[i], BOOST_IOS::beg);
			EntryHeader entry;
			resourceFile.read(reinterpret_cast<char*>(&entry), sizeof(EntryHeader));
			mEntryHeaders[entries] = entry;
			entries++;
		}
	}
	return mEntryHeaders.size();
}

uint16_t RESOURCE::Resource::getAssets(file_source resourceFile) {
	DirectoryBlock block = mDirBlocks.begin()->second;
	uint16_t id = 0;
	std::string table1 = "";
	std::string table2 = "";
	uint16_t currentDirBlock = 0;
	uint16_t currentEntry = 0;
	uint32_t start = 0;
	uint32_t offset = 0;
	std::vector<uint8_t> blank(sizeof(uint8_t));

	if (mAssets.size() > 0)	// if already initialized, return how big it is
		return mAssets.size();

	blank[0] = ' ';
	// add info about the first 5 special tables
	mAssets[0] =
			Assets( 0,
				(char*)"Special table 0: Resource names",
				fileHeader.create_time,
				block.data_attributes[0],
				block.entry_header_index[1] - block.entry_header_index[0] - sizeof(EntryHeader),
				block.entry_header_index[0],
				block.entry_header_index[0] + sizeof(EntryHeader),
				table1,
				table2,
				blank
				);

	mAssets[1] =
			Assets( 1,
				(char*)"Special table 1 ",
				fileHeader.create_time,
				block.data_attributes[1],
				block.entry_header_index[2] - block.entry_header_index[1] - sizeof(EntryHeader),
				block.entry_header_index[1],
				block.entry_header_index[1] + sizeof(EntryHeader),
				table1,
				table2,
				blank
				);

	mAssets[2] =
			Assets( 2,
				(char*)"Special table 2 ",
				fileHeader.create_time,
				block.data_attributes[2],
				block.entry_header_index[3] - block.entry_header_index[2] - sizeof(EntryHeader),
				block.entry_header_index[2],
				block.entry_header_index[2] + sizeof(EntryHeader),
				table1,
				table2,
				blank
				);

	mAssets[3] =
			Assets( 3,
				(char*)"Special table 3: Low level functions ",
				fileHeader.create_time,
				block.data_attributes[3],
				block.entry_header_index[4] - block.entry_header_index[3] - sizeof(EntryHeader),
				block.entry_header_index[3],
				block.entry_header_index[3] + sizeof(EntryHeader),
				table1,
				table2,
				blank
				);

	mAssets[4] =
			Assets( 4,
				(char*)"Special table 4: Message names ",
				fileHeader.create_time,
				block.data_attributes[4],
				block.entry_header_index[5] - block.entry_header_index[4] - sizeof(EntryHeader),
				block.entry_header_index[4],
				block.entry_header_index[4] + sizeof(EntryHeader),
				table1,
				table2,
				blank
				);

	std::map<std::string, Dictionary>::iterator dictionary;
	for (dictionary = mTable0.begin(); dictionary != mTable0.end(); dictionary++){
		id = boost::lexical_cast<uint16_t>(dictionary->second.second);
		currentDirBlock = boost::lexical_cast<uint16_t>( id ) / DIRECTORY_BLOCK_ITEMS;
		currentEntry = boost::lexical_cast<uint16_t>( id ) % DIRECTORY_BLOCK_ITEMS;
		table1 = searchDictionary(mTable1, dictionary->second.first);
		table2 = searchDictionary(mTable2, dictionary->second.first);
		start = mDirBlocks[currentDirBlock].entry_header_index[currentEntry];
		offset = mDirBlocks[currentDirBlock].entry_header_index[currentEntry] + sizeof(EntryHeader);

		std::vector<uint8_t> data(mEntryHeaders[id].data_size);
		seek(resourceFile, offset, BOOST_IOS::beg);
		resourceFile.read(reinterpret_cast<char*>(&data[0]), mEntryHeaders[id].data_size);

		mAssets[id] =
				Assets( boost::lexical_cast<uint16_t>(dictionary->second.second),
						dictionary->second.first,
						mEntryHeaders[id].storage_time,
						mEntryHeaders[id].data_attributes,
						mEntryHeaders[id].data_size,
						start,
						offset,
						table1,
						table2,
						data
						);
	}
	return mAssets.size();
}


uint16_t RESOURCE::Resource::getTable(file_source resourceFile, uint16_t table, std::map<std::string, Dictionary> &dictionary) {
	DirectoryBlock dirBlock = mDirBlocks.begin()->second;
	uint32_t dictOffset;
	uint16_t dictStringListsNumber;
	uint32_t stringListIndex;
	uint16_t stringLength;
	char string[256];
	char prevString[256];
	uint16_t counter = 1;

	dictOffset = dirBlock.entry_header_index[table] + sizeof(EntryHeader);
	seek(resourceFile, dictOffset, BOOST_IOS::beg);
	resourceFile.read(reinterpret_cast<char*>(&dictStringListsNumber), sizeof(uint16_t));

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

				dictionary[boost::lexical_cast<std::string>( prevString )] =
						Dictionary(
								prevString,
								string
								);
			} else {
				std::strcpy(prevString, string);
			}
		}
	}
	return counter/2;
}

std::string RESOURCE::Resource::searchDictionary(std::map<std::string, Dictionary> &haystack, std::string needle){
	std::map<std::string, Dictionary>::iterator found;
	found = haystack.find(needle);
	if ( found != haystack.end() ) return found->second.second;
	else return "";
}

std::vector<uint8_t> RESOURCE::Resource::getAsset(std::string name){
	return getAsset( boost::lexical_cast<uint16_t>( searchDictionary(mTable0, name) ) );
}

std::vector<uint8_t> RESOURCE::Resource::getAsset(uint16_t number){
	return mAssets[number].data;
}

void RESOURCE::Resource::showResources(){
	std::cout << "NUMBER	START	OFFSET	SIZE	DATE			ATTRIB	NAME" << std::endl;
	for (uint16_t i = 0; i < mEntryHeaders.size(); i++){
		std::cout << mAssets[i].id
				<< "	" << mAssets[i].start
				<< "	" << mAssets[i].offset
				<< "	" << mAssets[i].size
				<< "	" << getDate(mAssets[i].date)
				<< "	" << mAssets[i].attributes
				<< "	" << mAssets[i].name
				<< "	" << mAssets[i].table1
				<< "	" << mAssets[i].table2
				//<< "	" << mAssets[i].data.size()
				<< std::endl;
	}
}
