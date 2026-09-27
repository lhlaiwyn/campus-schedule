#include "campus/portal/portal_json.h"

#include <cstddef>
#include <exception>
#include <utility>

#include <nlohmann/json.hpp>

namespace campus {
namespace {

using json = nlohmann::json;

// 解析失败时统一用这个错误码：教务系统返回了不符合约定的数据，
// 对上层来说属于「拿到的东西不能用」，不是数据库故障。
Result<std::vector<RawScheduleEntry>> FailParse(const std::string& message) {
    return Result<std::vector<RawScheduleEntry>>::Fail(ErrorCode::kInvalidArgument, message);
}

bool ReadScheduleEntry(const json& item, RawScheduleEntry& out, std::string& error,
                       std::size_t index) {
    const std::string prefix = "第 " + std::to_string(index + 1) + " 条课表记录";

    if (!item.is_object()) {
        error = prefix + "不是 JSON 对象";
        return false;
    }

    try {
        out.course_name = item.value("courseName", std::string{});
        out.course_code = item.value("courseCode", std::string{});
        out.teacher = item.value("teacher", std::string{});
        out.location = item.value("location", std::string{});
        out.day_of_week = item.value("dayOfWeek", 0);
        out.start_period = item.value("startPeriod", 0);
        out.end_period = item.value("endPeriod", 0);
        out.weeks_text = item.value("weeksText", std::string{});
        out.credits = item.value("credits", 0.0);
    } catch (const std::exception& e) {
        error = prefix + "字段类型错误: " + e.what();
        return false;
    }

    if (out.course_name.empty()) {
        error = prefix + "缺少课程名称";
        return false;
    }
    if (out.weeks_text.empty()) {
        error = prefix + "（" + out.course_name + "）缺少周次信息";
        return false;
    }
    if (out.day_of_week < 1 || out.day_of_week > 7) {
        error = prefix + "（" + out.course_name + "）星期取值非法: " + std::to_string(out.day_of_week);
        return false;
    }
    if (out.start_period < 1 || out.end_period < out.start_period) {
        error = prefix + "（" + out.course_name + "）节次非法: " +
                std::to_string(out.start_period) + "-" + std::to_string(out.end_period);
        return false;
    }
    return true;
}

}  // namespace

Result<CaptchaChallenge> ParseCaptchaJson(const std::string& body) {
    try {
        const json root = json::parse(body);
        if (!root.is_object()) {
            return Result<CaptchaChallenge>::Fail(ErrorCode::kInvalidArgument,
                                                 "验证码响应必须是 JSON 对象");
        }
        CaptchaChallenge challenge;
        challenge.salt = root.value("salt", std::string{});
        challenge.captcha_text = root.value("captchaText", std::string{});
        challenge.captcha_id = root.value("captchaId", std::string{});

        if (challenge.salt.empty()) {
            return Result<CaptchaChallenge>::Fail(ErrorCode::kInvalidArgument,
                                                 "验证码响应缺少 salt");
        }
        if (challenge.captcha_text.empty()) {
            return Result<CaptchaChallenge>::Fail(ErrorCode::kInvalidArgument,
                                                 "验证码响应缺少 captchaText");
        }
        return Result<CaptchaChallenge>::Ok(std::move(challenge));
    } catch (const std::exception& e) {
        return Result<CaptchaChallenge>::Fail(
            ErrorCode::kInvalidArgument, std::string("验证码响应不是合法 JSON: ") + e.what());
    }
}

Result<std::string> ParseTokenJson(const std::string& body) {
    try {
        const json root = json::parse(body);
        if (!root.is_object()) {
            return Result<std::string>::Fail(ErrorCode::kInvalidArgument,
                                             "登录响应必须是 JSON 对象");
        }
        const std::string token = root.value("token", std::string{});
        if (token.empty()) {
            return Result<std::string>::Fail(ErrorCode::kInvalidArgument, "登录响应里没有 token");
        }
        return Result<std::string>::Ok(token);
    } catch (const std::exception& e) {
        return Result<std::string>::Fail(
            ErrorCode::kInvalidArgument, std::string("登录响应不是合法 JSON: ") + e.what());
    }
}

Result<std::vector<RawScheduleEntry>> ParseScheduleJson(const std::string& body) {
    json root;
    try {
        root = json::parse(body);
    } catch (const std::exception& e) {
        return FailParse(std::string("课表响应不是合法 JSON: ") + e.what());
    }

    if (!root.is_object()) {
        return FailParse("课表响应必须是 JSON 对象");
    }
    if (!root.contains("entries") || !root.at("entries").is_array()) {
        return FailParse("课表响应缺少 entries 数组");
    }

    std::vector<RawScheduleEntry> entries;
    std::size_t index = 0;
    for (const auto& item : root.at("entries")) {
        RawScheduleEntry entry;
        std::string error;
        if (!ReadScheduleEntry(item, entry, error, index)) {
            return FailParse(error);
        }
        entries.push_back(std::move(entry));
        ++index;
    }
    return Result<std::vector<RawScheduleEntry>>::Ok(std::move(entries));
}

std::string ExtractErrorMessage(const std::string& body, const std::string& fallback) {
    try {
        const json root = json::parse(body);
        if (root.is_object()) {
            return root.value("message", fallback);
        }
    } catch (...) {
    }
    return fallback;
}

}  // namespace campus
