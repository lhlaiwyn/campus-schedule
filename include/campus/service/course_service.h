#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "campus/domain/course.h"
#include "campus/domain/result.h"
#include "campus/repository/course_repository.h"

namespace campus {

// 业务层：负责参数校验和业务规则，屏蔽数据库细节
class CourseService {
public:
    explicit CourseService(CourseRepository& repository) : repository_(repository) {}

    Result<std::vector<Course>> ListCourses(const std::string& semester);
    Result<Course> GetCourse(std::int64_t id);
    Result<Course> CreateCourse(Course course);
    Result<Course> UpdateCourse(const Course& course);
    Result<bool> DeleteCourse(std::int64_t id);

private:
    CourseRepository& repository_;
};

}  // namespace campus
