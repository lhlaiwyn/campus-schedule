#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "campus/domain/course_codec.h"
#include "campus/infra/cache_store.h"
#include "campus/repository/cached_course_repository.h"
#include "campus/repository/fake_course_repository.h"

namespace campus {
namespace {

Course MakeCourseWithSemester(const std::string& name, const std::string& code,
                              const std::string& teacher, const std::string& semester) {
    Course course;
    course.name = name;
    course.code = code;
    course.teacher = teacher;
    course.semester = semester;
    course.credits = 3.0;

    CourseSession session;
    session.day_of_week = 2;
    session.start_period = 3;
    session.end_period = 4;
    session.weeks = WeekRange{1, 16, WeekParity::kEvery};
    session.location = "教学楼A301";
    course.sessions.push_back(session);
    return course;
}

// ========================= 内存缓存 =========================

TEST(InMemoryCacheStoreTest, SetGetDel) {
    InMemoryCacheStore store;
    EXPECT_FALSE(store.Get("missing").ok());

    ASSERT_TRUE(store.Set("k", "v", 60).ok());
    auto value = store.Get("k");
    ASSERT_TRUE(value.ok());
    EXPECT_EQ(value.value(), "v");
    EXPECT_EQ(store.Size(), 1U);

    EXPECT_TRUE(store.Del("k").ok());
    EXPECT_FALSE(store.Get("k").ok());
}

TEST(InMemoryCacheStoreTest, ExpiresAfterTtl) {
    InMemoryCacheStore store;
    ASSERT_TRUE(store.Set("k", "v", 10).ok());

    store.SetNow(9999);
    EXPECT_TRUE(store.Get("k").ok());

    store.SetNow(10000);  // 到点即过期
    EXPECT_FALSE(store.Get("k").ok());
}

TEST(InMemoryCacheStoreTest, PurgesExpiredEntries) {
    InMemoryCacheStore store;
    ASSERT_TRUE(store.Set("a", "1", 10).ok());
    ASSERT_TRUE(store.Set("b", "2", 100).ok());

    store.SetNow(10000);
    EXPECT_EQ(store.PurgeExpired(), 1U);
    EXPECT_EQ(store.Size(), 1U);
}

// ========================= 命中统计 =========================

TEST(CacheStatsTest, ComputesHitRate) {
    CacheStats stats;
    EXPECT_DOUBLE_EQ(stats.HitRate(), 0.0);  // 没有请求时不应除零

    stats.RecordHit();
    stats.RecordHit();
    stats.RecordMiss();
    stats.RecordMiss();
    stats.RecordInvalidation();

    EXPECT_EQ(stats.Hits(), 2U);
    EXPECT_EQ(stats.Misses(), 2U);
    EXPECT_EQ(stats.Invalidations(), 1U);
    EXPECT_DOUBLE_EQ(stats.HitRate(), 0.5);

    stats.Reset();
    EXPECT_EQ(stats.Hits(), 0U);
    EXPECT_DOUBLE_EQ(stats.HitRate(), 0.0);
}

// ========================= 缓存编解码 =========================

TEST(CourseCodecTest, RoundTripsCourseList) {
    std::vector<Course> courses{
        MakeCourseWithSemester("操作系统", "CS2001", "张伟", "2026-2027-1"),
        MakeCourseWithSemester("数据结构", "CS2002", "李娜", "2026-2027-1"),
    };
    courses[0].id = 7;
    courses[1].id = 8;

    const std::string payload = EncodeCourseList(courses, "2026-2027-1");

    std::string semester;
    std::vector<Course> decoded;
    std::string error;
    ASSERT_TRUE(DecodeCourseList(payload, semester, decoded, error)) << error;
    EXPECT_EQ(semester, "2026-2027-1");
    ASSERT_EQ(decoded.size(), 2U);
    EXPECT_EQ(decoded[0].id, 7);
    EXPECT_EQ(decoded[0].name, "操作系统");
    EXPECT_EQ(decoded[1].code, "CS2002");
    EXPECT_EQ(decoded[0].sessions.size(), 1U);
    EXPECT_EQ(decoded[0].sessions[0].location, "教学楼A301");
}

TEST(CourseCodecTest, PreservesSourceField) {
    std::vector<Course> courses{
        MakeCourseWithSemester("操作系统", "CS2001", "张伟", "2026-2027-1"),
        MakeCourseWithSemester("人工智能讲座", "LECTURE-1", "特邀", "2026-2027-1"),
    };
    courses[0].source = kSourcePortal;
    courses[1].source = kSourceManual;

    std::string semester;
    std::vector<Course> decoded;
    std::string error;
    ASSERT_TRUE(DecodeCourseList(EncodeCourseList(courses, "2026-2027-1"), semester, decoded,
                                 error))
        << error;
    ASSERT_EQ(decoded.size(), 2U);
    EXPECT_EQ(decoded[0].source, kSourcePortal);
    EXPECT_EQ(decoded[1].source, kSourceManual);
}

TEST(CourseCodecTest, RejectsCorruptPayload) {
    std::string semester;
    std::vector<Course> courses;
    std::string error;

    EXPECT_FALSE(DecodeCourseList("not json", semester, courses, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(DecodeCourseList(R"({"semester":"x"})", semester, courses, error));
    EXPECT_FALSE(DecodeCourseList(R"({"items":"not-an-array"})", semester, courses, error));
}

// ========================= 缓存装饰器 =========================

TEST(CachedCourseRepositoryTest, MissThenHitWithoutTouchingDatabase) {
    FakeCourseRepository fake;
    InMemoryCacheStore cache;
    CacheStats stats;
    CachedCourseRepository repository(fake, &cache, &stats, 300);

    ASSERT_TRUE(repository.Insert(MakeCourseWithSemester("操作系统", "CS2001", "张伟",
                                                         "2026-2027-1"))
                    .ok());

    // 第一次：未命中，去查库
    auto first = repository.ListBySemester("2026-2027-1");
    ASSERT_TRUE(first.ok()) << first.error().message;
    EXPECT_EQ(first.value().size(), 1U);
    EXPECT_EQ(stats.Misses(), 1U);
    EXPECT_EQ(stats.Hits(), 0U);
    const std::size_t calls_after_miss = fake.ListCallCount();

    // 第二次：命中，不应该再查库
    auto second = repository.ListBySemester("2026-2027-1");
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(second.value().size(), 1U);
    EXPECT_EQ(stats.Hits(), 1U);
    EXPECT_EQ(fake.ListCallCount(), calls_after_miss);
    EXPECT_DOUBLE_EQ(stats.HitRate(), 0.5);
}

TEST(CachedCourseRepositoryTest, WriteInvalidatesCache) {
    FakeCourseRepository fake;
    InMemoryCacheStore cache;
    CacheStats stats;
    CachedCourseRepository repository(fake, &cache, &stats, 300);

    const std::string semester = "2026-2027-1";
    ASSERT_TRUE(repository.Insert(MakeCourseWithSemester("操作系统", "CS2001", "张伟", semester))
                    .ok());
    ASSERT_TRUE(repository.ListBySemester(semester).ok());  // 回填缓存
    ASSERT_EQ(cache.Size(), 1U);

    // 再写一门课：缓存必须失效，下一次读要能立刻看到新数据
    ASSERT_TRUE(repository.Insert(MakeCourseWithSemester("数据结构", "CS2002", "李娜", semester))
                    .ok());
    EXPECT_EQ(cache.Size(), 0U);
    EXPECT_EQ(stats.Invalidations(), 2U);  // 两次插入各失效一次

    auto fresh = repository.ListBySemester(semester);
    ASSERT_TRUE(fresh.ok());
    EXPECT_EQ(fresh.value().size(), 2U);
}

TEST(CachedCourseRepositoryTest, UpdateInvalidatesBothOldAndNewSemester) {
    FakeCourseRepository fake;
    InMemoryCacheStore cache;
    CacheStats stats;
    CachedCourseRepository repository(fake, &cache, &stats, 300);

    auto course = MakeCourseWithSemester("操作系统", "CS2001", "张伟", "2026-2027-1");
    auto id = repository.Insert(course);
    ASSERT_TRUE(id.ok());
    ASSERT_TRUE(repository.ListBySemester("2026-2027-1").ok());
    ASSERT_EQ(cache.Size(), 1U);

    // 把课程的学期改掉：旧学期和新学期的缓存都必须是干净的
    course.id = id.value();
    course.semester = "2026-2027-2";
    ASSERT_TRUE(repository.Update(course).ok());
    EXPECT_EQ(cache.Size(), 0U);

    auto old_semester = repository.ListBySemester("2026-2027-1");
    ASSERT_TRUE(old_semester.ok());
    EXPECT_TRUE(old_semester.value().empty());  // 旧学期已经查不到它了
}

TEST(CachedCourseRepositoryTest, DeleteInvalidatesCache) {
    FakeCourseRepository fake;
    InMemoryCacheStore cache;
    CacheStats stats;
    CachedCourseRepository repository(fake, &cache, &stats, 300);

    auto id = repository.Insert(MakeCourseWithSemester("操作系统", "CS2001", "张伟",
                                                      "2026-2027-1"));
    ASSERT_TRUE(id.ok());
    ASSERT_TRUE(repository.ListBySemester("2026-2027-1").ok());
    ASSERT_EQ(cache.Size(), 1U);

    ASSERT_TRUE(repository.Delete(id.value()).ok());
    EXPECT_EQ(cache.Size(), 0U);
    auto after = repository.ListBySemester("2026-2027-1");
    ASSERT_TRUE(after.ok());
    EXPECT_TRUE(after.value().empty());
}

TEST(CachedCourseRepositoryTest, TtlExpiryFallsBackToDatabase) {
    FakeCourseRepository fake;
    InMemoryCacheStore cache;
    CacheStats stats;
    CachedCourseRepository repository(fake, &cache, &stats, 10);

    ASSERT_TRUE(repository.Insert(MakeCourseWithSemester("操作系统", "CS2001", "张伟",
                                                         "2026-2027-1"))
                    .ok());
    ASSERT_TRUE(repository.ListBySemester("2026-2027-1").ok());
    EXPECT_EQ(stats.Misses(), 1U);

    cache.SetNow(20000);  // 超过 10 秒 TTL
    ASSERT_TRUE(repository.ListBySemester("2026-2027-1").ok());
    EXPECT_EQ(stats.Misses(), 2U);
    EXPECT_EQ(stats.Hits(), 0U);
}

TEST(CachedCourseRepositoryTest, CorruptCacheEntryIsDiscardedAndRefetched) {
    FakeCourseRepository fake;
    InMemoryCacheStore cache;
    CacheStats stats;
    CachedCourseRepository repository(fake, &cache, &stats, 300);

    const std::string semester = "2026-2027-1";
    ASSERT_TRUE(repository.Insert(MakeCourseWithSemester("操作系统", "CS2001", "张伟", semester))
                    .ok());

    // 手动塞一份坏数据
    ASSERT_TRUE(cache.Set(CachedCourseRepository::CacheKey(semester), "坏掉的内容", 300).ok());

    auto result = repository.ListBySemester(semester);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().size(), 1U);   // 退化成查库，仍然拿到正确数据
    EXPECT_EQ(stats.Misses(), 1U);
    EXPECT_EQ(stats.Hits(), 0U);
    EXPECT_EQ(cache.Size(), 1U);            // 坏数据已被替换成新回填的内容
}

TEST(CachedCourseRepositoryTest, DisabledCachePassesThrough) {
    FakeCourseRepository fake;
    CachedCourseRepository repository(fake, nullptr, nullptr, 300);

    ASSERT_TRUE(repository.Insert(MakeCourseWithSemester("操作系统", "CS2001", "张伟",
                                                         "2026-2027-1"))
                    .ok());

    const std::size_t before = fake.ListCallCount();
    EXPECT_TRUE(repository.ListBySemester("2026-2027-1").ok());
    EXPECT_TRUE(repository.ListBySemester("2026-2027-1").ok());
    // 缓存关掉时每次都直接查库
    EXPECT_EQ(fake.ListCallCount(), before + 2);
}

TEST(CachedCourseRepositoryTest, DoesNotCacheAllSemestersQuery) {
    FakeCourseRepository fake;
    InMemoryCacheStore cache;
    CacheStats stats;
    CachedCourseRepository repository(fake, &cache, &stats, 300);

    ASSERT_TRUE(repository.ListBySemester("").ok());
    EXPECT_EQ(cache.Size(), 0U);   // 没有写缓存
    EXPECT_EQ(stats.Misses(), 0U); // 也不算命中/未命中
}

}  // namespace
}  // namespace campus
