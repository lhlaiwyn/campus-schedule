#include <string>

#include <gtest/gtest.h>

#include "campus/net/http_parser.h"

namespace campus::net {
namespace {

const char* kGetRequest =
    "GET /api/courses?semester=2026-2027-1 HTTP/1.1\r\n"
    "Host: 127.0.0.1\r\n"
    "Accept: application/json\r\n"
    "\r\n";

TEST(HttpRequestParserTest, ParsesSimpleGet) {
    ByteBuffer buffer;
    HttpRequestParser parser;
    buffer.Append(kGetRequest);

    EXPECT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
    EXPECT_TRUE(buffer.Empty());

    const HttpRequest& request = parser.Request();
    EXPECT_EQ(request.method, "GET");
    // path 不含查询串，原始请求目标另存在 target 里
    EXPECT_EQ(request.path, "/api/courses");
    EXPECT_EQ(request.target, "/api/courses?semester=2026-2027-1");
    EXPECT_EQ(request.version, "HTTP/1.1");
    EXPECT_TRUE(request.keep_alive);
    EXPECT_TRUE(request.body.empty());
    EXPECT_EQ(request.content_length, 0U);
}

TEST(HttpRequestParserTest, ParsesQueryString) {
    ByteBuffer buffer;
    HttpRequestParser parser;
    buffer.Append("GET /api/courses?semester=2026-2027-1&page=2 HTTP/1.1\r\n\r\n");
    ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);

    const HttpRequest& request = parser.Request();
    EXPECT_EQ(request.path, "/api/courses");
    ASSERT_EQ(request.query.size(), 2U);
    EXPECT_EQ(request.query[0].first, "semester");   // 保序
    EXPECT_EQ(request.query[0].second, "2026-2027-1");
    EXPECT_EQ(request.query[1].first, "page");
    EXPECT_EQ(request.query[1].second, "2");

    EXPECT_EQ(request.Query("semester"), "2026-2027-1");
    EXPECT_EQ(request.Query("page"), "2");
    EXPECT_TRUE(request.HasQuery("semester"));
    EXPECT_FALSE(request.HasQuery("missing"));
    EXPECT_TRUE(request.Query("missing").empty());
}

TEST(HttpRequestParserTest, NoQueryStringLeavesQueryEmpty) {
    ByteBuffer buffer;
    HttpRequestParser parser;
    buffer.Append("GET /api/health HTTP/1.1\r\n\r\n");
    ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
    EXPECT_EQ(parser.Request().path, "/api/health");
    EXPECT_EQ(parser.Request().target, "/api/health");
    EXPECT_TRUE(parser.Request().query.empty());
}

// 查询串的边界情况：空值、只有 key、空段、非法百分号序列、'+' 当空格。
// 这些都必须不崩、不抛异常，且行为可预测。
TEST(HttpRequestParserTest, QueryStringEdgeCases) {
    {
        ByteBuffer buffer;
        HttpRequestParser parser;
        buffer.Append("GET /x?a=1&&b=2& HTTP/1.1\r\n\r\n");
        ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
        // 空段被跳过
        ASSERT_EQ(parser.Request().query.size(), 2U);
        EXPECT_EQ(parser.Request().query[0].first, "a");
        EXPECT_EQ(parser.Request().query[1].first, "b");
    }
    {
        ByteBuffer buffer;
        HttpRequestParser parser;
        buffer.Append("GET /x?flag HTTP/1.1\r\n\r\n");
        ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
        ASSERT_EQ(parser.Request().query.size(), 1U);
        EXPECT_EQ(parser.Request().query[0].first, "flag");
        EXPECT_EQ(parser.Request().query[0].second, std::string{});
        EXPECT_FALSE(parser.Request().HasQuery("flag"));  // 值为空
    }
    {
        ByteBuffer buffer;
        HttpRequestParser parser;
        buffer.Append("GET /x?a= HTTP/1.1\r\n\r\n");
        ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
        ASSERT_EQ(parser.Request().query.size(), 1U);
        EXPECT_EQ(parser.Request().query[0].first, "a");
        EXPECT_EQ(parser.Request().query[0].second, std::string{});
    }
    {
        // '?' 之后什么都没有
        ByteBuffer buffer;
        HttpRequestParser parser;
        buffer.Append("GET /x? HTTP/1.1\r\n\r\n");
        ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
        EXPECT_EQ(parser.Request().path, "/x");
        EXPECT_TRUE(parser.Request().query.empty());
    }
}

TEST(PercentDecodeTest, DecodesPercentAndPlus) {
    EXPECT_EQ(PercentDecode("2026-2027-1"), "2026-2027-1");
    EXPECT_EQ(PercentDecode("hello%20world"), "hello world");
    EXPECT_EQ(PercentDecode("a+b"), "a b");          // '+' 按表单约定是空格
    EXPECT_EQ(PercentDecode("%E6%95%B0%E5%AD%A6"), "数学");  // UTF-8 中文
    EXPECT_EQ(PercentDecode("%2f%2F"), "//");
    // 非法序列原样保留，不抛异常
    EXPECT_EQ(PercentDecode("%ZZ"), "%ZZ");
    EXPECT_EQ(PercentDecode("%2"), "%2");
    EXPECT_EQ(PercentDecode("100%"), "100%");
    EXPECT_EQ(PercentDecode(""), "");
}

TEST(ParseRequestTargetTest, SplitsPathAndQuery) {
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;

    ParseRequestTarget("/a/b?x=1&y=2", path, query);
    EXPECT_EQ(path, "/a/b");
    ASSERT_EQ(query.size(), 2U);
    EXPECT_EQ(query[0].first, "x");
    EXPECT_EQ(query[0].second, "1");
    EXPECT_EQ(query[1].first, "y");
    EXPECT_EQ(query[1].second, "2");

    // 值里带 '='：只按第一个 '=' 切分
    ParseRequestTarget("/x?token=a=b", path, query);
    EXPECT_EQ(path, "/x");
    ASSERT_EQ(query.size(), 1U);
    EXPECT_EQ(query[0].first, "token");
    EXPECT_EQ(query[0].second, "a=b");

    // 没有查询串时 query 必须被清空（同一个 vector 复用）
    ParseRequestTarget("/plain", path, query);
    EXPECT_EQ(path, "/plain");
    EXPECT_TRUE(query.empty());
}

TEST(HttpRequestParserTest, HeaderLookupIsCaseInsensitive) {
    ByteBuffer buffer;
    HttpRequestParser parser;
    buffer.Append(kGetRequest);
    ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);

