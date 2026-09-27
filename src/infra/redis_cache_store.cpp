#include "campus/infra/redis_cache_store.h"

#include <utility>

#include <hiredis/hiredis.h>
#include <spdlog/spdlog.h>

namespace campus {
namespace {

Result<std::string> FailCache(const std::string& message) {
    return Result<std::string>::Fail(ErrorCode::kInternalError, message);
}

Result<bool> FailSet(const std::string& message) {
    return Result<bool>::Fail(ErrorCode::kInternalError, message);
}

}  // namespace

RedisCacheStore::RedisCacheStore(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

RedisCacheStore::~RedisCacheStore() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_ != nullptr) {
        redisFree(context_);
        context_ = nullptr;
    }
}

void RedisCacheStore::ConnectLocked() {
    if (context_ != nullptr) {
        redisFree(context_);
        context_ = nullptr;
    }

    timeval timeout{};
    timeout.tv_sec = 2;
    context_ = redisConnectWithTimeout(host_.c_str(), port_, timeout);
    if (context_ == nullptr || context_->err != 0) {
        const std::string reason = context_ != nullptr ? context_->errstr : "无法分配连接";
        if (context_ != nullptr) {
            redisFree(context_);
            context_ = nullptr;
        }
        spdlog::warn("连接 Redis {}:{} 失败: {}", host_, port_, reason);
    }
}

bool RedisCacheStore::Available() {
    std::lock_guard<std::mutex> lock(mutex_);
    ConnectLocked();
    if (context_ == nullptr) {
        return false;
    }

    auto* reply = static_cast<redisReply*>(redisCommand(context_, "PING"));
    if (reply == nullptr) {
        redisFree(context_);
        context_ = nullptr;
        return false;
    }
    const bool healthy = reply->type == REDIS_REPLY_STATUS;
    freeReplyObject(reply);
    return healthy;
}

Result<std::string> RedisCacheStore::Get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (context_ == nullptr) {
        ConnectLocked();
        if (context_ == nullptr) {
            return FailCache("Redis 未连接");
        }
    }

    auto* reply = static_cast<redisReply*>(redisCommand(context_, "GET %s", key.c_str()));
    if (reply == nullptr) {
        redisFree(context_);
        context_ = nullptr;
        return FailCache("Redis GET 执行失败");
    }

    Result<std::string> result = FailCache("Redis GET 返回了意外的类型");
    if (reply->type == REDIS_REPLY_STRING) {
        // 用 len 而不是依赖 '\0' 结尾：缓存里存的是 JSON，但也可能包含二进制
        result = Result<std::string>::Ok(std::string(reply->str, reply->len));
    } else if (reply->type == REDIS_REPLY_NIL) {
        result = Result<std::string>::Fail(ErrorCode::kNotFound, "缓存未命中: " + key);
    }
    freeReplyObject(reply);
    return result;
}

Result<bool> RedisCacheStore::Set(const std::string& key, const std::string& value,
                                  int ttl_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (context_ == nullptr) {
        ConnectLocked();
        if (context_ == nullptr) {
            return FailSet("Redis 未连接");
        }
    }

    // 兜底：不允许不带过期时间地写入，否则缓存会无限堆积
    if (ttl_seconds <= 0) {
        ttl_seconds = 300;
    }

    // %b 让 value 按「指针 + 长度」传递，二进制安全
    auto* reply = static_cast<redisReply*>(redisCommand(
        context_, "SET %s %b EX %d", key.c_str(), value.data(), value.size(), ttl_seconds));
    if (reply == nullptr) {
        redisFree(context_);
        context_ = nullptr;
        return FailSet("Redis SET 执行失败");
    }
    const bool ok = reply->type == REDIS_REPLY_STATUS;
    freeReplyObject(reply);
    return ok ? Result<bool>::Ok(true) : FailSet("Redis SET 没有返回 OK");
}

Result<bool> RedisCacheStore::Del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (context_ == nullptr) {
        ConnectLocked();
        if (context_ == nullptr) {
            return FailSet("Redis 未连接");
        }
    }

    auto* reply = static_cast<redisReply*>(redisCommand(context_, "DEL %s", key.c_str()));
    if (reply == nullptr) {
        redisFree(context_);
        context_ = nullptr;
        return FailSet("Redis DEL 执行失败");
    }
    const bool ok = reply->type == REDIS_REPLY_INTEGER;
    freeReplyObject(reply);
    return ok ? Result<bool>::Ok(true) : FailSet("Redis DEL 返回了意外的类型");
}

}  // namespace campus

