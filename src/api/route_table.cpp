#include "campus/api/route_table.h"

#include <cctype>

namespace campus {
namespace {

constexpr const char* kCourseIdPrefix = "/api/courses/";

// 匹配 /api/courses/<数字>，并把数字取出来。
// 不用正则：手写更可控，而且两个引擎必须给出完全一样的判断。
bool MatchCourseId(const std::string& path, std::int64_t& id) {
    const std::string prefix(kCourseIdPrefix);
    if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string tail = path.substr(prefix.size());
    for (char ch : tail) {
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
            return false;
        }
    }
    try {
        id = std::stoll(tail);
    } catch (...) {
        // 数字长到溢出（比如 20 位以上）：当成「路径不存在」，
        // 而不是让它抛出去变成 500。
        return false;
    }
    return true;
}

}  // namespace

RouteMatch MatchRoute(const std::string& method, const std::string& path) {
    RouteMatch match;

    if (path == "/api/health") {
        if (method == "GET") {
            match.route = Route::kHealth;
        }
        return match;
    }
    if (path == "/api/version") {
        if (method == "GET") {
            match.route = Route::kVersion;
        }
        return match;
    }
    if (path == "/api/auth/login") {
        if (method == "POST") {
            match.route = Route::kLogin;
        }
        return match;
    }
    if (path == "/api/sync") {
        if (method == "POST") {
            match.route = Route::kSync;
        }
        return match;
    }
    if (path == "/api/courses") {
        if (method == "GET") {
            match.route = Route::kListCourses;
        } else if (method == "POST") {
            match.route = Route::kCreateCourse;
        }
        return match;
    }

    std::int64_t id = 0;
    if (MatchCourseId(path, id)) {
        match.course_id = id;
        if (method == "GET") {
            match.route = Route::kGetCourse;
        } else if (method == "PUT") {
            match.route = Route::kUpdateCourse;
        } else if (method == "DELETE") {
            match.route = Route::kDeleteCourse;
        }
    }
    return match;
}

}  // namespace campus
