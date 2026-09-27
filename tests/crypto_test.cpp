#include <string>

#include <gtest/gtest.h>

#include "campus/infra/crypto.h"

namespace campus {
namespace {

// 期望值来自标准 SHA-256 测试向量，并与 Python hashlib 逐一对照过
TEST(Sha256Test, MatchesStandardVectors) {
    EXPECT_EQ(Sha256Hex(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(Sha256Hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, HandlesMultiBlockInput) {
    // 112 字节，刚好跨两个数据分组
    EXPECT_EQ(Sha256Hex(
                  "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                  "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
              "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");

    EXPECT_EQ(Sha256Hex(std::string(1000, 'a')),
              "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST(Sha256Test, ProducesLowercaseHexOfFixedLength) {
    const std::string digest = Sha256Hex("whatever");
    EXPECT_EQ(digest.size(), 64U);
    for (char c : digest) {
        const bool is_digit = c >= '0' && c <= '9';
        const bool is_lower = c >= 'a' && c <= 'f';
        EXPECT_TRUE(is_digit || is_lower) << "出现非小写十六进制字符: " << c;
    }
}

TEST(Sha256Test, HashPasswordCombinesSaltAndPassword) {
    EXPECT_EQ(HashPassword("jwgl-salt:", "secret123"),
              "c9f8e018911fdec9214c7533117e0670edaaf7acfabdb40d86dd4b9b5f6b6d27");
}

TEST(Sha256Test, DifferentInputsProduceDifferentDigests) {
    EXPECT_NE(Sha256Hex("password1"), Sha256Hex("password2"));
    // 盐不同，同一密码的哈希也必须不同
    EXPECT_NE(HashPassword("salt-a:", "secret123"), HashPassword("salt-b:", "secret123"));
}

}  // namespace
}  // namespace campus

