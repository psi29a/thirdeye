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
    mSOP[mCurrentSOP] = std::make_unique<SOP>(mRes, mCurrentSOP);
    // set position counter to where the 'create' message is
    mSOP[mCurrentSOP]->setPC(mSOP[mCurrentSOP]->getSOPMessagePosition(INDEX_CREATE));
    mStaticVariable.resize(512);   // total static variable capacity in uint8_t units
}

Aesop::~Aesop() {
    //cleanup
}

void Aesop::run() {
    std::stringbuf op_output;
    std::ostream op_output_stream(&op_output);

    std::cout << "Entering '" << mSOP[mCurrentSOP]->getSOPMessageName(INDEX_CREATE) << "'"<< std::endl;
    // Find the total size of all local variables, including THIS
    uint16_t local_var_size = reinterpret_cast<uint16_t&>(mSOP[mCurrentSOP]->getWord()); // get THIS
    mSOP[mCurrentSOP]->mLocalVariable.resize(local_var_size);
    *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mSOP[mCurrentSOP]->mLocalVariable.data())) = local_var_size;

    // the big loop
    bool is_more = true;
    while (is_more){
        op_output.str("");
        std::string s_op = "";
        std::string s_value = "";

        uint32_t value = 0;
        uint32_t pc = mSOP[mCurrentSOP]->getPC();

        uint8_t &op = mSOP[mCurrentSOP]->getByte();
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
        case OP_NOT:
            s_op = "NOT";
            do_NOT();
            break;
        case OP_NEG:
            s_op = "NEG";
            do_NEG();
            break;
        case OP_ADD:
            s_op = "ADD";
            do_ADD();
            break;
        case OP_MUL:
            s_op = "MUL";
            do_MUL();
            break;
        case OP_DIV:
            s_op = "DIV";
            do_DIV();
            break;
        case OP_EXP:
            s_op = "EXP";
            break;
        case OP_LT:
            s_op = "LT";
            do_LT();
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
        case OP_AIS:
            s_op = "AIS";
            do_AIS();
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
        case OP_LSB:
            s_op = "LSB";
            do_LSB();
            break;
        case OP_LSW:
            s_op = "LSW";
            do_LSW();
            break;
        case OP_LSD:
            s_op = "LSD";
            do_LSD();
            break;
        case OP_SSB:
            s_op = "SSB";
            do_SSB();
            break;
        case OP_SSW:
            s_op = "SSW";
            do_SSW();
            break;
        case OP_SSD:
            s_op = "SSD";
            do_SSD();
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

void Aesop::do_ADD(){
    // add the top two values from the stack
    std::vector<uint8_t> value(sizeof(uint16_t));
    uint8_t ops = 2;
    int32_t op[ops];

    for (uint8_t i = 0; i < ops; i++){
        if (mStack.top().size() == sizeof(uint8_t))
            op[i] = *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else if (mStack.top().size() == sizeof(uint16_t))
            op[i] = *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else if (mStack.top().size() == sizeof(uint32_t))
            op[i] = *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else
            std::throw_with_nested(std::runtime_error("Unknown vector in op of LT!"));
        mStack.pop();

        if (i == 0 && *mStack.top().data() == 0)
            mStack.pop();
        else if (i == 0 && *mStack.top().data() != 0)
            std::throw_with_nested(std::runtime_error("Delimiter not found!"));
    }

    *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(value.data())) = op[1] + op[0];

    std::cout << "ADD: " << op[1] << " + " << op[0] << " = "
              << *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(value.data()))
              << std::endl;

    mStack.push(value);
}

