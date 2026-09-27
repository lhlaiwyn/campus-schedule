#include "campus/portal/mock_portal_adapter.h"

#include <algorithm>

#include "campus/portal/mock_portal_account.h"

namespace campus {

Result<std::string> MockPortalAdapter::Login(const PortalCredentials& credentials) {
    if (credentials.student_id.empty()) {
        return Result<std::string>::Fail(ErrorCode::kInvalidArgument, "学号不能为空");
    }
    if (credentials.password.size() < mock_portal::kMinPasswordLength) {
        return Result<std::string>::Fail(ErrorCode::kInvalidArgument,
                                         "密码长度不足（模拟教务系统要求至少 6 位）");
    }

    // 与独立进程的模拟教务系统保持一致的校验逻辑：
    // 只有演示账号能登录。否则内存版会「什么密码都放行」，
    // 基于它的测试就测不出真实的鉴权行为。
    if (credentials.student_id != mock_portal::kDemoStudentId ||
        credentials.password != mock_portal::kDemoPassword) {
        return Result<std::string>::Fail(
            ErrorCode::kInvalidArgument,
            "学号或密码错误（演示账号 " + std::string(mock_portal::kDemoStudentId) + " / " +
                mock_portal::kDemoPassword + "）");
    }

    const std::string token = "mock-token-" + credentials.student_id + "-" +
                              std::to_string(credentials.password.size());
    active_tokens_.push_back(token);
    return Result<std::string>::Ok(token);
}

Result<std::vector<RawScheduleEntry>> MockPortalAdapter::FetchSchedule(
    const std::string& token, const std::string& semester) {
    if (std::find(active_tokens_.begin(), active_tokens_.end(), token) == active_tokens_.end()) {
        return Result<std::vector<RawScheduleEntry>>::Fail(ErrorCode::kInvalidArgument,
                                                           "会话已失效，请重新登录");
    }
    if (semester.empty()) {
        return Result<std::vector<RawScheduleEntry>>::Fail(ErrorCode::kInvalidArgument,
                                                           "学期不能为空");
    }

    // 固定数据：三门课、四种周次写法，覆盖周六上课和单双周
    std::vector<RawScheduleEntry> entries;
    entries.push_back(RawScheduleEntry{"操作系统", "CS2001", "张伟", "教学楼A301", 2, 3, 4,
                                       "1-16周", 3.5});
    entries.push_back(RawScheduleEntry{"数据结构", "CS2002", "李娜", "教学楼B102", 4, 1, 2,
                                       "1-16周(单)", 4.0});
    entries.push_back(RawScheduleEntry{"计算机网络", "CS2003", "王强", "实验楼C201", 6, 5, 8,
                                       "3-15周(双)", 3.0});
    return Result<std::vector<RawScheduleEntry>>::Ok(std::move(entries));
}

void MockPortalAdapter::Logout(const std::string& token) {
    active_tokens_.erase(std::remove(active_tokens_.begin(), active_tokens_.end(), token),
                         active_tokens_.end());
}

}  // namespace campus
