#include "campus/infra/jwt.h"

#include <cstddef>
#include <exception>
#include <string>

#include <nlohmann/json.hpp>

#include "campus/infra/base64.h"
#include "campus/infra/crypto.h"
#include "campus/infra/hmac.h"

namespace campus {
namespace {

using json = nlohmann::json;

constexpr std::size_t kMinSecretLength = 16;
constexpr const char* kAlgorithm = "HS256";

Result<std::string> FailToken(const std::string& message) {
    return Result<std::string>::Fail(ErrorCode::kInternalError, message);
}

Result<JwtClaims> FailClaims(ErrorCode code, const std::string& message) {
    return Result<JwtClaims>::Fail(code, message);
}

// nlohmann 的对象是按 key 排序的，dump() 输出紧凑格式，
// 因此序列化结果稳定，同一个 claims 每次签出的 token 完全一致。
std::string HeaderJson() {
    return json{{"alg", kAlgorithm}, {"typ", "JWT"}}.dump();
}

std::string PayloadJson(const JwtClaims& claims) {
    return json{{"sub", claims.subject},
                {"iat", claims.issued_at},
                {"exp", claims.expires_at}}
        .dump();
}

}  // namespace

Result<std::string> SignJwt(const JwtClaims& claims, const std::string& secret) {
    if (secret.size() < kMinSecretLength) {
        return FailToken("JWT 密钥太短（至少 16 字节），拒绝用弱密钥签发");
    }
    if (claims.subject.empty()) {
        return FailToken("JWT 的 subject 不能为空");
    }
    if (claims.expires_at <= claims.issued_at) {
        return FailToken("JWT 的过期时间必须晚于签发时间");
    }

    const std::string header_b64 = Base64UrlEncode(HeaderJson());
    const std::string payload_b64 = Base64UrlEncode(PayloadJson(claims));
    const std::string signing_input = header_b64 + "." + payload_b64;
    const std::string signature_b64 = Base64UrlEncode(HmacSha256(secret, signing_input));
    return Result<std::string>::Ok(signing_input + "." + signature_b64);
}

Result<JwtClaims> VerifyJwt(const std::string& token, const std::string& secret,
                            std::int64_t now) {
    if (token.empty()) {
        return FailClaims(ErrorCode::kUnauthorized, "缺少令牌");
    }

    const std::size_t first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT 格式非法：缺少分隔符");
    }
    const std::size_t second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT 格式非法：段数不足");
    }
    if (token.find('.', second_dot + 1) != std::string::npos) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT 格式非法：段数过多");
    }

    const std::string header_b64 = token.substr(0, first_dot);
    const std::string payload_b64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
    const std::string signature_b64 = token.substr(second_dot + 1);
    if (header_b64.empty() || payload_b64.empty() || signature_b64.empty()) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT 格式非法：存在空段");
    }

    // 先验签再解析内容：未通过签名校验的数据一律不能信
    const std::string expected_signature =
        Base64UrlEncode(HmacSha256(secret, header_b64 + "." + payload_b64));
    if (!ConstantTimeEquals(expected_signature, signature_b64)) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT 签名校验失败");
    }

    std::string header_json;
    if (!Base64UrlDecode(header_b64, header_json)) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT header 不是合法 base64url");
    }
    try {
        const json header = json::parse(header_json);
        if (!header.is_object() || header.value("alg", std::string{}) != kAlgorithm) {
            return FailClaims(ErrorCode::kUnauthorized, "JWT 的 alg 不是 HS256，拒绝接受");
        }
    } catch (const std::exception& e) {
        return FailClaims(ErrorCode::kUnauthorized,
                          std::string("JWT header 不是合法 JSON: ") + e.what());
    }

    std::string payload_json;
    if (!Base64UrlDecode(payload_b64, payload_json)) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT payload 不是合法 base64url");
    }

    JwtClaims claims;
    try {
        const json payload = json::parse(payload_json);
        if (!payload.is_object()) {
            return FailClaims(ErrorCode::kUnauthorized, "JWT payload 必须是 JSON 对象");
        }
        claims.subject = payload.value("sub", std::string{});
        claims.issued_at = payload.value("iat", static_cast<std::int64_t>(0));
        claims.expires_at = payload.value("exp", static_cast<std::int64_t>(0));
    } catch (const std::exception& e) {
        return FailClaims(ErrorCode::kUnauthorized,
                          std::string("JWT payload 字段类型错误: ") + e.what());
    }

    if (claims.subject.empty()) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT 缺少 sub");
    }
    if (claims.expires_at <= now) {
        return FailClaims(ErrorCode::kUnauthorized, "JWT 已过期");
    }
    return Result<JwtClaims>::Ok(claims);
}

}  // namespace campus

