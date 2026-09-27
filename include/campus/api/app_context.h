#pragma once

#include <map>
#include <memory>
#include <string>

#include "campus/infra/cache_store.h"
#include "campus/infra/config.h"
#include "campus/infra/mysql_pool.h"
#include "campus/infra/portal_session_store.h"
#include "campus/infra/rate_limiter.h"
#include "campus/portal/portal_adapter.h"

namespace campus {

// 进程运行期间共享的上下文。
//
// 这个头文件刻意不包含任何 HTTP 库：业务层和分发层只依赖它，
// 因此换一个 HTTP 实现（httplib / 自研 epoll 网络库）不需要动业务代码。
struct AppContext {
    AppConfig config;
    MySqlPool* pool = nullptr;
    // 教务系统会话缓存：登录时写入，同步时读取，避免用户反复提交密码
    PortalSessionStore* sessions = nullptr;
    // 限流器必须跨请求共享，所以放在上下文里而不是每个请求新建
    RateLimiter* rate_limiter = nullptr;
    // 缓存为空表示不启用缓存（比如没装 Redis），此时所有查询直接走数据库
    CacheStore* cache = nullptr;
    // 命中统计必须跨请求共享，否则统计不出真实命中率
    CacheStats* cache_stats = nullptr;
    // 教务适配器按名字索引，由 main 在启动时创建并复用。
    //
    // 不能每个请求新建：适配器可能有会话状态（模拟实现就把令牌存在实例里），
    // 新建实例会丢掉登录后拿到的会话，导致「刚登录就提示会话失效」。
    std::map<std::string, std::shared_ptr<PortalAdapter>>* adapters = nullptr;
};

}  // namespace campus