void Aesop::do_AIS(){
    // Array Index Shift
    std::vector<uint8_t> value(sizeof(uint16_t));
    uint8_t shift = mSOP[mCurrentSOP]->getByte();
    uint32_t op = 0;

    if (mStack.top().size() == sizeof(uint8_t))
        op = *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() == sizeof(uint16_t))
        op = *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() == sizeof(uint32_t))
        op = *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else
        std::throw_with_nested(std::runtime_error("Unknown vector in op of AIS!"));
    mStack.pop();

    /* is this necessary? */
    if (*mStack.top().data() != 0)
        std::throw_with_nested(std::runtime_error("Delimiter not found!"));
    mStack.pop();

    std::cout << "AIS: " << op << " << " << (uint16_t) shift << " = ";
    op <<= shift;
    std::cout << op << std::endl;
    *value.data() = op;
    mStack.push(value);

}

void Aesop::do_BRA(){
    // branch unconditionaly
    mSOP[mCurrentSOP]->setPC(mSOP[mCurrentSOP]->getWord());
}

void Aesop::do_BRF(){
    // branch when false
    uint16_t location = mSOP[mCurrentSOP]->getWord();
    bool do_branch = false;
    if (mStack.top().size() == sizeof(uint8_t) && *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data())) == 0)
        do_branch = true;
    else if (mStack.top().size() == sizeof(uint16_t) && *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data())) == 0)
        do_branch = true;
    else if (mStack.top().size() == sizeof(uint32_t) && *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data())) == 0)
        do_branch = true;

    mStack.pop();
    if (do_branch)
        mSOP[mCurrentSOP]->setPC(location);
}

void Aesop::do_BRT(){
    // branch when true
    uint16_t location = mSOP[mCurrentSOP]->getWord();
    bool do_branch = false;
    if (mStack.top().size() == sizeof(uint8_t) && *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data())) != 0)
        do_branch = true;
    else if (mStack.top().size() == sizeof(uint16_t) && *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data())) != 0)
        do_branch = true;
    else if (mStack.top().size() == sizeof(uint32_t) && *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data())) != 0)
        do_branch = true;

    mStack.pop();
    if (do_branch)
        mSOP[mCurrentSOP]->setPC(location);
}

void Aesop::do_CASE(){
    // case statement
    uint16_t case_entries = mSOP[mCurrentSOP]->getWord();  //number of entries in this CASE
    int32_t switch_value = *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
    mStack.pop();
    int32_t jump_address = -1;
    std::cout << "CASE: entries - " << case_entries << ", switch: " << switch_value << std::endl;
    for (uint16_t i = 0; i < case_entries; i++){
        uint32_t case_value = mSOP[mCurrentSOP]->getLong();
        uint16_t case_address = mSOP[mCurrentSOP]->getWord();
        std::cout << "          CASE #"
                  << i << ": " << case_value << " -> "
                  << case_address << std::endl;
        if (switch_value == case_value)
            jump_address = case_address;
    }
    uint16_t default_jump_address = mSOP[mCurrentSOP]->getWord();
    std::cout << "          DEFAULT: -> "
              << default_jump_address << std::endl;
    if (jump_address == -1)
        jump_address = default_jump_address;
    std::cout << "CASE: jump_address - " << jump_address << std::endl;
    mSOP[mCurrentSOP]->setPC(jump_address);
}

void Aesop::do_CALL(){
    // call function with parameter validation
    uint8_t num_of_parameters = mSOP[mCurrentSOP]->getByte(); // number of parameters
    std::map<uint8_t, std::vector<uint8_t>> parameter;
    for (uint8_t i = num_of_parameters; i > 0; i--){
        parameter[i] = std::vector<uint8_t>(mStack.top().size());
        std::copy(mStack.top().begin(), mStack.top().end(), parameter[i].begin());
        mStack.pop();

        std::cout << "CALL: - parameter (" << (uint16_t) i << ") " << (uint16_t) *parameter[i].data() << std::endl;
        // check for parameter delimiter
        if (*mStack.top().data() != 0)
            std::throw_with_nested(std::runtime_error("Delimiter not found!"));
        else
            mStack.pop();
    }
    call_function(parameter);
}

