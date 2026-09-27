#pragma once

#include <string>

namespace campus {

// HMAC-SHA256，返回 32 字节原始摘要。
// JWT 的 HS256 签名就是对「base64url(header).base64url(payload)」做 HMAC。
std::string HmacSha256(const std::string& key, const std::string& data);

// 十六进制形式，便于日志与测试
std::string HmacSha256Hex(const std::string& key, const std::string& data);

}  // namespace campus

