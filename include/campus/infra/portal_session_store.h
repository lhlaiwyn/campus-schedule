#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "campus/domain/result.h"

namespace campus {

// 保存「学号 -> 教务系统会话令牌」的映射。
//
// 登录时教务系统会发一个会话令牌，把它缓存起来之后，
// 同步课表就不需要用户再提交一次密码——**密码全程只出现一次**。
//
// 这是单进程内存实现：多实例部署时应该换成 Redis（共享 + 自带过期），
// 这也是项目后续计划里的一项。时间由调用方传入，便于测试。
class PortalSessionStore {
public:
    void Put(const std::string& student_id, const std::string& portal_token,
             std::int64_t expires_at);

    // 取会话；不存在或已过期都返回失败，并在过期时顺手清掉
    Result<std::string> Get(const std::string& student_id, std::int64_t now);

    bool Remove(const std::string& student_id);

    std::size_t Size() const;

    // 清掉所有已过期会话
    std::size_t PurgeExpired(std::int64_t now);

private:
    struct Entry {
        std::string token;
        std::int64_t expires_at = 0;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> sessions_;
};

}  // namespace campus

