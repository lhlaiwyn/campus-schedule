#pragma once

#include <cstdint>
#include <string>

#include "campus/domain/result.h"

namespace campus {

struct JwtClaims {
    std::string subject;          // sub：学号
    std::int64_t issued_at = 0;   // iat
    std::int64_t expires_at = 0;  // exp
};

// 用 HS256 签发 JWT。
// 密钥短于 16 字节直接拒绝，避免不知不觉用弱密钥上线。
Result<std::string> SignJwt(const JwtClaims& claims, const std::string& secret);

// 校验签名与有效期。now 由调用方传入，便于用固定时间做测试。
//
// 这里同时校验 alg 是否为 HS256，防止攻击者把 header 改成 alg=none
// 之类的降级手法绕过签名。
Result<JwtClaims> VerifyJwt(const std::string& token, const std::string& secret,
                            std::int64_t now);

}  // namespace campus

