#include <filesystem>
/*
 * res.cpp
 *
 *  Created on: Jul 9, 2013
 *      Author: bcurtis
 */
#include "res.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>


RESOURCES::Resource::Resource(std::filesystem::path resourcePath) {
	mResFile = std::move(resourcePath);

	std::cout << "Initializing Resources:" << std::endl;
	// does resource exist
	if (std::filesystem::exists(mResFile) == false)
		throw std::runtime_error(mResFile.string() + " does not exist!");
	else
		std::cout << "  Loading: " << mResFile;

	// how big is the file on disk?
	resourceFileSize = std::filesystem::file_size(mResFile);
	std::cout << " " << resourceFileSize << " bytes " << std::endl;

	// open our resource
	std::ifstream fResource;
	fResource.open(mResFile.c_str(), std::ios_base::in | std::ios_base::binary);
	/*
	 std::cout << std::boolalpha;
	 std::cout << "good/bad/ugly: " << fResource.good() << " " << fResource.bad() << " " << fResource.fail() << " " << fResource.is_open() << std::endl;
	 std::cout << "error reading file "
	 << " error: " << strerror( errno ) << std::endl;
	 fResource.exceptions( std::ios::failbit );
	 */

	if (fResource.is_open() == false)
		throw std::runtime_error("Could not open file " + mResFile.string());

	// read in our initial header
	fResource.read(reinterpret_cast<char*>(&fileHeader), sizeof(GlobalHeader));

	// is resource a valid RES
	if (std::string(fileHeader.signature) != AESOP_ID)
		throw std::runtime_error(
				mResFile.string() + " is not a valid AESOP resource");

	showFileHeader(fileHeader);

	std::cout << "    Number of blocks:	"
			<< getDirBlocks(fResource, fileHeader.first_directory_block)
			<< std::endl;

	std::cout << "    Number of entries:	" << getEntries(fResource)
			<< std::endl;

	std::cout << "    Entries in Table0:	" << getTable(fResource, 0, mTable0)
			<< std::endl;

	std::cout << "    Entries in Table1:	" << getTable(fResource, 1, mTable1)
			<< std::endl;

	std::cout << "    Entries in Table2:	" << getTable(fResource, 2, mTable2)
			<< std::endl;

	std::cout << "    Entries in Table3:	" << getTable(fResource, 3, mTable3)
			<< std::endl;

	std::cout << "    Entries in Table4:	" << getTable(fResource, 4, mTable4)
			<< std::endl;

	getAssets(fResource);
	fResource.close();

	//std::cout << std::endl;
	//showResources();
}

RESOURCES::Resource::~Resource() {
	//cleanup
}

std::string RESOURCES::Resource::getDate(uint32_t uiDate) {
	std::string months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
			"Aug", "Sep", "Oct", "Nov", "Dec" };

	std::ostringstream out;
	out << months[((uiDate >> 21) & 0x000f) - 1] << " " << std::setfill('0')
			<< std::setw(2) << ((uiDate >> 16) & 0x001f) << ", "
			<< (1980 + ((uiDate >> 25) & 0x003f)) << " "
			<< ((uiDate >> 11) & 0x001f) << ":" << std::setfill('0')
			<< std::setw(2) << ((uiDate >> 5) & 0x001f) << ":"
			<< std::setfill('0') << std::setw(2) << ((uiDate & 0x001f) << 1);

	return (out.str());
}

void RESOURCES::Resource::showFileHeader(GlobalHeader localFileHeader) {
	std::cout << "    Signature:		" << localFileHeader.signature << std::endl;
	std::cout << "    Size:		" << localFileHeader.file_size << std::endl;
	std::cout << "    Lost Space:		" << localFileHeader.lost_space << std::endl;
	std::cout << "    Created:		" << getDate(localFileHeader.create_time)
			<< std::endl;
	std::cout << "    Modified:		" << getDate(localFileHeader.modify_time)
			<< std::endl;
}

