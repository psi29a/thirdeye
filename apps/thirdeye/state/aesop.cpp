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
        uint8_t &op = getByte();
        std::string s_op = "";
        uint32_t value;
        uint32_t end_value;
        std::string s_value = "";

        switch (op) {
        case OP_BRT:
            s_op = "BRT";
            value = getWord();
            break;
        case OP_BRF:
            s_op = "BRF";
            value = getWord();
            break;
        case OP_BRA:
            s_op = "BRA";
            value = getWord();
            break;
        case OP_PUSH:
            // TODO: no value, what do we do?
            s_op = "PUSH";
            break;
        case OP_SHTC:
            s_op = "SHTC";
            value = getByte();
            break;
        case OP_INTC:
            s_op = "INTC";
            value = getWord();
            break;
        case OP_LNGC:
            s_op = "LNGC";
            value = getLong();
            break;
        case OP_RCRS:
            s_op = "RCRS";
            value = getWord();
            break;
        case OP_CALL:
            s_op = "CALL";
            value = getByte();
            break;
        case OP_SEND:
            s_op = "SEND";
            value = getByte();  // TODO: has two values?
            value = getWord();
            break;
        case OP_LAB:
            s_op = "LAB";
            value = getWord();
            break;
        case OP_LAW:
            s_op = "LAW";
            value = getWord();
            break;
        case OP_LAD:
            s_op = "LAD";
            value = getWord();
            break;
        case OP_SAW:
            s_op = "SAW";
            value = getWord();
            break;
        case OP_SAD:
            s_op = "SAD";
            value = getWord();
            break;
        case OP_LXD:
            s_op = "LXD";
            value = getWord();
            break;
        case OP_SXB:
            s_op = "SXB";
            value = getWord();
            break;
        case OP_LXDA:
            s_op = "LXDA";
            value = getWord();
            break;
        case OP_LECA:
            s_op = "LECA";
            value = getWord();
            getByte();
            end_value = getWord();
            s_value = std::string(reinterpret_cast<const char*>(sop_data.data()) + value, end_value - value);
            position += end_value-value;
            break;
        case OP_END:
            s_op = "END";
            break;
        default:
            s_op = "UNKN";
            value = 0xFF;
            break;
        }
        std::cout << std::setfill(' ') << std::setw(4) <<
                     position << "/" << sop_data.size() << " " <<
                     std::hex << std::setw(4) <<
                     s_op << " (" << std::setfill('0') << std::setw(2) <<
                     (uint16_t) op << "):  " << std::dec <<
                     value << " (" << s_value << ") " <<
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
