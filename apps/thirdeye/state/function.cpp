#include "function.hpp"

namespace STATE {

Functions::Functions() {}

Functions::~Functions() {}

void Functions::pokemem(std::map<uint8_t, std::vector<uint8_t>> parameters)
{
    uint32_t index = *parameters[1].data();
    mMemory[index] = std::vector<uint8_t>(parameters[2].size());
    std::copy(mMemory[index].begin(), mMemory[index].end(), parameters[2].begin());
}

int32_t Functions::peekmem(std::map<uint8_t, std::vector<uint8_t>> parameters)
{
    uint32_t index = *parameters[1].data();
    if (!mMemory.count(index))
        mMemory[index] = std::vector<uint8_t>(sizeof(int32_t));
    return *mMemory[index].data();
}

}
