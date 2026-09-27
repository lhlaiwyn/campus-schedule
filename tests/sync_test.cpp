#include <string>

#include <gtest/gtest.h>

#include "campus/portal/mock_portal_adapter.h"
#include "campus/repository/fake_course_repository.h"
#include "campus/service/sync_service.h"

namespace campus {
namespace {

PortalCredentials MakeCredentials() {
    PortalCredentials credentials;
    credentials.student_id = "2024001";
    credentials.password = "secret123";
    return credentials;
}

TEST(SyncServiceTest, FirstSyncInsertsEverything) {
    MockPortalAdapter portal;
    FakeCourseRepository repository;
    SyncService service(portal, repository);

    auto result = service.Sync(MakeCredentials(), "2026-2027-1");
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_EQ(result.value().semester, "2026-2027-1");
    EXPECT_EQ(result.value().fetched, 3U);    // 教务系统返回 3 条原始记录
    EXPECT_EQ(result.value().converted, 3U);  // 合并成 3 门课
    EXPECT_EQ(result.value().inserted, 3U);
    EXPECT_EQ(result.value().updated, 0U);
    EXPECT_EQ(repository.Size(), 3U);
    // 本地课表原本是空的，所以三门课都是新增
    EXPECT_EQ(result.value().changes.size(), 3U);
}

TEST(SyncServiceTest, SecondSyncUpdatesInsteadOfDuplicating) {
    MockPortalAdapter portal;
    FakeCourseRepository repository;
    SyncService service(portal, repository);

    ASSERT_TRUE(service.Sync(MakeCredentials(), "2026-2027-1").ok());

    auto again = service.Sync(MakeCredentials(), "2026-2027-1");
    ASSERT_TRUE(again.ok()) << again.error().message;

    EXPECT_EQ(again.value().inserted, 0U);
    EXPECT_EQ(again.value().updated, 3U);
    EXPECT_EQ(repository.Size(), 3U);            // 没有产生重复课程
    EXPECT_TRUE(again.value().changes.empty());  // 课表没变就不该报变动
}

TEST(SyncServiceTest, BadCredentialsDoNotWriteAnything) {
    MockPortalAdapter portal;
    FakeCourseRepository repository;
    SyncService service(portal, repository);

    PortalCredentials bad;
    bad.student_id = "2024001";
    bad.password = "123";  // 太短

    auto result = service.Sync(bad, "2026-2027-1");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(repository.Size(), 0U);
}

TEST(SyncServiceTest, RejectsEmptySemester) {
    MockPortalAdapter portal;
    FakeCourseRepository repository;
    SyncService service(portal, repository);
    EXPECT_FALSE(service.Sync(MakeCredentials(), "").ok());
}

TEST(SyncServiceTest, MarksSyncedCoursesAsPortalSourced) {
    MockPortalAdapter portal;
    FakeCourseRepository repository;
    SyncService service(portal, repository);

    ASSERT_TRUE(service.Sync(MakeCredentials(), "2026-2027-1").ok());

    auto courses = repository.ListBySemester("2026-2027-1");
    ASSERT_TRUE(courses.ok());
    ASSERT_FALSE(courses.value().empty());
    for (const auto& course : courses.value()) {
        EXPECT_EQ(course.source, kSourcePortal);
    }
}

TEST(SyncServiceTest, ManualCoursesAreNotReportedAsRemoved) {
    MockPortalAdapter portal;
    FakeCourseRepository repository;
    SyncService service(portal, repository);

    ASSERT_TRUE(service.Sync(MakeCredentials(), "2026-2027-1").ok());

    // 用户手动加一门教务系统里没有的课（讲座、体育私教之类）
    Course manual;
    manual.name = "人工智能讲座";
    manual.code = "LECTURE-1";
    manual.teacher = "特邀讲师";
    manual.semester = "2026-2027-1";
    manual.source = kSourceManual;
    ASSERT_TRUE(repository.Insert(manual).ok());

    auto again = service.Sync(MakeCredentials(), "2026-2027-1");
    ASSERT_TRUE(again.ok()) << again.error().message;

    // 手动课程不属于教务系统，不该被同步报成「课程移除」
    for (const auto& change : again.value().changes) {
        EXPECT_NE(change.course_name, "人工智能讲座");
    }
    EXPECT_TRUE(again.value().changes.empty());
    EXPECT_EQ(repository.Size(), 4U);  // 手动课程依然在库里
}

}  // namespace
}  // namespace campus
