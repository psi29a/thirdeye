/*
 * color.hpp
 *
 *  Created on: Mar 28, 2013
 *      Author: bcurtis
 */

#ifndef COMPONENTS_UTILS_COLOR_HPP
#define COMPONENTS_UTILS_COLOR_HPP

namespace Utils
{
// color macros for conversions
struct rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

unsigned long testing(uint8_t r, uint8_t g, uint8_t b){
	return r << 24 | g << 16 | b << 8;
}
/*
#define RGBTOONE(r,g,b) (((r) & 224) + (((g) & 224)>>3) + (((b) & 192)>>6))

#define RGBTO24(r,g,b) ((((unsigned long)(b))<<16)|(((unsigned long)(g))<<8)|(r))

#define RGB2LONG(l,r,g,b) (l = ((((unsigned char)(r)) << 24) \
                              | (((unsigned char)(g)) << 16) \
                              | (((unsigned char)(b)) << 8)))

#define LONG2RGB(l,r,g,b) ((r = ((0xff000000 & l) >> 24)), \
                           (g = ((0xff0000 & l) >> 16)) , \
                           (b = ((0xff00 & l) >> 8)))

#define COLOR2LONG(l,p) (l = ((((p).r) << 24) | (((p).g) << 16) | (((p).b) << 8)))
*/
}

#endif /* COLOR_HPP_ */
