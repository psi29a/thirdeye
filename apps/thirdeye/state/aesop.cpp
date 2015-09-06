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

namespace STATE {

Aesop::Aesop(RESOURCES::Resource &resource):mRes(resource) {
    // load and initialize 'start' sop
    uint16_t start_index = mRes.getIndex("start");
    mSOP[start_index] = std::make_unique<SOP>(mRes, start_index);

    std::cout << "DEBUG: " << mSOP[start_index]->getPC() << std::endl;
    std::cout << "DEBUG: " << mSOP[start_index]->getSOPHeader().import_resource << std::endl;

    mSOP[start_index]->setPC(47); // TODO: use SOP index 0 as starting point.
}

Aesop::~Aesop() {
    //cleanup
}

void Aesop::run() {
    std::stringbuf op_output;
    std::ostream op_output_stream(&op_output);

    uint16_t start_index = mRes.getIndex("start");
    uint16_t local_var_size = reinterpret_cast<uint16_t&>(mSOP[start_index]->getWord()); // get THIS
    bool is_more = true;
    while (is_more){
        op_output.str("");
        std::string s_op = "";
        std::string s_value = "";
        uint32_t value = 0;
        uint32_t end_value = 0;
        uint32_t pc = mSOP[start_index]->getPC();

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
            mSOP[start_index]->setPC(mSOP[start_index]->getPC() + end_value-value);
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
                     pc << " " <<
                     std::hex << std::setw(4) <<
                     s_op << " (0x" << std::setfill('0') << std::setw(2) <<
                     (uint16_t) op << "):  " << std::dec <<
                     (uint16_t) value << " (" << s_value << ") " << op_output.str() <<
                     std::endl;
    }

    return;
}

}