void Aesop::do_DIV(){
    std::vector<uint8_t> value(sizeof(uint16_t));
    uint8_t ops = 2;
    int32_t op[ops];

    for (uint8_t i = 0; i < ops; i++){
        if (mStack.top().size() == sizeof(uint8_t))
            op[i] = *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else if (mStack.top().size() == sizeof(uint16_t))
            op[i] = *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else if (mStack.top().size() == sizeof(uint32_t))
            op[i] = *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else
            std::throw_with_nested(std::runtime_error("Unknown vector in op of DIV!"));
        mStack.pop();

        if (i == 0 && *mStack.top().data() == 0)
            mStack.pop();
        else if (i == 0 && *mStack.top().data() != 0)
            std::throw_with_nested(std::runtime_error("Delimiter not found!"));
    }

    *value.data() = op[1] / op[0];

    std::cout << "DIV: " << op[1] << " / " << op[0] << " = "
              << *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(value.data()))
              << std::endl;

    mStack.push(value);
}

void Aesop::do_INTC(){
    // push word in bytecode onto stack
    std::vector<uint8_t> temp(sizeof(uint16_t));
    *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(temp.data())) = mSOP[mCurrentSOP]->getWord();
    mStack.push(temp);
}

void Aesop::do_LAB(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord()-sizeof(int8_t); // offset of byte
    std::cout << "LAB offset:" << (uint16_t) offset << std::endl;
    std::vector<uint8_t> value(sizeof(int8_t));
    std::copy(mSOP[mCurrentSOP]->mLocalVariable.data()+offset, mSOP[mCurrentSOP]->mLocalVariable.data()+offset+sizeof(int8_t), value.begin());
    mStack.push(value);
    std::cout << "LAB value:" << *reinterpret_cast<int8_t*>(mStack.top().data()) << " " << *reinterpret_cast<int8_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
}

void Aesop::do_LAD(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord()-sizeof(int32_t); // offset of long
    std::cout << "LAD offset:" << (uint16_t) offset << std::endl;
    std::vector<uint8_t> value(sizeof(int32_t));
    std::copy(mSOP[mCurrentSOP]->mLocalVariable.data()+offset, mSOP[mCurrentSOP]->mLocalVariable.data()+offset+sizeof(int32_t), value.begin());
    mStack.push(value);
    std::cout << "LAD value:" << *reinterpret_cast<int32_t*>(mStack.top().data()) << " " << *reinterpret_cast<int32_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
}

