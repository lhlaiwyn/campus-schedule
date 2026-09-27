// 分发层的单元测试。
//
// 这一层的价值就是「不解网络库也能测」：不监听端口、不需要数据库，
// 直接构造 ApiRequest 就能验证路由、鉴权和错误体格式。
#include <string>

#include <gtest/gtest.h>

#include "campus/api/dispatcher.h"

namespace campus {
namespace {

ApiRequest MakeRequest(const std::string& method, const std::string& path,
                       const std::string& target = "") {
    ApiRequest request;
    request.method = method;
    request.path = path;
    request.target = target.empty() ? path : target;
    return request;
}

// ---------- 路由表（纯函数，与 HTTP 引擎无关）----------

TEST(RouteTableTest, MatchesEveryInterface) {
    EXPECT_EQ(MatchRoute("GET", "/api/health").route, Route::kHealth);
    EXPECT_EQ(MatchRoute("GET", "/api/version").route, Route::kVersion);
    EXPECT_EQ(MatchRoute("POST", "/api/auth/login").route, Route::kLogin);
    EXPECT_EQ(MatchRoute("POST", "/api/sync").route, Route::kSync);
    EXPECT_EQ(MatchRoute("GET", "/api/courses").route, Route::kListCourses);
    EXPECT_EQ(MatchRoute("POST", "/api/courses").route, Route::kCreateCourse);
    EXPECT_EQ(MatchRoute("GET", "/api/courses/7").route, Route::kGetCourse);
    EXPECT_EQ(MatchRoute("PUT", "/api/courses/7").route, Route::kUpdateCourse);
    EXPECT_EQ(MatchRoute("DELETE", "/api/courses/7").route, Route::kDeleteCourse);
}

TEST(RouteTableTest, CourseIdIsParsedAlongWithRoute) {
    EXPECT_EQ(MatchRoute("GET", "/api/courses/42").course_id, 42);
    EXPECT_EQ(MatchRoute("PUT", "/api/courses/1234567890").course_id, 1234567890);
    // 不是课程 id 的路由，id 一律是 0
    EXPECT_EQ(MatchRoute("GET", "/api/courses").course_id, 0);
    EXPECT_EQ(MatchRoute("GET", "/api/health").course_id, 0);
}

TEST(RouteTableTest, WrongMethodOrUnknownPathIsNotFound) {
    EXPECT_EQ(MatchRoute("DELETE", "/api/health").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("GET", "/api/sync").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("POST", "/api/courses/1").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("GET", "/api").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("GET", "/api/nope").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("", "").route, Route::kNotFound);
}

TEST(RouteTableTest, CourseIdMustBeAllDigits) {
    EXPECT_EQ(MatchRoute("GET", "/api/courses/abc").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("GET", "/api/courses/1a").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("GET", "/api/courses/-1").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("GET", "/api/courses/").route, Route::kNotFound);
    EXPECT_EQ(MatchRoute("GET", "/api/courses/1/2").route, Route::kNotFound);
    // 20 位数字会溢出 std::stoll：按「路径不存在」处理，不能抛异常
    EXPECT_EQ(MatchRoute("GET", "/api/courses/99999999999999999999").route,
              Route::kNotFound);
}

TEST(DispatcherTest, VersionReportsCurrentEngine) {
    AppContext ctx;
    SetHttpEngineName("net");
    const ApiResponse response = DispatchRequest(MakeRequest("GET", "/api/version"), ctx);

    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.content_type, "application/json; charset=utf-8");
    EXPECT_NE(response.body.find("\"httpEngine\":\"net\""), std::string::npos);

