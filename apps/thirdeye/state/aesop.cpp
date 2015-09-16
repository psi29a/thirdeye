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
    bool is_more = true;
    while (is_more){
        op_output.str("");
        std::string s_op = "";
        std::string s_value = "";

        uint32_t value = 0;
        uint32_t pc = sop->getPC();

        uint8_t &op = sop->getByte();
        switch (op) {
        case OP_BRT:
            s_op = "BRT";
            do_BRT();
            break;
        case OP_BRF:
            s_op = "BRF";
            do_BRF();
            break;
        case OP_BRA:
            s_op = "BRA";
            do_BRA();
            break;
        case OP_CASE:
            s_op = "CASE";
            do_CASE();
            break;
        case OP_PUSH:
            s_op = "PUSH";
            do_PUSH();
            break;
        case OP_NEG:
            s_op = "NEG";
            break;
        case OP_EXP:
            s_op = "EXP";
            break;
        case OP_SHTC:
            s_op = "SHTC";
            do_SHTC();
            break;
        case OP_INTC:
            s_op = "INTC";
            do_INTC();
            break;
        case OP_LNGC:
            s_op = "LNGC";
            do_LNGC();
            break;
        case OP_RCRS:
            s_op = "RCRS";
            do_RCRS();
            break;
        case OP_CALL:
            s_op = "CALL";
            do_CALL();
            break;
        case OP_SEND:
            s_op = "SEND";
            do_SEND();
            break;
        case OP_LAB:
            s_op = "LAB";
            do_LAB();
            break;
        case OP_LAW:
            s_op = "LAW";
            do_LAW();
            break;
        case OP_LAD:
            s_op = "LAD";
            do_LAD();
            break;
        case OP_SAB:
            s_op = "SAB";
            do_SAB();
            break;
        case OP_SAW:
            s_op = "SAW";
            do_SAW();
            break;
        case OP_SAD:
            s_op = "SAD";
            do_SAD();
            break;
        case OP_LXD:
            s_op = "LXD";
            do_LXD();
            break;
        case OP_SXB:
            s_op = "SXB";
            do_SXB();
            break;
        case OP_LXDA:
            s_op = "LXDA";
            do_LXDA();
            break;
        case OP_LECA:
            s_op = "LECA";
            do_LECA();
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
                     //(uint32_t) value << " (" << s_value << ") " << op_output.str() <<
                     std::endl;
    }

    return;
}

void Aesop::do_BRA(){
    // branch unconditionaly
    mSOP[mCurrentSOP]->setPC(mSOP[mCurrentSOP]->getWord());
}

void Aesop::do_BRF(){
    // branch when false
    uint16_t location = mSOP[mCurrentSOP]->getWord();
    //sop->setPC(location);
}

void Aesop::do_BRT(){
    // branch when true
    uint16_t location = mSOP[mCurrentSOP]->getWord();
    //sop->setPC(location);
}

void Aesop::do_CASE(){
    // case statement
    uint16_t value = mSOP[mCurrentSOP]->getWord();  //number of entries in this CASE
    for (uint16_t i = 0; i < value; i++){
        uint32_t case_value = mSOP[mCurrentSOP]->getLong();
        uint16_t jump_address = mSOP[mCurrentSOP]->getWord();
        std::cout << "          CASE #"
                  << i << ": " << case_value << " -> "
                  << jump_address << std::endl;
        // TODO: check if case_value is true, then jump
    }
    uint16_t default_jump_address = mSOP[mCurrentSOP]->getWord();
    std::cout << "          DEFAULT: -> "
              << default_jump_address << std::endl;
    // when all else fails, we default to this jump address
    mSOP[mCurrentSOP]->setPC(default_jump_address);
}

void Aesop::do_PUSH(){
    // push 0 on to stack, used a delimiter for CALL and SEND
    std::vector<uint8_t> temp(sizeof(uint8_t));
    *temp.data() = 0;
    mStack.push(temp);
}

void Aesop::do_CALL(){
    // call function with parameter validation
    uint8_t num_of_parameters = mSOP[mCurrentSOP]->getByte(); // number of parameters
    std::map<uint8_t, std::vector<uint8_t>> parameter;
    for (uint8_t i = num_of_parameters; i > 0; i--){
        parameter[i] = std::vector<uint8_t>(mStack.top().size());
        std::copy(mStack.top().begin(), mStack.top().end(), parameter[i].begin());
        mStack.pop();

        // check for parameter delimiter
        if (*mStack.top().data() != 0)
            std::throw_with_nested(std::runtime_error("Delimiter not found!"));
        else
            mStack.pop();
    }
    call_function(parameter);
}

void Aesop::do_INTC(){
    // push word in bytecode onto stack
    std::vector<uint8_t> temp(sizeof(uint16_t));
    *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(temp.data())) = mSOP[mCurrentSOP]->getWord();
    mStack.push(temp);
}

