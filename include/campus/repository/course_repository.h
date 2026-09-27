#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "campus/domain/course.h"
#include "campus/domain/result.h"

namespace campus {

// 课程数据访问接口。
//
// 定义成抽象接口而不是直接绑定具体实现，有两个实际好处：
//   1. service 层只依赖抽象，换存储（MySQL -> 别的）不用动业务代码；
//   2. 单元测试可以用内存实现替换数据库，测试不再依赖外部服务，
//      同步、变动检测这类编排逻辑才可能被完整测到。
class CourseRepository {
public:
    virtual ~CourseRepository() = default;

    CourseRepository(const CourseRepository&) = delete;
    CourseRepository& operator=(const CourseRepository&) = delete;

    // semester 为空表示查全部学期
    virtual Result<std::vector<Course>> ListBySemester(const std::string& semester) = 0;

    virtual Result<Course> FindById(std::int64_t id) = 0;

    // 按业务主键找已存在的课程 id：优先「课程号 + 学期」，
    // 没有课程号时用「名称 + 教师 + 学期」。返回 0 表示没找到（正常情况）。
    virtual Result<std::int64_t> FindIdByNaturalKey(const Course& course) = 0;

    // 插入课程及其所有课次，返回新生成的课程 id
    virtual Result<std::int64_t> Insert(const Course& course) = 0;

    // 整体覆盖更新：课程行 + 课次行
    virtual Result<bool> Update(const Course& course) = 0;

    virtual Result<bool> Delete(std::int64_t id) = 0;

protected:
    CourseRepository() = default;
};

}  // namespace campus

