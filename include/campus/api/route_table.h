#pragma once

#include <cstdint>
#include <string>

namespace campus {

// 接口清单。一条路由 = 一个「方法 + 路径模式」。
enum class Route {
    kIndex,      // GET /  网页版课表看板
    kFavicon,    // GET /favicon.ico  直接回 204，省掉浏览器控制台的 404 噪音
    kHealth,
    kVersion,
    kLogin,
    kSync,
    kListCourses,
    kCreateCourse,
    kGetCourse,
    kUpdateCourse,
    kDeleteCourse,
    kNotFound,  // 没有匹配到任何接口
};

struct RouteMatch {
    Route route = Route::kNotFound;
    // 只有 kGetCourse / kUpdateCourse / kDeleteCourse 才有意义
    std::int64_t course_id = 0;
};

// 方法 + 路径 -> 路由。
//
// 刻意做成「纯函数 + 零依赖」：两个 HTTP 引擎（cpp-httplib 与自研 epoll 网络库）
// 共用同一份判断，行为不可能分叉；同时这一段可以脱离网络库和数据库单独做单元测试。
//
// 匹配不到（包括路径存在但方法不对）一律返回 kNotFound，和 httplib 的行为一致。
RouteMatch MatchRoute(const std::string& method, const std::string& path);

}  // namespace campus