void Aesop::do_LAB(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::do_LAD(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord()-sizeof(int32_t); // offset of long
    std::cout << "LAD offset:" << (uint16_t) offset << std::endl;
    {
        std::vector<uint8_t> value(sizeof(int32_t));
        std::copy(value.begin(), value.end(), mLocalVariable.data()+offset);
        mStack.push(value);
    }
    std::cout << "LAD value:" << *reinterpret_cast<int32_t*>(mStack.top().data()) << " " << *reinterpret_cast<int32_t*>(mLocalVariable.data()+offset) << std::endl;
}

void Aesop::do_LAW(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::do_LECA(){
    // retrieve strings embedded in SOP bytecode
    uint16_t value = mSOP[mCurrentSOP]->getWord();
    mSOP[mCurrentSOP]->getByte();
    uint16_t end_value = mSOP[mCurrentSOP]->getWord();
    std::string s_value = mSOP[mCurrentSOP]->getStringFromLECA(value, end_value);
    mStack.push(std::vector<uint8_t>(s_value.c_str(), s_value.c_str() + s_value.length() + 1));
    mSOP[mCurrentSOP]->setPC(mSOP[mCurrentSOP]->getPC() + end_value - value);
    std::cout << "LECA: " << std::string(mStack.top().begin(), mStack.top().end()) << std::endl;
}

void Aesop::do_LNGC(){
    std::vector<uint8_t> temp(sizeof(uint32_t));
    *reinterpret_cast<uint32_t*>(reinterpret_cast<void*>(temp.data())) = mSOP[mCurrentSOP]->getLong();
    mStack.push(temp);
}

void Aesop::do_LXB(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::do_LXD(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::do_LXDA(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::do_RCRS(){
    std::vector<uint8_t> function_id(sizeof(uint16_t));
    *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(function_id.data())) = mSOP[mCurrentSOP]->getWord();
    mStack.push(function_id);
    std::cout << "RCRS: "
              << mSOP[mCurrentSOP]->getSOPImportName(*reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mStack.top().data())))
              << std::endl;
}

void Aesop::do_SAB(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::do_SAD(){
    // Store Auto Dword
    uint16_t offset = mSOP[mCurrentSOP]->getWord()-4; // offset of long
    std::cout << "SAD offset:" << (uint16_t) offset << std::endl;
    std::cout << "SAD value:" << *reinterpret_cast<int32_t*>(mStack.top().data()) << " " << *reinterpret_cast<int32_t*>(mLocalVariable.data()+offset) << std::endl;
    std::copy(mStack.top().begin(), mStack.top().end(), mLocalVariable.data()+offset);
    std::cout << "SAD value:" << *reinterpret_cast<int32_t*>(mStack.top().data()) << " " << *reinterpret_cast<int32_t*>(mLocalVariable.data()+offset) << std::endl;
    mStack.pop();
}

void Aesop::do_SAW(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::do_SHTC(){
    std::vector<uint8_t> temp(sizeof(uint8_t));
    *reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(temp.data())) = mSOP[mCurrentSOP]->getByte();
    mStack.push(temp);
}

void Aesop::do_SEND(){
    uint16_t value = mSOP[mCurrentSOP]->getByte();
    std::cout << "SEND: " << value;
    value = mSOP[mCurrentSOP]->getWord();
    std::cout << " -> '" << mRes.getTableEntry(value, 4) // TODO: get this through SOP
              << "'" << std::endl;
}

void Aesop::do_SXB(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::call_function(std::map<uint8_t, std::vector<uint8_t>> &parameters){
    // call up function and send parameters
    uint16_t function_id = *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mStack.top().data()));
    std::cout << "CALL - Calling: " << mSOP[mCurrentSOP]->getSOPImportName(function_id) << std::endl;
    mStack.pop();

    /*
    if (mStack.top().size() == 1)
        parameter[i] = *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() == 2)
        parameter[i] = *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() == 4)
        parameter[i] = *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() > 4) // maybe a string?
        std::cout << "CALL - Parameter: " << std::string(mStack.top().begin(), mStack.top().end()) << " at index: " << (int16_t) i << std::endl;

    std::cout << "CALL - Parameter: " << parameter[i] << " at index: " << (int16_t) i << std::endl;
    */

    switch(function_id){
    case C_PEEKMEM:
    {
        std::vector<uint8_t> value(sizeof(int32_t));
        *value.data() = peekmem(parameters);
        mStack.push(value);
    }
        break;
    case C_POKEMEM:
        pokemem(parameters);
        break;
    default:
        std::cout << "CALL -> Unimplimented function: " << std::hex << function_id << std::dec<< std::endl;
    }

}

}
