#include "campus/infra/portal_session_store.h"

#include <vector>

namespace campus {

void PortalSessionStore::Put(const std::string& student_id, const std::string& portal_token,
                             std::int64_t expires_at) {
    if (student_id.empty() || portal_token.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[student_id] = Entry{portal_token, expires_at};
}

Result<std::string> PortalSessionStore::Get(const std::string& student_id, std::int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = sessions_.find(student_id);
    if (it == sessions_.end()) {
        return Result<std::string>::Fail(ErrorCode::kUnauthorized,
                                         "没有可用的教务系统会话，请先调用 /api/auth/login 登录");
    }
    if (it->second.expires_at > 0 && it->second.expires_at <= now) {
        sessions_.erase(it);
        return Result<std::string>::Fail(ErrorCode::kUnauthorized,
                                         "教务系统会话已过期，请重新登录");
    }
    return Result<std::string>::Ok(it->second.token);
}

bool PortalSessionStore::Remove(const std::string& student_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.erase(student_id) > 0;
}

std::size_t PortalSessionStore::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

std::size_t PortalSessionStore::PurgeExpired(std::int64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> expired;
    for (const auto& entry : sessions_) {
        if (entry.second.expires_at > 0 && entry.second.expires_at <= now) {
            expired.push_back(entry.first);
        }
    }
    for (const auto& key : expired) {
        sessions_.erase(key);
    }
    return expired.size();
}

}  // namespace campus

