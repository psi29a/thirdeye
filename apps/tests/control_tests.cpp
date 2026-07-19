#include "gtest/gtest.h"

#include "../thirdeye/control.hpp"

// Just the pure line parser -- socket / SDL are not linked into runtests.
// See docs/control_channel.md Testing section.

TEST(control, tokenizeEmpty) {
    EXPECT_TRUE(THIRDEYE::control::tokenize("").empty());
    EXPECT_TRUE(THIRDEYE::control::tokenize("   \t\r").empty());
}

TEST(control, tokenizeVerbOnly) {
    auto t = THIRDEYE::control::tokenize("ping");
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0], "ping");
}

TEST(control, tokenizeKey) {
    auto t = THIRDEYE::control::tokenize("key 4800");
    ASSERT_EQ(t.size(), 2u);
    EXPECT_EQ(t[0], "key");
    EXPECT_EQ(t[1], "4800");
}

TEST(control, tokenizeClickSpacesAndTabs) {
    auto t = THIRDEYE::control::tokenize("  click\tL   126  133  ");
    ASSERT_EQ(t.size(), 4u);
    EXPECT_EQ(t[0], "click");
    EXPECT_EQ(t[1], "L");
    EXPECT_EQ(t[2], "126");
    EXPECT_EQ(t[3], "133");
}

TEST(control, tokenizeStripsTrailingCR) {
    // Real clients send "\r\n"; tokenize() sees the "\r" after socket strips "\n".
    auto t = THIRDEYE::control::tokenize("dump /tmp/x.bmp\r");
    ASSERT_EQ(t.size(), 2u);
    EXPECT_EQ(t[0], "dump");
    EXPECT_EQ(t[1], "/tmp/x.bmp");
}