void Aesop::do_LAW(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord()-sizeof(int16_t); // offset of word
    std::cout << "LAW offset:" << (uint16_t) offset << std::endl;
    std::vector<uint8_t> value(sizeof(uint16_t));
    std::copy(mSOP[mCurrentSOP]->mLocalVariable.data()+offset, mSOP[mCurrentSOP]->mLocalVariable.data()+offset+sizeof(uint16_t), value.begin());
    mStack.push(value);
    std::cout << "LAW value:" << *reinterpret_cast<int16_t*>(mStack.top().data()) << " " << *reinterpret_cast<int16_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
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

void Aesop::do_LSB(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord();
    std::cout << "LSB offset:" << (uint16_t) offset << std::endl;
    std::vector<uint8_t> value(sizeof(uint8_t));
    std::copy(mStaticVariable.data()+offset, mStaticVariable.data()+offset+sizeof(uint8_t), value.begin());
    mStack.push(value);
    std::cout << "LSB value:" << *mStack.top().data() << " " << *reinterpret_cast<int8_t*>(mStaticVariable.data()+offset) << std::endl;
}

void Aesop::do_LSD(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord();
    std::cout << "LSD offset:" << (uint16_t) offset << std::endl;
    std::vector<uint8_t> value(sizeof(uint32_t));
    std::copy(mStaticVariable.data()+offset, mStaticVariable.data()+offset+sizeof(uint32_t), value.begin());
    mStack.push(value);
    std::cout << "LSD value:" << *mStack.top().data() << " " << *reinterpret_cast<int32_t*>(mStaticVariable.data()+offset) << std::endl;
}

void Aesop::do_LSW(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord();
    std::cout << "LSW offset:" << (uint16_t) offset << std::endl;
    std::vector<uint8_t> value(sizeof(uint16_t));
    std::copy(mStaticVariable.data()+offset, mStaticVariable.data()+offset+sizeof(uint16_t), value.begin());
    mStack.push(value);
    std::cout << "LSW value:" << *mStack.top().data() << " " << *reinterpret_cast<int16_t*>(mStaticVariable.data()+offset) << std::endl;
}

void Aesop::do_LT(){
    std::vector<uint8_t> value(sizeof(uint8_t));
    uint8_t ops = 2;
    int32_t op[ops];

    for (uint8_t i = 0; i < ops; i++){
        if (mStack.top().size() == sizeof(uint8_t))
            op[i] = *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else if (mStack.top().size() == sizeof(uint16_t))
            op[i] = *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else if (mStack.top().size() == sizeof(uint32_t))
            op[i] = *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else
            std::throw_with_nested(std::runtime_error("Unknown vector in op of LT!"));
        mStack.pop();

        if (i == 0 && *mStack.top().data() == 0)
            mStack.pop();
        else if (i == 0 && *mStack.top().data() != 0)
            std::throw_with_nested(std::runtime_error("Delimiter not found!"));
    }

    if (op[1] < op[0])
        *value.data() = 1;
    else
        *value.data() = 0;

    std::cout << "LT: " << op[1] << " < " << op[0] << " == "
              << (uint16_t) *value.data() << std::endl;

    mStack.push(value);
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

void Aesop::do_MUL(){
    std::vector<uint8_t> value(sizeof(uint16_t));
    uint8_t ops = 2;
    int32_t op[ops];

    for (uint8_t i = 0; i < ops; i++){
        if (mStack.top().size() == sizeof(uint8_t))
            op[i] = *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else if (mStack.top().size() == sizeof(uint16_t))
            op[i] = *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else if (mStack.top().size() == sizeof(uint32_t))
            op[i] = *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
        else
            std::throw_with_nested(std::runtime_error("Unknown vector in op of MUL!"));
        mStack.pop();

        if (i == 0 && *mStack.top().data() == 0)
            mStack.pop();
        else if (i == 0 && *mStack.top().data() != 0)
            std::throw_with_nested(std::runtime_error("Delimiter not found!"));
    }

    *value.data() = op[1] * op[0];

    std::cout << "MUL: " << op[1] << " * " << op[0] << " = "
              << *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(value.data()))
              << std::endl;

    mStack.push(value);
}

void Aesop::do_NEG(){
    std::vector<uint8_t> value(sizeof(uint8_t));
    if (mStack.top().size() == sizeof(uint8_t))
        *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(value.data())) = -*reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() == sizeof(uint16_t))
        *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(value.data())) = -*reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() == sizeof(uint32_t))
        *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(value.data())) = -*reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else
        std::throw_with_nested(std::runtime_error("Unknown vector to NEGate!"));

    mStack.pop();
    mStack.push(value);
}

void Aesop::do_NOT(){
    std::vector<uint8_t> value(sizeof(uint8_t));
    if (mStack.top().size() == sizeof(uint8_t))
        *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(value.data())) = ~*reinterpret_cast<int8_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() == sizeof(uint16_t))
        *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(value.data())) = ~*reinterpret_cast<int16_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else if (mStack.top().size() == sizeof(uint32_t))
        *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(value.data())) = ~*reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mStack.top().data()));
    else
        std::throw_with_nested(std::runtime_error("Unknown vector to logical NOT!"));

    mStack.pop();
    mStack.push(value);
}

