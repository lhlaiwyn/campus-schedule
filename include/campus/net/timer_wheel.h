#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace campus::net {

// 定时器轮：把到期时间映射到固定数量的槽上，O(1) 添加、O(1) 取消、按槽批量过期。
//
// 适合「每个连接一个超时定时器」的场景（连接空闲超时、keep-alive 超时）。
// 对比最小堆：堆的插入/删除是 O(log n)，轮子把这两个操作都压到 O(1)。
//
// 已知特性（如实记录）：一个定时器的实际触发时间可能在
// [deadline, deadline + 一轮周期] 之间——因为同一槽里比当前时刻晚的条目
// 会被留到下一轮再检查。对「秒级超时」这类场景，这个抖动完全可接受；
// 需要精确到毫秒的场景才该用别的时间结构。
class TimerWheel {
public:
    // slot_ms 是每个槽覆盖的时间，slot_count 是槽数；
    // 单条定时器的最长可设超时约等于 slot_ms * slot_count。
    TimerWheel(std::int64_t slot_ms, std::size_t slot_count);

    // 在 deadline_ms（绝对毫秒时间戳）到期。重复 schedule 同一 id 视为更新。
    void Schedule(std::uint64_t id, std::int64_t deadline_ms);

    // 取消 id 的定时器，返回是否真的存在并取消掉
    bool Cancel(std::uint64_t id);

    // 把当前时间推进到 now_ms，返回这期间到期的所有 id（顺序不保证）
    std::vector<std::uint64_t> AdvanceTo(std::int64_t now_ms);

    std::size_t Size() const { return index_.size(); }

private:
    struct Entry {
        std::uint64_t id;
        std::int64_t deadline_ms;
    };

    struct Location {
        std::size_t bucket;
        std::list<Entry>::iterator iterator;
    };

    std::size_t BucketFor(std::int64_t deadline_ms) const {
        return static_cast<std::size_t>((deadline_ms / slot_ms_) % slot_count_);
    }

    std::int64_t slot_ms_;
    std::size_t slot_count_;
    std::int64_t current_ms_ = 0;

    std::vector<std::list<Entry>> buckets_;
    std::unordered_map<std::uint64_t, Location> index_;
};

}  // namespace campus::net

