#include "campus/portal/schedule_converter.h"

#include <algorithm>

#include "campus/domain/week_parser.h"

namespace campus {
namespace {

// 同一门课的判定：优先课程号，没有课程号时退回「名称 + 教师」
bool SameCourseInRaw(const Course& course, const RawScheduleEntry& entry) {
    if (!entry.course_code.empty() && !course.code.empty()) {
        return course.code == entry.course_code;
    }
    return course.name == entry.course_name && course.teacher == entry.teacher;
}

}  // namespace

Result<std::vector<Course>> ConvertRawSchedule(const std::vector<RawScheduleEntry>& raw,
                                               const std::string& semester) {
    if (semester.empty()) {
        return Result<std::vector<Course>>::Fail(ErrorCode::kInvalidArgument, "学期不能为空");
    }

    std::vector<Course> courses;
    courses.reserve(raw.size());

    for (const auto& entry : raw) {
        if (entry.course_name.empty()) {
            return Result<std::vector<Course>>::Fail(ErrorCode::kInvalidArgument,
                                                     "教务系统返回了没有课程名的记录");
        }

        auto weeks = ParseWeekText(entry.weeks_text);
        if (!weeks) {
            return Result<std::vector<Course>>::Fail(
                ErrorCode::kInvalidArgument,
                "课程「" + entry.course_name + "」的周次无法解析：" + weeks.error().message);
        }

        CourseSession session;
        session.day_of_week = entry.day_of_week;
        session.start_period = entry.start_period;
        session.end_period = entry.end_period;
        session.weeks = weeks.value();
        session.location = entry.location;
        session.teacher = entry.teacher;

        if (!session.Valid()) {
            return Result<std::vector<Course>>::Fail(
                ErrorCode::kInvalidArgument,
                "课程「" + entry.course_name + "」的课次参数非法（星期/节次/周次超出范围）");
        }

        Course* target = nullptr;
        for (auto& course : courses) {
            if (SameCourseInRaw(course, entry)) {
                target = &course;
                break;
            }
        }

        if (target == nullptr) {
            Course course;
            course.name = entry.course_name;
            course.code = entry.course_code;
            course.teacher = entry.teacher;
            course.credits = entry.credits;
            course.semester = semester;
            // 从教务系统来的课统一标记为 portal，同步时才会被管理
            course.source = kSourcePortal;
            course.sessions.push_back(session);
            courses.push_back(std::move(course));
        } else {
            if (target->credits == 0.0) {
                target->credits = entry.credits;
            }
            target->sessions.push_back(session);
        }
    }

    // 课次排序，保证同样的输入永远产出同样的顺序，方便比对和测试
    for (auto& course : courses) {
        std::sort(course.sessions.begin(), course.sessions.end(),
                  [](const CourseSession& lhs, const CourseSession& rhs) {
                      if (lhs.day_of_week != rhs.day_of_week) {
                          return lhs.day_of_week < rhs.day_of_week;
                      }
                      if (lhs.start_period != rhs.start_period) {
                          return lhs.start_period < rhs.start_period;
                      }
                      if (lhs.weeks.from != rhs.weeks.from) {
                          return lhs.weeks.from < rhs.weeks.from;
                      }
                      return lhs.weeks.to < rhs.weeks.to;
                  });
    }

    return Result<std::vector<Course>>::Ok(std::move(courses));
}

}  // namespace campus
