#pragma once

#include <exception>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "campus/domain/course.h"
#include "campus/domain/result.h"

namespace campus {

// 课程相关的 JSON 编解码。
//
// 放在 domain 层而不是 api 层，是因为它有两个使用方：
// 接口层用它收发请求，缓存层用它把课程列表存进 Redis。
// 两边共用同一份实现，序列化格式就不会悄悄分叉。

// ---------- 领域对象 -> JSON ----------

inline nlohmann::json ToJson(const WeekRange& weeks) {
    return nlohmann::json{
        {"from", weeks.from},
        {"to", weeks.to},
        {"parity", static_cast<int>(weeks.parity)},
        {"text", weeks.ToString()},
    };
}

inline nlohmann::json ToJson(const CourseSession& session) {
    return nlohmann::json{
        {"id", session.id},
        {"dayOfWeek", session.day_of_week},
        {"startPeriod", session.start_period},
        {"endPeriod", session.end_period},
        {"periodCount", session.PeriodCount()},
        {"weeks", ToJson(session.weeks)},
        {"location", session.location},
        {"teacher", session.teacher},
    };
}

inline nlohmann::json ToJson(const Course& course) {
    nlohmann::json sessions = nlohmann::json::array();
    for (const auto& session : course.sessions) {
        sessions.push_back(ToJson(session));
    }
    return nlohmann::json{
        {"id", course.id},
        {"name", course.name},
        {"code", course.code},
        {"teacher", course.teacher},
        {"credits", course.credits},
        {"semester", course.semester},
        {"source", course.source},
        {"weeklyPeriods", course.WeeklyPeriods()},
        {"sessions", sessions},
    };
}

// ---------- JSON -> 领域对象 ----------

inline bool FromJson(const nlohmann::json& j, WeekRange& out, std::string& error) {
    try {
        if (!j.is_object()) {
            error = "weeks 必须是对象";
            return false;
        }
        out.from = j.value("from", 1);
        out.to = j.value("to", 16);
        const int parity = j.value("parity", 0);
        if (parity < 0 || parity > 2) {
            error = "weeks.parity 只能是 0(每周)/1(单周)/2(双周)";
            return false;
        }
        out.parity = static_cast<WeekParity>(parity);
        return true;
    } catch (const std::exception& e) {
        error = std::string("weeks 字段类型错误: ") + e.what();
        return false;
    }
}

inline bool FromJson(const nlohmann::json& j, CourseSession& out, std::string& error) {
    try {
        if (!j.is_object()) {
            error = "sessions 的每一项必须是对象";
            return false;
        }
        out.id = j.value("id", 0LL);
        out.day_of_week = j.value("dayOfWeek", 1);
        out.start_period = j.value("startPeriod", 1);
        out.end_period = j.value("endPeriod", out.start_period);
        out.location = j.value("location", std::string{});
        out.teacher = j.value("teacher", std::string{});
        if (j.contains("weeks") && !FromJson(j.at("weeks"), out.weeks, error)) {
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = std::string("sessions 字段类型错误: ") + e.what();
        return false;
    }
}

inline bool FromJson(const nlohmann::json& j, Course& out, std::string& error) {
    try {
        if (!j.is_object()) {
            error = "请求体必须是 JSON 对象";
            return false;
        }
        out.id = j.value("id", 0LL);
        out.name = j.value("name", std::string{});
        out.code = j.value("code", std::string{});
        out.teacher = j.value("teacher", std::string{});
        out.credits = j.value("credits", 0.0);
        out.semester = j.value("semester", std::string{});
        // 通过接口新增的课默认算「手动添加」，不会被同步触碰
        out.source = j.value("source", std::string(kSourceManual));

        out.sessions.clear();
        if (j.contains("sessions")) {
            const auto& sessions = j.at("sessions");
            if (!sessions.is_array()) {
                error = "sessions 必须是数组";
                return false;
            }
            for (const auto& item : sessions) {
                CourseSession session;
                if (!FromJson(item, session, error)) {
                    return false;
                }
                out.sessions.push_back(std::move(session));
            }
        }
        return true;
    } catch (const std::exception& e) {
        error = std::string("请求体字段类型错误: ") + e.what();
        return false;
    }
}

}  // namespace campus
