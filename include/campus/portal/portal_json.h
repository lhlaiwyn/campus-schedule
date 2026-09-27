#pragma once

#include <string>
#include <vector>

#include "campus/domain/result.h"
#include "campus/portal/portal_types.h"

namespace campus {

// 教务系统下发的验证码挑战：salt 用于密码哈希，captcha_text 是本次验证码
struct CaptchaChallenge {
    std::string salt;
    std::string captcha_text;
    std::string captcha_id;
};

// 把教务系统的 HTTP 响应解析成领域结构。
//
// 这些函数不碰网络，所以解析逻辑可以单独测试：
// 只要构造一段 JSON 字符串就能验，不需要真的起一个教务系统。
Result<CaptchaChallenge> ParseCaptchaJson(const std::string& body);

Result<std::string> ParseTokenJson(const std::string& body);

Result<std::vector<RawScheduleEntry>> ParseScheduleJson(const std::string& body);

// 从错误响应里取出可读原因，取不到就返回 fallback
std::string ExtractErrorMessage(const std::string& body, const std::string& fallback);

}  // namespace campus

