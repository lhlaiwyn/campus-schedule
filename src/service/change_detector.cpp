#include "campus/service/change_detector.h"

#include <algorithm>
#include <map>

namespace campus {
namespace {

// 时间槽标识：星期 + 起止节次
std::string SlotKey(const CourseSession& session) {
    return std::to_string(session.day_of_week) + ":" + std::to_string(session.start_period) + ":" +
           std::to_string(session.end_period);
}

const Course* FindSameCourse(const std::vector<Course>& list, const Course& target) {
    for (const auto& course : list) {
        if (IsSameCourse(course, target)) {
            return &course;
        }
    }
    return nullptr;
}

}  // namespace

const char* ToString(ChangeType type) {
    switch (type) {
        case ChangeType::kNewCourse:
            return "新增课程";
        case ChangeType::kRemovedCourse:
            return "课程移除";
        case ChangeType::kSuspended:
            return "停课";
        case ChangeType::kTimeChanged:
            return "时间变动";
        case ChangeType::kRoomChanged:
            return "换教室";
        case ChangeType::kTeacherChanged:
            return "换老师";
        case ChangeType::kWeeksChanged:
            return "周次变动";
    }
    return "未知变动";
}

std::vector<ScheduleChange> DetectChanges(const std::vector<Course>& before,
                                          const std::vector<Course>& after) {
    std::vector<ScheduleChange> changes;

    for (const auto& old_course : before) {
        const Course* new_course = FindSameCourse(after, old_course);
        if (new_course == nullptr) {
            changes.push_back({ChangeType::kRemovedCourse, old_course.name, "课表中已不存在这门课"});
            continue;
        }

        if (new_course->sessions.empty()) {
            changes.push_back({ChangeType::kSuspended, old_course.name, "本学期暂无排课"});
            continue;
        }

        if (!old_course.teacher.empty() && !new_course->teacher.empty() &&
            old_course.teacher != new_course->teacher) {
            changes.push_back({ChangeType::kTeacherChanged, old_course.name,
                               "教师 " + old_course.teacher + " -> " + new_course->teacher});
        }

        // 新数据的课次按时间槽建索引，方便按槽比对
        std::map<std::string, const CourseSession*> new_slots;
        for (const auto& session : new_course->sessions) {
            new_slots[SlotKey(session)] = &session;
        }

        for (const auto& old_session : old_course.sessions) {
            const auto it = new_slots.find(SlotKey(old_session));
            if (it == new_slots.end()) {
                changes.push_back({ChangeType::kTimeChanged, old_course.name,
                                   old_session.ToString() + " 这一节已取消或改时间"});
                continue;
            }

            const CourseSession& new_session = *it->second;
            if (!old_session.location.empty() && old_session.location != new_session.location) {
                changes.push_back({ChangeType::kRoomChanged, old_course.name,
                                   "教室 " + old_session.location + " -> " +
                                       new_session.location});
            }
            if (old_session.weeks.ToString() != new_session.weeks.ToString()) {
                changes.push_back({ChangeType::kWeeksChanged, old_course.name,
                                   "周次 " + old_session.weeks.ToString() + " -> " +
                                       new_session.weeks.ToString()});
            }
        }
    }

    for (const auto& new_course : after) {
        if (FindSameCourse(before, new_course) == nullptr) {
            changes.push_back({ChangeType::kNewCourse, new_course.name, "新增课程"});
        }
    }

    // 结果排序，保证同样的输入永远得到同样的输出
    std::sort(changes.begin(), changes.end(),
              [](const ScheduleChange& lhs, const ScheduleChange& rhs) {
                  if (lhs.type != rhs.type) {
                      return static_cast<int>(lhs.type) < static_cast<int>(rhs.type);
                  }
                  if (lhs.course_name != rhs.course_name) {
                      return lhs.course_name < rhs.course_name;
                  }
                  return lhs.detail < rhs.detail;
              });
    return changes;
}

std::vector<std::string> DescribeChanges(const std::vector<ScheduleChange>& changes) {
    std::vector<std::string> lines;
    lines.reserve(changes.size());
    for (const auto& change : changes) {
        lines.push_back(std::string("[") + ToString(change.type) + "] " + change.course_name +
                        "：" + change.detail);
    }
    return lines;
}

}  // namespace campus
