#include <gtest/gtest.h>
#include "dlr.h"


TEST(TVM, Test1) {
  EXPECT_EQ(1, 1);
}

int main(int argc, char ** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}