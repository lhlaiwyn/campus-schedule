#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "campus/domain/result.h"
#include "campus/portal/portal_adapter.h"
#include "campus/repository/course_repository.h"
#include "campus/service/change_detector.h"

namespace campus {

// 一次同步的执行结果，接口层直接把它转成 JSON 返回给客户端
struct SyncSummary {
    std::string semester;
    std::size_t fetched = 0;    // 教务系统返回的原始条目数
    std::size_t converted = 0;  // 转换后的课程数
    std::size_t inserted = 0;   // 新写入的课程数
    std::size_t updated = 0;    // 覆盖更新的课程数
    std::vector<ScheduleChange> changes;  // 与同步前的课表差异
};

// 课表同步流程：登录 -> 拉取 -> 转换 -> 与本地课表比对 -> 写库。
//
// 只依赖 PortalAdapter 接口和 CourseRepository，
// 因此不关心具体是哪一所学校的教务系统，也不需要改代码就能支持新学校。
class SyncService {
public:
    SyncService(PortalAdapter& portal, CourseRepository& repository)
        : portal_(portal), repository_(repository) {}

    // 便利方法：自己完成登录与注销（单测和一些脚本用得上）
    Result<SyncSummary> Sync(const PortalCredentials& credentials, const std::string& semester);

    // 用已经建立好的教务系统会话同步。接口层走这条路径，
    // 这样用户登录之后就不需要为每次同步重复提交密码。
    Result<SyncSummary> SyncWithToken(const std::string& portal_token,
                                      const std::string& semester);

private:
    PortalAdapter& portal_;
    CourseRepository& repository_;
};

}  // namespace campus
