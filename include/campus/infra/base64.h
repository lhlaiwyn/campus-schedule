#pragma once

#include <string>

namespace campus {

// URL 安全的 Base64（RFC 4648 §5）：用 - 和 _ 代替 + 和 /，不写 = 填充。
// JWT 的三个部分都用这个编码。
std::string Base64UrlEncode(const std::string& input);

// 解析失败返回 false。带不带 = 填充都接受，字符集和长度都必须合法。
bool Base64UrlDecode(const std::string& input, std::string& output);

}  // namespace campus

