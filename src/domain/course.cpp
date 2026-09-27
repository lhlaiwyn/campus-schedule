#include "campus/domain/course.h"

#include <algorithm>
#include <sstream>

namespace campus {
namespace {

constexpr int kMinWeek = 1;
constexpr int kMaxWeek = 30;
constexpr int kMaxPeriod = 14;

const char* ParityName(WeekParity parity) {
    switch (parity) {
        case WeekParity::kEvery:
            return "每周";
        case WeekParity::kOdd:
            return "单周";
        case WeekParity::kEven:
            return "双周";
    }
    return "每周";
}

}  // namespace

bool WeekRange::Contains(int week) const {
    if (week < from || week > to) {
        return false;
    }
    switch (parity) {
        case WeekParity::kEvery:
            return true;
        case WeekParity::kOdd:
            return week % 2 == 1;
        case WeekParity::kEven:
            return week % 2 == 0;
    }
    return false;
}

bool WeekRange::Normalize() {
    if (from > to) {
        std::swap(from, to);
    }
    if (from < kMinWeek || to > kMaxWeek) {
        return false;
    }
    return true;
}

std::string WeekRange::ToString() const {
    std::ostringstream oss;
    oss << from << '-' << to << "周" << ParityName(parity);
    return oss.str();
}

bool CourseSession::Valid() const {
    if (day_of_week < 1 || day_of_week > 7) {
        return false;
    }
    if (start_period < 1 || end_period > kMaxPeriod || start_period > end_period) {
        return false;
    }
    if (weeks.from < kMinWeek || weeks.to > kMaxWeek || weeks.from > weeks.to) {
        return false;
    }
    return true;
}

std::string CourseSession::ToString() const {
    static const char* kWeekdayNames[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    std::ostringstream oss;
    oss << kWeekdayNames[day_of_week - 1] << ' ' << weeks.ToString() << " 第"
        << start_period << '-' << end_period << "节";
    if (!location.empty()) {
        oss << " @" << location;
    }
    return oss.str();
}

int Course::WeeklyPeriods() const {
    int total = 0;
    for (const auto& session : sessions) {
        total += session.PeriodCount();
    }
    return total;
}

bool Course::Valid() const {
    if (name.empty() || semester.empty()) {
        return false;
    }
    for (const auto& session : sessions) {
        if (!session.Valid()) {
            return false;
        }
    }
    return true;
}

std::string Course::ToString() const {
    std::ostringstream oss;
    oss << name;
    if (!teacher.empty()) {
        oss << " (" << teacher << ')';
    }
    oss << " · " << semester << " · 每周 " << WeeklyPeriods() << " 节";
    return oss.str();
}

bool IsSameCourse(const Course& lhs, const Course& rhs) {
    // 有课程号就按课程号比，没有就按「名称 + 教师」比
    if (!lhs.code.empty() && !rhs.code.empty()) {
        return lhs.code == rhs.code && lhs.semester == rhs.semester;
    }
    return lhs.name == rhs.name && lhs.teacher == rhs.teacher && lhs.semester == rhs.semester;
}

bool HasTimeConflict(const CourseSession& lhs, const CourseSession& rhs) {
    if (lhs.day_of_week != rhs.day_of_week) {
        return false;
    }
    const bool period_overlap =
        lhs.start_period <= rhs.end_period && rhs.start_period <= lhs.end_period;
    if (!period_overlap) {
        return false;
    }
    const bool week_overlap = lhs.weeks.from <= rhs.weeks.to && rhs.weeks.from <= lhs.weeks.to;
    if (!week_overlap) {
        return false;
    }
    // 只要有一边是每周，就一定有交集
    if (lhs.weeks.parity == WeekParity::kEvery || rhs.weeks.parity == WeekParity::kEvery) {
        return true;
    }
    // 同为单周或同为双周才有交集
    return lhs.weeks.parity == rhs.weeks.parity;
}

}  // namespace campus