uint16_t RESOURCES::Resource::getDirBlocks(std::ifstream &resourceFile,
		uint32_t firstBlock) {
	uint16_t blocks = 0;
	uint32_t currentBlock = firstBlock;

	if (mDirBlocks.size() > 0)	// if already initialized, return how big it is
		return (static_cast<uint16_t>(mDirBlocks.size()));

	// loop through all our blocks
	do {
		//std::cout << "block: " << (int) blocks << " @ " << currentBlock << std::endl;
		resourceFile.seekg(currentBlock, std::ios::beg);
		DirectoryBlock block;
		resourceFile.read(reinterpret_cast<char*>(&block),
				sizeof(DirectoryBlock));
		currentBlock = block.next_directory_block;
		mDirBlocks[blocks] = block;
		blocks++;
	} while (currentBlock != 0);
	//std::cout << "blocks: " << mDirBlocks.size() << std::endl;

	return (static_cast<uint16_t>(mDirBlocks.size()));
}

uint16_t RESOURCES::Resource::getEntries(std::ifstream &resourceFile) {
	uint16_t entries = 0;

	if (mEntryHeaders.size() > 0)// if already initialized, return how big it is
		return (static_cast<uint16_t>(mEntryHeaders.size()));

	std::map<uint16_t, DirectoryBlock>::iterator block;
	for (block = mDirBlocks.begin(); block != mDirBlocks.end(); block++) {
		for (uint8_t i = 0; i < DIRECTORY_BLOCK_ITEMS; i++) {
			if (block->second.entry_header_index[i] == 0) // ignore non-existing entries
				break;

			resourceFile.seekg(block->second.entry_header_index[i],
					std::ios::beg);
			EntryHeader entry;
			resourceFile.read(reinterpret_cast<char*>(&entry),
					sizeof(EntryHeader));
			mEntryHeaders[entries] = entry;
			entries++;
		}
	}
	return (static_cast<uint16_t>(mEntryHeaders.size()));
}

uint16_t RESOURCES::Resource::getAssets(std::ifstream &resourceFile) {
	const DirectoryBlock &block = mDirBlocks.begin()->second;
	uint16_t id = 0;
	std::string table1 = "";
	std::string table2 = "";
	uint16_t currentDirBlock = 0;
	uint16_t currentEntry = 0;
	uint32_t start = 0;
	uint32_t offset = 0;
	std::vector<uint8_t> blank(sizeof(uint8_t));

	if (mAssets.size() > 0)	// if already initialized, return how big it is
		return (static_cast<uint16_t>(mAssets.size()));

	blank[0] = ' ';
	// add info about the first 5 special tables
	mAssets[0] = Assets(0, (char*) "Special table 0: Resource names",
			fileHeader.create_time, block.data_attributes[0],
			block.entry_header_index[1] - block.entry_header_index[0]
					- sizeof(EntryHeader), block.entry_header_index[0],
			block.entry_header_index[0] + sizeof(EntryHeader), table1, table2,
			blank);

	mAssets[1] = Assets(1, (char*) "Special table 1 ", fileHeader.create_time,
			block.data_attributes[1],
			block.entry_header_index[2] - block.entry_header_index[1]
					- sizeof(EntryHeader), block.entry_header_index[1],
			block.entry_header_index[1] + sizeof(EntryHeader), table1, table2,
			blank);

	mAssets[2] = Assets(2, (char*) "Special table 2 ", fileHeader.create_time,
			block.data_attributes[2],
			block.entry_header_index[3] - block.entry_header_index[2]
					- sizeof(EntryHeader), block.entry_header_index[2],
			block.entry_header_index[2] + sizeof(EntryHeader), table1, table2,
			blank);

	mAssets[3] = Assets(3, (char*) "Special table 3: Low level functions ",
			fileHeader.create_time, block.data_attributes[3],
			block.entry_header_index[4] - block.entry_header_index[3]
					- sizeof(EntryHeader), block.entry_header_index[3],
			block.entry_header_index[3] + sizeof(EntryHeader), table1, table2,
			blank);

	mAssets[4] = Assets(4, (char*) "Special table 4: Message names ",
			fileHeader.create_time, block.data_attributes[4],
			block.entry_header_index[5] - block.entry_header_index[4]
					- sizeof(EntryHeader), block.entry_header_index[4],
			block.entry_header_index[4] + sizeof(EntryHeader), table1, table2,
			blank);

	std::map<std::string, Dictionary>::iterator dictionary;
	for (dictionary = mTable0.begin(); dictionary != mTable0.end();
			dictionary++) {
		id = static_cast<uint16_t>(std::stoi(dictionary->second.second));
		currentDirBlock = id / DIRECTORY_BLOCK_ITEMS;
		currentEntry = id % DIRECTORY_BLOCK_ITEMS;
		table1 = searchDictionary(mTable1, dictionary->second.first);
		table2 = searchDictionary(mTable2, dictionary->second.first);
		start = mDirBlocks[currentDirBlock].entry_header_index[currentEntry];
		offset = mDirBlocks[currentDirBlock].entry_header_index[currentEntry]
				+ sizeof(EntryHeader);

		std::vector<uint8_t> data(mEntryHeaders[id].data_size);
		resourceFile.seekg(offset, std::ios::beg);
		resourceFile.read(reinterpret_cast<char*>(&data[0]),
				mEntryHeaders[id].data_size);

		// table1/table2 get reassigned at the top of the next iteration.
		mAssets[id] = Assets(
				id,
				dictionary->second.first, mEntryHeaders[id].storage_time,
				mEntryHeaders[id].data_attributes, mEntryHeaders[id].data_size,
				start, offset, std::move(table1), std::move(table2),
				std::move(data));
	}
	return (static_cast<uint16_t>(mAssets.size()));
}

