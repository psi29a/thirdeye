/*
 * sop.cpp
 *
 *  Created on: Aug 19, 2015
 *      Author: bcurtis
 */

#include <iostream>
#include <iomanip>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include "sop.hpp"

namespace STATE {

SOP::SOP(RESOURCES::Resource &resource, uint16_t index):
mRes(resource), mIndex(index){
    mPC = 0;    // set our position counter
    mData = mRes.getAsset(mIndex);
    mSOPHeader = reinterpret_cast<SOPScriptHeader&>(mData[0]);
    mPC += sizeof(SOPScriptHeader);

    std::cout << "SOP HEADER " <<
                 mSOPHeader.static_size << " " <<
                 mSOPHeader.import_resource << " " <<
                 mSOPHeader.export_resource << " " <<
                 std::setw(2) << std::setfill('0') << std::hex <<
                 mSOPHeader.parent << " " <<
                 std::dec <<
                 mPC << std::endl;

    std::cout << std::endl << "IMPORT:" << std::endl;
    getImExData(mRes.getAsset(mSOPHeader.import_resource), true);

    std::cout << std::endl << "EXPORT:" << std::endl;
    getImExData(mRes.getAsset(mSOPHeader.export_resource), false);

}

SOP::~SOP() {
    //cleanup
}

SOPScriptHeader &SOP::getSOPHeader(){
    return mSOPHeader;
}

void SOP::getImExData(const std::vector<uint8_t> &ImEx, bool import){
    mSOPExportHeader = reinterpret_cast<const SOPImExHeader&>(ImEx[0]);
    uint16_t EP = sizeof(SOPImExHeader);
    while (EP < ImEx.size()){
        uint32_t end_marker = reinterpret_cast<const uint32_t&>(ImEx[EP]);
        if (end_marker == 0)
            break;

        // parse first string
        uint16_t string_size = reinterpret_cast<const uint16_t&>(ImEx[EP]);
        EP += 2;
        std::string first_string(reinterpret_cast<const char*>(ImEx.data()) + EP, string_size);
        EP += string_size;
        first_string.pop_back();  // trim off null terminator
        std::cout << first_string;
        // split on : in first_string
        std::vector<std::string> first_fields;
        boost::split(first_fields, first_string, boost::is_any_of(":"));

        // parse second string
        string_size = reinterpret_cast<const uint16_t&>(ImEx[EP]);
        EP += 2;
        std::string second_string(reinterpret_cast<const char*>(ImEx.data()) + EP, string_size);
        EP += string_size;
        second_string.pop_back();  // trim off null terminator
        std::cout << ", " << second_string;
        // split on , in second_string
        std::vector<std::string> second_fields;
        boost::split(second_fields, second_string, boost::is_any_of(","));
        int8_t number_of_elements = -1;
        if (!import && second_fields.size() == 2)
            number_of_elements = boost::lexical_cast<int8_t>(second_fields[second_fields.size()-1]);

        int16_t sop_index = -1;
        if (import && second_fields.size() == 2)
            sop_index = boost::lexical_cast<int16_t>(second_fields[second_fields.size()-1]);

        int16_t import_from = -1;
        if (import && second_fields.size() == 2)
            import_from = boost::lexical_cast<uint16_t>(second_fields[second_fields.size()-1]);

        std::map<uint16_t, ImExEntry> &data = mSOPImportData;
        if (!import)
            data = mSOPExportData;

        uint16_t index;
        // massage data by index
        if (first_string[0] == IMEX_METHOD){
            index = boost::lexical_cast<uint16_t>(first_fields[1]);
            if (data.count(index)) std::throw_with_nested(std::runtime_error("Duplicate index: `"+first_string+"`, "+second_string));
            data[index].first = first_string;
            data[index].second = second_string;
            data[index].position = boost::lexical_cast<uint16_t>(second_string);
            data[index].type = IMEX_METHOD;
            data[index].table_entry = mRes.getTableEntry(index, 4);
            data[index].elements = number_of_elements;
            data[index].sop_index = sop_index;
            data[index].import_from = import_from;
        } else if (first_string[0] == IMEX_BYTE){
            index = boost::lexical_cast<uint16_t>(second_fields[0]);
            if (data.count(index)) std::throw_with_nested(std::runtime_error("Duplicate index: `"+first_string+"`, "+second_string));
            data[index].first = first_string;
            data[index].second = second_string;
            data[index].position = -1;
            data[index].type = IMEX_BYTE;
            data[index].table_entry = first_fields[first_fields.size()-1];
            data[index].elements = number_of_elements;
            data[index].sop_index = sop_index;
            data[index].import_from = import_from;
        } else if (first_string[0] == IMEX_LONG){
            index = boost::lexical_cast<uint16_t>(second_fields[0]);
            if (data.count(index)) std::throw_with_nested(std::runtime_error("Duplicate index: `"+first_string+"`, "+second_string));
            data[index].first = first_string;
            data[index].second = second_string;
            data[index].position = -1;
            data[index].type = IMEX_LONG;
            data[index].table_entry = first_fields[first_fields.size()-1];
            data[index].elements = number_of_elements;
            data[index].sop_index = sop_index;
            data[index].import_from = boost::lexical_cast<uint16_t>(second_fields[second_fields.size()-1]);
        } else if (first_string[0] == IMEX_WORD){
            index = boost::lexical_cast<uint16_t>(second_fields[0]);
            if (data.count(index)) std::throw_with_nested(std::runtime_error("Duplicate index: `"+first_string+"`, "+second_string));
            data[index].first = first_string;
            data[index].second = second_string;
            data[index].position = -1;
            data[index].type = IMEX_WORD;
            data[index].table_entry = first_fields[first_fields.size()-1];
            data[index].elements = number_of_elements;
            data[index].sop_index = sop_index;
            data[index].import_from = import_from;
        } else if (first_string[0] == IMEX_CFUNCTION){
            index = boost::lexical_cast<uint16_t>(second_fields[0]);
            if (data.count(index)) std::throw_with_nested(std::runtime_error("Duplicate index: `"+first_string+"`, "+second_string));
            data[index].first = first_string;
            data[index].second = second_string;
            data[index].position = -1;
            data[index].type = IMEX_CFUNCTION;
            data[index].table_entry = first_fields[first_fields.size()-1];
            data[index].elements = -1;
            data[index].sop_index = -1;
            data[index].import_from = import_from;
        } else if (first_string[0] == IMEX_N) {
            // ignore this, it is just a header
            std::cout << std::endl;
            continue;
        } else {
            std::throw_with_nested(std::runtime_error("Uknown prefix `"+first_string+"`."));
        }

        std::cout << "; " << data[index].table_entry << " from "
                  << data[index].import_from << ":"
                  << mRes.getTableEntry(data[index].import_from, 0) << std::endl;

    }
}

void SOP::setPC(uint32_t PC){
    mPC = PC;
}

uint32_t SOP::getPC(){
    return mPC;
}

uint8_t &SOP::getByte(){
    mPC += 1;
    return mData[mPC-1];
}

uint16_t &SOP::getWord(){
    mPC += 2;
    return reinterpret_cast<uint16_t&>(mData[mPC-2]);
}

uint32_t &SOP::getLong(){
    mPC += 4;
    return reinterpret_cast<uint32_t&>(mData[mPC-4]);
}


}
