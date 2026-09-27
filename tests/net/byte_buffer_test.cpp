#include <string>

#include <gtest/gtest.h>

#include "campus/net/byte_buffer.h"

namespace campus::net {
namespace {

TEST(ByteBufferTest, AppendAndRead) {
    ByteBuffer buffer(8);
    EXPECT_EQ(buffer.ReadableBytes(), 0U);

    buffer.Append("hello", 5);
    EXPECT_EQ(buffer.ReadableBytes(), 5U);
    EXPECT_EQ(buffer.ReadAsString(), "hello");

    buffer.Consume(2);
    EXPECT_EQ(buffer.ReadAsString(), "llo");

    buffer.Consume(3);
    EXPECT_TRUE(buffer.Empty());
}

TEST(ByteBufferTest, GrowsWhenNeeded) {
    ByteBuffer buffer(4);
    buffer.Append("abcd", 4);
    EXPECT_EQ(buffer.Capacity(), 4U);

    buffer.Append("e", 1);
    EXPECT_GE(buffer.Capacity(), 5U);
    EXPECT_EQ(buffer.ReadAsString(), "abcde");
}

TEST(ByteBufferTest, CompactReclaimsConsumedSpaceWithoutGrowing) {
    ByteBuffer buffer(8);
    buffer.Append("12345678", 8);
    buffer.Consume(7);

    buffer.Append("9", 1);
    EXPECT_LE(buffer.Capacity(), 8U);
    EXPECT_EQ(buffer.ReadAsString(), "89");
}

TEST(ByteBufferTest, FindLocatesNeedleWithoutConsuming) {
    ByteBuffer buffer(64);
    const std::string request = "GET /api/health HTTP/1.1\r\n\r\n";
    buffer.Append(request);

    const std::size_t pos = buffer.Find("\r\n\r\n", 4);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(pos, request.size() - 4);

    EXPECT_EQ(buffer.ReadableBytes(), request.size());
    EXPECT_EQ(buffer.Find("\r\n\r\n", 4), pos);
}

TEST(ByteBufferTest, FindMissesWhenAbsentOrOversized) {
    ByteBuffer buffer(16);
    buffer.Append("abc", 3);
    EXPECT_EQ(buffer.Find("xyz", 3), std::string::npos);
    EXPECT_EQ(buffer.Find("abcd", 4), std::string::npos);
    EXPECT_EQ(buffer.Find("", 0), std::string::npos);
}

TEST(ByteBufferTest, ClearResetsContentButKeepsCapacity) {
    ByteBuffer buffer(8);
    buffer.Append("data", 4);
    buffer.Clear();
    EXPECT_TRUE(buffer.Empty());
    EXPECT_EQ(buffer.Capacity(), 8U);
}

}  // namespace
}  // namespace campus::net
