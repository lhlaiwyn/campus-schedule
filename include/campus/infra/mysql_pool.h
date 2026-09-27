#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <mysql/mysql.h>

#include "campus/infra/config.h"

namespace campus {

// 对 MYSQL* 的 RAII 封装：析构时自动 mysql_close
class MySqlConnection {
public:
    explicit MySqlConnection(MYSQL* handle) : handle_(handle) {}
    ~MySqlConnection();

    MySqlConnection(const MySqlConnection&) = delete;
    MySqlConnection& operator=(const MySqlConnection&) = delete;

    MYSQL* handle() const { return handle_; }
    bool connected() const { return handle_ != nullptr; }

    // mysql_ping 在连接断开时会自动重连，所以借出连接前先 ping 一下
    bool Ping();

    // 转义字符串并加上引号，返回可直接拼进 SQL 的字面量
    std::string Quote(const std::string& value) const;

private:
    MYSQL* handle_;
};

// 固定大小的 MySQL 连接池
// - 启动时一次性建好连接，避免每个请求都做 TCP 握手和认证
// - Acquire() 返回 shared_ptr，出作用域自动归还给池子
class MySqlPool {
public:
    MySqlPool(const DbConfig& config, std::size_t pool_size);
    ~MySqlPool();

    MySqlPool(const MySqlPool&) = delete;
    MySqlPool& operator=(const MySqlPool&) = delete;

    // 借出一条连接；池子空时阻塞等待
    std::shared_ptr<MySqlConnection> Acquire();

    std::size_t Size() const { return all_.size(); }
    std::size_t IdleCount() const;

private:
    void Release(MySqlConnection* conn);

    DbConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::unique_ptr<MySqlConnection>> all_;
    std::vector<MySqlConnection*> idle_;
    bool closed_ = false;
};

}  // namespace campus

