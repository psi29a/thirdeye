#ifndef FONT_HPP
#define FONT_HPP

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>

namespace GRAPHICS {

/*
 struct Font
 {
 uint16_t numberOfCharacters;
 uint16_t charHeight;
 }
 */

class Font {
public:
	Font(std::vector<uint8_t> vec);
	virtual ~Font();
	SDL_Surface* getCharacter(uint8_t ascii);
private:
	std::vector<uint8_t> vec_;
	std::map<uint8_t, SDL_Surface*> character;
	std::map<uint8_t, uint16_t> index;
};

}
#endif //FONT_HPP
