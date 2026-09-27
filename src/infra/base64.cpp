#include "campus/infra/base64.h"

#include <array>
#include <cstddef>

namespace campus {
namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// 反查表：255 表示非法字符
std::array<unsigned char, 256> BuildReverseTable() {
    std::array<unsigned char, 256> table{};
    table.fill(255);
    for (unsigned char i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kAlphabet[i])] = i;
    }
    return table;
}

const std::array<unsigned char, 256>& ReverseTable() {
    static const std::array<unsigned char, 256> table = BuildReverseTable();
    return table;
}

}  // namespace

std::string Base64UrlEncode(const std::string& input) {
    std::string out;
    out.reserve((input.size() + 2) / 3 * 4);

    std::size_t i = 0;
    while (i + 3 <= input.size()) {
        const unsigned int chunk = (static_cast<unsigned char>(input[i]) << 16) |
                                   (static_cast<unsigned char>(input[i + 1]) << 8) |
                                   static_cast<unsigned char>(input[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kAlphabet[chunk & 0x3F]);
        i += 3;
    }

    const std::size_t remaining = input.size() - i;
    if (remaining == 1) {
        const unsigned int chunk = static_cast<unsigned char>(input[i]) << 16;
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
    } else if (remaining == 2) {
        const unsigned int chunk = (static_cast<unsigned char>(input[i]) << 16) |
                                   (static_cast<unsigned char>(input[i + 1]) << 8);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
    }
    return out;
}

bool Base64UrlDecode(const std::string& input, std::string& output) {
    output.clear();

    // 先去掉可能存在的填充，统一按无填充处理
    std::size_t length = input.size();
    while (length > 0 && input[length - 1] == '=') {
        --length;
    }

    // 合法的 base64 长度除以 4 不可能余 1
    if (length % 4 == 1) {
        return false;
    }

    const auto& table = ReverseTable();
    unsigned int buffer = 0;
    int bits = 0;
    output.reserve(length * 3 / 4);

    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char value = table[static_cast<unsigned char>(input[i])];
        if (value == 255) {
            output.clear();
            return false;
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }

    // 末尾多余的位必须全是 0，否则说明输入被篡改
    if (bits > 0 && (buffer & ((1u << bits) - 1u)) != 0) {
        output.clear();
        return false;
    }
    return true;
}

}  // namespace campus

