#include "gtest/gtest.h"
#include <boost/assign/list_of.hpp>

#include "../thirdeye/graphics/graphics.hpp"

TEST (Palette_Test, Zeros_RES){
	std::vector<uint8_t> data(PALHEADEROFFSET+3,0);
	data[0] = 1;	// set number of colours
	GRAPHICS::Palette pal(data);
	EXPECT_EQ(0, pal[0].r);
	EXPECT_EQ(0, pal[0].g);
	EXPECT_EQ(0, pal[0].b);
	EXPECT_EQ(0, pal[0].a);
	EXPECT_EQ(1, pal.getNumOfColours());
}

TEST (Palette_Test, ProperlyShifted_RES){
	std::vector<uint8_t> data(PALHEADEROFFSET+3,0);
	data[0] = 1;	// set number of colours
	data[PALHEADEROFFSET] = 1;
	data[PALHEADEROFFSET+1] = 2;
	data[PALHEADEROFFSET+2] = 63;
	GRAPHICS::Palette pal(data);
	EXPECT_EQ(4, pal[0].r);
	EXPECT_EQ(8, pal[0].g);
	EXPECT_EQ(252, pal[0].b);
	EXPECT_EQ(0, pal[0].a);
	EXPECT_EQ(1, pal.getNumOfColours());
}

TEST (Palette_Test, Zeros_GFFI){
	std::vector<uint8_t> data(3,0);
	GRAPHICS::Palette pal(data, false);
	EXPECT_EQ(0, pal[0].r);
	EXPECT_EQ(0, pal[0].g);
	EXPECT_EQ(0, pal[0].b);
	EXPECT_EQ(0, pal[0].a);
	EXPECT_EQ(1, pal.getNumOfColours());
}

TEST (Palette_Test, ProperlyShifted_GFFI){
	std::vector<uint8_t> data = boost::assign::list_of(1)(2)(63);
	GRAPHICS::Palette pal(data, false);
	EXPECT_EQ(4, pal[0].r);
	EXPECT_EQ(8, pal[0].g);
	EXPECT_EQ(252, pal[0].b);
	EXPECT_EQ(0, pal[0].a);
	EXPECT_EQ(1, pal.getNumOfColours());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
