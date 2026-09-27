#include "campus/infra/cache_store.h"

#include <utility>
#include <vector>

namespace campus {

Result<std::string> InMemoryCacheStore::Get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return Result<std::string>::Fail(ErrorCode::kNotFound, "缓存未命中: " + key);
    }
    if (it->second.expires_at_ms > 0 && it->second.expires_at_ms <= now_ms_) {
        entries_.erase(it);
        return Result<std::string>::Fail(ErrorCode::kNotFound, "缓存已过期: " + key);
    }
    return Result<std::string>::Ok(it->second.value);
}

Result<bool> InMemoryCacheStore::Set(const std::string& key, const std::string& value,
                                     int ttl_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    Entry entry;
    entry.value = value;
    entry.expires_at_ms = ttl_seconds > 0 ? now_ms_ + static_cast<std::int64_t>(ttl_seconds) * 1000
                                          : 0;
    entries_[key] = std::move(entry);
    return Result<bool>::Ok(true);
}

Result<bool> InMemoryCacheStore::Del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return Result<bool>::Ok(entries_.erase(key) > 0);
}

void InMemoryCacheStore::SetNow(std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    now_ms_ = now_ms;
}

std::size_t InMemoryCacheStore::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::size_t InMemoryCacheStore::PurgeExpired() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> expired;
    for (const auto& entry : entries_) {
        if (entry.second.expires_at_ms > 0 && entry.second.expires_at_ms <= now_ms_) {
            expired.push_back(entry.first);
        }
    }
    for (const auto& key : expired) {
        entries_.erase(key);
    }
    return expired.size();
}

}  // namespace campus

