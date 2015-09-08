/*
 * sop.hpp
 *
 *  Created on: Aug 19, 2015
 *      Author: bcurtis
 */

#include <stdint.h>
#include <vector>
#include <map>
#include <memory>

#include "../resources/res.hpp"

#ifndef SOP_HPP
#define SOP_HPP

namespace STATE {

/* IMPORT/EXPORT PREFIXES */
#define IMEX_METHOD     'M'
#define IMEX_N          'N'
#define IMEX_BYTE       'B'
#define IMEX_WORD       'W'
#define IMEX_LONG       'L'
#define IMEX_CFUNCTION  'C'

PACK(struct SOPScriptHeader
{
  uint16_t static_size;  // probably size of class variables/constants (??)
  uint32_t import_resource; // the number of the corresponding import resource
  uint32_t export_resource; // the number of the corresponding export resource
  uint32_t parent; // the number of parent object (ffffffff if none)
});

PACK(struct SOPImExHeader
{
  uint16_t hashsize;  // probably number of string lists (??)
  uint32_t start_of_the_list; // starting position of string list
});

struct ImExEntry
{
    std::string first;
    std::string second;
    char type;
    int16_t position;
    int8_t elements;
    std::string table_entry;
    int16_t sop_index;
    int16_t import_from;
};

struct LocalVariables
{
    uint8_t size;
    std::map<uint16_t, int64_t> value;
    std::map<uint16_t, char> type;
};

class SOP
{
    uint32_t mPC; // position counter
    RESOURCES::Resource &mRes;  // resource reference
    uint16_t mIndex;    // index offset to sop data
    std::string mName;  // name of SOP

    std::vector<uint8_t> mData;

    SOPScriptHeader mSOPHeader;
    std::vector<uint8_t> mSOPImport;
    SOPImExHeader mSOPImportHeader;
    std::map<uint16_t, ImExEntry> mSOPImportData;

    std::vector<uint8_t> mSOPExport;
    SOPImExHeader mSOPExportHeader;
    std::map<uint16_t, ImExEntry> mSOPExportData;

private:
    void getImExData(const std::vector<uint8_t> &ImEx, bool import);

public:
    SOP(RESOURCES::Resource &resource, uint16_t index);
    virtual ~SOP();
    uint8_t &getByte();
    uint16_t &getWord();
    uint32_t &getLong();
    uint32_t getPC();
    void setPC(uint32_t);
    SOPScriptHeader &getSOPHeader();
    std::string getSOPMessageName(uint16_t index);
    int16_t getSOPMessagePosition(uint16_t index);
    std::string getSOPImportName(uint16_t index);
    std::string getStringFromLECA(uint32_t start, uint32_t end);
};

}

#endif /* SOP_HPP */
