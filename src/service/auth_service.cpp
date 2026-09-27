#include "campus/service/auth_service.h"

#include <utility>

#include "campus/infra/jwt.h"

namespace campus {
namespace {

// 会话属于「某个适配器 + 某个学号」：
// 同一个学号在不同学校的教务系统里是两个独立会话，不能互相覆盖。
std::string SessionKeyFor(const std::string& adapter_name, const std::string& student_id) {
    return adapter_name + ":" + student_id;
}

}  // namespace

AuthService::AuthService(PortalAdapter& portal, PortalSessionStore& sessions, std::string secret,
                         std::int64_t token_ttl_seconds)
    : portal_(portal),
      sessions_(sessions),
      secret_(std::move(secret)),
      token_ttl_seconds_(token_ttl_seconds > 0 ? token_ttl_seconds : 3600) {}

Result<std::string> AuthService::Login(const PortalCredentials& credentials,
                                       std::int64_t now) {
    // 凭据是否正确交给教务系统判断，本服务不存第二份密码
    auto session = portal_.Login(credentials);
    if (!session) {
        return Result<std::string>::Fail(session.error().code, session.error().message);
    }

    // 把教务系统的会话缓存起来，后续同步就不用再让用户提交密码
    sessions_.Put(SessionKeyFor(portal_.Name(), credentials.student_id), session.value(),
                  now + token_ttl_seconds_);

    JwtClaims claims;
    claims.subject = credentials.student_id;
    claims.issued_at = now;
    claims.expires_at = now + token_ttl_seconds_;
    return SignJwt(claims, secret_);
}

Result<std::string> AuthService::PortalTokenFor(const std::string& student_id, std::int64_t now) {
    return sessions_.Get(SessionKeyFor(portal_.Name(), student_id), now);
}

Result<AuthToken> AuthService::Verify(const std::string& token, std::int64_t now) const {
    auto claims = VerifyJwt(token, secret_, now);
    if (!claims) {
        return Result<AuthToken>::Fail(claims.error().code, claims.error().message);
    }
    AuthToken result;
    result.student_id = claims.value().subject;
    result.expires_at = claims.value().expires_at;
    return Result<AuthToken>::Ok(std::move(result));
}

}  // namespace campus
