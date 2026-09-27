#include "campus/infra/rate_limiter.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace campus {
namespace {

constexpr double kEpsilon = 1e-9;

}  // namespace

RateLimiter::RateLimiter(double rate_per_second, double burst)
    : rate_(rate_per_second > 0.0 ? rate_per_second : 1.0),
      burst_(burst > 0.0 ? burst : 1.0) {}

bool RateLimiter::Allow(const std::string& key, std::int64_t now_ms,
                        double& retry_after_seconds) {
    retry_after_seconds = 0.0;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        EvictIfNeeded();
        Bucket fresh;
        fresh.tokens = burst_;
        fresh.last_refill_ms = now_ms;
        it = buckets_.emplace(key, fresh).first;
    }

    Bucket& bucket = it->second;

    // 按经过的时间补令牌，但不超过桶容量
    if (now_ms > bucket.last_refill_ms) {
        const double elapsed_seconds = static_cast<double>(now_ms - bucket.last_refill_ms) / 1000.0;
        bucket.tokens = std::min(burst_, bucket.tokens + elapsed_seconds * rate_);
        bucket.last_refill_ms = now_ms;
    }

    if (bucket.tokens + kEpsilon >= 1.0) {
        bucket.tokens -= 1.0;
        return true;
    }

    // 还差多少令牌，就还需要等多久
    retry_after_seconds = (1.0 - bucket.tokens) / rate_;
    return false;
}

std::size_t RateLimiter::TrackedKeys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buckets_.size();
}

void RateLimiter::SetMaxKeys(std::size_t max_keys) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_keys_ = max_keys > 0 ? max_keys : 1;
}

void RateLimiter::EvictIfNeeded() {
    if (buckets_.size() < max_keys_) {
        return;
    }

    // 只保留最近活跃的一半，避免每来一个新 key 都做一次全量排序
    std::vector<std::pair<std::int64_t, std::string>> by_age;
    by_age.reserve(buckets_.size());
    for (const auto& entry : buckets_) {
        by_age.emplace_back(entry.second.last_refill_ms, entry.first);
    }
    std::sort(by_age.begin(), by_age.end());

    const std::size_t keep = max_keys_ / 2;
    for (std::size_t i = 0; i + keep < by_age.size(); ++i) {
        buckets_.erase(by_age[i].second);
    }
}

}  // namespace campus
