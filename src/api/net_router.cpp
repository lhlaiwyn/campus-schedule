#include "campus/api/net_router.h"

#include <algorithm>
#include <thread>

#include <spdlog/spdlog.h>

#include "campus/api/dispatcher.h"

namespace campus {
namespace {

// 自研网络库的请求 -> 平台无关的 ApiRequest
ApiRequest FromNet(const net::HttpRequest& request) {
    ApiRequest out;
    out.method = request.method;
    out.path = request.path;
    out.target = request.target;
    out.body = request.body;
    for (const auto& [name, value] : request.headers) {
        out.headers.emplace_back(name, value);
    }
    // 解析器已经做过百分号解码，这里直接搬
    for (const auto& [name, value] : request.query) {
        out.query.emplace_back(name, value);
    }
    return out;
}

// ApiResponse -> 自研网络库的响应
net::HttpResponse ToNet(const ApiResponse& from) {
    net::HttpResponse out;
    out.status = from.status;
    out.reason = net::ReasonPhrase(from.status);
    out.headers.emplace_back("Content-Type", from.content_type);
    for (const auto& [name, value] : from.headers) {
        out.headers.emplace_back(name, value);
    }
    out.body = from.body;
    return out;
}

// worker 线程数默认按 CPU 核数走，但不无脑开满：
// 连接是按 SO_REUSEPORT 分给各 worker 的，线程太多只会平白多出调度开销。
int ResolveWorkerCount(const AppConfig& config) {
    if (config.server.workers > 0) {
        return config.server.workers;
    }
    const unsigned hardware = std::thread::hardware_concurrency();
    const int auto_workers = hardware == 0 ? 1 : static_cast<int>(hardware);
    return std::clamp(auto_workers, 1, 16);
}

}  // namespace

net::TcpServer::Handler MakeNetHandler(AppContext& ctx) {
    return [&ctx](const net::HttpRequest& request) -> net::HttpResponse {
        return ToNet(DispatchRequest(FromNet(request), ctx));
    };
}

void RunNetServer(AppContext& ctx) {
    const int workers = ResolveWorkerCount(ctx.config);
    net::TcpServer server(ctx.config.server.host, ctx.config.server.port, MakeNetHandler(ctx),
                          workers, ctx.config.server.edge_triggered);
    SetHttpEngineName("net");

    spdlog::info("HTTP 服务已启动（引擎: 自研 epoll，{} 个 worker，{}）: http://{}:{}",
                 server.WorkerThreads(), server.EdgeTriggered() ? "边缘触发 ET" : "水平触发 LT",
                 ctx.config.server.host, ctx.config.server.port);
    spdlog::info("试一下: curl http://{}:{}/api/health", ctx.config.server.host,
                 ctx.config.server.port);
    spdlog::default_logger()->flush();

    if (!server.Run()) {
        spdlog::error("监听失败，端口 {} 可能已被占用", ctx.config.server.port);
        return;
    }
    spdlog::info("HTTP 服务已退出");
}

}  // namespace campus
