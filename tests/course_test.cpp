#include <string>

#include <gtest/gtest.h>

#include "campus/domain/course_json.h"
#include "campus/domain/course.h"
#include "campus/service/course_service.h"

namespace campus {
namespace {

Course MakeSampleCourse() {
    Course course;
    course.name = "操作系统";
    course.code = "CS2001";
    course.teacher = "张老师";
    course.credits = 3.5;
    course.semester = "2026-2027-1";

    CourseSession session;
    session.day_of_week = 2;
    session.start_period = 3;
    session.end_period = 4;
    session.weeks = WeekRange{1, 16, WeekParity::kEvery};
    session.location = "教学楼 A301";
    course.sessions.push_back(session);
    return course;
}

CourseSession MakeSession(int day, int start_period, int end_period, WeekParity parity) {
    CourseSession session;
    session.day_of_week = day;
    session.start_period = start_period;
    session.end_period = end_period;
    session.weeks = WeekRange{1, 16, parity};
    return session;
}

// ------------------------- 周次 -------------------------

TEST(WeekRangeTest, EveryWeekCoversWholeRange) {
    const WeekRange weeks{1, 16, WeekParity::kEvery};
    EXPECT_TRUE(weeks.Contains(1));
    EXPECT_TRUE(weeks.Contains(8));
    EXPECT_TRUE(weeks.Contains(16));
    EXPECT_FALSE(weeks.Contains(0));
    EXPECT_FALSE(weeks.Contains(17));
}

TEST(WeekRangeTest, OddWeeksSkipEvenWeeks) {
    const WeekRange weeks{1, 16, WeekParity::kOdd};
    EXPECT_TRUE(weeks.Contains(1));
    EXPECT_TRUE(weeks.Contains(15));
    EXPECT_FALSE(weeks.Contains(2));
    EXPECT_FALSE(weeks.Contains(16));
}

TEST(WeekRangeTest, EvenWeeksSkipOddWeeks) {
    const WeekRange weeks{2, 16, WeekParity::kEven};
    EXPECT_TRUE(weeks.Contains(2));
    EXPECT_TRUE(weeks.Contains(16));
    EXPECT_FALSE(weeks.Contains(3));
}

TEST(WeekRangeTest, NormalizeSwapsReversedRange) {
    WeekRange weeks{16, 1, WeekParity::kEvery};
    EXPECT_TRUE(weeks.Normalize());
    EXPECT_EQ(weeks.from, 1);
    EXPECT_EQ(weeks.to, 16);
}

TEST(WeekRangeTest, NormalizeRejectsOutOfRange) {
    WeekRange too_early{0, 16, WeekParity::kEvery};
    EXPECT_FALSE(too_early.Normalize());

    WeekRange too_late{1, 40, WeekParity::kEvery};
    EXPECT_FALSE(too_late.Normalize());
}

// ------------------------- 课次 -------------------------

TEST(CourseSessionTest, AcceptsReasonableValues) {
    EXPECT_TRUE(MakeSession(3, 1, 2, WeekParity::kEvery).Valid());
}

TEST(CourseSessionTest, RejectsInvalidDayOfWeek) {
    EXPECT_FALSE(MakeSession(0, 1, 2, WeekParity::kEvery).Valid());
    EXPECT_FALSE(MakeSession(8, 1, 2, WeekParity::kEvery).Valid());
}

TEST(CourseSessionTest, RejectsReversedPeriods) {
    EXPECT_FALSE(MakeSession(1, 5, 3, WeekParity::kEvery).Valid());
}

TEST(CourseSessionTest, RejectsPeriodBeyondLimit) {
    EXPECT_FALSE(MakeSession(1, 1, 20, WeekParity::kEvery).Valid());
}

TEST(CourseSessionTest, PeriodCountCountsConnectedPeriods) {
    EXPECT_EQ(MakeSession(1, 3, 5, WeekParity::kEvery).PeriodCount(), 3);
    EXPECT_EQ(MakeSession(1, 3, 3, WeekParity::kEvery).PeriodCount(), 1);
}

// ------------------------- 课程 -------------------------

TEST(CourseTest, WeeklyPeriodsSumsAllSessions) {
    Course course = MakeSampleCourse();
    course.sessions.push_back(MakeSession(4, 1, 2, WeekParity::kEvery));
    EXPECT_EQ(course.WeeklyPeriods(), 4);
}

TEST(CourseTest, ValidRequiresNameAndSemester) {
    Course course = MakeSampleCourse();
    EXPECT_TRUE(course.Valid());

    course.name.clear();
    EXPECT_FALSE(course.Valid());

    course = MakeSampleCourse();
    course.semester.clear();
    EXPECT_FALSE(course.Valid());
}

TEST(CourseTest, IsSameCoursePrefersCourseCode) {
    Course lhs = MakeSampleCourse();
    Course rhs = MakeSampleCourse();
    EXPECT_TRUE(IsSameCourse(lhs, rhs));

    rhs.code = "CS9999";
    EXPECT_FALSE(IsSameCourse(lhs, rhs));
}

TEST(CourseTest, IsSameCourseFallsBackToNameAndTeacher) {
    Course lhs = MakeSampleCourse();
    Course rhs = MakeSampleCourse();
    lhs.code.clear();
    rhs.code.clear();
    EXPECT_TRUE(IsSameCourse(lhs, rhs));

    rhs.teacher = "李老师";
    EXPECT_FALSE(IsSameCourse(lhs, rhs));
}

// ------------------------- 时间冲突 -------------------------

TEST(TimeConflictTest, DetectsOverlapOnSameDay) {
    const auto morning = MakeSession(1, 1, 2, WeekParity::kEvery);
    const auto next = MakeSession(1, 2, 3, WeekParity::kEvery);
    EXPECT_TRUE(HasTimeConflict(morning, next));
}

TEST(TimeConflictTest, DifferentDayNeverConflicts) {
    const auto monday = MakeSession(1, 1, 2, WeekParity::kEvery);
    const auto tuesday = MakeSession(2, 1, 2, WeekParity::kEvery);
    EXPECT_FALSE(HasTimeConflict(monday, tuesday));
}

TEST(TimeConflictTest, SeparatePeriodsDoNotConflict) {
    const auto first = MakeSession(1, 1, 2, WeekParity::kEvery);
    const auto second = MakeSession(1, 3, 4, WeekParity::kEvery);
    EXPECT_FALSE(HasTimeConflict(first, second));
}

TEST(TimeConflictTest, OddAndEvenWeeksDoNotConflict) {
    const auto odd = MakeSession(1, 1, 2, WeekParity::kOdd);
    const auto even = MakeSession(1, 1, 2, WeekParity::kEven);
    EXPECT_FALSE(HasTimeConflict(odd, even));
}

TEST(TimeConflictTest, EveryWeekConflictsWithAnyParity) {
    const auto every = MakeSession(1, 1, 2, WeekParity::kEvery);
    const auto odd = MakeSession(1, 1, 2, WeekParity::kOdd);
    EXPECT_TRUE(HasTimeConflict(every, odd));
}

// ------------------------- JSON 转换 -------------------------

TEST(JsonConvertTest, RoundTripKeepsAllFields) {
    const Course original = MakeSampleCourse();
    const nlohmann::json encoded = ToJson(original);

    Course decoded;
    std::string error;
    ASSERT_TRUE(FromJson(encoded, decoded, error)) << error;

    EXPECT_EQ(decoded.name, original.name);
    EXPECT_EQ(decoded.code, original.code);
    EXPECT_EQ(decoded.teacher, original.teacher);
    EXPECT_DOUBLE_EQ(decoded.credits, original.credits);
    EXPECT_EQ(decoded.semester, original.semester);
    ASSERT_EQ(decoded.sessions.size(), 1U);
    EXPECT_EQ(decoded.sessions[0].day_of_week, 2);
    EXPECT_EQ(decoded.sessions[0].start_period, 3);
    EXPECT_EQ(decoded.sessions[0].end_period, 4);
    EXPECT_EQ(decoded.sessions[0].location, "教学楼 A301");
    EXPECT_EQ(decoded.sessions[0].weeks.parity, WeekParity::kEvery);
    EXPECT_TRUE(decoded.Valid());
}

TEST(JsonConvertTest, RejectsUnknownParityValue) {
    const nlohmann::json payload = {
        {"dayOfWeek", 2},
        {"startPeriod", 1},
        {"endPeriod", 2},
        {"weeks", {{"from", 1}, {"to", 16}, {"parity", 9}}},
    };

    CourseSession session;
    std::string error;
    EXPECT_FALSE(FromJson(payload, session, error));
    EXPECT_FALSE(error.empty());
}

TEST(JsonConvertTest, DefaultsNewCoursesToManualSource) {
    // 通过接口新增的课默认算「手动添加」，不会被同步触碰
    const nlohmann::json payload = {
        {"name", "人工智能讲座"},
        {"semester", "2026-2027-1"},
    };
    Course course;
    std::string error;
    ASSERT_TRUE(FromJson(payload, course, error)) << error;
    EXPECT_EQ(course.source, kSourceManual);
}

TEST(JsonConvertTest, PreservesExplicitSource) {
    const nlohmann::json payload = {
        {"name", "操作系统"},
        {"semester", "2026-2027-1"},
        {"source", "portal"},
    };
    Course course;
    std::string error;
    ASSERT_TRUE(FromJson(payload, course, error)) << error;
    EXPECT_EQ(course.source, kSourcePortal);
}

TEST(JsonConvertTest, RejectsNonArraySessions) {
    const nlohmann::json payload = {
        {"name", "操作系统"},
        {"semester", "2026-2027-1"},
        {"sessions", "not-an-array"},
    };

    Course course;
    std::string error;
    EXPECT_FALSE(FromJson(payload, course, error));
    EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace campus
