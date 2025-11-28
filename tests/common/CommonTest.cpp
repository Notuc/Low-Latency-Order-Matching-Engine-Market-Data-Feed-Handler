#include <gtest/gtest.h>

// A basic test suite to check GTest functionality
TEST(GTestTest, IsWorking) {
    EXPECT_EQ(1 + 1, 2);
}

// A second test to demonstrate a failure
TEST(GTestTest, FailsAsExpected) {
    EXPECT_TRUE(false);
}
