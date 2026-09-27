#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "campus/api/router.h"
#include "campus/api/net_router.h"
#include "campus/infra/cache_store.h"
#include "campus/infra/config.h"
#include "campus/infra/mysql_pool.h"
#include "campus/infra/portal_session_store.h"
#include "campus/infra/rate_limiter.h"
#include "campus/infra/redis_cache_store.h"
#include "campus/portal/adapter_registry.h"

int main() {
    // 日志格式：时间 + 级别 + 内容
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);

    const campus::AppConfig config = campus::LoadConfigFromEnv();
    spdlog::info("campus-schedule v{} 启动", CAMPUS_VERSION);
    spdlog::info("配置: {}", campus::DescribeConfig(config));
    if (campus::UsesDefaultAuthSecret(config)) {
        spdlog::warn("正在用内置的开发密钥签发 JWT，上线前请设置 CAMPUS_AUTH_SECRET");
    }

    // 注册内置的教务适配器，必须在处理请求之前完成
    campus::RegisterBuiltinAdapters();

    // 适配器在启动时创建一次并复用：模拟实现会把教务系统令牌存在实例里，
    // 每请求新建会丢掉状态，导致登录之后同步时提示会话失效。
    std::map<std::string, std::shared_ptr<campus::PortalAdapter>> adapters;
    for (const auto& name : campus::AdapterRegistry::Names()) {
        adapters[name] = campus::AdapterRegistry::Create(name);
    }
    spdlog::info("已加载教务适配器 {} 个", adapters.size());

    // 连接池放在栈上，生命周期覆盖整个服务运行期。
    // 析构时会关闭所有连接，所以它必须比 HTTP 服务活得更久。
    campus::MySqlPool pool(config.db, static_cast<std::size_t>(config.db.pool_size));

    // 会话缓存与限流器必须活到服务结束，所以和连接池一样放在栈上
    campus::PortalSessionStore sessions;
    campus::RateLimiter rate_limiter(config.auth.rate_limit_per_second,
                                     config.auth.rate_limit_burst);

    // 缓存启用与否都不影响服务可用性：Redis 连不上就退化成「每次都查库」
    campus::CacheStats cache_stats;
    std::unique_ptr<campus::RedisCacheStore> cache_store;
    if (config.cache.enabled) {
        cache_store =
            std::make_unique<campus::RedisCacheStore>(config.cache.host, config.cache.port);
        if (cache_store->Available()) {
            spdlog::info("课表缓存已启用: redis://{}:{} TTL={}s", config.cache.host,
                         config.cache.port, config.cache.ttl_seconds);
        } else {
            spdlog::warn("Redis 不可用，本次运行不启用缓存（服务照常工作，只是每次都查库）");
            cache_store.reset();
        }
    } else {
        spdlog::info("课表缓存未启用（设置 CAMPUS_CACHE_ENABLED=true 可开启）");
    }

    campus::AppContext ctx;
    ctx.config = config;
    ctx.pool = &pool;
    ctx.sessions = &sessions;
    ctx.rate_limiter = &rate_limiter;
    ctx.cache_stats = &cache_stats;
    ctx.cache = cache_store.get();
    ctx.adapters = &adapters;

    // 同一个进程只跑一个 HTTP 引擎，业务代码完全共用（见 api/dispatcher.cpp）。
    // 默认走自研的 epoll 网络库；要跑 httplib 对照组就设置 CAMPUS_HTTP_ENGINE=httplib。
    const std::string& engine = config.server.engine;
    if (engine == "net") {
        campus::RunNetServer(ctx);
    } else if (engine == "httplib") {
        campus::RunServer(ctx);
    } else {
        spdlog::error("未知的 HTTP 引擎: {}（可选 net / httplib）", engine);
        return 1;
    }
    return 0;
}
