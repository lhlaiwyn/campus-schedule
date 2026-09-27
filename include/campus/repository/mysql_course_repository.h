#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "campus/domain/course.h"
#include "campus/domain/result.h"
#include "campus/infra/mysql_pool.h"
#include "campus/repository/course_repository.h"

namespace campus {

// CourseRepository 的 MySQL 实现。SQL 全部集中在这个文件里。
class MySqlCourseRepository : public CourseRepository {
public:
    explicit MySqlCourseRepository(MySqlPool& pool) : pool_(pool) {}

    Result<std::vector<Course>> ListBySemester(const std::string& semester) override;
    Result<Course> FindById(std::int64_t id) override;
    Result<std::int64_t> FindIdByNaturalKey(const Course& course) override;
    Result<std::int64_t> Insert(const Course& course) override;
    Result<bool> Update(const Course& course) override;
    Result<bool> Delete(std::int64_t id) override;

private:
    MySqlPool& pool_;
};

}  // namespace campus

