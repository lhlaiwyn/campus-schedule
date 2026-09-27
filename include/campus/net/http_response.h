#pragma once

#include <string>
#include <utility>
#include <vector>

namespace campus::net {

// 一条 HTTP 响应。ToString() 序列化成可直接写进 socket 的字节。
struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    bool keep_alive = true;

    std::string ToString() const;

    static HttpResponse MakeText(int status, const std::string& body,
                                 const std::string& content_type);
    static HttpResponse MakeError(int status, const std::string& message);
};

// 常见的状态码对应的原因短语
const char* ReasonPhrase(int status);

}  // namespace campus::net

