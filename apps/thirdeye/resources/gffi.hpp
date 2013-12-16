/*
 * resource.hpp
 *
 *  Created on: Sept 3, 2013
 *      Author: bcurtis
 */

#ifndef GFFI_HPP
#define GFFI_HPP

#include <map>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>
#include "boost/tuple/tuple.hpp"

using boost::iostreams::file_source;
using boost::tuples::tuple;
using boost::tuples::tie;

namespace RESOURCES {

#define GFFI_ID "GFFI"

#define NOOP	 		0
#define SET_PAL			1
#define DISP_BMP		2
#define DISP_BMA		3
#define FADE_IN			4
#define FADE_LEFT		5
#define PAN_LEFT		6
#define DRAW_CURTAIN	7
#define MATERIALIZE 	8
#define DISP_OVERLAY	9

#define T_TAG		0
#define	T_DATA		1

struct GFFIHeader {
	char signature[4]; 	// must be == GFFI"
	uint16_t unknown1;	// 0
	uint16_t unknown2;	// 3
	uint32_t header;	// 0x1C (start of body?)
	uint32_t directory_offset;
	uint32_t directory_size;
	uint32_t unknown3;	// 0
	uint32_t unknown4;
};

struct GFFIDirectoryHeader {
	uint32_t unknown1;			// 8
	uint32_t directory_size; 	// minus 2 bytes trailer tag
	uint16_t number_of_tags;
	// .. tagged blocks
	// uint16_t trailer;		// 0 trailer: directory_size + 2
}__attribute__((packed));	// because the compiler wants to pad

struct GFFIBlockHeader {
	char tag[4];					// tag
	uint32_t number_of_elements;	// num of elements in block
	//uint32_t unique;				// unique identifier
	//uint32_t element_offset;		// location of first element
	//uint32_t resource_size;		// size of first element
	//	...							// size of Nth element
};

struct GFFIBlock {
	uint32_t unique;				// unique identifier
	uint32_t offset;				// location of first element
	uint32_t size;					// size of first element
}__attribute__((packed));

struct GFFIBlock2 {
	char tag[4];					// tag
	uint32_t unknown1;				// number in the block or 0x80000000
	uint32_t unknown2;				// number in the block
	uint32_t unique_table;			// number of the resource table for this block
	uint32_t unknown3;				// number in the block
	//uint32_t unqiue_number;			// unique res number of 1st item
	//uint32_t num_of_resources;	// number of res indexed by a row for the
									// first item
	// uint32_t ...
}__attribute__((packed));

struct File {
	uint32_t offset;
	//std::vector<uint8_t> data;
	std::map<uint8_t, std::vector<uint8_t> > data;
};

class GFFI {

private:
	boost::filesystem::path mGFFIFile;
	uint32_t mGFFIFileSize;
	GFFIHeader mGFFIHeader;
	GFFIDirectoryHeader mGFFIDirectoryHeader;
	std::map< std::string, std::map< uint32_t, File > > mFiles;

public:
	GFFI(boost::filesystem::path gffiPath);
	std::map<uint8_t, tuple<uint8_t, uint8_t, std::vector<uint8_t> > > getSequence();
	virtual ~GFFI();
};
}

#endif /* GFFI_HPP */
