#include "campus/service/course_service.h"

#include <cstddef>

namespace campus {
namespace {

bool ValidateCourse(const Course& course, std::string& error) {
    if (course.name.empty()) {
        error = "课程名称不能为空";
        return false;
    }
    if (course.semester.empty()) {
        error = "学期不能为空（例如 2026-2027-1）";
        return false;
    }
    if (course.credits < 0.0 || course.credits > 30.0) {
        error = "学分必须在 0 到 30 之间";
        return false;
    }
    for (const auto& session : course.sessions) {
        if (!session.Valid()) {
            error = "课次参数非法：星期应为 1-7，节次应为 1-14，周次应为 1-30 且起始不大于结束";
            return false;
        }
    }
    return true;
}

// 同一门课内部不应该有两段互相冲突的时间
bool ValidateNoSelfConflict(const Course& course, std::string& error) {
    for (std::size_t i = 0; i < course.sessions.size(); ++i) {
        for (std::size_t j = i + 1; j < course.sessions.size(); ++j) {
            if (HasTimeConflict(course.sessions[i], course.sessions[j])) {
                error = "课程内部存在时间冲突: " + course.sessions[i].ToString() + " 与 " +
                        course.sessions[j].ToString();
                return false;
            }
        }
    }
    return true;
}

}  // namespace

Result<std::vector<Course>> CourseService::ListCourses(const std::string& semester) {
    return repository_.ListBySemester(semester);
}

Result<Course> CourseService::GetCourse(std::int64_t id) {
    if (id <= 0) {
        return Result<Course>::Fail(ErrorCode::kInvalidArgument, "课程 id 必须为正整数");
    }
    return repository_.FindById(id);
}

Result<Course> CourseService::CreateCourse(Course course) {
    std::string error;
    if (!ValidateCourse(course, error)) {
        return Result<Course>::Fail(ErrorCode::kInvalidArgument, error);
    }
    if (!ValidateNoSelfConflict(course, error)) {
        return Result<Course>::Fail(ErrorCode::kInvalidArgument, error);
    }

    auto new_id = repository_.Insert(course);
    if (!new_id) {
        return Result<Course>::Fail(new_id.error().code, new_id.error().message);
    }

    // 重新读一次，把数据库生成的课次 id 一起返回给调用方。
    // POST 不是热路径，多一次查询换来返回值完整，划算。
    course.id = new_id.value();
    auto persisted = repository_.FindById(course.id);
    if (!persisted) {
        return Result<Course>::Fail(persisted.error().code, persisted.error().message);
    }
    return Result<Course>::Ok(std::move(persisted.value()));
}

Result<Course> CourseService::UpdateCourse(const Course& course) {
    if (course.id <= 0) {
        return Result<Course>::Fail(ErrorCode::kInvalidArgument, "课程 id 必须为正整数");
    }
    std::string error;
    if (!ValidateCourse(course, error)) {
        return Result<Course>::Fail(ErrorCode::kInvalidArgument, error);
    }
    if (!ValidateNoSelfConflict(course, error)) {
        return Result<Course>::Fail(ErrorCode::kInvalidArgument, error);
    }

    auto updated = repository_.Update(course);
    if (!updated) {
        return Result<Course>::Fail(updated.error().code, updated.error().message);
    }
    return Result<Course>::Ok(course);
}

Result<bool> CourseService::DeleteCourse(std::int64_t id) {
    if (id <= 0) {
        return Result<bool>::Fail(ErrorCode::kInvalidArgument, "课程 id 必须为正整数");
    }
    return repository_.Delete(id);
}

}  // namespace campus
