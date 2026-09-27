#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "campus/domain/course.h"
#include "campus/domain/result.h"
#include "campus/infra/cache_store.h"
#include "campus/repository/course_repository.h"

namespace campus {

// 缓存装饰器：在不改动 CourseRepository 任何实现的前提下加上 Cache-Aside。
//
//   ListBySemester  读缓存，未命中就查库并回填
//   Insert/Update/Delete  写库成功后让对应学期的缓存失效
//
// 之所以用装饰器而不是把缓存写进 CourseService：
//   1. service 层完全不需要知道缓存的存在；
//   2. 缓存策略可以单独测试（用内存仓储 + 内存缓存，不需要 MySQL 和 Redis）；
//   3. 想关掉缓存只需要传一个空指针。
class CachedCourseRepository : public CourseRepository {
public:
    // cache 为空表示不启用缓存，所有调用直接透传
    CachedCourseRepository(CourseRepository& inner, CacheStore* cache, CacheStats* stats,
                           int ttl_seconds);

    Result<std::vector<Course>> ListBySemester(const std::string& semester) override;
    Result<Course> FindById(std::int64_t id) override;
    Result<std::int64_t> FindIdByNaturalKey(const Course& course) override;
    Result<std::int64_t> Insert(const Course& course) override;
    Result<bool> Update(const Course& course) override;
    Result<bool> Delete(std::int64_t id) override;

    static std::string CacheKey(const std::string& semester);

private:
    void Invalidate(const std::string& semester);

    CourseRepository& inner_;
    CacheStore* cache_;
    CacheStats* stats_;
    int ttl_seconds_;
};

}  // namespace campus