void Aesop::do_PUSH(){
    // push 0 on to stack, used a delimiter for CALL and SEND
    std::vector<uint8_t> temp(sizeof(uint8_t));
    *temp.data() = 0;
    mStack.push(temp);
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
    uint16_t offset = mSOP[mCurrentSOP]->getWord()-sizeof(int8_t); // offset of byte
    std::cout << "SAB offset:" << (uint16_t) offset << std::endl;
    std::cout << "SAB value:" << *reinterpret_cast<int8_t*>(mStack.top().data()) << " " << *reinterpret_cast<int8_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
    std::copy(mStack.top().begin(), mStack.top().end(), mSOP[mCurrentSOP]->mLocalVariable.data()+offset);
    std::cout << "SAB value:" << *reinterpret_cast<int8_t*>(mStack.top().data()) << " " << *reinterpret_cast<int8_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
    mStack.pop();
}

void Aesop::do_SAD(){
    // Store Auto Dword
    uint16_t offset = mSOP[mCurrentSOP]->getWord()-sizeof(int32_t); // offset of long
    std::cout << "SAD offset:" << (uint16_t) offset << std::endl;
    std::cout << "SAD value:" << *reinterpret_cast<int32_t*>(mStack.top().data()) << " " << *reinterpret_cast<int32_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
    std::copy(mStack.top().begin(), mStack.top().end(), mSOP[mCurrentSOP]->mLocalVariable.data()+offset);
    std::cout << "SAD value:" << *reinterpret_cast<int32_t*>(mStack.top().data()) << " " << *reinterpret_cast<int32_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
    mStack.pop();
}

void Aesop::do_SAW(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord()-sizeof(int16_t); // offset of word
    std::cout << "SAW offset:" << (uint16_t) offset << std::endl;
    std::cout << "SAW value:" << *reinterpret_cast<int16_t*>(mStack.top().data()) << " " << *reinterpret_cast<int16_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
    std::copy(mStack.top().begin(), mStack.top().end(), mSOP[mCurrentSOP]->mLocalVariable.data()+offset);
    std::cout << "SAW value:" << *reinterpret_cast<int16_t*>(mStack.top().data()) << " " << *reinterpret_cast<int16_t*>(mSOP[mCurrentSOP]->mLocalVariable.data()+offset) << std::endl;
    mStack.pop();
}

void Aesop::do_SHTC(){
    std::vector<uint8_t> value(sizeof(uint8_t));
    *reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(value.data())) = mSOP[mCurrentSOP]->getByte();
    mStack.push(value);
}

void Aesop::do_SEND(){
    uint16_t value = mSOP[mCurrentSOP]->getByte();

    std::cout << "SEND: " << value;
    if (value == 0)
        std::cout << " (THIS) ";

    uint16_t message_number = mSOP[mCurrentSOP]->getWord();
    std::cout << ", " << message_number
              << " -> '" << mRes.getTableEntry(message_number, 4) // TODO: get this through SOP
              << "'" << std::endl;

    // TODO: is this right? set it to the value on the stack?
    mCurrentSOP = *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mStack.top().data()));
    mStack.pop();
    mSOP[mCurrentSOP]->setPC(mSOP[mCurrentSOP]->getSOPMessagePosition(message_number));
    uint16_t local_var_size = reinterpret_cast<uint16_t&>(mSOP[mCurrentSOP]->getWord()); // get THIS
    mSOP[mCurrentSOP]->mLocalVariable.resize(local_var_size);
    *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mSOP[mCurrentSOP]->mLocalVariable.data())) = local_var_size;
}

void Aesop::do_SSB(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord();
    std::cout << "SSB offset:" << (int16_t) offset << std::endl;
    std::cout << "SSB value:" << *mStack.top().data() << " " << *reinterpret_cast<int8_t*>(mStaticVariable.data()+offset) << std::endl;
    std::copy(mStack.top().begin(), mStack.top().end(), mStaticVariable.data()+offset);
    std::cout << "SSB value:" << *mStack.top().data() << " " << *reinterpret_cast<int8_t*>(mStaticVariable.data()+offset) << std::endl;
    mStack.pop();
}

