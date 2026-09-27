#include <cstdint>

#include <gtest/gtest.h>

#include "campus/net/timer_wheel.h"

namespace campus::net {
namespace {

TEST(TimerWheelTest, FiresWithinOneRevolutionAndNeverEarly) {
    TimerWheel wheel(10, 4);  // 40ms 周期
    wheel.Schedule(1, 15);

    EXPECT_TRUE(wheel.AdvanceTo(10).empty());
    EXPECT_TRUE(wheel.AdvanceTo(14).empty());

    const auto fired = wheel.AdvanceTo(50);
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0], 1U);
    EXPECT_EQ(wheel.Size(), 0U);
}

TEST(TimerWheelTest, CancelRemovesTimer) {
    TimerWheel wheel(10, 8);
    wheel.Schedule(1, 100);
    EXPECT_EQ(wheel.Size(), 1U);

    EXPECT_TRUE(wheel.Cancel(1));
    EXPECT_FALSE(wheel.Cancel(1));
    EXPECT_EQ(wheel.Size(), 0U);
    EXPECT_TRUE(wheel.AdvanceTo(1000).empty());
}

TEST(TimerWheelTest, RescheduleUpdatesDeadline) {
    TimerWheel wheel(10, 8);
    wheel.Schedule(1, 1000);
    wheel.Schedule(1, 15);  // 更新到更早的时间
    EXPECT_EQ(wheel.Size(), 1U);

    // deadline 15，80ms 周期内触发（实际在 90，即下一轮扫到该槽时）
    const auto fired = wheel.AdvanceTo(100);
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0], 1U);
}

TEST(TimerWheelTest, HandlesClockGoingBackwards) {
    TimerWheel wheel(10, 8);
    wheel.Schedule(1, 100);

    // 正常推进：触发
    EXPECT_EQ(wheel.AdvanceTo(200).size(), 1U);
    // 时钟回拨：不崩、不重复触发
    EXPECT_TRUE(wheel.AdvanceTo(100).empty());
    EXPECT_EQ(wheel.Size(), 0U);
}

TEST(TimerWheelTest, FiresAllDueTimers) {
    TimerWheel wheel(10, 8);
    wheel.Schedule(1, 100);
    wheel.Schedule(2, 5);
    wheel.Schedule(3, 100);

    const auto fired = wheel.AdvanceTo(500);
    ASSERT_EQ(fired.size(), 3U);

    bool seen[4] = {false, false, false, false};
    for (std::uint64_t id : fired) {
        ASSERT_GE(id, 1U);
        ASSERT_LE(id, 3U);
        seen[id] = true;
    }
    EXPECT_TRUE(seen[1]);
    EXPECT_TRUE(seen[2]);
    EXPECT_TRUE(seen[3]);
    EXPECT_EQ(wheel.Size(), 0U);
}

}  // namespace
}  // namespace campus::net
