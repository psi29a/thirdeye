#include "font.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>

#include <stdio.h>
#include <string.h>

GRAPHICS::Font::Font(const std::vector<uint8_t> vec) :
		vec_(vec) {
	// CHARGEN/FONT6.FNT / FONT8.FNT (Westwood DOS bit-packed font, used by
	// CHGEN.EXE for race/class/stat/name text on the chargen screens):
	//   u16  fileSize - 2
	//   u16  offsets[128]    (one per ASCII 0..127; values are file-relative)
	//   u8   data[]           (glyph data: each glyph is `H` bytes, where H
	//                          is the glyph height -- 6 for FONT6, 8 for FONT8;
	//                          each byte is one row, bit 7 = leftmost of 8 cols)
	// Height is derived from the offset of the next glyph (or file size for
	// the last). Detected by: u16@0 == size - 2 AND the first table entry
	// points past the table itself (typically 260 = 2 + 128*2 + 2).
	if (vec_.size() >= 4 + 128 * 2) {
		uint16_t sm2 = vec_[0] | (vec_[1] << 8);
		uint16_t off0 = vec_[2] | (vec_[3] << 8);
		bool isChargenFnt = (sm2 == vec_.size() - 2) && off0 >= 2 + 128 * 2 &&
		                    off0 < vec_.size();
		if (isChargenFnt) {
			auto rd16 = [&](size_t o) -> uint16_t {
				return vec_[o] | (vec_[o + 1] << 8);
			};
			// Walk offsets[0..127]; the height of glyph i is offsets[i+1] - offsets[i]
			// (or filesize - offsets[i] for i = 127).
			for (int i = 0; i < 128; ++i) {
				uint16_t go = rd16(2 + i * 2);
				uint16_t go_next = (i + 1 < 128) ? rd16(2 + (i + 1) * 2)
				                                  : static_cast<uint16_t>(vec_.size());
				if (go_next <= go || go_next > vec_.size()) continue;
				uint16_t height = go_next - go;
				if (height == 0 || height > 64) continue;
				// 8 columns per byte, one byte per row, bit 7 = leftmost pixel.
				constexpr int width = 8;
				SDL_Surface *surf = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
				Uint32 white = SDL_MapSurfaceRGB(surf, 255, 255, 255);
				for (int y = 0; y < height; ++y) {
					uint8_t b = vec_[go + y];
					for (int x = 0; x < width; ++x) {
						if (b & (1 << (7 - x))) {
							SDL_Rect r = { x, y, 1, 1 };
							SDL_FillSurfaceRect(surf, &r, white);
						}
					}
				}
				SDL_SetSurfaceColorKey(surf, true,
				                       SDL_MapSurfaceRGB(surf, 0, 0, 0));
				character[static_cast<uint8_t>(i)] = surf;
			}
			return;
		}
	}

	// AESOP/16 "2." VFX font (every EYE.RES font): a 4-byte version ("2.\0\0"),
	// then u32 char_count / char_height / font_background, a u32 offset table
	// (one entry per char), and per char a u32 pixel-width followed by
	// width*height bytes (1 byte per pixel, 0 = transparent). The older format
	// (no version string) is handled by the branch below.
	if (vec_.size() >= 16 && vec_[0] == '2' && vec_[1] == '.') {
		auto rd32 = [&](size_t o) -> uint32_t {
			return vec_[o] | (vec_[o + 1] << 8) | (vec_[o + 2] << 16) |
			       (static_cast<uint32_t>(vec_[o + 3]) << 24);
		};
		uint32_t count = rd32(4);
		uint32_t height = rd32(8);
		for (uint32_t i = 0; i < count && i < 256; i++) {
			uint32_t glyphOff = rd32(16 + i * 4);
			if (glyphOff + 4 > vec_.size())
				continue;
			uint32_t width = rd32(glyphOff);
			if (width == 0 || width > 512 || height == 0 || height > 512)
				continue;
			size_t data = glyphOff + 4;
			SDL_Surface *surf = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
			Uint32 white = SDL_MapSurfaceRGB(surf, 255, 255, 255);
			for (uint32_t y = 0; y < height; y++)
				for (uint32_t x = 0; x < width; x++) {
					size_t p = data + static_cast<size_t>(y) * width + x;
					if (p < vec_.size() && vec_[p] != 0) {
						SDL_Rect r = { static_cast<int>(x), static_cast<int>(y),
						               1, 1 };
						SDL_FillSurfaceRect(surf, &r, white);
					}
				}
			SDL_SetSurfaceColorKey(surf, true, SDL_MapSurfaceRGB(surf, 0, 0, 0));
			character[static_cast<uint8_t>(i)] = surf;
		}
		return;
	}

	//uint16_t prev = 518;
	uint8_t characters = *reinterpret_cast<const uint16_t*>(&vec_[0]);
	uint8_t fontHeight = *reinterpret_cast<const uint16_t*>(&vec_[2]);

	for (uint16_t i = 0; i < characters; i++) {

		// find character information
		index[i] = *reinterpret_cast<const uint16_t*>(&vec_[264 + i * 2]);
		uint8_t charWidth = *reinterpret_cast<const uint16_t*>(&vec_[index[i]]);

		//std::cout << i << " " << index[i] << " " << index[i] - prev << " "
		//		<< (int) charWidth << std::endl;
		//prev = index[i];

		// create a surface to draw on
		character[i] = SDL_CreateSurface(charWidth, fontHeight, SDL_PIXELFORMAT_ARGB8888);
		Uint32 white = SDL_MapSurfaceRGB(character[i], 255, 255, 255);

		uint8_t counter = 2;
		uint16_t pixel = 0;

		// draw character to surface
		for (uint16_t x = 0; x < fontHeight; x++) {
			for (uint16_t y = 0; y < charWidth; y++) {
				SDL_Rect rect = { y, x, 1, 1 };
				pixel = vec_[index[i] + counter];
				if (pixel > 0) {
					SDL_FillSurfaceRect(character[i], &rect, white);
				}
				counter++;
			}
		}
		// set black as our transparency colour
		SDL_SetSurfaceColorKey(character[i], true,
				SDL_MapSurfaceRGB(character[i], 0, 0, 0));
	}
}

GRAPHICS::Font::~Font() {
	std::map<uint8_t, SDL_Surface*>::iterator it;
	for (it = character.begin(); it != character.end(); it++) {
		SDL_DestroySurface(it->second);
	}
}
SDL_Surface* GRAPHICS::Font::getCharacter(uint8_t ascii) {
	return (character[ascii]);
}
