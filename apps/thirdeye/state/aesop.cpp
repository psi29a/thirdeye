/*
 * aesop.cpp
 *
 *  Created on: Aug 19, 2015
 *      Author: bcurtis
 */

#include <iostream>
#include <iomanip>
#include <boost/lexical_cast.hpp>
#include "aesop.hpp"

AESOP::Aesop::Aesop(std::vector<uint8_t> &data) {
    sop_data = data;
}

AESOP::Aesop::~Aesop() {
    //cleanup
}

void AESOP::Aesop::show() {

    sopHeader = reinterpret_cast<SOPScriptHeader&>(sop_data[0]);
    position = sizeof(SOPScriptHeader);
    local_var_size = reinterpret_cast<uint16_t&>(sop_data[position]);
    position += sizeof(uint16_t);

    std::cout << "SOP HEADER " <<
                 sopHeader.static_size << " " <<
                 sopHeader.import_resource << " " <<
                 sopHeader.export_resource << " " <<
                 std::setw(2) << std::setfill('0') << std::hex <<
                 sopHeader.parent << " " <<
                 local_var_size << " " <<
                 std::dec <<
                 position << " " << sizeof(uint16_t) << std::endl;

    while (position < sop_data.size()){
        uint8_t &OP = getByte();
        std::string S_OP = "";
        uint32_t value;
        uint32_t end_value;
        std::string SValue = "";

        switch (OP) {
        case OP_BRT:
            S_OP = "BRT";
            value = getWord();
            break;
        case OP_BRF:
            S_OP = "BRF";
            value = getWord();
            break;
        case OP_BRA:
            S_OP = "BRA";
            value = getWord();
            break;
        case OP_PUSH:
            // TODO: no value, what do we do?
            S_OP = "PUSH";
            break;
        case OP_SHTC:
            S_OP = "SHTC";
            value = getByte();
            break;
        case OP_INTC:
            S_OP = "INTC";
            value = getWord();
            break;
        case OP_LNGC:
            S_OP = "LNGC";
            value = getLong();
            break;
        case OP_RCRS:
            S_OP = "RCRS";
            value = getWord();
            break;
        case OP_CALL:
            S_OP = "CALL";
            value = getByte();
            break;
        case OP_SEND:
            S_OP = "SEND";
            value = getByte();  // TODO: has two values?
            value = getWord();
            break;
        case OP_LAB:
            S_OP = "LAB";
            value = getWord();
            break;
        case OP_LAW:
            S_OP = "LAW";
            value = getWord();
            break;
        case OP_LAD:
            S_OP = "LAD";
            value = getWord();
            break;
        case OP_SAW:
            S_OP = "SAW";
            value = getWord();
            break;
        case OP_SAD:
            S_OP = "SAD";
            value = getWord();
            break;
        case OP_LXD:
            S_OP = "LXD";
            value = getWord();
            break;
        case OP_SXB:
            S_OP = "SXB";
            value = getWord();
            break;
        case OP_LXDA:
            S_OP = "LXDA";
            value = getWord();
            break;
        case OP_LECA:
            S_OP = "LECA";
            value = getWord();
            getByte();
            end_value = getWord();
            SValue = std::string(reinterpret_cast<const char*>(sop_data.data()) + value, end_value - value);
            position += end_value-value;
            break;
        case OP_END:
            S_OP = "END";
            break;
        default:
            S_OP = "UNKN";
            value = 0xFF;
            break;
        }
        std::cout << std::setfill(' ') << std::setw(4) <<
                     position << "/" << sop_data.size() << " " <<
                     std::hex << std::setw(4) <<
                     S_OP << " (" << std::setfill('0') << std::setw(2) <<
                     (uint16_t) OP << "):  " << std::dec <<
                     value << " (" << SValue << ") " <<
                     std::endl;
        //break;
    }
    return;
}

uint8_t &AESOP::Aesop::getByte(){
    position += 1;
    return sop_data[position-1];
}

uint16_t &AESOP::Aesop::getWord(){
    position += 2;
    return reinterpret_cast<uint16_t&>(sop_data[position-2]);
}

uint32_t &AESOP::Aesop::getLong(){
    position += 4;
    return reinterpret_cast<uint32_t&>(sop_data[position-4]);
}
