#include "campus/net/tcp_server.h"

#include <cerrno>
#include <cstdint>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "campus/net/http_response.h"

namespace campus::net {
namespace {

// 请求体上限。Content-Length 是客户端随便写的，不设上限的话
// 一个「声明 1GB body」的请求就能把服务器内存吃光（这是个真实的攻击面）。
constexpr std::size_t kMaxBodyBytes = 8 * 1024 * 1024;

}  // namespace

TcpServer::TcpServer(std::string host, int port, Handler handler, int worker_threads,
                     bool edge_triggered)
    : host_(std::move(host)),
      port_(port),
      handler_(std::move(handler)),
      worker_threads_(worker_threads > 0 ? worker_threads : 1),
      edge_triggered_(edge_triggered) {}

TcpServer::~TcpServer() = default;

bool TcpServer::ListenOn(Worker& worker) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return false;
    }
    worker.listen_socket.Adopt(fd);
    worker.listen_socket.SetReuseAddr();
    // 多 worker 时开 SO_REUSEPORT，让内核把新连接分摊给各线程的监听 socket
    if (worker_threads_ > 1) {
        worker.listen_socket.SetReusePort();
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port_));
    if (host_.empty() || host_ == "0.0.0.0") {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        ::inet_pton(AF_INET, host_.c_str(), &address.sin_addr);
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        return false;
    }
    if (::listen(fd, 128) < 0) {
        return false;
    }
    return worker.reactor.Add(fd, EventMask(EPOLLIN), &worker.listen_socket);
}

bool TcpServer::Run() {
    running_.store(true, std::memory_order_relaxed);

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_threads_));
    for (int i = 0; i < worker_threads_; ++i) {
        workers.emplace_back([this] { WorkerLoop(); });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    // 一个都没监听上（端口被占用 / 权限不足）要如实返回 false，
    // 否则调用方会以为服务已经起来了，日志里却什么都没有。
    return listening_.load(std::memory_order_relaxed) > 0;
}

void TcpServer::Stop() { running_.store(false, std::memory_order_relaxed); }

void TcpServer::WorkerLoop() {
    Worker worker;
    if (!ListenOn(worker)) {
        return;
    }
    listening_.fetch_add(1, std::memory_order_relaxed);

    std::vector<epoll_event> events(1024);
    while (running_.load(std::memory_order_relaxed)) {
        const int ready = worker.reactor.Wait(events, 1000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < ready; ++i) {
            const epoll_event& event = events[i];
            void* data = event.data.ptr;

            if (data == static_cast<void*>(&worker.listen_socket)) {
                AcceptAll(worker);
                continue;
            }

            auto* conn = static_cast<Connection*>(data);
            const std::uint64_t id = conn->id;
            if (worker.to_close.count(id) != 0) {
                continue;
            }
            if ((event.events & (EPOLLERR | EPOLLHUP)) != 0) {
                ScheduleClose(worker, id);
                continue;
            }
            if ((event.events & EPOLLIN) != 0) {
                HandleReadable(worker, id, *conn);
            }
            if (worker.to_close.count(id) != 0) {
                continue;
            }
            if ((event.events & EPOLLOUT) != 0) {
                Flush(worker, id, *conn);
            }
        }

        ProcessClosures(worker);
    }
}

void TcpServer::AcceptAll(Worker& worker) {
    while (true) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        const int fd = ::accept4(worker.listen_socket.Fd(),
                                 reinterpret_cast<sockaddr*>(&peer), &peer_len,
                                 SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            return;  // EAGAIN 表示队列已空；其它错误也先退出，避免死循环
        }

        const std::uint64_t id = worker.next_conn_id++;
        auto [it, inserted] = worker.connections.emplace(id, Connection{});
        if (!inserted) {
            ::close(fd);
            continue;
        }
        Connection& conn = it->second;
        conn.id = id;
        conn.socket.Adopt(fd);
        conn.socket.SetNoDelay();

        if (!worker.reactor.Add(fd, EventMask(EPOLLIN), &conn)) {
            worker.connections.erase(it);
            continue;
        }
        accepted_.fetch_add(1, std::memory_order_relaxed);
    }
}

