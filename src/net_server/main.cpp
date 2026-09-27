// 自研 HTTP 服务器演示。
//
// 用法：
//   ./build/net_server [worker线程数] [每次请求的CPU工作量] [et]
//   ./build/net_server 4 0        # 4 个 worker，水平触发
//   ./build/net_server 4 2000 et  # 4 个 worker，CPU 密集，边缘触发
//
// 第二个参数用来把负载从「纯 I/O」变成「CPU 密集」。
// 为什么需要它：请求太轻时（比如只回显几个字节），单线程就能跑到 5 万 QPS，
// 瓶颈在回环网络和压测工具上，加线程反而只是增加开销。
// 只有让每个请求真的消耗 CPU，才能看出多 worker 有没有用上多核。

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "campus/net/http_response.h"
#include "campus/net/tcp_server.h"

namespace {

// 简单的 FNV-1a 哈希，重复 rounds 轮，用来模拟真实业务里的计算开销。
// 结果会写进响应，所以编译器不会把它优化掉。
std::uint64_t ComputeHash(const std::string& text, int rounds) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (int round = 0; round < rounds; ++round) {
        for (char ch : text) {
            hash ^= static_cast<unsigned char>(ch);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

}  // namespace

int main(int argc, char** argv) {
    int worker_threads = 1;
    int cpu_work = 0;
    bool edge_triggered = false;
    if (argc > 1) {
        const int parsed = std::atoi(argv[1]);
        worker_threads = parsed > 0 ? parsed : 1;
    }
    if (argc > 2) {
        const int parsed = std::atoi(argv[2]);
        cpu_work = parsed > 0 ? parsed : 0;
    }
    if (argc > 3 && std::string(argv[3]) == "et") {
        edge_triggered = true;
    }

    const campus::net::TcpServer::Handler handler =
        [cpu_work](const campus::net::HttpRequest& request) -> campus::net::HttpResponse {
        const std::uint64_t hash = ComputeHash(request.target, cpu_work);
        return campus::net::HttpResponse::MakeText(
            200,
            "{\"method\":\"" + request.method + "\",\"path\":\"" + request.path +
                "\",\"query\":\"" + request.Query("q") + "\",\"hash\":" +
                std::to_string(hash) + "}\n",
            "application/json; charset=utf-8");
    };

    campus::net::TcpServer server("0.0.0.0", 8081, handler, worker_threads, edge_triggered);
    std::printf("自研 HTTP 服务器已启动: http://127.0.0.1:8081"
                "（%d 个 worker 线程，每次请求 CPU 工作量 %d，%s）\n",
                worker_threads, cpu_work, edge_triggered ? "边缘触发 ET" : "水平触发 LT");
    std::printf("试一下: curl http://127.0.0.1:8081/anything\n");
    // stdout 重定向到文件时是全缓冲的，不显式 flush 的话日志要等进程退出才落盘，
    // 用 tail -f 或压测脚本读日志都看不到启动信息。
    std::fflush(stdout);

    if (!server.Run()) {
        std::fprintf(stderr, "监听失败\n");
        return 1;
    }
    return 0;
}
