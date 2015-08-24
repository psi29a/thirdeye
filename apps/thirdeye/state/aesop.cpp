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

AESOP::Aesop::Aesop(RESOURCES::Resource *resource) {
    res = resource;
}

AESOP::Aesop::~Aesop() {
    //cleanup
}

void AESOP::Aesop::show() {
    std::vector<uint8_t> &sop = res->getAsset("start");
    sop_data = sop;

    SOPScriptHeader sopHeader = reinterpret_cast<SOPScriptHeader&>(sop_data[0]);
    position = sizeof(SOPScriptHeader);
    uint16_t local_var_size = reinterpret_cast<uint16_t&>(sop_data[position]);
    position += sizeof(uint16_t);

    std::cout << "SOP HEADER " <<
                 sopHeader.static_size << " " <<
                 sopHeader.import_resource << " " <<
                 sopHeader.export_resource << " " <<
                 std::setw(2) << std::setfill('0') << std::hex <<
                 sopHeader.parent << " " <<
                 local_var_size << " " <<
                 std::dec <<
                 position << std::endl;

    std::vector<uint8_t> &sop_import = res->getAsset(sopHeader.import_resource);
    SOPImExHeader sopImportHeader = reinterpret_cast<SOPImExHeader&>(sop_import[0]);
    uint16_t imPosition = sizeof(SOPImExHeader);
    while (imPosition < sop_import.size()){
        uint16_t string_size = reinterpret_cast<uint16_t&>(sop_import[imPosition]);
        imPosition += 2;
        std::cout << "First IMPORT string size: " << string_size;
        std::string string(reinterpret_cast<const char*>(sop_import.data()) + imPosition, string_size);
        imPosition += string_size;
        std::cout << " value: " << string;

        string_size = reinterpret_cast<uint16_t&>(sop_import[imPosition]);
        imPosition += 2;
        std::cout << " Second IMPORT string size: " << string_size;
        string = std::string(reinterpret_cast<const char*>(sop_import.data()) + imPosition, string_size);
        imPosition += string_size;
        std::cout << " value: " << string;

        std::cout << std::endl;
    }

    std::vector<uint8_t> &sop_export = res->getAsset(sopHeader.export_resource);
    SOPImExHeader sopExportHeader = reinterpret_cast<SOPImExHeader&>(sop_export[0]);
    uint16_t exPosition = sizeof(SOPImExHeader);
    while (exPosition < sop_export.size()){
        uint16_t string_size = reinterpret_cast<uint16_t&>(sop_export[exPosition]);
        exPosition += 2;
        std::cout << "First EXPORT string size: " << string_size;
        std::string string(reinterpret_cast<const char*>(sop_export.data()) + exPosition, string_size);
        exPosition += string_size;
        std::cout << " value: " << string;

        string_size = reinterpret_cast<uint16_t&>(sop_export[exPosition]);
        exPosition += 2;
        std::cout << " Second EXPORT string size: " << string_size;
        string = std::string(reinterpret_cast<const char*>(sop_export.data()) + exPosition, string_size);
        exPosition += string_size;
        std::cout << " value: " << string;

        std::cout << std::endl;
    }


    while (position < sop_data.size()){
        uint16_t start_position = position;
        uint8_t &op = getByte();
        std::string s_op = "";
        uint32_t value = 0;
        uint32_t end_value = 0;
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
        case OP_CASE:
            s_op = "CASE";
            value = getWord();  //number of entries in this CASE
            for (uint16_t i = 0; i < value; i++){
                uint32_t case_value = getLong();
                uint16_t jump_address = getWord();
            }
            //value = getWord();
            {
                uint16_t default_jump_address = getWord();
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
            std::cout << "GOT TO END";  // TODO: this isn't right, use *.EXPRT
            if (position+2 < sop_data.size()){ // just end of message handler
                local_var_size = reinterpret_cast<uint16_t&>(sop_data[position]);
                position += sizeof(uint16_t);
                std::cout << "THIS HANDLER: " << local_var_size;
            } // else, really end of whole things.
            break;
        default:
            s_op = "UNKN";
            value = 0xFF;
            break;
        }
        std::cout << std::setfill(' ') << std::setw(4) <<
                     start_position << "/" << sop_data.size() << " " <<
                     std::hex << std::setw(4) <<
                     s_op << " (" << std::setfill('0') << std::setw(2) <<
                     (uint16_t) op << "):  " << std::dec <<
                     (uint16_t) value << " (" << s_value << ") " <<
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
