#pragma once

#include <cstdint>

namespace campus {

// 当前 Unix 时间戳（秒）。
// 抽成函数而不是到处调系统时间，是为了让「过期判定」这类逻辑
// 可以把时间当参数传进去做确定性测试。
std::int64_t UnixSecondsNow();

// 当前单调时钟（毫秒），用于限流这类只看时间差的场景
std::int64_t MonotonicMillisNow();

}  // namespace campus

