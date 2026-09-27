#pragma once

#include <string>
#include <vector>

#include "campus/domain/course.h"
#include "campus/domain/result.h"
#include "campus/portal/portal_types.h"

namespace campus {

// 把教务系统的原始课表条目转换成领域模型。
//
// 同一门课的多个课次会被合并到一条 Course 上，因此这一步同时完成了
// 「教务系统的行式课表」到「领域模型的课程 + 课次」的转换。
//
// 设计取舍：只要有任意一行解析失败就整体失败并指出是哪门课，
// 而不是跳过坏数据继续同步。课表少一节课的后果比同步失败严重得多。
Result<std::vector<Course>> ConvertRawSchedule(const std::vector<RawScheduleEntry>& raw,
                                               const std::string& semester);

}  // namespace campus

