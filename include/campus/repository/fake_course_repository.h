#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "campus/domain/course.h"
#include "campus/domain/result.h"
#include "campus/repository/course_repository.h"

namespace campus {

// 内存版课程仓储，只用于单元测试。
//
// 有了它，同步流程、变动检测这些编排逻辑可以在不启动 MySQL 的情况下被测到，
// 测试跑得快也不会因为外部服务抖动而失败。
class FakeCourseRepository : public CourseRepository {
public:
    Result<std::vector<Course>> ListBySemester(const std::string& semester) override {
        ++list_calls_;
        std::vector<Course> result;
        for (const auto& entry : courses_) {
            if (semester.empty() || entry.second.semester == semester) {
                result.push_back(entry.second);
            }
        }
        std::sort(result.begin(), result.end(),
                  [](const Course& lhs, const Course& rhs) { return lhs.id < rhs.id; });
        return Result<std::vector<Course>>::Ok(std::move(result));
    }

    Result<Course> FindById(std::int64_t id) override {
        const auto it = courses_.find(id);
        if (it == courses_.end()) {
            return Result<Course>::Fail(ErrorCode::kNotFound,
                                        "课程不存在: id=" + std::to_string(id));
        }
        return Result<Course>::Ok(it->second);
    }

    Result<std::int64_t> FindIdByNaturalKey(const Course& course) override {
        for (const auto& entry : courses_) {
            if (entry.second.semester != course.semester) {
                continue;
            }
            if (!course.code.empty() && !entry.second.code.empty()) {
                if (entry.second.code == course.code) {
                    return Result<std::int64_t>::Ok(entry.first);
                }
                continue;
            }
            if (entry.second.name == course.name && entry.second.teacher == course.teacher) {
                return Result<std::int64_t>::Ok(entry.first);
            }
        }
        return Result<std::int64_t>::Ok(0);
    }

    Result<std::int64_t> Insert(const Course& course) override {
        const std::int64_t id = next_id_++;
        Course stored = course;
        stored.id = id;
        courses_[id] = std::move(stored);
        return Result<std::int64_t>::Ok(id);
    }

    Result<bool> Update(const Course& course) override {
        const auto it = courses_.find(course.id);
        if (it == courses_.end()) {
            return Result<bool>::Fail(ErrorCode::kNotFound,
                                      "课程不存在: id=" + std::to_string(course.id));
        }
        it->second = course;
        return Result<bool>::Ok(true);
    }

    Result<bool> Delete(std::int64_t id) override {
        if (courses_.erase(id) == 0) {
            return Result<bool>::Fail(ErrorCode::kNotFound,
                                      "课程不存在: id=" + std::to_string(id));
        }
        return Result<bool>::Ok(true);
    }

    std::size_t Size() const { return courses_.size(); }

    // 让测试能断言「这次读到底有没有真的查库」——缓存是否生效靠它验证
    std::size_t ListCallCount() const { return list_calls_; }

private:
    std::map<std::int64_t, Course> courses_;
    std::int64_t next_id_ = 1;
    std::size_t list_calls_ = 0;
};

}  // namespace campus