uint16_t RESOURCES::Resource::getTable(std::ifstream &resourceFile,
		uint16_t table, std::map<std::string, Dictionary> &dictionary) {
	DirectoryBlock dirBlock = mDirBlocks.begin()->second;
	uint32_t dictOffset;
	uint16_t dictStringListsNumber;
	uint32_t stringListIndex;
	uint16_t stringLength;
	char string[256];
	char prevString[256];
	uint16_t counter = 1;

	dictOffset = dirBlock.entry_header_index[table] + sizeof(EntryHeader);
	resourceFile.seekg(dictOffset, std::ios::beg);
	resourceFile.read(reinterpret_cast<char*>(&dictStringListsNumber),
			sizeof(uint16_t));

	for (uint16_t i = 0; i < dictStringListsNumber; i++) {
		resourceFile.seekg(dictOffset + sizeof(uint16_t) + i * sizeof(uint32_t),
				std::ios::beg);
		resourceFile.read(reinterpret_cast<char*>(&stringListIndex),
				sizeof(uint32_t));

		if (stringListIndex == 0) // end of list index
			continue;

		resourceFile.seekg(stringListIndex + dictOffset, std::ios::beg);
		for (;; counter++) {
			resourceFile.read(reinterpret_cast<char*>(&stringLength),
					sizeof(uint16_t));

			if (stringLength == 0) // end of string list
				break;

			resourceFile.read(reinterpret_cast<char*>(&string), stringLength);
			string[stringLength] = '\0'; // terminate our string

			if (counter % 2 == 0) {

				dictionary[std::string(prevString)] =
						Dictionary(prevString, string);
			} else {
				std::strcpy(prevString, string);
			}
		}
	}
	return (counter / 2);
}

std::string RESOURCES::Resource::searchDictionary(
		std::map<std::string, Dictionary> &haystack, std::string needle) {
	std::map<std::string, Dictionary>::iterator found;
	found = haystack.find(needle);
	if (found != haystack.end())
		return (found->second.second);
	else
		return ("");
}

std::vector<uint8_t> &RESOURCES::Resource::getAsset(std::string name) {
	return getAsset(
			static_cast<uint16_t>(std::stoi(searchDictionary(mTable0, std::move(name)))));
}

std::vector<uint8_t> &RESOURCES::Resource::getAsset(uint16_t number) {
	return (mAssets[number].data);
}

std::string RESOURCES::Resource::getTableEntry(const std::string &name,
		uint8_t table) {
	return getTableEntry(
			static_cast<uint16_t>(std::stoi(searchDictionary(mTable0, name))),
			table);
}

std::string RESOURCES::Resource::getTableEntry(uint16_t number, uint8_t table) {
	if (table == 1)
		return (mAssets[number].table1);
	else if (table == 2)
		return (mAssets[number].table2);
	else
		throw(std::runtime_error("Wrong table!"));
}

