#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace campus {

// 课程来源：从教务系统同步来的，还是用户手动添加的。
//
// 这个区分是必须的：同步时只应该管理 portal 来源的课程。
// 否则用户手动加的课（讲座、体育私教等）每次同步都会被误报成「课程移除」。
inline constexpr const char* kSourcePortal = "portal";
inline constexpr const char* kSourceManual = "manual";

// 单双周
enum class WeekParity : std::uint8_t {
    kEvery = 0,  // 每周都上
    kOdd = 1,    // 单周
    kEven = 2,   // 双周
};

// 周次范围，例如 "1-16 周单周"
struct WeekRange {
    int from = 1;
    int to = 16;
    WeekParity parity = WeekParity::kEvery;

    // 第 week 周是否上这门课
    bool Contains(int week) const;

    // 校验并修正（from > to 时交换），非法返回 false
    bool Normalize();

    std::string ToString() const;
};

// 一次课：星期几、第几节到第几节、在哪个教室
struct CourseSession {
    std::int64_t id = 0;
    int day_of_week = 1;    // 1 = 周一 ... 7 = 周日
    int start_period = 1;   // 开始节次
    int end_period = 1;     // 结束节次
    WeekRange weeks;
    std::string location;
    std::string teacher;

    // 连堂几节
    int PeriodCount() const { return end_period - start_period + 1; }

    bool Valid() const;
    std::string ToString() const;
};

// 一门课程
struct Course {
    std::int64_t id = 0;
    std::string name;
    std::string code;       // 课程号
    std::string teacher;
    double credits = 0.0;
    std::string semester;   // 学期，例如 2026-2027-1
    // 课程来源，取值见 kSourcePortal / kSourceManual
    std::string source = kSourceManual;
    std::vector<CourseSession> sessions;

    // 每周总课时（各次课节数之和）
    int WeeklyPeriods() const;

    bool Valid() const;
    std::string ToString() const;
};

// 两门课是否代表同一门课（用于课表变动检测）
bool IsSameCourse(const Course& lhs, const Course& rhs);

// 两次课是否时间冲突：同一天、节次区间相交、周次区间相交。
// 这是纯粹的领域规则，不依赖数据库，因此放在 domain 层。
bool HasTimeConflict(const CourseSession& lhs, const CourseSession& rhs);

}  // namespace campus
