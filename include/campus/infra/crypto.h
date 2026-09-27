#pragma once

#include <string>

namespace campus {

// 计算输入字节串的 SHA-256，返回 64 位小写十六进制。
// 用 OpenSSL 的 EVP 接口实现，不自己造哈希算法。
std::string Sha256Hex(const std::string& input);

// 教务系统常见的密码处理方式：服务端下发一次性 salt，
// 客户端提交 sha256(salt + password)，明文密码不落到网络上。
std::string HashPassword(const std::string& salt, const std::string& password);

// 定长时间比较，避免通过比较耗时逐字节猜出正确值。
// 长度不同会直接返回 false（长度本身不算秘密）。
bool ConstantTimeEquals(const std::string& lhs, const std::string& rhs);

}  // namespace campus
