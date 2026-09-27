#include "campus/net/http_parser.h"

#include <cctype>
#include <sstream>

namespace campus::net {
namespace {

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::string PercentDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '+') {
            out.push_back(' ');
            continue;
        }
        if (ch == '%' && i + 2 < text.size()) {
            const int high = HexValue(text[i + 1]);
            const int low = HexValue(text[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        out.push_back(ch);  // 非法的 % 序列原样保留
    }
    return out;
}

void ParseRequestTarget(const std::string& target, std::string& path,
                        std::vector<std::pair<std::string, std::string>>& query) {
    path.clear();
    query.clear();

    const std::size_t mark = target.find('?');
    if (mark == std::string::npos) {
        path = target;
        return;
    }
    path = target.substr(0, mark);

    const std::string query_text = target.substr(mark + 1);
    std::size_t cursor = 0;
    while (cursor <= query_text.size()) {
        const std::size_t amp = query_text.find('&', cursor);
        const std::string item = amp == std::string::npos
                                     ? query_text.substr(cursor)
                                     : query_text.substr(cursor, amp - cursor);
        if (!item.empty()) {
            const std::size_t equals = item.find('=');
            if (equals == std::string::npos) {
                query.emplace_back(PercentDecode(item), std::string{});
            } else {
                query.emplace_back(PercentDecode(item.substr(0, equals)),
                                   PercentDecode(item.substr(equals + 1)));
            }
        }
        if (amp == std::string::npos) {
            break;
        }
        cursor = amp + 1;
    }
}

HttpRequestParser::Status HttpRequestParser::Parse(ByteBuffer& buffer) {
    if (phase_ == Phase::kDone) {
        return Status::kComplete;
    }

    if (phase_ == Phase::kHeader) {
        static constexpr const char kTerminator[] = "\r\n\r\n";
        const std::size_t terminator = buffer.Find(kTerminator, 4);
        if (terminator == std::string::npos) {
            if (buffer.ReadableBytes() > kMaxHeaderBytes) {
                error_ = "HTTP 请求头超过 64KB 上限";
                phase_ = Phase::kDone;
                return Status::kError;
            }
            return Status::kIncomplete;
        }

        // 头部块（不含终止符），复制出来解析，避免 buffer 后续搬移影响引用
        const std::string block(buffer.ReadPtr(), terminator);
        buffer.Consume(terminator + 4);

        if (!ParseHeaderBlock(block, error_)) {
            phase_ = Phase::kDone;
            return Status::kError;
        }

        body_remaining_ = request_.content_length;
        if (body_remaining_ == 0) {
            phase_ = Phase::kDone;
            return Status::kComplete;
        }
        phase_ = Phase::kBody;
    }

    // Phase::kBody
    if (buffer.ReadableBytes() < body_remaining_) {
        return Status::kIncomplete;
    }
    request_.body.assign(buffer.ReadPtr(), body_remaining_);
    buffer.Consume(body_remaining_);
    phase_ = Phase::kDone;
    return Status::kComplete;
}

void HttpRequestParser::Reset() {
    phase_ = Phase::kHeader;
    request_ = HttpRequest{};
    error_.clear();
    body_remaining_ = 0;
}

bool HttpRequestParser::ParseHeaderBlock(const std::string& block, std::string& error) {
    // 按行拆分
    std::vector<std::string> lines;
    std::size_t cursor = 0;
    while (cursor <= block.size()) {
        const std::size_t line_end = block.find("\r\n", cursor);
        if (line_end == std::string::npos) {
            lines.push_back(block.substr(cursor));
            break;
        }
        lines.push_back(block.substr(cursor, line_end - cursor));
        cursor = line_end + 2;
    }

    if (lines.empty()) {
        error = "空请求";
        return false;
    }

    // 请求行：METHOD SP TARGET SP VERSION
    std::istringstream request_line(lines[0]);
    std::string method;
    std::string target;
    std::string version;
    if (!(request_line >> method >> target >> version)) {
        error = "请求行格式非法";
        return false;
    }
    if (version.rfind("HTTP/", 0) != 0) {
        error = "不支持的协议版本: " + version;
        return false;
    }
    if (target.empty() || target[0] != '/') {
        error = "请求目标必须以 / 开头";
        return false;
    }

    // 头部
    request_.headers.clear();
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            error = "头部行缺少冒号: " + line;
            return false;
        }
        const std::string name = Trim(line.substr(0, colon));
        const std::string value = Trim(line.substr(colon + 1));
        if (name.empty()) {
            error = "头部名为空";
            return false;
        }
        request_.headers.emplace_back(name, value);
    }

    request_.method = std::move(method);
    request_.target = target;
    ParseRequestTarget(target, request_.path, request_.query);
    request_.version = std::move(version);

    // 长连接判定：HTTP/1.1 默认开，除非 Connection: close
    const std::string connection = request_.Header("Connection");
    if (request_.version == "HTTP/1.0") {
        request_.keep_alive = EqualsIgnoreCase(connection, "keep-alive");
    } else {
        request_.keep_alive = !EqualsIgnoreCase(connection, "close");
    }

    // Content-Length
    const std::string length_text = request_.Header("Content-Length");
    if (!length_text.empty()) {
        try {
            const long long parsed = std::stoll(length_text);
            if (parsed < 0) {
                error = "Content-Length 不能为负";
                return false;
            }
            request_.content_length = static_cast<std::size_t>(parsed);
        } catch (...) {
            error = "Content-Length 不是合法数字: " + length_text;
            return false;
        }
    }

    return true;
}

}  // namespace campus::net
