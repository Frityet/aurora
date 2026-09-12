#include "internal.hpp"
#include <gtest/gtest.h>

#include <array>

TEST(ByteBufferTest, EmptyAppendPreservesBorrowedCapacity) {
  std::array<uint8_t, 16> mapped;
  mapped.fill(0xa5);
  aurora::ByteBuffer buffer{mapped.data(), mapped.size()};
  buffer.append(nullptr, 0);
  buffer.append_zeroes(0);
  buffer.reserve_extra(0);
  EXPECT_EQ(buffer.data(), mapped.data());
  EXPECT_EQ(buffer.size(), 0);
  buffer.append_zeroes(mapped.size());
  EXPECT_EQ(buffer.data(), mapped.data());
  EXPECT_EQ(buffer.size(), mapped.size());
  EXPECT_EQ(mapped, (std::array<uint8_t, 16>{}));
}

TEST(ByteBufferTest, ZeroAppendClearsReusedBytesWithoutChangingLivePrefix) {
  std::array<uint8_t, 16> mapped;
  mapped.fill(0xa5);
  aurora::ByteBuffer buffer{mapped.data(), mapped.size()};
  const std::array<uint8_t, 3> prefix{1, 2, 3};
  buffer.append(prefix.data(), prefix.size());
  buffer.append_zeroes(5);
  EXPECT_EQ(buffer.size(), 8);
  EXPECT_EQ(mapped, (std::array<uint8_t, 16>{1, 2, 3, 0, 0, 0, 0, 0,
                                           0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5}));
  buffer.clear();
  buffer.append_zeroes(mapped.size());
  EXPECT_EQ(mapped, (std::array<uint8_t, 16>{}));
}

TEST(ByteBufferTest, OwnedBufferCanGrowAfterEmptyOperations) {
  aurora::ByteBuffer buffer;
  buffer.append(nullptr, 0);
  buffer.append_zeroes(0);
  buffer.reserve_extra(0);
  EXPECT_EQ(buffer.size(), 0);
  buffer.append_zeroes(17);
  ASSERT_EQ(buffer.size(), 17);
  for (size_t i = 0; i < buffer.size(); ++i) EXPECT_EQ(buffer.data()[i], 0);
  buffer.clear();
  buffer.append_zeroes(33);
  ASSERT_EQ(buffer.size(), 33);
  for (size_t i = 0; i < buffer.size(); ++i) EXPECT_EQ(buffer.data()[i], 0);
}
