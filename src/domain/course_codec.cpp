#include "campus/domain/course_codec.h"

#include <exception>

#include "campus/domain/course_json.h"

namespace campus {

std::string EncodeCourseList(const std::vector<Course>& courses, const std::string& semester) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& course : courses) {
        items.push_back(ToJson(course));
    }
    return nlohmann::json{{"semester", semester}, {"items", items}}.dump();
}

bool DecodeCourseList(const std::string& payload, std::string& semester,
                      std::vector<Course>& courses, std::string& error) {
    try {
        const nlohmann::json root = nlohmann::json::parse(payload);
        if (!root.is_object() || !root.contains("items") || !root.at("items").is_array()) {
            error = "缓存内容缺少 items 数组";
            return false;
        }
        semester = root.value("semester", std::string{});
        courses.clear();
        for (const auto& item : root.at("items")) {
            Course course;
            if (!FromJson(item, course, error)) {
                return false;
            }
            courses.push_back(std::move(course));
        }
        return true;
    } catch (const std::exception& e) {
        error = std::string("缓存内容不是合法 JSON: ") + e.what();
        return false;
    }
}

}  // namespace campus