    // 切引擎之后同一份代码要如实上报新引擎
    SetHttpEngineName("httplib");
    const ApiResponse again = DispatchRequest(MakeRequest("GET", "/api/version"), ctx);
    EXPECT_NE(again.body.find("\"httpEngine\":\"httplib\""), std::string::npos);
    SetHttpEngineName("net");
}

TEST(DispatcherTest, HealthReportsDegradedWithoutDatabase) {
    AppContext ctx;  // pool 为空，代表数据库不可用
    const ApiResponse response = DispatchRequest(MakeRequest("GET", "/api/health"), ctx);

    EXPECT_EQ(response.status, 200);
    EXPECT_NE(response.body.find("\"status\":\"degraded\""), std::string::npos);
    EXPECT_NE(response.body.find("\"cacheEnabled\":false"), std::string::npos);
}

TEST(DispatcherTest, UnknownPathReturnsJson404WithPath) {
    AppContext ctx;
    const ApiResponse response = DispatchRequest(MakeRequest("GET", "/api/nope"), ctx);

    EXPECT_EQ(response.status, 404);
    EXPECT_NE(response.body.find("\"code\":404"), std::string::npos);
    EXPECT_NE(response.body.find("\"error\":\"not_found\""), std::string::npos);
    EXPECT_NE(response.body.find("\"path\":\"/api/nope\""), std::string::npos);
}

TEST(DispatcherTest, WrongMethodOnKnownPathIsNotRouted) {
    AppContext ctx;
    // /api/health 只支持 GET，/api/sync 只支持 POST
    EXPECT_EQ(DispatchRequest(MakeRequest("DELETE", "/api/health"), ctx).status, 404);
    EXPECT_EQ(DispatchRequest(MakeRequest("GET", "/api/sync"), ctx).status, 404);
    EXPECT_EQ(DispatchRequest(MakeRequest("DELETE", "/api/courses"), ctx).status, 404);
}

TEST(DispatcherTest, CourseIdMustBeNumeric) {
    AppContext ctx;
    // 非数字的 id 不匹配任何路由（不会走到 stoll 抛异常）
    EXPECT_EQ(DispatchRequest(MakeRequest("GET", "/api/courses/abc"), ctx).status, 404);
    EXPECT_EQ(DispatchRequest(MakeRequest("GET", "/api/courses/1a"), ctx).status, 404);
    EXPECT_EQ(DispatchRequest(MakeRequest("GET", "/api/courses/"), ctx).status, 404);
    // 超长数字溢出时也当成「路径不存在」，而不是 500
    EXPECT_EQ(
        DispatchRequest(MakeRequest("GET", "/api/courses/99999999999999999999"), ctx).status, 404);
}

TEST(DispatcherTest, ProtectedRouteWithoutTokenReturns401) {
    AppContext ctx;
    const ApiResponse response = DispatchRequest(MakeRequest("GET", "/api/courses"), ctx);

    EXPECT_EQ(response.status, 401);
    // 错误体里的 code 必须等于 HTTP 状态码（不能是内部枚举值）
    EXPECT_NE(response.body.find("\"code\":401"), std::string::npos);
    EXPECT_NE(response.body.find("\"error\":\"unauthorized\""), std::string::npos);
    EXPECT_NE(response.body.find("Bearer"), std::string::npos);
}

TEST(DispatcherTest, ErrorBodyCodeEqualsHttpStatus) {
    AppContext ctx;
    // 未知适配器：走到 400 分支，且不需要数据库和适配器实例
    ApiRequest login = MakeRequest("POST", "/api/auth/login");
    login.body = R"({"studentId":"2024001","password":"secret123","adapter":"nope"})";

    const ApiResponse response = DispatchRequest(login, ctx);
    EXPECT_EQ(response.status, 400);
    EXPECT_NE(response.body.find("\"code\":400"), std::string::npos);
    EXPECT_NE(response.body.find("\"error\":\"invalid_argument\""), std::string::npos);
    // 可读原因里要带上「可用的适配器有哪些」，否则用户不知道怎么写
    EXPECT_NE(response.body.find("未知的教务适配器"), std::string::npos);
}

TEST(DispatcherTest, ProtectedRouteWithForgedTokenReturns401) {
    AppContext ctx;
    ApiRequest request = MakeRequest("GET", "/api/courses");
    request.headers.emplace_back("authorization", "Bearer forged.token.value");

    // 头部名小写也要能查到（HTTP 头大小写不敏感）
    EXPECT_EQ(DispatchRequest(request, ctx).status, 401);
}

TEST(DispatcherTest, MissingBearerPrefixReturns401) {
    AppContext ctx;
    ApiRequest request = MakeRequest("POST", "/api/courses");
    request.headers.emplace_back("Authorization", "token-without-bearer");
    EXPECT_EQ(DispatchRequest(request, ctx).status, 401);
}

TEST(DispatcherTest, ApiRequestHeaderLookupIsCaseInsensitive) {
    ApiRequest request;
    request.headers.emplace_back("Content-Type", "application/json");
    EXPECT_EQ(request.Header("content-type"), "application/json");
    EXPECT_EQ(request.Header("CONTENT-TYPE"), "application/json");
    EXPECT_TRUE(request.Header("missing").empty());
}

TEST(DispatcherTest, ApiRequestQueryLookup) {
    ApiRequest request;
    request.query.emplace_back("semester", "2026-2027-1");
    request.query.emplace_back("empty", "");

    EXPECT_EQ(request.Query("semester"), "2026-2027-1");
    EXPECT_TRUE(request.HasQuery("empty"));  // 存在但值为空
    EXPECT_FALSE(request.HasQuery("missing"));
}

}  // namespace
}  // namespace campus