void TcpServer::HandleReadable(Worker& worker, std::uint64_t id, Connection& conn) {
    if (conn.close_after_write) {
        return;  // 正在等写完后关闭，不再读新数据
    }

    while (true) {
        conn.in.EnsureWritable(4096);
        const ssize_t n = ::recv(conn.socket.Fd(), conn.in.WritePtr(), conn.in.WritableBytes(), 0);
        if (n > 0) {
            conn.in.CommitWrite(static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            ScheduleClose(worker, id);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        ScheduleClose(worker, id);
        return;
    }

    while (true) {
        const HttpRequestParser::Status status = conn.parser.Parse(conn.in);
        if (status == HttpRequestParser::Status::kIncomplete) {
            // 请求体超过上限：立刻回 413 并关闭，不把内存交出去
            if (conn.parser.HeaderParsed() &&
                conn.parser.Request().content_length > kMaxBodyBytes) {
                HttpResponse too_large = HttpResponse::MakeError(413, "请求体超过 8MB 上限");
                too_large.keep_alive = false;
                conn.out.Append(too_large.ToString());
                conn.close_after_write = true;
                Flush(worker, id, conn);
                return;
            }
            // Expect: 100-continue：curl 对超过 1KB 的 body 会先只发头再等这个中间响应，
            // 不回就会干等到自己的超时（默认 1 秒）才发 body，表现为「同步接口很慢」。
            if (!conn.continue_sent && conn.parser.HeaderParsed() &&
                EqualsIgnoreCase(conn.parser.Request().Header("Expect"), "100-continue")) {
                conn.continue_sent = true;
                conn.out.Append("HTTP/1.1 100 Continue\r\n\r\n");
                Flush(worker, id, conn);
            }
            return;
        }
        if (status == HttpRequestParser::Status::kError) {
            // 协议错误要关连接，响应头里也必须如实写 Connection: close，
            // 否则客户端会以为还能复用这条连接（然后撞上一个已关闭的 socket）。
            HttpResponse bad_request = HttpResponse::MakeError(400, conn.parser.Error());
            bad_request.keep_alive = false;
            conn.out.Append(bad_request.ToString());
            conn.close_after_write = true;
            Flush(worker, id, conn);
            return;
        }

        const HttpRequest& request = conn.parser.Request();
        served_.fetch_add(1, std::memory_order_relaxed);

        HttpResponse response;
        try {
            response = handler_(request);
        } catch (...) {
            response = HttpResponse::MakeError(500, "服务器内部错误");
        }
        if (response.reason.empty()) {
            response.reason = ReasonPhrase(response.status);
        }
        // 连接是否复用由请求决定，处理器不参与判断
        response.keep_alive = request.keep_alive;
        conn.close_after_write = !request.keep_alive;

        conn.out.Append(response.ToString());
        conn.continue_sent = false;
        conn.parser.Reset();
        Flush(worker, id, conn);

        if (conn.close_after_write) {
            return;
        }
    }
}

void TcpServer::Flush(Worker& worker, std::uint64_t id, Connection& conn) {
    while (conn.out.ReadableBytes() > 0) {
        const ssize_t n =
            ::send(conn.socket.Fd(), conn.out.ReadPtr(), conn.out.ReadableBytes(), MSG_NOSIGNAL);
        if (n > 0) {
            conn.out.Consume(static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            worker.reactor.Modify(conn.socket.Fd(), EventMask(EPOLLIN | EPOLLOUT), &conn);
            return;
        }
        ScheduleClose(worker, id);
        return;
    }

    if (conn.close_after_write) {
        ScheduleClose(worker, id);
        return;
    }
    worker.reactor.Modify(conn.socket.Fd(), EventMask(EPOLLIN), &conn);
}

void TcpServer::ScheduleClose(Worker& worker, std::uint64_t id) { worker.to_close.insert(id); }

void TcpServer::ProcessClosures(Worker& worker) {
    for (std::uint64_t id : worker.to_close) {
        auto it = worker.connections.find(id);
        if (it == worker.connections.end()) {
            continue;
        }
        worker.reactor.Remove(it->second.socket.Fd());
        worker.connections.erase(it);
    }
    worker.to_close.clear();
}

}  // namespace campus::net
