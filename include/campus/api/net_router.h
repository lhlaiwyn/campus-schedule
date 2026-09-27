#pragma once

#include "campus/api/app_context.h"
#include "campus/net/tcp_server.h"

namespace campus {

// 把业务分发包装成自研网络库的处理器。
// 单独暴露出来，是为了能在测试里不开监听端口就验证「请求进来能拿到正确响应」。
net::TcpServer::Handler MakeNetHandler(AppContext& ctx);

// 用自研 epoll 网络库启动 HTTP 服务（默认引擎），正常运行时阻塞不返回
void RunNetServer(AppContext& ctx);

}  // namespace campus
