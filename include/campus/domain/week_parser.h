#pragma once

#include <string>

#include "campus/domain/course.h"
#include "campus/domain/result.h"

namespace campus {

// 把教务系统的周次文本解析成 WeekRange。
//
// 支持的写法（各高校常见形式）：
//   "1-16周"        -> 1-16 周，每周
//   "1-16周(单)"    -> 1-16 周，单周
//   "1-16周（双）"  -> 1-16 周，双周
//   "5周"           -> 第 5 周
//   "1-16"          -> 省略「周」字也接受
//
// 明确不支持的写法会返回失败并说明原因，而不是猜：
//   "1-8,10-16周"   -> 多区间，需要上层拆成多条课次
Result<WeekRange> ParseWeekText(const std::string& text);

}  // namespace campus

