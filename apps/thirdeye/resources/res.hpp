/*
 * resource.hpp
 *
 *  Created on: Jul 9, 2013
 *      Author: bcurtis
 */

#ifndef RES_HPP
#define RES_HPP

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <filesystem>

#if defined(__GNUC__)
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#elif defined(_MSC_VER )
#define PACK(__Declaration__) __pragma(pack(push,1)) __Declaration__ __pragma(pack(pop))
#else
#error "Unknown platform!"
#endif

namespace RESOURCES {

#define AESOP_ID "AESOP/16 V1.00"
#define DIRECTORY_BLOCK_ITEMS 128

PACK(struct GlobalHeader {
	char signature[16]; // must be == "AESOP/16 V1.00\0" + 1 garbage character
		uint32_t file_size; // the total size of the .RES file
		uint32_t lost_space;
		uint32_t first_directory_block; // offset of first directory block within the file
		uint32_t create_time;            // DOS format (32 bit)
		uint32_t modify_time;
		// DOS format (32 bit)
	});

PACK(struct DirectoryBlock { uint32_t next_directory_block; // the offset of the next RESDirectoryBlock struct in the file
		uint8_t data_attributes[DIRECTORY_BLOCK_ITEMS];// =1 if the corresponding entry is unused (free)
		uint32_t entry_header_index[DIRECTORY_BLOCK_ITEMS];// exactly 128 file entries follow
		});

struct EntryHeader {
	uint32_t storage_time;
	uint32_t data_attributes;
	uint32_t data_size;
};

struct Assets {
	Assets(uint16_t fst, std::string snd, uint32_t thr, uint32_t frt,
			uint32_t fth, uint32_t sxt, uint32_t svt, std::string ect,
			std::string nth, std::vector<uint8_t> ten) :
			id(fst), name(std::move(snd)), date(thr), attributes(frt), size(fth), start(
					sxt), offset(svt), table1(std::move(ect)), table2(std::move(nth)), data(std::move(ten))

	{
	}
	uint16_t id;		// id of entry
	std::string name;	// name of entry
	uint32_t date;		// date created
	uint32_t attributes;		// attribute of entry
	uint32_t size;		// size of entry
	uint32_t start;		// where entry begins in file
	uint32_t offset;	// where data of entry begins in file
	std::string table1;	// object name from table1
	std::string table2;	// object name from table1
	std::vector<uint8_t> data;	// object data

	Assets() {
		id = 0;
		name = "";
		date = 0;
		attributes = 0;
		size = 0;
		start = 0;
		offset = 0;
		table1 = "";
		table2 = "";
	}
};

struct Dictionary {
	Dictionary(char* fst, char* snd) :
			first(fst), second(snd) {
	}
	std::string first;
	std::string second;

	Dictionary() {
		first = "";
		second = "";
	}
};

class Resource {
	std::filesystem::path mResFile;
	std::map<uint16_t, DirectoryBlock> mDirBlocks;
	std::map<uint16_t, EntryHeader> mEntryHeaders;
	std::map<uint16_t, Assets> mAssets;
	std::map<std::string, Dictionary> mTable0;
	std::map<std::string, Dictionary> mTable1;
	std::map<std::string, Dictionary> mTable2;
	std::map<std::string, Dictionary> mTable3;
	std::map<std::string, Dictionary> mTable4;
	GlobalHeader fileHeader;
	uint32_t resourceFileSize;

private:
	uint16_t getDirBlocks(std::ifstream &resourceFile, uint32_t firstBlock);
	uint16_t getEntries(std::ifstream &resourceFile);
	uint16_t getTable(std::ifstream &resourceFile, uint16_t table,
			std::map<std::string, Dictionary> &dictionary);
	static std::map<std::string, std::string> parseDictionary(
			const std::vector<uint8_t> &bytes);
	uint16_t getAssets(std::ifstream &resourceFile);
	std::string getDate(uint32_t uiDate);
	std::string searchDictionary(std::map<std::string, Dictionary> &dictionary,
			std::string needle);

public:
	Resource(std::filesystem::path resourcePath);
	virtual ~Resource();

	/// The .RES file this resource was opened from (used to locate sibling
	/// files like the INTRO.GFF cinematic that live beside it).
	const std::filesystem::path &resourcePath() const { return mResFile; }

	std::vector<uint8_t> &getAsset(std::string name);
	std::vector<uint8_t> &getAsset(uint16_t number);
	std::string getTableEntry(std::string name, uint8_t table);
	std::string getTableEntry(uint16_t number, uint8_t table);

	/// Names of SOP code resources (those with the code attribute set).
	std::vector<std::string> getCodeResourceNames();

	/// Resource number for a given name, or -1 if not found.
	int getResourceNumber(const std::string &name);

	/// Resource name for a given number, or "" if not found.
	std::string getResourceName(uint16_t number);

	/// Parse a code object's <name>.EXPT export dictionary into key->value
	/// pairs (e.g. "N:OBJECT"->object name, "M:0"->handler entry offset).
	/// Returns an empty map if the export resource is missing.
	std::map<std::string, std::string> getExports(const std::string &codeName);

	/// Parse a code object's <name>.IMPT import dictionary into key->value
	/// pairs (e.g. "C:launch"->runtime-function number). Empty if missing.
	std::map<std::string, std::string> getImports(const std::string &codeName);

	void showFileHeader(GlobalHeader fileHeader);
	void showResources();
};
}

#endif /* RES_HPP */
