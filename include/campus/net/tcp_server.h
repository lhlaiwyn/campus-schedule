#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "campus/net/byte_buffer.h"
#include "campus/net/epoll_reactor.h"
#include "campus/net/http_parser.h"
#include "campus/net/http_response.h"
#include "campus/net/socket.h"

namespace campus::net {

// 多线程事件驱动 HTTP 服务器。
//
// 每个 worker 线程持有自己的 epoll 实例和连接表，靠 SO_REUSEPORT 让内核把
// 新连接均匀分发到各线程——这就是 Nginx / Redis 用的「多 Reactor」模型：
//
//   - 事件驱动解决「每连接一线程」的并发瓶颈；
//   - 多 worker 解决「单线程只能吃满一个核」的吞吐瓶颈；
//   - 共享 nothing：每个线程只管自己的连接，连接表完全不用加锁。
//
// 触发模式可切换：默认水平触发（LT）语义简单、不容易漏事件；
// edge_triggered = true 切到边缘触发（ET），要求每次读写都读到 EAGAIN。
// 另外沿用第一版的「延迟关闭连接」：关闭只记 id，等一轮事件处理完再统一执行。
class TcpServer {
public:
    // 处理一条请求，返回完整响应（状态码 + 头 + body）。
    // 抛异常会被捕获并转成 500，保证一个坏处理器不会把整个连接循环带崩。
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    // worker_threads 是 worker 线程数；1 表示单线程。
    // edge_triggered：true 用边缘触发（EPOLLET），false 用水平触发（LT）。
    TcpServer(std::string host, int port, Handler handler, int worker_threads = 1,
              bool edge_triggered = false);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // 阻塞直到所有 worker 线程退出（Stop() 被调用）。
    // 返回 false 表示没有任何 worker 成功监听（通常是端口被占用）。
    bool Run();
    void Stop();

    std::uint64_t AcceptedConnections() const { return accepted_.load(std::memory_order_relaxed); }
    std::uint64_t ServedRequests() const { return served_.load(std::memory_order_relaxed); }
    int WorkerThreads() const { return worker_threads_; }
    bool EdgeTriggered() const { return edge_triggered_; }

private:
    struct Connection {
        std::uint64_t id = 0;
        Socket socket;
        ByteBuffer in;
        ByteBuffer out;
        HttpRequestParser parser;
        bool close_after_write = false;
        // Expect: 100-continue 只回一次，避免每收一段数据就重复回
        bool continue_sent = false;
    };

    // 每个 worker 的私有状态：互不共享，连接操作无需加锁
    struct Worker {
        Socket listen_socket;
        EpollReactor reactor;
        std::unordered_map<std::uint64_t, Connection> connections;
        std::unordered_set<std::uint64_t> to_close;
        std::uint64_t next_conn_id = 1;
    };

    bool ListenOn(Worker& worker);
    void WorkerLoop();

    // 按触发模式拼出事件掩码。边缘触发下每次读写都必须持续到 EAGAIN，
    // 否则会永远收不到下一次通知——本文件里的读、写、accept 都是按这个前提写的。
    std::uint32_t EventMask(std::uint32_t base) const {
        return edge_triggered_ ? (base | EPOLLET) : base;
    }

    void AcceptAll(Worker& worker);
    void HandleReadable(Worker& worker, std::uint64_t id, Connection& conn);
    void Flush(Worker& worker, std::uint64_t id, Connection& conn);
    void ScheduleClose(Worker& worker, std::uint64_t id);
    void ProcessClosures(Worker& worker);

    std::string host_;
    int port_;
    Handler handler_;
    int worker_threads_;
    bool edge_triggered_ = false;

    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> served_{0};
    std::atomic<int> listening_{0};
};

}  // namespace campus::net