std::vector<std::string> RESOURCES::Resource::getCodeResourceNames() {
	// A SOP code object always has a companion "<name>.EXPT" export resource.
	// (The data attribute alone is not reliable -- bitmaps etc. can share it.)
	std::vector<std::string> names;
	for (const auto &entry : mAssets) {
		const std::string &name = entry.second.name;
		if (name.empty())
			continue;
		// Skip the .IMPT/.EXPT companions themselves.
		auto endsWith = [&](const char *suf) {
			size_t n = std::strlen(suf);
			return name.size() >= n && name.compare(name.size() - n, n, suf) == 0;
		};
		if (endsWith(".EXPT") || endsWith(".IMPT"))
			continue;
		if (!searchDictionary(mTable0, name + ".EXPT").empty())
			names.push_back(name);
	}
	return names;
}

// Parse an AESOP dictionary blob (same on-disk format as the special tables
// 0..4, see getTable): a u16 bucket count, then that many u32 chain offsets
// (relative to the blob start); each non-zero chain is a run of length-prefixed
// strings (u16 length incl. trailing NUL) alternating key, value, terminated by
// a zero length.
std::map<std::string, std::string> RESOURCES::Resource::parseDictionary(
		const std::vector<uint8_t> &d) {
	std::map<std::string, std::string> out;
	if (d.size() < 2)
		return out;

	auto rd16 = [&](size_t off) -> uint16_t {
		return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
	};
	auto rd32 = [&](size_t off) -> uint32_t {
		return d[off] | (d[off + 1] << 8) | (d[off + 2] << 16) |
		       (static_cast<uint32_t>(d[off + 3]) << 24);
	};

	uint16_t buckets = rd16(0);
	uint16_t counter = 1;
	std::string prev;
	for (uint16_t i = 0; i < buckets; i++) {
		size_t boff = 2 + static_cast<size_t>(i) * 4;
		if (boff + 4 > d.size())
			break;
		uint32_t chain = rd32(boff);
		if (chain == 0)
			continue;
		size_t p = chain;
		for (;; counter++) {
			if (p + 2 > d.size())
				break;
			uint16_t len = rd16(p);
			p += 2;
			if (len == 0) // end of chain
				break;
			if (p + len > d.size())
				break;
			std::string s(reinterpret_cast<const char *>(&d[p]), len);
			if (!s.empty() && s.back() == '\0')
				s.pop_back(); // drop trailing NUL counted in len
			p += len;
			if (counter % 2 == 0)
				out[prev] = std::move(s);
			else
				prev = std::move(s);
		}
	}
	return out;
}

std::map<std::string, std::string> RESOURCES::Resource::getExports(
		const std::string &codeName) {
	std::string num = searchDictionary(mTable0, codeName + ".EXPT");
	if (num.empty())
		return {};
	return parseDictionary(getAsset(static_cast<uint16_t>(std::stoi(num))));
}

int RESOURCES::Resource::getResourceNumber(const std::string &name) {
	std::string num = searchDictionary(mTable0, name);
	return num.empty() ? -1 : std::stoi(num);
}

std::string RESOURCES::Resource::getResourceName(uint16_t number) {
	auto it = mAssets.find(number);
	return it == mAssets.end() ? std::string() : it->second.name;
}

std::map<std::string, std::string> RESOURCES::Resource::getImports(
		const std::string &codeName) {
	std::string num = searchDictionary(mTable0, codeName + ".IMPT");
	if (num.empty())
		return {};
	return parseDictionary(getAsset(static_cast<uint16_t>(std::stoi(num))));
}

void RESOURCES::Resource::showResources() {
	std::cout << "NUMBER	START	OFFSET	SIZE	DATE			ATTRIB	NAME" << std::endl;
	for (size_t i = 0; i < mEntryHeaders.size(); i++) {
		const auto &a = mAssets[static_cast<uint16_t>(i)];
		std::cout << a.id << "	" << a.start << "	"
				<< a.offset << "	" << a.size << "	"
				<< getDate(a.date) << "	" << a.attributes
				<< "	" << a.name << "	" << a.table1 << "	"
				<< a.table2
				//<< "	" << a.data.size()
				<< std::endl;
	}
}
