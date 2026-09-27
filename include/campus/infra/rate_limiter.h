#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace campus {

// 令牌桶限流器。
//
// 时间由调用方以毫秒传入，因此测试可以用固定时间做确定性验证，
// 不需要在测试里 sleep，也不会因为机器慢而偶发失败。
class RateLimiter {
public:
    RateLimiter(double rate_per_second, double burst);

    // 取一个令牌。返回 false 表示被限流，retry_after_seconds 给出建议等待时间。
    bool Allow(const std::string& key, std::int64_t now_ms, double& retry_after_seconds);

    std::size_t TrackedKeys() const;

    // 跟踪的 key 数超过上限时淘汰最久未活跃的那些，避免 map 无限增长
    void SetMaxKeys(std::size_t max_keys);

private:
    struct Bucket {
        double tokens = 0.0;
        std::int64_t last_refill_ms = 0;
    };

    void EvictIfNeeded();

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
    double rate_;
    double burst_;
    std::size_t max_keys_ = 10000;
};

}  // namespace campus

