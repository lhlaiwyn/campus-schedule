#include "campus/infra/config.h"

#include <cstdlib>
#include <sstream>

namespace campus {
namespace {

std::string EnvString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return value;
}

int EnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

double EnvDouble(const char* name, double fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    try {
        return std::stod(value);
    } catch (...) {
        return fallback;
    }
}

bool EnvBool(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    const std::string text(value);
    return text == "1" || text == "true" || text == "TRUE" || text == "yes" || text == "on";
}

}  // namespace

AppConfig LoadConfigFromEnv() {
    AppConfig config;

    config.server.host = EnvString("CAMPUS_HOST", config.server.host);
    config.server.port = EnvInt("CAMPUS_PORT", config.server.port);
    config.server.engine = EnvString("CAMPUS_HTTP_ENGINE", config.server.engine);
    config.server.workers = EnvInt("CAMPUS_HTTP_WORKERS", config.server.workers);
    config.server.edge_triggered = EnvBool("CAMPUS_HTTP_ET", config.server.edge_triggered);

    config.db.host = EnvString("CAMPUS_DB_HOST", config.db.host);
    config.db.port = EnvInt("CAMPUS_DB_PORT", config.db.port);
    config.db.user = EnvString("CAMPUS_DB_USER", config.db.user);
    config.db.password = EnvString("CAMPUS_DB_PASSWORD", config.db.password);
    config.db.database = EnvString("CAMPUS_DB_NAME", config.db.database);
    config.db.pool_size = EnvInt("CAMPUS_DB_POOL_SIZE", config.db.pool_size);
    if (config.db.pool_size < 1) {
        config.db.pool_size = 1;
    }

    config.portal.adapter = EnvString("CAMPUS_PORTAL_ADAPTER", config.portal.adapter);
    config.portal.host = EnvString("CAMPUS_PORTAL_HOST", config.portal.host);
    config.portal.port = EnvInt("CAMPUS_PORTAL_PORT", config.portal.port);

    config.auth.secret = EnvString("CAMPUS_AUTH_SECRET", config.auth.secret);
    config.auth.token_ttl_seconds = EnvInt("CAMPUS_AUTH_TTL_SECONDS", config.auth.token_ttl_seconds);
    if (config.auth.token_ttl_seconds < 1) {
        config.auth.token_ttl_seconds = 1;
    }
    config.auth.rate_limit_per_second =
        EnvDouble("CAMPUS_RATE_LIMIT_RPS", config.auth.rate_limit_per_second);
    config.auth.rate_limit_burst =
        EnvDouble("CAMPUS_RATE_LIMIT_BURST", config.auth.rate_limit_burst);

    config.cache.enabled = EnvBool("CAMPUS_CACHE_ENABLED", config.cache.enabled);
    config.cache.host = EnvString("CAMPUS_CACHE_HOST", config.cache.host);
    config.cache.port = EnvInt("CAMPUS_CACHE_PORT", config.cache.port);
    config.cache.ttl_seconds = EnvInt("CAMPUS_CACHE_TTL_SECONDS", config.cache.ttl_seconds);
    if (config.cache.ttl_seconds < 1) {
        config.cache.ttl_seconds = 1;
    }

    return config;
}

bool UsesDefaultAuthSecret(const AppConfig& config) {
    return config.auth.secret == kDefaultAuthSecret;
}

std::string DescribeConfig(const AppConfig& config) {
    std::ostringstream oss;
    oss << "server=" << config.server.host << ':' << config.server.port
        << " engine=" << config.server.engine << '('
        << (config.server.workers > 0 ? "workers=" + std::to_string(config.server.workers)
                                      : std::string("workers=auto"))
        << ',' << (config.server.edge_triggered ? "ET" : "LT") << ')'
        << " db=" << config.db.user << '@' << config.db.host << ':' << config.db.port
        << '/' << config.db.database << " pool=" << config.db.pool_size
        << " portal=" << config.portal.adapter << '@' << config.portal.host << ':'
        << config.portal.port
        // 注意：不要把密钥打印到日志里
        << " auth=ttl" << config.auth.token_ttl_seconds << "s"
        << (UsesDefaultAuthSecret(config) ? "(默认密钥!)" : "(已自定义密钥)")
        << " rateLimit=" << config.auth.rate_limit_per_second << "/s"
        << " burst=" << config.auth.rate_limit_burst
        << " cache=" << (config.cache.enabled ? "on@" : "off@") << config.cache.host << ':'
        << config.cache.port << " ttl=" << config.cache.ttl_seconds << 's';
    return oss.str();
}

}  // namespace campus
