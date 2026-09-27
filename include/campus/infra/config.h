#pragma once

#include <string>

namespace campus {

struct ServerConfig {
    std::string host = "127.0.0.1";
    int port = 8080;
    // 用哪个 HTTP 引擎：
    //   net     = 自研的 epoll 网络库（默认，项目的主角）
    //   httplib = cpp-httplib，作为对照组，用来做性能对比和回归验证
    std::string engine = "net";
    // 自研引擎的 worker 线程数，0 表示按 CPU 核数自动决定（上限 16）
    int workers = 0;
    // 自研引擎是否用边缘触发（EPOLLET）。默认水平触发，语义简单不容易漏事件。
    bool edge_triggered = false;
};

struct DbConfig {
    std::string host = "127.0.0.1";
    int port = 3306;
    std::string user = "campus";
    std::string password = "campus_dev_2026";
    std::string database = "campus_schedule";
    int pool_size = 8;
};

// 教务系统相关的配置
struct PortalConfig {
    // 用哪个适配器：mock（内置模拟）或 http（连接模拟教务系统服务）
    std::string adapter = "mock";
    std::string host = "127.0.0.1";
    int port = 9090;
};

// 开发用的默认 JWT 密钥。生产环境必须用 CAMPUS_AUTH_SECRET 覆盖。
inline constexpr const char* kDefaultAuthSecret = "dev-secret-change-me";

struct AuthConfig {
    std::string secret = kDefaultAuthSecret;
    int token_ttl_seconds = 3600;          // 令牌有效期
    double rate_limit_per_second = 20.0;   // 每个学号每秒允许的请求数
    double rate_limit_burst = 40.0;        // 允许的突发量
};

struct CacheConfig {
    // 默认关闭：没装 Redis 也能直接跑，不会因为连不上而拖慢每个请求。
    // 要在有 Redis 的环境里启用，设置 CAMPUS_CACHE_ENABLED=true。
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = 6379;
    int ttl_seconds = 300;  // 课表变化的频率很低，缓存 5 分钟足够
};

struct AppConfig {
    ServerConfig server;
    DbConfig db;
    PortalConfig portal;
    AuthConfig auth;
    CacheConfig cache;
};

// 从环境变量读取配置，读不到就用默认值。
// 支持：CAMPUS_HOST / CAMPUS_PORT / CAMPUS_DB_HOST / CAMPUS_DB_PORT /
//       CAMPUS_DB_USER / CAMPUS_DB_PASSWORD / CAMPUS_DB_NAME / CAMPUS_DB_POOL_SIZE
//       CAMPUS_PORTAL_ADAPTER / CAMPUS_PORTAL_HOST / CAMPUS_PORTAL_PORT
//       CAMPUS_AUTH_SECRET / CAMPUS_AUTH_TTL_SECONDS
//       CAMPUS_RATE_LIMIT_RPS / CAMPUS_RATE_LIMIT_BURST
//       CAMPUS_CACHE_ENABLED / CAMPUS_CACHE_HOST / CAMPUS_CACHE_PORT / CAMPUS_CACHE_TTL_SECONDS
AppConfig LoadConfigFromEnv();

// 当前是否还在用内置的开发密钥（启动时据此告警）
bool UsesDefaultAuthSecret(const AppConfig& config);

// 把配置打印成一行摘要（不含密码），方便排查问题
std::string DescribeConfig(const AppConfig& config);

}  // namespace campus
