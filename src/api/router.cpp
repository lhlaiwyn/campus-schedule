#include "campus/api/router.h"

#include <exception>
#include <string>

#include <spdlog/spdlog.h>

#include "campus/api/dispatcher.h"

namespace campus {
namespace {

// httplib 请求 -> 平台无关的 ApiRequest
ApiRequest FromHttplib(const httplib::Request& request) {
    ApiRequest out;
    out.method = request.method;
    out.path = request.path;  // httplib 的 path 本身就不含查询串
    out.target = request.target;
    out.body = request.body;
    for (const auto& [name, value] : request.headers) {
        out.headers.emplace_back(name, value);
    }
    for (const auto& [name, value] : request.params) {
        out.query.emplace_back(name, value);
    }
    return out;
}

// ApiResponse -> httplib 响应
void ToHttplib(const ApiResponse& from, httplib::Response& to) {
    to.status = from.status;
    to.set_content(from.body, from.content_type);
    for (const auto& [name, value] : from.headers) {
        to.set_header(name, value);
    }
}

// 所有已注册路由共用同一个处理器：走哪个接口由分发层决定。
void Handle(const httplib::Request& request, httplib::Response& response, AppContext* ctx) {
    const ApiResponse result = DispatchRequest(FromHttplib(request), *ctx);
    ToHttplib(result, response);
}

}  // namespace

void RegisterRoutes(httplib::Server& server, AppContext& ctx) {
    const auto route = [&ctx](const httplib::Request& request, httplib::Response& response) {
        Handle(request, response, &ctx);
    };

    server.Get("/api/health", route);
    server.Get("/api/version", route);
    server.Post("/api/auth/login", route);
    server.Post("/api/sync", route);
    server.Get("/api/courses", route);
    server.Post("/api/courses", route);
    // 正则路由：/api/courses/<数字>。id 的具体解析仍留在分发层，
    // 两个引擎共用同一段判断，避免行为分叉。
    server.Get(R"(/api/courses/(\d+))", route);
    server.Put(R"(/api/courses/(\d+))", route);
    server.Delete(R"(/api/courses/(\d+))", route);

    // 兜底异常处理。正常情况下 DispatchRequest 自己已经把异常转成 500 JSON，
    // 这里只防「连请求转换都抛异常」的极端情况。
    server.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                    std::exception_ptr ep) {
        std::string message = "服务器内部错误";
        try {
            if (ep) {
                std::rethrow_exception(ep);
            }
        } catch (const std::exception& e) {
            message = e.what();
        } catch (...) {
        }
        spdlog::error("请求处理异常: {}", message);
        res.status = 500;
        res.set_content("{\"code\":500,\"error\":\"internal_error\",\"message\":\"" + message +
                            "\"}",
                        "application/json; charset=utf-8");
    });

    // 没有任何路由匹配时 httplib 会走到这里（状态码 404），
    // 让它调同一个分发层，保证两个引擎给出的 404 响应字节一致。
    server.set_error_handler([&ctx](const httplib::Request& req, httplib::Response& res) {
        // 业务处理器已经填好了带具体原因的 JSON 错误体，这里必须原样保留。
        // 早期版本会无条件覆盖，导致「时间冲突」这类可读原因被替换成通用文案。
        if (!res.body.empty()) {
            return;
        }
        if (res.status == 404) {
            const ApiResponse result = DispatchRequest(FromHttplib(req), ctx);
            if (result.status == 404) {
                ToHttplib(result, res);
                return;
            }
        }
        res.set_content("{\"code\":" + std::to_string(res.status) +
                            ",\"error\":\"request_failed\",\"message\":\"请求处理失败\"}",
                        "application/json; charset=utf-8");
    });
}

void RunServer(AppContext& ctx) {
    httplib::Server server;

    // 显式打开 TCP_NODELAY：httplib 默认关闭它，
    // 会让每个 keep-alive 请求多出约 40ms 的 Nagle / 延迟 ACK 等待。
    server.set_tcp_nodelay(true);

    RegisterRoutes(server, ctx);
    SetHttpEngineName("httplib");

    spdlog::info("HTTP 服务已启动（引擎: cpp-httplib）: http://{}:{}", ctx.config.server.host,
                 ctx.config.server.port);
    spdlog::info("试一下: curl http://{}:{}/api/health", ctx.config.server.host,
                 ctx.config.server.port);
    // 日志重定向到文件时是块缓冲的，不刷一下的话启动信息要等进程退出才落盘
    spdlog::default_logger()->flush();

    if (!server.listen(ctx.config.server.host, ctx.config.server.port)) {
        spdlog::error("监听失败，端口 {} 可能已被占用", ctx.config.server.port);
        return;
    }
    spdlog::info("HTTP 服务已退出");
}

}  // namespace campus
