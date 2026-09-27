#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "campus/infra/base64.h"
#include "campus/infra/hmac.h"
#include "campus/infra/jwt.h"
#include "campus/infra/portal_session_store.h"
#include "campus/infra/rate_limiter.h"
#include "campus/portal/mock_portal_adapter.h"
#include "campus/service/auth_service.h"

namespace campus {
namespace {

// 与 config.h 里的开发默认密钥保持一致，JWT 参考向量就是用它算的
constexpr const char* kTestSecret = "dev-secret-change-me";

// ========================= Base64URL =========================

TEST(Base64UrlTest, MatchesRfc4648Vectors) {
    EXPECT_EQ(Base64UrlEncode(""), "");
    EXPECT_EQ(Base64UrlEncode("f"), "Zg");
    EXPECT_EQ(Base64UrlEncode("fo"), "Zm8");
    EXPECT_EQ(Base64UrlEncode("foo"), "Zm9v");
    EXPECT_EQ(Base64UrlEncode("foob"), "Zm9vYg");
    EXPECT_EQ(Base64UrlEncode("fooba"), "Zm9vYmE");
    EXPECT_EQ(Base64UrlEncode("foobar"), "Zm9vYmFy");
}

TEST(Base64UrlTest, UsesUrlSafeAlphabet) {
    // 标准 base64 的 + / 在 URL 安全变体里必须是 - _
    EXPECT_EQ(Base64UrlEncode(std::string("\xfb\xff", 2)), "-_8");
    EXPECT_EQ(Base64UrlEncode(std::string("\xff\xff\xff", 3)), "____");
}

TEST(Base64UrlTest, RoundTripsBinaryData) {
    std::string payload;
    for (int i = 0; i < 256; ++i) {
        payload.push_back(static_cast<char>(i));
    }
    const std::string encoded = Base64UrlEncode(payload);
    std::string decoded;
    ASSERT_TRUE(Base64UrlDecode(encoded, decoded));
    EXPECT_EQ(decoded, payload);
}

TEST(Base64UrlTest, AcceptsPadding) {
    std::string decoded;
    ASSERT_TRUE(Base64UrlDecode("Zg==", decoded));
    EXPECT_EQ(decoded, "f");
}

TEST(Base64UrlTest, RejectsInvalidInput) {
    std::string out;
    EXPECT_FALSE(Base64UrlDecode("a", out));     // 长度 % 4 == 1
    EXPECT_FALSE(Base64UrlDecode("****", out));  // 非法字符
    EXPECT_FALSE(Base64UrlDecode("Zh", out));    // 尾部多余位不为 0
}

// ========================= HMAC =========================

TEST(HmacTest, MatchesReferenceValues) {
    EXPECT_EQ(HmacSha256Hex("key", "The quick brown fox jumps over the lazy dog"),
              "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    EXPECT_EQ(HmacSha256Hex("", ""),
              "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad");
    EXPECT_EQ(HmacSha256Hex("jwgl-secret", "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIyMDI0MDAxIn0"),
              "e8e3a0cec21fa5d0023ebaa9574de02aa32cd5e6dcef38e38e06498e7aa5053b");
}

TEST(HmacTest, DigestIsThirtyTwoBytes) {
    EXPECT_EQ(HmacSha256("key", "data").size(), 32U);
    EXPECT_EQ(HmacSha256Hex("key", "data").size(), 64U);
}

// ========================= JWT =========================

TEST(JwtTest, ProducesExactTokenMatchingReferenceImplementation) {
    JwtClaims claims;
    claims.subject = "2024001";
    claims.issued_at = 1758888000;
    claims.expires_at = 1758891600;

    auto token = SignJwt(claims, kTestSecret);
    ASSERT_TRUE(token.ok()) << token.error().message;

    // 这个期望值是用 Python 的 json + hmac + hashlib + base64 独立算出来的，
    // 用来确认我们的实现与标准实现逐字节一致
    EXPECT_EQ(token.value(),
              "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
              "eyJleHAiOjE3NTg4OTE2MDAsImlhdCI6MTc1ODg4ODAwMCwic3ViIjoiMjAyNDAwMSJ9."
              "O-0wuvb6_WKvcWsgIbY9k76bL4FH-2hA9YPbURUzyWc");
}

TEST(JwtTest, VerifiesOwnTokenAndExtractsClaims) {
    const JwtClaims claims{"2024001", 1758888000, 1758891600};
    auto token = SignJwt(claims, kTestSecret);
    ASSERT_TRUE(token.ok()) << token.error().message;

    auto verified = VerifyJwt(token.value(), kTestSecret, 1758888100);
    ASSERT_TRUE(verified.ok()) << verified.error().message;
    EXPECT_EQ(verified.value().subject, "2024001");
    EXPECT_EQ(verified.value().issued_at, 1758888000);
    EXPECT_EQ(verified.value().expires_at, 1758891600);
}

TEST(JwtTest, RejectsTamperedPayload) {
    const JwtClaims claims{"2024001", 100, 999999};
    auto token = SignJwt(claims, kTestSecret);
    ASSERT_TRUE(token.ok());

    // 把 payload 换成攻击者想要的内容，但保留原来的签名
    const std::size_t first_dot = token.value().find('.');
    const std::size_t second_dot = token.value().find('.', first_dot + 1);
    const std::string evil =
        Base64UrlEncode(R"({"exp":9999999999,"iat":100,"sub":"hacker"})");
    const std::string forged =
        token.value().substr(0, first_dot + 1) + evil + token.value().substr(second_dot);

    EXPECT_FALSE(VerifyJwt(forged, kTestSecret, 200).ok());
}

TEST(JwtTest, RejectsExpiredToken) {
    const JwtClaims claims{"2024001", 100, 200};
    auto token = SignJwt(claims, kTestSecret);
    ASSERT_TRUE(token.ok());

    EXPECT_TRUE(VerifyJwt(token.value(), kTestSecret, 199).ok());
    EXPECT_FALSE(VerifyJwt(token.value(), kTestSecret, 200).ok());  // 到点即过期
    EXPECT_FALSE(VerifyJwt(token.value(), kTestSecret, 500).ok());
}

TEST(JwtTest, RejectsTokenSignedWithAnotherSecret) {
    const JwtClaims claims{"2024001", 100, 999999};
    auto token = SignJwt(claims, "another-secret-key-1234567");
    ASSERT_TRUE(token.ok());
    EXPECT_FALSE(VerifyJwt(token.value(), kTestSecret, 200).ok());
}

TEST(JwtTest, RejectsTokenClaimingAnotherAlgorithm) {
    const std::string header_b64 = Base64UrlEncode(R"({"alg":"HS512","typ":"JWT"})");
    const std::string payload_b64 =
        Base64UrlEncode(R"({"exp":999999,"iat":100,"sub":"someone"})");
    // 故意用 HS256 正确签名，只有 alg 字段是假的
    const std::string signature =
        Base64UrlEncode(HmacSha256(kTestSecret, header_b64 + "." + payload_b64));

    auto verified = VerifyJwt(header_b64 + "." + payload_b64 + "." + signature, kTestSecret, 200);
    EXPECT_FALSE(verified.ok());
    EXPECT_NE(verified.error().message.find("alg"), std::string::npos);
}

TEST(JwtTest, RejectsMalformedTokens) {
    EXPECT_FALSE(VerifyJwt("", kTestSecret, 100).ok());
    EXPECT_FALSE(VerifyJwt("onlyone", kTestSecret, 100).ok());
    EXPECT_FALSE(VerifyJwt("a.b", kTestSecret, 100).ok());
    EXPECT_FALSE(VerifyJwt("a.b.c.d", kTestSecret, 100).ok());
    EXPECT_FALSE(VerifyJwt("..", kTestSecret, 100).ok());
}

TEST(JwtTest, RefusesToSignWithShortSecretOrInvalidClaims) {
    const JwtClaims ok_claims{"2024001", 100, 999999};

    auto weak = SignJwt(ok_claims, "short");
    EXPECT_FALSE(weak.ok());
    EXPECT_NE(weak.error().message.find("密钥"), std::string::npos);

    const JwtClaims no_subject{"", 100, 999999};
    EXPECT_FALSE(SignJwt(no_subject, kTestSecret).ok());

    const JwtClaims zero_lifetime{"2024001", 100, 100};
    EXPECT_FALSE(SignJwt(zero_lifetime, kTestSecret).ok());
}

// ========================= 教务系统会话缓存 =========================

TEST(PortalSessionStoreTest, StoresAndReturnsToken) {
    PortalSessionStore store;
    store.Put("2024001", "jwgl-token-1", 2000);

    auto token = store.Get("2024001", 1500);
    ASSERT_TRUE(token.ok()) << token.error().message;
    EXPECT_EQ(token.value(), "jwgl-token-1");
    EXPECT_EQ(store.Size(), 1U);
}

TEST(PortalSessionStoreTest, ExpiredSessionIsRejectedAndDropped) {
    PortalSessionStore store;
    store.Put("2024001", "jwgl-token-1", 2000);

    auto expired = store.Get("2024001", 2000);  // 到点即过期
    EXPECT_FALSE(expired.ok());
    EXPECT_EQ(expired.error().code, ErrorCode::kUnauthorized);
    EXPECT_EQ(store.Size(), 0U);  // 顺手清掉
}

TEST(PortalSessionStoreTest, MissingSessionFails) {
    PortalSessionStore store;
    auto missing = store.Get("nobody", 100);
    EXPECT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::kUnauthorized);
}

TEST(PortalSessionStoreTest, RemoveAndPurge) {
    PortalSessionStore store;
    store.Put("a", "t-a", 1000);
    store.Put("b", "t-b", 2000);
    store.Put("c", "t-c", 3000);

    EXPECT_TRUE(store.Remove("a"));
    EXPECT_FALSE(store.Remove("a"));

    EXPECT_EQ(store.PurgeExpired(2000), 1U);  // b 到期（c 还没）
    EXPECT_EQ(store.Size(), 1U);
}

TEST(PortalSessionStoreTest, IgnoresEmptyInput) {
    PortalSessionStore store;
    store.Put("", "token", 9999);
    store.Put("2024001", "", 9999);
    EXPECT_EQ(store.Size(), 0U);
}

// ========================= 登录服务 =========================

TEST(AuthServiceTest, LoginIssuesTokenAndCachesPortalSession) {
    MockPortalAdapter portal;
    PortalSessionStore sessions;
    AuthService auth(portal, sessions, kTestSecret, 3600);

    PortalCredentials credentials;
    credentials.student_id = "2024001";
    credentials.password = "secret123";

    const std::int64_t now = 1758888000;
    auto token = auth.Login(credentials, now);
    ASSERT_TRUE(token.ok()) << token.error().message;

    auto verified = auth.Verify(token.value(), now + 10);
    ASSERT_TRUE(verified.ok()) << verified.error().message;
    EXPECT_EQ(verified.value().student_id, "2024001");
    EXPECT_EQ(verified.value().expires_at, now + 3600);

    // 关键点：教务系统会话被缓存下来，同步时不必再提交密码
    EXPECT_EQ(sessions.Size(), 1U);
    auto portal_token = auth.PortalTokenFor("2024001", now + 10);
    ASSERT_TRUE(portal_token.ok()) << portal_token.error().message;
    EXPECT_FALSE(portal_token.value().empty());

    // 令牌与教务系统会话都在同一时刻过期
    EXPECT_FALSE(auth.Verify(token.value(), now + 3600).ok());
    EXPECT_FALSE(auth.PortalTokenFor("2024001", now + 3600).ok());
}

TEST(AuthServiceTest, LoginFailureCachesNothing) {
    MockPortalAdapter portal;
    PortalSessionStore sessions;
    AuthService auth(portal, sessions, kTestSecret, 3600);

    PortalCredentials bad;
    bad.student_id = "2024001";
    bad.password = "123";  // 模拟教务系统要求至少 6 位

    EXPECT_FALSE(auth.Login(bad, 1000).ok());
    EXPECT_EQ(sessions.Size(), 0U);
}

// ========================= 限流 =========================

TEST(RateLimiterTest, AllowsBurstThenThrottles) {
    RateLimiter limiter(10.0, 3.0);
    double retry_after = 0.0;

    EXPECT_TRUE(limiter.Allow("student-1", 1000, retry_after));
    EXPECT_TRUE(limiter.Allow("student-1", 1000, retry_after));
    EXPECT_TRUE(limiter.Allow("student-1", 1000, retry_after));

    EXPECT_FALSE(limiter.Allow("student-1", 1000, retry_after));
    EXPECT_GT(retry_after, 0.0);
}

TEST(RateLimiterTest, RefillsOverTime) {
    RateLimiter limiter(10.0, 3.0);
    double retry_after = 0.0;

    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(limiter.Allow("student-1", 1000, retry_after));
    }
    EXPECT_FALSE(limiter.Allow("student-1", 1000, retry_after));

