#include "campus/service/sync_service.h"

#include <string>

#include "campus/portal/schedule_converter.h"

namespace campus {
namespace {

// 无论同步成功还是中途失败，都要退出登录，避免在教务系统侧留下悬挂会话。
class LogoutGuard {
public:
    LogoutGuard(PortalAdapter& portal, std::string token)
        : portal_(portal), token_(std::move(token)) {}

    ~LogoutGuard() { portal_.Logout(token_); }

    LogoutGuard(const LogoutGuard&) = delete;
    LogoutGuard& operator=(const LogoutGuard&) = delete;

private:
    PortalAdapter& portal_;
    std::string token_;
};

}  // namespace

Result<SyncSummary> SyncService::Sync(const PortalCredentials& credentials,
                                      const std::string& semester) {
    if (semester.empty()) {
        return Result<SyncSummary>::Fail(ErrorCode::kInvalidArgument, "学期不能为空");
    }

    auto token = portal_.Login(credentials);
    if (!token) {
        return Result<SyncSummary>::Fail(token.error().code,
                                         "登录教务系统失败：" + token.error().message);
    }
    LogoutGuard guard(portal_, token.value());

    return SyncWithToken(token.value(), semester);
}

Result<SyncSummary> SyncService::SyncWithToken(const std::string& portal_token,
                                               const std::string& semester) {
    if (portal_token.empty()) {
        return Result<SyncSummary>::Fail(ErrorCode::kUnauthorized,
                                         "教务系统会话令牌为空，请重新登录");
    }
    if (semester.empty()) {
        return Result<SyncSummary>::Fail(ErrorCode::kInvalidArgument, "学期不能为空");
    }

    auto raw = portal_.FetchSchedule(portal_token, semester);
    if (!raw) {
        return Result<SyncSummary>::Fail(raw.error().code,
                                         "拉取课表失败：" + raw.error().message);
    }

    auto converted = ConvertRawSchedule(raw.value(), semester);
    if (!converted) {
        return Result<SyncSummary>::Fail(converted.error().code, converted.error().message);
    }

    // 先读出本地已有的课表，再和刚拉到的新课表比对，得到变动列表
    auto before = repository_.ListBySemester(semester);
    if (!before) {
        return Result<SyncSummary>::Fail(before.error().code, before.error().message);
    }

    // 只把「从教务系统同步来的」课程纳入比对：
    // 用户手动添加的课不属于教务系统，否则每次同步都会被误报成「课程移除」。
    std::vector<Course> managed;
    managed.reserve(before.value().size());
    for (const auto& course : before.value()) {
        if (course.source == kSourcePortal) {
            managed.push_back(course);
        }
    }

    SyncSummary summary;
    summary.semester = semester;
    summary.fetched = raw.value().size();
    summary.converted = converted.value().size();
    summary.changes = DetectChanges(managed, converted.value());

    for (auto& course : converted.value()) {
        auto existing_id = repository_.FindIdByNaturalKey(course);
        if (!existing_id) {
            return Result<SyncSummary>::Fail(existing_id.error().code, existing_id.error().message);
        }

        if (existing_id.value() == 0) {
            auto created = repository_.Insert(course);
            if (!created) {
                return Result<SyncSummary>::Fail(created.error().code, created.error().message);
            }
            ++summary.inserted;
        } else {
            course.id = existing_id.value();
            auto updated = repository_.Update(course);
            if (!updated) {
                return Result<SyncSummary>::Fail(updated.error().code, updated.error().message);
            }
            ++summary.updated;
        }
    }

    return Result<SyncSummary>::Ok(std::move(summary));
}

}  // namespace campus
