#include "function.hpp"

namespace STATE {

Functions::Functions() {}

Functions::~Functions() {}

void Functions::pokemem(std::map<uint8_t, std::vector<uint8_t>> &parameters)
{
    uint32_t index = *parameters[1].data();
    mMemory[index] = std::vector<uint8_t>(parameters[2].size());
    std::copy(parameters[2].begin(), parameters[2].end(), mMemory[index].begin());
}

int32_t Functions::peekmem(std::map<uint8_t, std::vector<uint8_t>> &parameters)
{
    uint32_t index = *parameters[1].data();
    if (!mMemory.count(index)){
        mMemory[index] = std::vector<uint8_t>(sizeof(int32_t));
        std::cout << "PEEKMEM: index "  << index << " not found!" << std::endl;
    }
    std::cout << "PEEKMEM: returning " << *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mMemory[index].data())) << std::endl;
    return *reinterpret_cast<int32_t*>(reinterpret_cast<void*>(mMemory[index].data()));
}

}
