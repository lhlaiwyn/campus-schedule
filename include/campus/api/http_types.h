#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace campus {

// 与具体 HTTP 库解耦的请求 / 响应结构。
//
// 业务层只认这两个结构：httplib 和自研网络库各自在边界处做一次转换，
// 于是同一套路由、鉴权、JSON 转换代码可以被两个引擎复用，
// 换引擎不需要改一行业务逻辑，也让业务代码可以脱离网络库单独测试。
struct ApiRequest {
    std::string method;
    std::string path;    // 不含查询串
    std::string target;  // 原始请求目标（含查询串），用于日志
    // 保序的查询参数（已解码）
    std::vector<std::pair<std::string, std::string>> query;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // 大小写不敏感的头部查询；不存在返回空串
    std::string Header(const std::string& name) const {
        for (const auto& [key, value] : headers) {
            if (key.size() == name.size()) {
                bool same = true;
                for (std::size_t i = 0; i < key.size(); ++i) {
                    const auto lower = [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    };
                    if (lower(static_cast<unsigned char>(key[i])) !=
                        lower(static_cast<unsigned char>(name[i]))) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    return value;
                }
            }
        }
        return {};
    }

    // 查询参数（区分大小写）；不存在返回空串
    std::string Query(const std::string& name) const {
        for (const auto& [key, value] : query) {
            if (key == name) {
                return value;
            }
        }
        return {};
    }

    bool HasQuery(const std::string& name) const {
        for (const auto& [key, value] : query) {
            (void)value;
            if (key == name) {
                return true;
            }
        }
        return false;
    }
};

struct ApiResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    // 额外响应头（例如限流时的 Retry-After）
    std::vector<std::pair<std::string, std::string>> headers;
};

}  // namespace campus
