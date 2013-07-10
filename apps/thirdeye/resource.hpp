/*
 * resource.hpp
 *
 *  Created on: Jul 9, 2013
 *      Author: bcurtis
 */

#ifndef RESOURCE_HPP_
#define RESOURCE_HPP_

#include <map>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>

using boost::iostreams::file_source;

#define AESOP_ID "AESOP/16 V1.00"
#define DIRECTORY_BLOCK_ITEMS 128
//#define MAX_DIRECTORIES 200

struct GlobalHeader {
	char signature[16]; // must be == "AESOP/16 V1.00\0" + 1 garbage character
	uint32_t file_size; // the total size of the .RES file
	uint32_t lost_space;
	uint32_t first_directory_block; // offset of first directory block within the file
	uint32_t create_time;            // DOS format (32 bit)
	uint32_t modify_time;            // DOS format (32 bit)
};

struct DirectoryBlock {
	uint32_t next_directory_block; // the offset of the next RESDirectoryBlock struct in the file
	uint8_t data_attributes[DIRECTORY_BLOCK_ITEMS]; // =1 if the corresponding entry is unused (free)
	uint32_t entry_header_index[DIRECTORY_BLOCK_ITEMS]; // exactly 128 file entries follow
};

struct EntryHeader {
	uint32_t storage_time;
	uint32_t data_attributes;
	uint32_t data_size;
};

namespace RESOURCE {
// Main engine class, that brings together all the components of Thirdeye
class Resource {
	boost::filesystem::path mResFile;
	std::map<std::string, DirectoryBlock> mDirBlocks;
	std::map<std::string, EntryHeader> mEntryHeaders;
	GlobalHeader fileHeader;
	uint32_t resourceFileSize;
public:
	Resource(boost::filesystem::path resourcePath);
	virtual ~Resource();

	uint8_t getDirBlocks(file_source resourceFile, uint32_t firstBlock);
	uint8_t getEntries(file_source resourceFile);

	void getDir();
	void getEntry();

	void showFileHeader(GlobalHeader fileHeader);
	void showDirs();
	void showFiles();

	std::string getDate(uint32_t uiDate);
};
}

#endif /* RESOURCE_HPP_ */
