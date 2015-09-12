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
    mCurrentSOP = mRes.getIndex("start");
    mSOP[mCurrentSOP] = std::make_shared<SOP>(mRes, mCurrentSOP);

    // set position counter to where the 'create' message is
    mSOP[mCurrentSOP]->setPC(mSOP[mCurrentSOP]->getSOPMessagePosition(INDEX_CREATE));
    mStaticVariable.reserve(512);   // total static variable capacity in uint8_t units
    mLocalVariable.reserve(512);    // total local variable capacity in uint8_t units
}

Aesop::~Aesop() {
    //cleanup
}

void Aesop::run() {
    std::stringbuf op_output;
    std::ostream op_output_stream(&op_output);

    std::shared_ptr<STATE::SOP> sop = mSOP[mCurrentSOP];  // for convience

    std::cout << "Entering '" << sop->getSOPMessageName(INDEX_CREATE) << "'"<< std::endl;
    // Find the total size of all local variables, including THIS
    uint16_t local_var_size = reinterpret_cast<uint16_t&>(mSOP[mCurrentSOP]->getWord()); // get THIS
    mLocalVariable.resize(local_var_size);
    *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mLocalVariable.data())) = local_var_size;

    // the big loop
    std::map<uint8_t, int32_t> parameter;
    bool is_more = true;
    while (is_more){
        op_output.str("");
        std::string s_op = "";
        std::string s_value = "";

        uint32_t value = 0;
        uint32_t end_value = 0;
        uint32_t pc = sop->getPC();

        uint8_t &op = sop->getByte();
        switch (op) {
        case OP_BRT:
            s_op = "BRT";
            value = sop->getWord();
            //sop->setPC(value);
            break;
        case OP_BRF:
            s_op = "BRF";
            value = sop->getWord();
            //sop->setPC(value);
            break;
        case OP_BRA:
            s_op = "BRA";
            value = sop->getWord();
            sop->setPC(value);
            break;
        case OP_CASE:
            s_op = "CASE";
            value = sop->getWord();  //number of entries in this CASE
            for (uint16_t i = 0; i < value; i++){
                uint32_t case_value = sop->getLong();
                uint16_t jump_address = sop->getWord();
                op_output_stream << std::endl << "          CASE #"
                                 << i << ": "
                                 << case_value << " -> " << jump_address;
                // TODO: check if case_value is true, then jump
            }
            {
                uint16_t default_jump_address = sop->getWord();
                op_output_stream << std::endl << "          DEFAULT: -> "
                                 << default_jump_address;
                // when all else fails, we default to this jump address
                sop->setPC(default_jump_address);
            }
            break;
        case OP_PUSH:
            s_op = "PUSH";
            {
                std::vector<uint8_t> temp(sizeof(uint8_t));
                *temp.data() = 0;
                mStack.push(temp);
            }
            break;
        case OP_NEG:
            s_op = "NEG";
            break;
        case OP_EXP:
            s_op = "EXP";
            break;
        case OP_SHTC:
            s_op = "SHTC";
            {
                std::vector<uint8_t> temp(sizeof(uint8_t));
                *reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(temp.data())) = sop->getByte();
                mStack.push(temp);
            }
            break;
        case OP_INTC:
            s_op = "INTC";
            {
                std::vector<uint8_t> temp(sizeof(uint16_t));
                *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(temp.data())) = sop->getWord();
                mStack.push(temp);
            }
            break;
        case OP_LNGC:
            s_op = "LNGC";
            {
                std::vector<uint8_t> temp(sizeof(uint32_t));
                *reinterpret_cast<uint32_t*>(reinterpret_cast<void*>(temp.data())) = sop->getLong();
                mStack.push(temp);
            }
            break;
        case OP_RCRS:
            s_op = "RCRS";
            {
                std::vector<uint8_t> temp(sizeof(uint16_t));
                *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(temp.data())) = sop->getWord();
                mStack.push(temp);
            }
            op_output_stream << sop->getSOPImportName(*reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mStack.top().data())));
            break;
        case OP_CALL:
            s_op = "CALL";
            value = sop->getByte(); // number of parameters
            for (uint8_t i = value; i > 0; i--){

                if (mStack.top().size() == 1)
                    parameter[i] = *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
                else if (mStack.top().size() == 2)
                    parameter[i] = *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
                else if (mStack.top().size() == 4)
                    parameter[i] = *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));

                mStack.pop();
                std::cout << "DEBUG - Parameter: " << parameter[i] << " at index: " << (int16_t) i << std::endl;

                // check for parameter delimiter
                if (*mStack.top().data() != 0)
                    std::throw_with_nested(std::runtime_error("Delimiter not found! Got this: " + boost::lexical_cast<char>(mStack.top().data())));
                else
                    mStack.pop();
            }

            // call up function and send parameters
            std::cout << "DEBUG - Calling: " << sop->getSOPImportName(*reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mStack.top().data()))) << std::endl;
            mStack.pop();

            // clear parameter for next use
            parameter.clear();

            break;
        case OP_SEND:
            s_op = "SEND";
            value = sop->getByte();
            op_output_stream << " " << value;
            value = sop->getWord();
            op_output_stream << " -> '" << mRes.getTableEntry(value, 4) << "'"; // TODO: get this through SOP
            break;
        case OP_LAB:
            s_op = "LAB";
            value = sop->getWord();
            break;
        case OP_LAW:
            s_op = "LAW";
            value = sop->getWord();
            break;
        case OP_LAD:
            s_op = "LAD";
            value = sop->getWord();
            break;
        case OP_SAW:
            s_op = "SAW";
            value = sop->getWord();
            break;
        case OP_SAD:
            s_op = "SAD";
            value = sop->getWord();
            break;
        case OP_LXD:
            s_op = "LXD";
            value = sop->getWord();
            break;
        case OP_SXB:
            s_op = "SXB";
            value = sop->getWord();
            break;
        case OP_LXDA:
            s_op = "LXDA";
            value = sop->getWord();
            break;
        case OP_LECA:
            s_op = "LECA";
            value = sop->getWord();
            sop->getByte();
            end_value = sop->getWord();
            s_value = sop->getStringFromLECA(value, end_value);
            {
                std::vector<uint8_t> test(1);
                test[0] = -1;
                mStack.push(test); // Dummy value, until we figure out what do with strings.
            }
            sop->setPC(sop->getPC() + end_value-value);
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
                     (uint32_t) op << "):  " << std::dec <<
                     (uint32_t) value << " (" << s_value << ") " << op_output.str() <<
                     std::endl;
    }

    return;
}

}
