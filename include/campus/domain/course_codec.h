#pragma once

#include <string>
#include <vector>

#include "campus/domain/course.h"

namespace campus {

// 课程列表的字符串编解码，给缓存层用。
//
// 单独放一个头文件、并且不引入 nlohmann/json，
// 是为了让缓存装饰器这类逻辑单元不必依赖 JSON 库就能编译和测试。
// 实现放在 src/domain/course_codec.cpp，复用 course_json.h 的编解码。
//
// 解码失败不是致命错误：缓存内容可能因为版本升级或人为修改而不可读，
// 这时调用方应该当作未命中，重新查库即可。
std::string EncodeCourseList(const std::vector<Course>& courses, const std::string& semester);

bool DecodeCourseList(const std::string& payload, std::string& semester,
                      std::vector<Course>& courses, std::string& error);

}  // namespace campus

