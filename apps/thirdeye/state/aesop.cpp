/*
 * aesop.cpp
 *
 *  Created on: Aug 19, 2015
 *      Author: bcurtis
 */

#include <iostream>
#include <iomanip>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include "aesop.hpp"

#include <thread>         // std::this_thread::sleep_for
#include <chrono>         // std::chrono::seconds

AESOP::Aesop::Aesop(RESOURCES::Resource &resource):mRes(resource) {
    // load and initialize 'start' sop
    uint16_t start_index = mRes.getIndex("start");
    mSOP[start_index] = std::make_unique<SOP>(mRes, start_index);

    std::cout << "DEBUG: " << mSOP[start_index]->getPC() << std::endl;
    std::cout << "DEBUG: " << mSOP[start_index]->getSOPHeader().import_resource << std::endl;
}

AESOP::Aesop::~Aesop() {
    //cleanup
}

void AESOP::Aesop::show() {
    uint16_t start_index = mRes.getIndex("start");
    std::stringbuf op_output;
    std::ostream op_output_stream(&op_output);
    mSOP[start_index]->setPC(47); // TODO: use SOP index 0 as starting point.
    uint16_t local_var_size = reinterpret_cast<uint16_t&>(mSOP[start_index]->getWord()); // get THIS
    bool is_more = true;
    while (is_more){
        op_output.str("");
        std::string s_op = "";
        std::string s_value = "";
        uint32_t value = 0;
        uint32_t end_value = 0;

        uint8_t &op = mSOP[start_index]->getByte();
        switch (op) {
        case OP_BRT:
            s_op = "BRT";
            value = mSOP[start_index]->getWord();
            break;
        case OP_BRF:
            s_op = "BRF";
            value = mSOP[start_index]->getWord();
            break;
        case OP_BRA:
            s_op = "BRA";
            value = mSOP[start_index]->getWord();
            break;
        case OP_CASE:
            s_op = "CASE";
            value = mSOP[start_index]->getWord();  //number of entries in this CASE
            for (uint16_t i = 0; i < value; i++){
                uint32_t case_value = mSOP[start_index]->getLong();
                uint16_t jump_address = mSOP[start_index]->getWord();
                op_output_stream << std::endl << "          CASE #"
                                 << i << ": "
                                 << case_value << " -> " << jump_address;
            }
            {
                uint16_t default_jump_address = mSOP[start_index]->getWord();
                op_output_stream << std::endl << "          DEFAULT: -> "
                                 << default_jump_address;
            }
            break;
        case OP_PUSH:
            // TODO: no value, what do we do?
            s_op = "PUSH";
            break;
        case OP_NEG:
            s_op = "NEG";
            break;
        case OP_EXP:
            s_op = "EXP";
            break;
        case OP_SHTC:
            s_op = "SHTC";
            value = mSOP[start_index]->getByte();
            break;
        case OP_INTC:
            s_op = "INTC";
            value = mSOP[start_index]->getWord();
            break;
        case OP_LNGC:
            s_op = "LNGC";
            value = mSOP[start_index]->getLong();
            break;
        case OP_RCRS:
            s_op = "RCRS";
            value = mSOP[start_index]->getWord();
            //op_output_stream << mSOP[start_index]->mSOPImportData[boost::lexical_cast<uint16_t>(value)].table_entry;
            break;
        case OP_CALL:
            s_op = "CALL";
            value = mSOP[start_index]->getByte();
            break;
        case OP_SEND:
            s_op = "SEND";
            value = mSOP[start_index]->getByte();
            op_output_stream << " " << value;
            value = mSOP[start_index]->getWord();
            op_output_stream << " -> '" << mRes.getTableEntry(value, 4) << "'"; // TODO: get this through SOP
            break;
        case OP_LAB:
            s_op = "LAB";
            value = mSOP[start_index]->getWord();
            break;
        case OP_LAW:
            s_op = "LAW";
            value = mSOP[start_index]->getWord();
            break;
        case OP_LAD:
            s_op = "LAD";
            value = mSOP[start_index]->getWord();
            break;
        case OP_SAW:
            s_op = "SAW";
            value = mSOP[start_index]->getWord();
            break;
        case OP_SAD:
            s_op = "SAD";
            value = mSOP[start_index]->getWord();
            break;
        case OP_LXD:
            s_op = "LXD";
            value = mSOP[start_index]->getWord();
            break;
        case OP_SXB:
            s_op = "SXB";
            value = mSOP[start_index]->getWord();
            break;
        case OP_LXDA:
            s_op = "LXDA";
            value = mSOP[start_index]->getWord();
            break;
        case OP_LECA:
            s_op = "LECA";
            value = mSOP[start_index]->getWord();
            mSOP[start_index]->getByte();
            end_value = mSOP[start_index]->getWord();
            //s_value = std::string(reinterpret_cast<const char*>(sop_data.data()) + value, end_value - value);
            //position += end_value-value;
            break;
        case OP_END:
            s_op = "END";
            is_more = false;
            break;
        default:
            s_op = "UNKN";
            value = 0xFF;
            break;
        }

        std::cout << ' ' <<
                     std::setfill(' ') << std::setw(4) <<
                     mSOP[start_index]->getPC() << " " <<
                     std::hex << std::setw(4) <<
                     s_op << " (0x" << std::setfill('0') << std::setw(2) <<
                     (uint16_t) op << "):  " << std::dec <<
                     (uint16_t) value << " (" << s_value << ") " << op_output.str() <<
                     std::endl;
    }

    return;
}

AESOP::SOP::SOP(RESOURCES::Resource &resource, uint16_t index):
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

AESOP::SOP::~SOP() {
    //cleanup
}

AESOP::SOPScriptHeader &AESOP::SOP::getSOPHeader(){
    return mSOPHeader;
}

void AESOP::SOP::getImExData(const std::vector<uint8_t> &ImEx, bool import){
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

void AESOP::SOP::setPC(uint32_t PC){
    mPC = PC;
}

uint32_t AESOP::SOP::getPC(){
    return mPC;
}

uint8_t &AESOP::SOP::getByte(){
    mPC += 1;
    return mData[mPC-1];
}

uint16_t &AESOP::SOP::getWord(){
    mPC += 2;
    return reinterpret_cast<uint16_t&>(mData[mPC-2]);
}

uint32_t &AESOP::SOP::getLong(){
    mPC += 4;
    return reinterpret_cast<uint32_t&>(mData[mPC-4]);
}
