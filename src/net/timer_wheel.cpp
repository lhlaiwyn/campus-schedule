#include "campus/net/timer_wheel.h"

namespace campus::net {

TimerWheel::TimerWheel(std::int64_t slot_ms, std::size_t slot_count)
    : slot_ms_(slot_ms > 0 ? slot_ms : 1), slot_count_(slot_count > 0 ? slot_count : 1),
      buckets_(slot_count_) {}

void TimerWheel::Schedule(std::uint64_t id, std::int64_t deadline_ms) {
    Cancel(id);
    // 已经过去的截止时间直接归到当前槽，下一轮就会触发
    if (deadline_ms < current_ms_) {
        deadline_ms = current_ms_;
    }

    const std::size_t bucket = BucketFor(deadline_ms);
    buckets_[bucket].push_back(Entry{id, deadline_ms});
    index_[id] = Location{bucket, std::prev(buckets_[bucket].end())};
}

bool TimerWheel::Cancel(std::uint64_t id) {
    const auto found = index_.find(id);
    if (found == index_.end()) {
        return false;
    }
    buckets_[found->second.bucket].erase(found->second.iterator);
    index_.erase(found);
    return true;
}

std::vector<std::uint64_t> TimerWheel::AdvanceTo(std::int64_t now_ms) {
    std::vector<std::uint64_t> expired;
    if (now_ms < current_ms_) {
        now_ms = current_ms_;  // 时钟回拨保护
    }

    // 每次只推进一个槽：简单、可预测、容易测试
    while (current_ms_ < now_ms) {
        current_ms_ += slot_ms_;
        const std::size_t bucket = BucketFor(current_ms_);
        auto& list = buckets_[bucket];
        auto it = list.begin();
        while (it != list.end()) {
            if (it->deadline_ms <= current_ms_) {
                expired.push_back(it->id);
                index_.erase(it->id);
                it = list.erase(it);
            } else {
                ++it;
            }
        }
    }
    return expired;
}

}  // namespace campus::net
