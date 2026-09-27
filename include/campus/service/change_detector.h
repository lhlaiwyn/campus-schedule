#pragma once

#include <string>
#include <vector>

#include "campus/domain/course.h"

namespace campus {

enum class ChangeType {
    kNewCourse = 0,      // 新增课程
    kRemovedCourse = 1,  // 课程从课表中消失
    kSuspended = 2,      // 停课：课程还在，但本学期没有排课
    kTimeChanged = 3,    // 上课时间变了
    kRoomChanged = 4,    // 换教室
    kTeacherChanged = 5, // 换老师
    kWeeksChanged = 6,   // 周次范围变了
};

const char* ToString(ChangeType type);

struct ScheduleChange {
    ChangeType type = ChangeType::kNewCourse;
    std::string course_name;
    std::string detail;  // 人能看懂的说明，例如 "教室 教学楼A301 -> 教学楼B102"
};

// 对比同步前后的两份课表，产出变动列表。
//
// 匹配规则：先用课程号（没有课程号时用课程名 + 教师）判断是不是同一门课，
// 再用「星期 + 起止节次」作为时间槽匹配具体课次。
// 返回值按（变动类型, 课程名, 详情）排序，保证结果稳定、便于测试。
std::vector<ScheduleChange> DetectChanges(const std::vector<Course>& before,
                                          const std::vector<Course>& after);

// 把变动列表压成一行行通知文案，给日志或推送用
std::vector<std::string> DescribeChanges(const std::vector<ScheduleChange>& changes);

}  // namespace campus