    EXPECT_EQ(parser.Request().Header("host"), "127.0.0.1");
    EXPECT_EQ(parser.Request().Header("HOST"), "127.0.0.1");
    EXPECT_EQ(parser.Request().Header("Accept"), "application/json");
    EXPECT_TRUE(parser.Request().Header("missing").empty());
}

TEST(HttpRequestParserTest, ParsesBodyIncrementally) {
    ByteBuffer buffer;
    HttpRequestParser parser;

    buffer.Append("POST /api/courses HTTP/1.1\r\nContent-Length: 11\r\n\r\n");
    EXPECT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kIncomplete);

    buffer.Append("hello", 5);
    EXPECT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kIncomplete);

    buffer.Append(" world", 6);
    EXPECT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
    EXPECT_EQ(parser.Request().body, "hello world");
    EXPECT_EQ(parser.Request().content_length, 11U);
}

TEST(HttpRequestParserTest, KeepAliveSemantics) {
    {
        ByteBuffer buffer;
        HttpRequestParser parser;
        buffer.Append("GET / HTTP/1.1\r\n\r\n");
        ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
        EXPECT_TRUE(parser.Request().keep_alive);
    }
    {
        ByteBuffer buffer;
        HttpRequestParser parser;
        buffer.Append("GET / HTTP/1.1\r\nConnection: close\r\n\r\n");
        ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
        EXPECT_FALSE(parser.Request().keep_alive);
    }
    {
        ByteBuffer buffer;
        HttpRequestParser parser;
        buffer.Append("GET / HTTP/1.0\r\n\r\n");
        ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
        EXPECT_FALSE(parser.Request().keep_alive);
    }
    {
        ByteBuffer buffer;
        HttpRequestParser parser;
        buffer.Append("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
        ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
        EXPECT_TRUE(parser.Request().keep_alive);
    }
}

TEST(HttpRequestParserTest, RejectsMalformedRequestLine) {
    ByteBuffer buffer;
    HttpRequestParser parser;
    buffer.Append("THIS_IS_NOT_A_VALID_REQUEST_LINE\r\n\r\n");
    EXPECT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kError);
    EXPECT_FALSE(parser.Error().empty());
}

TEST(HttpRequestParserTest, RejectsHeaderWithoutColon) {
    ByteBuffer buffer;
    HttpRequestParser parser;
    buffer.Append("GET / HTTP/1.1\r\nNoColonHere\r\n\r\n");
    EXPECT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kError);
}

TEST(HttpRequestParserTest, RejectsNegativeContentLength) {
    ByteBuffer buffer;
    HttpRequestParser parser;
    buffer.Append("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n");
    EXPECT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kError);
}

TEST(HttpRequestParserTest, ResetAllowsReuse) {
    ByteBuffer buffer;
    HttpRequestParser parser;

    buffer.Append(kGetRequest);
    ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
    EXPECT_EQ(parser.Request().method, "GET");

    parser.Reset();
    buffer.Append("POST /x HTTP/1.1\r\nContent-Length: 2\r\n\r\nab");
    ASSERT_EQ(parser.Parse(buffer), HttpRequestParser::Status::kComplete);
    EXPECT_EQ(parser.Request().method, "POST");
    EXPECT_EQ(parser.Request().body, "ab");
}

}  // namespace
}  // namespace campus::net
