#include "campus/repository/cached_course_repository.h"

#include <spdlog/spdlog.h>

#include "campus/domain/course_codec.h"

namespace campus {
namespace {

constexpr const char* kKeyPrefix = "campus:courses:";

}  // namespace

CachedCourseRepository::CachedCourseRepository(CourseRepository& inner, CacheStore* cache,
                                               CacheStats* stats, int ttl_seconds)
    : inner_(inner),
      cache_(cache),
      stats_(stats),
      ttl_seconds_(ttl_seconds > 0 ? ttl_seconds : 300) {}

std::string CachedCourseRepository::CacheKey(const std::string& semester) {
    return std::string(kKeyPrefix) + semester;
}

Result<std::vector<Course>> CachedCourseRepository::ListBySemester(const std::string& semester) {
    // 不缓存「查全部学期」：数据量大且命中率低，缓存它收益不如代价
    if (cache_ == nullptr || semester.empty()) {
        return inner_.ListBySemester(semester);
    }

    const std::string key = CacheKey(semester);
    auto cached = cache_->Get(key);
    if (cached.ok()) {
        std::string cached_semester;
        std::vector<Course> courses;
        std::string error;
        if (DecodeCourseList(cached.value(), cached_semester, courses, error)) {
            if (stats_ != nullptr) {
                stats_->RecordHit();
            }
            return Result<std::vector<Course>>::Ok(std::move(courses));
        }

        // 缓存内容读不出来（版本升级、人为改动等）：删掉它当作未命中，
        // 否则这份坏数据会一直被读到
        spdlog::warn("缓存内容无法解析，已丢弃: {}", error);
        cache_->Del(key);
    }

    if (stats_ != nullptr) {
        stats_->RecordMiss();
    }

    auto fresh = inner_.ListBySemester(semester);
    if (!fresh) {
        return Result<std::vector<Course>>::Fail(fresh.error().code, fresh.error().message);
    }

    auto stored = cache_->Set(key, EncodeCourseList(fresh.value(), semester), ttl_seconds_);
    if (!stored) {
        // 写缓存失败不影响这次请求：数据已经从库里拿到了，只是下次还得查库
        spdlog::warn("写入缓存失败（不影响本次请求）: {}", stored.error().message);
    }
    return fresh;
}

// 单条查询不走缓存：它的调用频率远低于列表查询，
// 加了缓存只会多一份需要维护的失效逻辑。
Result<Course> CachedCourseRepository::FindById(std::int64_t id) {
    return inner_.FindById(id);
}

// 同步流程必须拿到准确结果，不能走缓存
Result<std::int64_t> CachedCourseRepository::FindIdByNaturalKey(const Course& course) {
    return inner_.FindIdByNaturalKey(course);
}

Result<std::int64_t> CachedCourseRepository::Insert(const Course& course) {
    auto created = inner_.Insert(course);
    if (created.ok()) {
        Invalidate(course.semester);
    }
    return created;
}

Result<bool> CachedCourseRepository::Update(const Course& course) {
    // 课程可能被改到别的学期，所以旧学期的缓存也要清
    std::string old_semester;
    if (cache_ != nullptr) {
        auto existing = inner_.FindById(course.id);
        if (existing.ok()) {
            old_semester = existing.value().semester;
        }
    }

    auto updated = inner_.Update(course);
    if (updated.ok()) {
        Invalidate(course.semester);
        if (!old_semester.empty() && old_semester != course.semester) {
            Invalidate(old_semester);
        }
    }
    return updated;
}

Result<bool> CachedCourseRepository::Delete(std::int64_t id) {
    std::string semester;
    if (cache_ != nullptr) {
        auto existing = inner_.FindById(id);
        if (existing.ok()) {
            semester = existing.value().semester;
        }
    }

    auto deleted = inner_.Delete(id);
    if (deleted.ok() && !semester.empty()) {
        Invalidate(semester);
    }
    return deleted;
}

void CachedCourseRepository::Invalidate(const std::string& semester) {
    if (cache_ == nullptr || semester.empty()) {
        return;
    }
    cache_->Del(CacheKey(semester));
    if (stats_ != nullptr) {
        stats_->RecordInvalidation();
    }
}

}  // namespace campus
