#pragma once

#include <cstdint>
#include <string>

#include "campus/domain/result.h"
#include "campus/infra/portal_session_store.h"
#include "campus/portal/portal_adapter.h"

namespace campus {

struct AuthToken {
    std::string student_id;
    std::int64_t expires_at = 0;
};

// 登录：用教务系统凭据换本服务自己的 JWT。
//
// 这样做的好处是后续请求只带 token，不用反复提交密码；
// 而且本服务不保存第二份密码，凭据由教务系统判定。
class AuthService {
public:
    AuthService(PortalAdapter& portal, PortalSessionStore& sessions, std::string secret,
                std::int64_t token_ttl_seconds);

    // 登录：让教务系统校验凭据 -> 签发本服务的 JWT -> 缓存教务系统会话。
    // 密码只在这一次请求里出现，既不落库也不写进 JWT。
    Result<std::string> Login(const PortalCredentials& credentials, std::int64_t now);

    Result<AuthToken> Verify(const std::string& token, std::int64_t now) const;

    // 用学号取出教务系统会话，供同步课表使用
    Result<std::string> PortalTokenFor(const std::string& student_id, std::int64_t now);

    std::int64_t TokenTtlSeconds() const { return token_ttl_seconds_; }

private:
    PortalAdapter& portal_;
    PortalSessionStore& sessions_;
    std::string secret_;
    std::int64_t token_ttl_seconds_;
};

}  // namespace campus
