#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "campus/net/byte_buffer.h"

namespace campus::net {

// 大小写不敏感的字符串比较，给 HTTP 头部查询用
inline bool EqualsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto lower = [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        };
        if (lower(static_cast<unsigned char>(lhs[i])) !=
            lower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
}

// 解析出来的一条 HTTP 请求
struct HttpRequest {
    std::string method;
    std::string path;    // 不含查询串的路径，例如 /api/courses
    std::string target;  // 原始请求目标（含查询串），便于日志
    std::string version;  // 例如 "HTTP/1.1"
    // 已做百分号解码的查询参数，保序
    std::vector<std::pair<std::string, std::string>> query;
    // 用 vector 保序、允许同名头重复出现
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::size_t content_length = 0;
    bool keep_alive = true;  // HTTP/1.1 默认长连接

    // 大小写不敏感的头部查询；不存在返回空串
    std::string Header(const std::string& name) const {
        for (const auto& [key, value] : headers) {
            if (EqualsIgnoreCase(key, name)) {
                return value;
            }
        }
        return {};
    }

    bool HasHeader(const std::string& name) const { return !Header(name).empty(); }

    // 查询参数（区分大小写）；不存在返回空串
    std::string Query(const std::string& name) const {
        for (const auto& [key, value] : query) {
            if (key == name) {
                return value;
            }
        }
        return {};
    }

    bool HasQuery(const std::string& name) const { return !Query(name).empty(); }
};

// 把 "/a/b?x=1&y=hello%20world" 拆成路径和查询参数。
// 查询参数的 key / value 会做百分号解码，'+' 按表单约定解码成空格。
void ParseRequestTarget(const std::string& target, std::string& path,
                        std::vector<std::pair<std::string, std::string>>& query);

// 百分号解码；非法的 % 序列原样保留，不抛异常
std::string PercentDecode(const std::string& text);

// 增量 HTTP 请求解析器。
//
// 为什么是「增量」：非阻塞 I/O 下一次 recv 可能只收到半个请求，
// 解析器必须能「先解析头部、body 等下一段数据」。
// 用法：把收进 ByteBuffer 的数据喂给 Parse，返回 kIncomplete 就继续收，
// kComplete 表示这次请求解析完了（body 已按 Content-Length 读全）。
class HttpRequestParser {
public:
    enum class Status {
        kIncomplete,  // 数据还不够，继续收
        kComplete,    // 解析完成
        kError,       // 协议错误，应关闭连接
    };

    // 单条请求头的最大字节数，防止恶意客户端撑爆内存
    static constexpr std::size_t kMaxHeaderBytes = 64 * 1024;

    // 增量解析。会消费 buffer 里已解析的部分；失败时错误原因在 Error()。
    Status Parse(ByteBuffer& buffer);

    void Reset();

    const HttpRequest& Request() const { return request_; }
    const std::string& Error() const { return error_; }

    // 请求头是否已经解析完（正在等 body 或已完成都算 true）。
    // 服务器用它处理 Expect: 100-continue——这时需要先回一个中间响应，
    // 客户端才会把 body 发过来。
    bool HeaderParsed() const { return phase_ != Phase::kHeader; }

private:
    enum class Phase { kHeader, kBody, kDone };

    bool ParseHeaderBlock(const std::string& block, std::string& error);

    Phase phase_ = Phase::kHeader;
    HttpRequest request_;
    std::string error_;
    std::size_t body_remaining_ = 0;
};

}  // namespace campus::net
