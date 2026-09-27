#pragma once

#include <string>
#include <vector>

namespace campus {

// 登录凭据。真实产品里不应该长期保存明文密码，
// 这里只在一次同步任务的生命周期内使用。
struct PortalCredentials {
    std::string student_id;
    std::string password;
};

// 教务系统返回的一条原始课表记录。
// 字段命名贴近教务系统的中文表头，保证「适配器只做翻译，不做业务」。
struct RawScheduleEntry {
    std::string course_name;   // 课程名称
    std::string course_code;   // 课程号
    std::string teacher;       // 任课教师
    std::string location;      // 上课地点
    int day_of_week = 1;       // 星期几，1 = 周一
    int start_period = 1;      // 开始节次
    int end_period = 1;        // 结束节次
    std::string weeks_text;    // 原始周次文本，例如 "1-16周"、"1-16周(单)"
    double credits = 0.0;      // 学分
};

}  // namespace campus

