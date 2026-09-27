#pragma once

namespace campus::net {

// socket 文件描述符的 RAII 封装：析构自动 close，避免忘记释放。
// 只可移动不可拷贝，防止 double-close。
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd);
    ~Socket();

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int Fd() const { return fd_; }
    bool Valid() const { return fd_ >= 0; }

    void Close();
    // 接管一个已有的 fd（会先关掉手里的旧 fd）
    void Adopt(int fd);

    bool SetNonBlocking();
    bool SetReuseAddr();
    bool SetNoDelay();  // TCP_NODELAY，禁用 Nagle 算法
    // SO_REUSEPORT：允许多个 socket 绑定同一地址，内核把新连接分摊给它们。
    // 这是「多线程 Reactor」的地基——每个 worker 线程各持一个监听 socket。
    bool SetReusePort();

private:
    int fd_ = -1;
};

}  // namespace campus::net