void Aesop::do_SSD(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord();
    std::cout << "SSD offset:" << (int16_t) offset << std::endl;
    std::cout << "SSD value:" << *mStack.top().data() << " " << *reinterpret_cast<int32_t*>(mStaticVariable.data()+offset) << std::endl;
    std::copy(mStack.top().begin(), mStack.top().end(), mStaticVariable.data()+offset);
    std::cout << "SSD value:" << *mStack.top().data() << " " << *reinterpret_cast<int32_t*>(mStaticVariable.data()+offset) << std::endl;
    mStack.pop();
}

void Aesop::do_SSW(){
    uint16_t offset = mSOP[mCurrentSOP]->getWord();
    std::cout << "SSW offset:" << (int16_t) offset << std::endl;
    std::cout << "SSW value:" << mStack.top().data() << " " << *reinterpret_cast<int16_t*>(mStaticVariable.data()+offset) << std::endl;
    std::copy(mStack.top().begin(), mStack.top().end(), mStaticVariable.data()+offset);
    std::cout << "SSW value:" << *mStack.top().data() << " " << *reinterpret_cast<int16_t*>(mStaticVariable.data()+offset) << std::endl;
    mStack.pop();
}

void Aesop::do_SXB(){
    uint16_t value = mSOP[mCurrentSOP]->getWord();
}

void Aesop::call_function(std::map<uint8_t, std::vector<uint8_t>> &parameters){
    // call up function and send parameters
    uint16_t function_id = *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mStack.top().data()));
    std::cout << "CALL - Calling: " << mSOP[mCurrentSOP]->getSOPImportName(function_id) << std::endl;
    mStack.pop();

    switch(function_id){
    case C_PEEKMEM:
    {
        std::vector<uint8_t> value(sizeof(int32_t));
        *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(value.data())) = peekmem(parameters);
        mStack.push(value);
    }
        break;
    case C_POKEMEM:
        pokemem(parameters);
        break;
    case C_LAUNCH:
        // TODO: launch application, for now we'll just jump right back
        // into the SOP bytecode from the beginning.
        mCurrentSOP = mRes.getIndex("start");
        mSOP[mCurrentSOP]->setPC(mSOP[mCurrentSOP]->getSOPMessagePosition(INDEX_CREATE));
        {
            uint16_t local_var_size = reinterpret_cast<uint16_t&>(mSOP[mCurrentSOP]->getWord()); // get THIS
            mSOP[mCurrentSOP]->mLocalVariable.resize(local_var_size);
            *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mSOP[mCurrentSOP]->mLocalVariable.data())) = local_var_size;
        }
        break;
    case C_CREATE_PROGRAM:
        {   /* When creating a program, we automatically launch into it's CREATE function. */
            std::cout << "CALL - C_CREATE_PROGRAM 1: "
                      << (int16_t) *reinterpret_cast<int8_t*>(reinterpret_cast<void*>(parameters[1].data())) << std::endl;
            mCurrentSOP = *reinterpret_cast<int16_t*>(reinterpret_cast<void*>(parameters[2].data()));
            std::cout << "CALL - C_CREATE_PROGRAM 2: " << mCurrentSOP << std::endl;
            mSOP[mCurrentSOP] = std::make_unique<SOP>(mRes, mCurrentSOP);

            mSOP[mCurrentSOP]->setPC(mSOP[mCurrentSOP]->getSOPMessagePosition(INDEX_CREATE));
            uint16_t local_var_size = reinterpret_cast<uint16_t&>(mSOP[mCurrentSOP]->getWord()); // get THIS
            mSOP[mCurrentSOP]->mLocalVariable.resize(local_var_size);
            *reinterpret_cast<uint16_t*>(reinterpret_cast<void*>(mSOP[mCurrentSOP]->mLocalVariable.data())) = local_var_size;
        }
        break;


    default:
        std::cout << "CALL -> Unimplimented function: " << std::hex << function_id << std::dec<< std::endl;
    }

}

}