    // 100 毫秒补回 1 个令牌
    EXPECT_TRUE(limiter.Allow("student-1", 1100, retry_after));
    EXPECT_FALSE(limiter.Allow("student-1", 1100, retry_after));

    // 停一会儿会补满，但不会超过桶容量
    EXPECT_TRUE(limiter.Allow("student-1", 60000, retry_after));
    EXPECT_TRUE(limiter.Allow("student-1", 60000, retry_after));
    EXPECT_TRUE(limiter.Allow("student-1", 60000, retry_after));
    EXPECT_FALSE(limiter.Allow("student-1", 60000, retry_after));
}

TEST(RateLimiterTest, KeysAreIndependent) {
    RateLimiter limiter(1.0, 1.0);
    double retry_after = 0.0;

    EXPECT_TRUE(limiter.Allow("alice", 0, retry_after));
    EXPECT_FALSE(limiter.Allow("alice", 0, retry_after));
    EXPECT_TRUE(limiter.Allow("bob", 0, retry_after));
    EXPECT_EQ(limiter.TrackedKeys(), 2U);
}

TEST(RateLimiterTest, EvictsOldKeysWhenMapGrows) {
    RateLimiter limiter(1.0, 1.0);
    limiter.SetMaxKeys(2);
    double retry_after = 0.0;

    EXPECT_TRUE(limiter.Allow("a", 0, retry_after));
    EXPECT_TRUE(limiter.Allow("b", 0, retry_after));
    EXPECT_TRUE(limiter.Allow("c", 0, retry_after));  // 触发淘汰
    EXPECT_LE(limiter.TrackedKeys(), 2U);
}

}  // namespace
}  // namespace campus
