#include "campus/domain/week_parser.h"

#include <cctype>

namespace campus {
namespace {

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };

    while (begin < end && is_space(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && is_space(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool ParseInt(const std::string& text, int& out) {
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    try {
        out = std::stoi(text);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

Result<WeekRange> ParseWeekText(const std::string& text) {
    const std::string trimmed = Trim(text);
    if (trimmed.empty()) {
        return Result<WeekRange>::Fail(ErrorCode::kInvalidArgument, "周次文本为空");
    }

    // 多区间写法需要上层拆分成多条课次，这里显式拒绝而不是猜一个结果
    // 注意：中文标点是多字节字符，必须用字符串字面量查找，
    // 写成 find('，') 会被当成单字节字符而错误匹配。
    if (trimmed.find(',') != std::string::npos || trimmed.find("，") != std::string::npos ||
        trimmed.find("、") != std::string::npos) {
        return Result<WeekRange>::Fail(
            ErrorCode::kInvalidArgument,
            "暂不支持多区间周次: " + trimmed + "（需要拆成多条课次）");
    }

    WeekRange weeks;

    // 单双周：中文「单」「双」两个关键字
    if (trimmed.find("单") != std::string::npos) {
        weeks.parity = WeekParity::kOdd;
    } else if (trimmed.find("双") != std::string::npos) {
        weeks.parity = WeekParity::kEven;
    }

    // 只保留数字和短横线，其余字符（周、括号、空格等）全部丢掉
    std::string digits;
    digits.reserve(trimmed.size());
    for (char c : trimmed) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '-') {
            digits.push_back(c);
        }
    }
    if (digits.empty()) {
        return Result<WeekRange>::Fail(ErrorCode::kInvalidArgument,
                                       "周次文本里找不到数字: " + trimmed);
    }

    const std::size_t dash = digits.find('-');
    if (dash == std::string::npos) {
        int single = 0;
        if (!ParseInt(digits, single)) {
            return Result<WeekRange>::Fail(ErrorCode::kInvalidArgument,
                                           "无法解析周次数字: " + trimmed);
        }
        weeks.from = single;
        weeks.to = single;
    } else {
        const std::string left = digits.substr(0, dash);
        const std::string right = digits.substr(dash + 1);
        if (right.find('-') != std::string::npos) {
            return Result<WeekRange>::Fail(ErrorCode::kInvalidArgument,
                                           "周次区间格式非法: " + trimmed);
        }
        if (!ParseInt(left, weeks.from) || !ParseInt(right, weeks.to)) {
            return Result<WeekRange>::Fail(ErrorCode::kInvalidArgument,
                                           "无法解析周次区间: " + trimmed);
        }
    }

    if (!weeks.Normalize()) {
        return Result<WeekRange>::Fail(
            ErrorCode::kInvalidArgument,
            "周次超出 1-30 的合法范围: " + trimmed);
    }
    return Result<WeekRange>::Ok(weeks);
}

}  // namespace campus
