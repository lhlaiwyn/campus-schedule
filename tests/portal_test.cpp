#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "campus/domain/week_parser.h"
#include "campus/portal/adapter_registry.h"
#include "campus/portal/mock_portal_adapter.h"
#include "campus/portal/schedule_converter.h"
#include "campus/service/change_detector.h"

namespace campus {
namespace {

RawScheduleEntry MakeRaw(const std::string& name, const std::string& code,
                         const std::string& teacher, int day, int start_period,
                         int end_period, const std::string& weeks_text) {
    RawScheduleEntry entry;
    entry.course_name = name;
    entry.course_code = code;
    entry.teacher = teacher;
    entry.location = "教学楼 A301";
    entry.day_of_week = day;
    entry.start_period = start_period;
    entry.end_period = end_period;
    entry.weeks_text = weeks_text;
    entry.credits = 3.0;
    return entry;
}

// ------------------------- 周次文本解析 -------------------------

TEST(WeekParserTest, ParsesPlainRange) {
    auto weeks = ParseWeekText("1-16周");
    ASSERT_TRUE(weeks.ok()) << weeks.error().message;
    EXPECT_EQ(weeks.value().from, 1);
    EXPECT_EQ(weeks.value().to, 16);
    EXPECT_EQ(weeks.value().parity, WeekParity::kEvery);
}

TEST(WeekParserTest, ParsesOddAndEvenParity) {
    auto odd = ParseWeekText("1-16周(单)");
    ASSERT_TRUE(odd.ok()) << odd.error().message;
    EXPECT_EQ(odd.value().parity, WeekParity::kOdd);

    auto even = ParseWeekText("3-15周（双）");
    ASSERT_TRUE(even.ok()) << even.error().message;
    EXPECT_EQ(even.value().from, 3);
    EXPECT_EQ(even.value().to, 15);
    EXPECT_EQ(even.value().parity, WeekParity::kEven);
}

TEST(WeekParserTest, ParsesSingleWeek) {
    auto weeks = ParseWeekText("5周");
    ASSERT_TRUE(weeks.ok()) << weeks.error().message;
    EXPECT_EQ(weeks.value().from, 5);
    EXPECT_EQ(weeks.value().to, 5);
}

TEST(WeekParserTest, RejectsUnsupportedOrInvalidText) {
    EXPECT_FALSE(ParseWeekText("").ok());
    EXPECT_FALSE(ParseWeekText("周周周").ok());
    EXPECT_FALSE(ParseWeekText("1-40周").ok());
    EXPECT_FALSE(ParseWeekText("1-8,10-16周").ok());
}

// ------------------------- 模拟教务适配器 -------------------------

TEST(MockPortalAdapterTest, LoginRejectsShortPassword) {
    MockPortalAdapter adapter;
    PortalCredentials credentials;
    credentials.student_id = "2024001";
    credentials.password = "123";
    EXPECT_FALSE(adapter.Login(credentials).ok());
}

TEST(MockPortalAdapterTest, LoginThenFetchSchedule) {
    MockPortalAdapter adapter;
    PortalCredentials credentials;
    credentials.student_id = "2024001";
    credentials.password = "secret123";

    auto token = adapter.Login(credentials);
    ASSERT_TRUE(token.ok()) << token.error().message;

    auto schedule = adapter.FetchSchedule(token.value(), "2026-2027-1");
    ASSERT_TRUE(schedule.ok()) << schedule.error().message;
    EXPECT_EQ(schedule.value().size(), 3U);
    EXPECT_EQ(schedule.value().front().course_name, "操作系统");
}

TEST(MockPortalAdapterTest, RejectsWrongPasswordAndUnknownStudent) {
    MockPortalAdapter adapter;

    PortalCredentials wrong_password;
    wrong_password.student_id = "2024001";
    wrong_password.password = "wrong-password";
    auto failed = adapter.Login(wrong_password);
    EXPECT_FALSE(failed.ok());
    // 内存版必须和独立进程的模拟教务系统行为一致，否则基于它的测试测不出真实行为
    EXPECT_NE(failed.error().message.find("密码"), std::string::npos);

    PortalCredentials unknown_id;
    unknown_id.student_id = "9999999";
    unknown_id.password = "secret123";
    EXPECT_FALSE(adapter.Login(unknown_id).ok());
}

TEST(MockPortalAdapterTest, FetchFailsWithUnknownToken) {
    MockPortalAdapter adapter;
    EXPECT_FALSE(adapter.FetchSchedule("bogus-token", "2026-2027-1").ok());
}

TEST(MockPortalAdapterTest, LogoutInvalidatesToken) {
    MockPortalAdapter adapter;
    PortalCredentials credentials;
    credentials.student_id = "2024001";
    credentials.password = "secret123";

    auto token = adapter.Login(credentials);
    ASSERT_TRUE(token.ok());
    adapter.Logout(token.value());
    EXPECT_FALSE(adapter.FetchSchedule(token.value(), "2026-2027-1").ok());
}

// ------------------------- 适配器注册表 -------------------------

TEST(AdapterRegistryTest, CreatesRegisteredAdapter) {
    RegisterBuiltinAdapters();

    const auto names = AdapterRegistry::Names();
    ASSERT_FALSE(names.empty());
    // 注册表按名字排序，所以顺序会随新增适配器变化；
    // 这里断言「包含哪些」而不是「第一个是谁」，避免新增实现就要改测试。
    EXPECT_NE(std::find(names.begin(), names.end(), "mock"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "http"), names.end());

    auto mock_adapter = AdapterRegistry::Create("mock");
    ASSERT_NE(mock_adapter, nullptr);
    EXPECT_EQ(mock_adapter->Name(), "mock");

    auto http_adapter = AdapterRegistry::Create("http");
    ASSERT_NE(http_adapter, nullptr);
    EXPECT_EQ(http_adapter->Name(), "http");
}

TEST(AdapterRegistryTest, ReturnsNullForUnknownName) {
    RegisterBuiltinAdapters();
    EXPECT_EQ(AdapterRegistry::Create("no-such-school"), nullptr);
}

// ------------------------- 原始课表转换 -------------------------

TEST(ScheduleConverterTest, MergesSessionsOfSameCourse) {
    std::vector<RawScheduleEntry> raw{
        MakeRaw("操作系统", "CS2001", "张伟", 2, 3, 4, "1-16周"),
        MakeRaw("操作系统", "CS2001", "张伟", 4, 5, 6, "1-16周"),
        MakeRaw("数据结构", "CS2002", "李娜", 4, 1, 2, "1-16周(单)"),
    };

    auto courses = ConvertRawSchedule(raw, "2026-2027-1");
    ASSERT_TRUE(courses.ok()) << courses.error().message;
    ASSERT_EQ(courses.value().size(), 2U);

    EXPECT_EQ(courses.value()[0].name, "操作系统");
    EXPECT_EQ(courses.value()[0].sessions.size(), 2U);
    EXPECT_EQ(courses.value()[0].WeeklyPeriods(), 4);
    EXPECT_EQ(courses.value()[1].sessions[0].weeks.parity, WeekParity::kOdd);
}

TEST(ScheduleConverterTest, SortsSessionsByTime) {
    std::vector<RawScheduleEntry> raw{
        MakeRaw("操作系统", "CS2001", "张伟", 5, 1, 2, "1-16周"),
        MakeRaw("操作系统", "CS2001", "张伟", 2, 5, 6, "1-16周"),
    };

    auto courses = ConvertRawSchedule(raw, "2026-2027-1");
    ASSERT_TRUE(courses.ok()) << courses.error().message;
    ASSERT_EQ(courses.value().size(), 1U);
    EXPECT_EQ(courses.value()[0].sessions[0].day_of_week, 2);
    EXPECT_EQ(courses.value()[0].sessions[1].day_of_week, 5);
}

TEST(ScheduleConverterTest, FailsOnUnparsableWeeksAndNamesTheCourse) {
    std::vector<RawScheduleEntry> raw{MakeRaw("编译原理", "CS2004", "赵老师", 1, 1, 2,
                                              "1-8,10-16周")};
    auto courses = ConvertRawSchedule(raw, "2026-2027-1");
    ASSERT_FALSE(courses.ok());
    EXPECT_NE(courses.error().message.find("编译原理"), std::string::npos);
}

TEST(ScheduleConverterTest, RejectsEmptyCourseNameAndSemester) {
    std::vector<RawScheduleEntry> no_name{MakeRaw("", "", "", 1, 1, 2, "1-16周")};
    EXPECT_FALSE(ConvertRawSchedule(no_name, "2026-2027-1").ok());

    std::vector<RawScheduleEntry> ok_raw{MakeRaw("操作系统", "CS2001", "张伟", 2, 3, 4, "1-16周")};
    EXPECT_FALSE(ConvertRawSchedule(ok_raw, "").ok());
}

TEST(ScheduleConverterTest, MarksConvertedCoursesAsPortalSourced) {
    std::vector<RawScheduleEntry> raw{MakeRaw("操作系统", "CS2001", "张伟", 2, 3, 4, "1-16周")};
    auto courses = ConvertRawSchedule(raw, "2026-2027-1");
    ASSERT_TRUE(courses.ok()) << courses.error().message;
    ASSERT_EQ(courses.value().size(), 1U);
    EXPECT_EQ(courses.value()[0].source, kSourcePortal);
}

TEST(ScheduleConverterTest, RejectsReversedPeriods) {
    std::vector<RawScheduleEntry> raw{MakeRaw("操作系统", "CS2001", "张伟", 2, 5, 3, "1-16周")};
    EXPECT_FALSE(ConvertRawSchedule(raw, "2026-2027-1").ok());
}

// ------------------------- 课表变动检测 -------------------------

Course MakeCourseWith(const std::string& name, const std::string& code, const std::string& teacher,
                      int day, int start_period, int end_period, const std::string& location) {
    Course course;
    course.name = name;
    course.code = code;
    course.teacher = teacher;
    course.semester = "2026-2027-1";

    CourseSession session;
    session.day_of_week = day;
    session.start_period = start_period;
    session.end_period = end_period;
    session.weeks = WeekRange{1, 16, WeekParity::kEvery};
    session.location = location;
    course.sessions.push_back(session);
    return course;
}

TEST(ChangeDetectorTest, NoChangeProducesEmptyList) {
    std::vector<Course> before{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301")};
    EXPECT_TRUE(DetectChanges(before, before).empty());
}

TEST(ChangeDetectorTest, DetectsRoomChange) {
    std::vector<Course> before{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301")};
    std::vector<Course> after{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "D401")};

    auto changes = DetectChanges(before, after);
    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].type, ChangeType::kRoomChanged);
    EXPECT_NE(changes[0].detail.find("A301"), std::string::npos);
    EXPECT_NE(changes[0].detail.find("D401"), std::string::npos);
}

TEST(ChangeDetectorTest, DetectsTimeChange) {
    std::vector<Course> before{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301")};
    std::vector<Course> after{MakeCourseWith("操作系统", "CS2001", "张伟", 3, 5, 6, "A301")};

    auto changes = DetectChanges(before, after);
    bool found = false;
    for (const auto& change : changes) {
        if (change.type == ChangeType::kTimeChanged) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ChangeDetectorTest, DetectsTeacherChange) {
    std::vector<Course> before{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301")};
    std::vector<Course> after{MakeCourseWith("操作系统", "CS2001", "王强", 2, 3, 4, "A301")};

    bool found = false;
    for (const auto& change : DetectChanges(before, after)) {
        if (change.type == ChangeType::kTeacherChanged) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ChangeDetectorTest, DetectsSuspension) {
    std::vector<Course> before{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301")};
    Course suspended = MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301");
    suspended.sessions.clear();
    std::vector<Course> after{suspended};

    auto changes = DetectChanges(before, after);
    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].type, ChangeType::kSuspended);
}

TEST(ChangeDetectorTest, DetectsNewAndRemovedCourses) {
    std::vector<Course> before{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301")};
    std::vector<Course> after{MakeCourseWith("计算机网络", "CS2003", "王强", 6, 5, 8, "C201")};

    auto changes = DetectChanges(before, after);
    ASSERT_EQ(changes.size(), 2U);
    // 结果排序后：kRemovedCourse(1) 在 kNewCourse(0) 之后，先确认两种都存在
    bool has_new = false;
    bool has_removed = false;
    for (const auto& change : changes) {
        if (change.type == ChangeType::kNewCourse) {
            has_new = true;
        }
        if (change.type == ChangeType::kRemovedCourse) {
            has_removed = true;
        }
    }
    EXPECT_TRUE(has_new);
    EXPECT_TRUE(has_removed);
}

TEST(ChangeDetectorTest, DetectChangesIsDeterministic) {
    std::vector<Course> before{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301")};
    std::vector<Course> after{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "B102")};

    const auto first = DetectChanges(before, after);
    const auto second = DetectChanges(before, after);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].detail, second[i].detail);
    }
}

TEST(ChangeDetectorTest, DescribesChangesInReadableText) {
    std::vector<Course> before{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "A301")};
    std::vector<Course> after{MakeCourseWith("操作系统", "CS2001", "张伟", 2, 3, 4, "D401")};

    auto lines = DescribeChanges(DetectChanges(before, after));
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_NE(lines[0].find("换教室"), std::string::npos);
}

}  // namespace
}  // namespace campus
