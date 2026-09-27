#include "campus/net/http_response.h"

#include "campus/net/http_parser.h"

namespace campus::net {

const char* ReasonPhrase(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 413:
            return "Payload Too Large";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

std::string HttpResponse::ToString() const {
    std::string out;
    out += "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";

    bool has_content_length = false;
    bool has_connection = false;
    for (const auto& [name, value] : headers) {
        out += name + ": " + value + "\r\n";
        if (EqualsIgnoreCase(name, "Content-Length")) {
            has_content_length = true;
        } else if (EqualsIgnoreCase(name, "Connection")) {
            has_connection = true;
        }
    }

    if (!has_content_length) {
        out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    if (!has_connection) {
        out += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    }

    out += "\r\n";
    out += body;
    return out;
}

HttpResponse HttpResponse::MakeText(int status, const std::string& body,
                                    const std::string& content_type) {
    HttpResponse response;
    response.status = status;
    // 状态行里的原因短语必须跟状态码对上，否则会出现 "HTTP/1.1 413 OK" 这种
    response.reason = ReasonPhrase(status);
    response.headers.emplace_back("Content-Type", content_type);
    response.body = body;
    return response;
}

HttpResponse HttpResponse::MakeError(int status, const std::string& message) {
    return MakeText(status, message + "\n", "text/plain; charset=utf-8");
}

}  // namespace campus::net
