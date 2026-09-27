#include <string>

#include <gtest/gtest.h>

#include "campus/portal/portal_json.h"

namespace campus {
namespace {

const char* kCaptchaBody = R"JSON({
  "captchaId": "captcha-1",
  "captchaText": "8A2F",
  "salt": "jwgl-salt:"
})JSON";

const char* kScheduleBody = R"JSON({
  "semester": "2026-2027-1",
  "entries": [
    {"courseName": "操作系统", "courseCode": "CS2001", "teacher": "张伟",
     "location": "教学楼A301", "dayOfWeek": 2, "startPeriod": 3, "endPeriod": 4,
     "weeksText": "1-16周", "credits": 3.5},
    {"courseName": "数据结构", "courseCode": "CS2002", "teacher": "李娜",
     "location": "教学楼B102", "dayOfWeek": 4, "startPeriod": 1, "endPeriod": 2,
     "weeksText": "1-16周(单)", "credits": 4.0}
  ]
})JSON";

// ------------------------- 验证码响应 -------------------------

TEST(PortalJsonTest, ParsesCaptchaChallenge) {
    auto challenge = ParseCaptchaJson(kCaptchaBody);
    ASSERT_TRUE(challenge.ok()) << challenge.error().message;
    EXPECT_EQ(challenge.value().salt, "jwgl-salt:");
    EXPECT_EQ(challenge.value().captcha_text, "8A2F");
    EXPECT_EQ(challenge.value().captcha_id, "captcha-1");
}

TEST(PortalJsonTest, RejectsCaptchaWithoutSalt) {
    auto challenge = ParseCaptchaJson(R"JSON({"captchaText": "8A2F"})JSON");
    EXPECT_FALSE(challenge.ok());
    EXPECT_NE(challenge.error().message.find("salt"), std::string::npos);
}

TEST(PortalJsonTest, RejectsMalformedCaptchaBody) {
    EXPECT_FALSE(ParseCaptchaJson("not json at all").ok());
    EXPECT_FALSE(ParseCaptchaJson("[]").ok());
}

// ------------------------- 登录响应 -------------------------

TEST(PortalJsonTest, ParsesTokenFromLoginResponse) {
    auto token = ParseTokenJson(R"JSON({"token": "jwgl-token-7"})JSON");
    ASSERT_TRUE(token.ok()) << token.error().message;
    EXPECT_EQ(token.value(), "jwgl-token-7");
}

TEST(PortalJsonTest, RejectsLoginResponseWithoutToken) {
    auto token = ParseTokenJson(R"JSON({"studentId": "2024001"})JSON");
    EXPECT_FALSE(token.ok());
}

// ------------------------- 课表响应 -------------------------

TEST(PortalJsonTest, ParsesScheduleEntries) {
    auto entries = ParseScheduleJson(kScheduleBody);
    ASSERT_TRUE(entries.ok()) << entries.error().message;
    ASSERT_EQ(entries.value().size(), 2U);

    EXPECT_EQ(entries.value()[0].course_name, "操作系统");
    EXPECT_EQ(entries.value()[0].course_code, "CS2001");
    EXPECT_EQ(entries.value()[0].day_of_week, 2);
    EXPECT_EQ(entries.value()[0].start_period, 3);
    EXPECT_EQ(entries.value()[0].end_period, 4);
    EXPECT_EQ(entries.value()[0].weeks_text, "1-16周");
    EXPECT_DOUBLE_EQ(entries.value()[0].credits, 3.5);

    EXPECT_EQ(entries.value()[1].course_name, "数据结构");
    EXPECT_EQ(entries.value()[1].weeks_text, "1-16周(单)");
}

TEST(PortalJsonTest, AcceptsEmptySchedule) {
    auto entries = ParseScheduleJson(R"JSON({"semester": "2026-2027-1", "entries": []})JSON");
    ASSERT_TRUE(entries.ok()) << entries.error().message;
    EXPECT_TRUE(entries.value().empty());
}

TEST(PortalJsonTest, RejectsMissingEntriesArray) {
    auto entries = ParseScheduleJson(R"JSON({"semester": "2026-2027-1"})JSON");
    EXPECT_FALSE(entries.ok());
    EXPECT_NE(entries.error().message.find("entries"), std::string::npos);
}

TEST(PortalJsonTest, RejectsEntryWithoutCourseName) {
    auto entries = ParseScheduleJson(R"JSON({"entries": [
        {"courseCode": "CS2001", "dayOfWeek": 2, "startPeriod": 1, "endPeriod": 2,
         "weeksText": "1-16周"}
    ]})JSON");
    EXPECT_FALSE(entries.ok());
    EXPECT_NE(entries.error().message.find("课程名称"), std::string::npos);
}

TEST(PortalJsonTest, RejectsEntryWithoutWeeksText) {
    auto entries = ParseScheduleJson(R"JSON({"entries": [
        {"courseName": "操作系统", "dayOfWeek": 2, "startPeriod": 1, "endPeriod": 2}
    ]})JSON");
    EXPECT_FALSE(entries.ok());
    EXPECT_NE(entries.error().message.find("周次"), std::string::npos);
}

TEST(PortalJsonTest, RejectsInvalidDayOfWeek) {
    auto entries = ParseScheduleJson(R"JSON({"entries": [
        {"courseName": "操作系统", "dayOfWeek": 9, "startPeriod": 1, "endPeriod": 2,
         "weeksText": "1-16周"}
    ]})JSON");
    EXPECT_FALSE(entries.ok());
    EXPECT_NE(entries.error().message.find("星期"), std::string::npos);
}

TEST(PortalJsonTest, RejectsReversedPeriods) {
    auto entries = ParseScheduleJson(R"JSON({"entries": [
        {"courseName": "操作系统", "dayOfWeek": 2, "startPeriod": 5, "endPeriod": 3,
         "weeksText": "1-16周"}
    ]})JSON");
    EXPECT_FALSE(entries.ok());
    EXPECT_NE(entries.error().message.find("节次"), std::string::npos);
}

TEST(PortalJsonTest, ErrorMessageMentionsEntryIndex) {
    auto entries = ParseScheduleJson(R"JSON({"entries": [
        {"courseName": "正常课程", "dayOfWeek": 2, "startPeriod": 1, "endPeriod": 2,
         "weeksText": "1-16周"},
        {"courseName": "", "dayOfWeek": 2, "startPeriod": 1, "endPeriod": 2,
         "weeksText": "1-16周"}
    ]})JSON");
    ASSERT_FALSE(entries.ok());
    EXPECT_NE(entries.error().message.find("第 2 条"), std::string::npos);
}

// ------------------------- 错误信息提取 -------------------------

TEST(PortalJsonTest, ExtractsMessageFromErrorBody) {
    EXPECT_EQ(ExtractErrorMessage(R"JSON({"message": "验证码错误"})JSON", "默认原因"), "验证码错误");
}

TEST(PortalJsonTest, FallsBackWhenBodyIsNotJson) {
    EXPECT_EQ(ExtractErrorMessage("<html>500</html>", "默认原因"), "默认原因");
}

}  // namespace
}  // namespace campus
