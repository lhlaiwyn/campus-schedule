#pragma once

#include <string>
#include <utility>
#include <variant>

namespace campus {

// 统一错误码：接口层靠它决定返回哪个 HTTP 状态码
enum class ErrorCode {
    kOk = 0,
    kNotFound,         // 404
    kInvalidArgument,  // 400
    kUnauthorized,     // 401
    kTooManyRequests,  // 429
    kConflict,         // 409
    kDatabaseError,    // 500
    kInternalError,    // 500
};

inline const char* ToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::kOk:
            return "ok";
        case ErrorCode::kNotFound:
            return "not_found";
        case ErrorCode::kInvalidArgument:
            return "invalid_argument";
        case ErrorCode::kUnauthorized:
            return "unauthorized";
        case ErrorCode::kTooManyRequests:
            return "too_many_requests";
        case ErrorCode::kConflict:
            return "conflict";
        case ErrorCode::kDatabaseError:
            return "database_error";
        case ErrorCode::kInternalError:
            return "internal_error";
    }
    return "unknown";
}

struct Error {
    ErrorCode code = ErrorCode::kInternalError;
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string m) : code(c), message(std::move(m)) {}
};

// 轻量 Result：要么拿到值，要么拿到错误。
// 用它代替抛异常，调用方必须显式处理失败分支。
template <typename T>
class Result {
public:
    static Result Ok(T value) { return Result(std::move(value)); }

    static Result Fail(ErrorCode code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }
    const Error& error() const { return std::get<Error>(data_); }

private:
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(Error error) : data_(std::move(error)) {}

    std::variant<T, Error> data_;
};

}  // namespace campus
