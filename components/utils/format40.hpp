#ifndef COMPONENTS_UTILS_FORMAT40_HPP
#define COMPONENTS_UTILS_FORMAT40_HPP

//#include <cstdint> /usr/include/c++/4.7/bits/c++0x_warning.h:32:2: error: #error This file requires compiler and library support for the ISO C++ 2011 standard. This support is currently experimental, and must be enabled with the -std=c++11 or -std=gnu++11 compiler options.
#include <stdint.h>

namespace Utils
{
	void Format40_Decode(uint8_t *dst, uint8_t *src);
}

#endif /* COMPONENTS_UTILS_FORMAT40_HPP */
