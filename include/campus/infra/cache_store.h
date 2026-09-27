#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "campus/domain/result.h"

namespace campus {

// 缓存抽象。
//
// 抽成接口的实际好处：缓存相关的逻辑可以在没有 Redis 的情况下被完整测试——
// 单元测试用内存实现，生产用 Redis 实现，上层的缓存策略代码完全一样。
class CacheStore {
public:
    virtual ~CacheStore() = default;

    CacheStore(const CacheStore&) = delete;
    CacheStore& operator=(const CacheStore&) = delete;

    virtual std::string Name() const = 0;

    // 命中返回内容；未命中返回 kNotFound（这是正常情况，不是故障）
    virtual Result<std::string> Get(const std::string& key) = 0;

    virtual Result<bool> Set(const std::string& key, const std::string& value,
                             int ttl_seconds) = 0;

    virtual Result<bool> Del(const std::string& key) = 0;

protected:
    CacheStore() = default;
};

// 缓存命中统计。
//
// 必须跨请求共享（放在 AppContext 里）：如果每个请求新建一个计数器，
// 就永远统计不出真实命中率。
class CacheStats {
public:
    void RecordHit() { hits_.fetch_add(1, std::memory_order_relaxed); }
    void RecordMiss() { misses_.fetch_add(1, std::memory_order_relaxed); }
    void RecordInvalidation() { invalidations_.fetch_add(1, std::memory_order_relaxed); }

    std::uint64_t Hits() const { return hits_.load(std::memory_order_relaxed); }
    std::uint64_t Misses() const { return misses_.load(std::memory_order_relaxed); }
    std::uint64_t Invalidations() const { return invalidations_.load(std::memory_order_relaxed); }

    // 还没有任何请求时返回 0，避免除零
    double HitRate() const {
        const std::uint64_t total = Hits() + Misses();
        if (total == 0) {
            return 0.0;
        }
        return static_cast<double>(Hits()) / static_cast<double>(total);
    }

    void Reset() {
        hits_.store(0, std::memory_order_relaxed);
        misses_.store(0, std::memory_order_relaxed);
        invalidations_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> hits_{0};
    std::atomic<std::uint64_t> misses_{0};
    std::atomic<std::uint64_t> invalidations_{0};
};

// 内存缓存，只用于单元测试。
// 时间由测试通过 SetNow 推进，因此验证 TTL 过期不需要 sleep。
class InMemoryCacheStore : public CacheStore {
public:
    std::string Name() const override { return "memory"; }

    Result<std::string> Get(const std::string& key) override;
    Result<bool> Set(const std::string& key, const std::string& value, int ttl_seconds) override;
    Result<bool> Del(const std::string& key) override;

    void SetNow(std::int64_t now_ms);
    std::size_t Size() const;
    std::size_t PurgeExpired();

private:
    struct Entry {
        std::string value;
        std::int64_t expires_at_ms = 0;  // 0 表示永不过期
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::int64_t now_ms_ = 0;
};

}  // namespace campus

