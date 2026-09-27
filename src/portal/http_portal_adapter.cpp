#include "campus/portal/http_portal_adapter.h"

#include <cctype>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "campus/infra/crypto.h"
#include "campus/portal/portal_json.h"

namespace campus {
namespace {

using json = nlohmann::json;

constexpr const char* kJsonContentType = "application/json";

Result<std::string> FailToken(ErrorCode code, const std::string& message) {
    return Result<std::string>::Fail(code, message);
}

Result<std::vector<RawScheduleEntry>> FailSchedule(ErrorCode code, const std::string& message) {
    return Result<std::vector<RawScheduleEntry>>::Fail(code, message);
}

// 极简 URL 编码：token 里可能带非字母数字字符，直接拼进查询串不安全
std::string UrlEncode(const std::string& value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

}  // namespace

HttpPortalAdapter::HttpPortalAdapter(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

std::string HttpPortalAdapter::Endpoint() const {
    return host_ + ":" + std::to_string(port_);
}

Result<std::string> HttpPortalAdapter::Login(const PortalCredentials& credentials) {
    // 环境变量优先（默认成员值保持不变）
    if (const char* host = std::getenv("CAMPUS_PORTAL_HOST")) {
        if (*host != '\0') {
            host_ = host;
        }
    }
    if (const char* port = std::getenv("CAMPUS_PORTAL_PORT")) {
        try {
            port_ = std::stoi(port);
        } catch (...) {
        }
    }

    if (credentials.student_id.empty()) {
        return FailToken(ErrorCode::kInvalidArgument, "学号不能为空");
    }
    if (credentials.password.empty()) {
        return FailToken(ErrorCode::kInvalidArgument, "密码不能为空");
    }

    httplib::Client client(host_, port_);
    client.set_connection_timeout(timeout_seconds_, 0);
    client.set_read_timeout(timeout_seconds_, 0);

    auto captcha_response = client.Get("/jwgl/captcha");
    if (!captcha_response) {
        return FailToken(ErrorCode::kInternalError,
                         "无法连接教务系统 " + Endpoint() + "，请先启动模拟教务系统服务");
    }
    if (captcha_response->status != 200) {
        return FailToken(ErrorCode::kInternalError,
                         "获取验证码失败: HTTP " + std::to_string(captcha_response->status) + " " +
                             ExtractErrorMessage(captcha_response->body, ""));
    }

    auto challenge = ParseCaptchaJson(captcha_response->body);
    if (!challenge) {
        return FailToken(challenge.error().code, challenge.error().message);
    }

    // 明文密码不出客户端：本地算出 sha256(salt + password) 再提交
    const json payload = {
        {"studentId", credentials.student_id},
        {"passwordHash", HashPassword(challenge.value().salt, credentials.password)},
        {"captchaText", challenge.value().captcha_text},
    };

    auto login_response = client.Post("/jwgl/login", payload.dump(), kJsonContentType);
    if (!login_response) {
        return FailToken(ErrorCode::kInternalError, "登录请求发送失败");
    }
    if (login_response->status != 200) {
        return FailToken(ErrorCode::kInvalidArgument,
                         ExtractErrorMessage(login_response->body, "教务系统拒绝了登录请求"));
    }

    auto token = ParseTokenJson(login_response->body);
    if (!token) {
        return FailToken(token.error().code, token.error().message);
    }
    spdlog::debug("教务系统登录成功: {}", Endpoint());
    return token;
}

Result<std::vector<RawScheduleEntry>> HttpPortalAdapter::FetchSchedule(
    const std::string& token, const std::string& semester) {
    if (token.empty()) {
        return FailSchedule(ErrorCode::kInvalidArgument, "会话令牌为空，请先登录");
    }
    if (semester.empty()) {
        return FailSchedule(ErrorCode::kInvalidArgument, "学期不能为空");
    }

    httplib::Client client(host_, port_);
    client.set_connection_timeout(timeout_seconds_, 0);
    client.set_read_timeout(timeout_seconds_, 0);

    const std::string path = "/jwgl/schedule?token=" + UrlEncode(token) +
                             "&semester=" + UrlEncode(semester);
    auto response = client.Get(path);
    if (!response) {
        return FailSchedule(ErrorCode::kInternalError, "拉取课表请求发送失败");
    }
    if (response->status != 200) {
        return FailSchedule(ErrorCode::kInvalidArgument,
                            ExtractErrorMessage(response->body, "拉取课表失败"));
    }
    return ParseScheduleJson(response->body);
}

void HttpPortalAdapter::Logout(const std::string& token) {
    if (token.empty()) {
        return;
    }
    httplib::Client client(host_, port_);
    client.set_connection_timeout(timeout_seconds_, 0);
    client.set_read_timeout(timeout_seconds_, 0);

    const json payload = {{"token", token}};
    auto response = client.Post("/jwgl/logout", payload.dump(), kJsonContentType);
    if (!response) {
        spdlog::warn("退出登录失败，不影响主流程: 无法连接 {}", Endpoint());
        return;
    }
    spdlog::debug("已通知教务系统注销会话");
}

}  // namespace campus
