#include "campus/infra/mysql_pool.h"

#include <spdlog/spdlog.h>

namespace campus {

MySqlConnection::~MySqlConnection() {
    if (handle_ != nullptr) {
        mysql_close(handle_);
        handle_ = nullptr;
    }
}

bool MySqlConnection::Ping() {
    if (handle_ == nullptr) {
        return false;
    }
    return mysql_ping(handle_) == 0;
}

std::string MySqlConnection::Quote(const std::string& value) const {
    if (handle_ == nullptr) {
        return "''";
    }
    std::string buffer(value.size() * 2 + 1, '\0');
    const auto escaped_len = mysql_real_escape_string(
        handle_, buffer.data(), value.c_str(), static_cast<unsigned long>(value.size()));
    buffer.resize(escaped_len);
    return "'" + buffer + "'";
}

MySqlPool::MySqlPool(const DbConfig& config, std::size_t pool_size) : config_(config) {
    all_.reserve(pool_size);
    idle_.reserve(pool_size);

    for (std::size_t i = 0; i < pool_size; ++i) {
        MYSQL* raw = mysql_init(nullptr);
        if (raw == nullptr) {
            spdlog::error("mysql_init 失败，连接池只建了 {} 条连接", all_.size());
            break;
        }

        unsigned int connect_timeout = 5;
        mysql_options(raw, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
        mysql_options(raw, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        if (mysql_real_connect(raw, config_.host.c_str(), config_.user.c_str(),
                               config_.password.c_str(), config_.database.c_str(),
                               static_cast<unsigned int>(config_.port), nullptr, 0) == nullptr) {
            spdlog::error("MySQL 连接失败: {} (已建成 {} 条)", mysql_error(raw), all_.size());
            mysql_close(raw);
            break;
        }

        auto conn = std::make_unique<MySqlConnection>(raw);
        idle_.push_back(conn.get());
        all_.push_back(std::move(conn));
    }

    if (all_.empty()) {
        spdlog::error("连接池为空，数据库相关接口将不可用");
    } else {
        spdlog::info("MySQL 连接池就绪: {} 条连接 -> {}/{}", all_.size(), config_.host,
                     config_.database);
    }
}

MySqlPool::~MySqlPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
    // all_ 中的 unique_ptr 在此析构，自动关闭所有连接
}

std::shared_ptr<MySqlConnection> MySqlPool::Acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return closed_ || !idle_.empty(); });

    if (closed_ || idle_.empty()) {
        return nullptr;
    }

    MySqlConnection* conn = idle_.back();
    idle_.pop_back();
    lock.unlock();

    conn->Ping();

    // 自定义删除器：句柄被释放时归还连接
    return std::shared_ptr<MySqlConnection>(conn,
                                           [this](MySqlConnection* c) { Release(c); });
}

std::size_t MySqlPool::IdleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return idle_.size();
}

void MySqlPool::Release(MySqlConnection* conn) {
    if (conn == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return;
        }
        idle_.push_back(conn);
    }
    cv_.notify_one();
}

}  // namespace campus
