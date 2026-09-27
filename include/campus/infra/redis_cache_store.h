#pragma once

#include <mutex>
#include <string>

#include "campus/domain/result.h"
#include "campus/infra/cache_store.h"

// hiredis 的类型只在实现文件里包含，头文件保持零依赖
struct redisContext;

namespace campus {

// 基于 Redis 的缓存实现（生产用）。
//
// hiredis 的 redisContext 不是线程安全的，这里用一把互斥锁串行化所有命令。
// 单连接在高并发下会成为瓶颈，连接池是后续优化项——先用最简单可靠的实现。
class RedisCacheStore : public CacheStore {
public:
    RedisCacheStore(std::string host, int port);
    ~RedisCacheStore() override;

    std::string Name() const override { return "redis"; }

    // 启动时探活。连不上就返回 false，让上层「不启用缓存」继续跑，
    // 而不是启动即崩溃或让每个请求都去等连接超时。
    bool Available();

    Result<std::string> Get(const std::string& key) override;
    Result<bool> Set(const std::string& key, const std::string& value, int ttl_seconds) override;
    Result<bool> Del(const std::string& key) override;

private:
    // 调用前必须已持有 mutex_
    void ConnectLocked();

    std::mutex mutex_;
    std::string host_;
    int port_;
    redisContext* context_ = nullptr;
};

}  // namespace campus

