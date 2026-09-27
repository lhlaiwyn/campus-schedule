#pragma once

#include "campus/api/app_context.h"
#include "campus/api/httplib_config.h"

namespace campus {

// 注册全部路由（cpp-httplib 引擎）。
// 真正的处理逻辑在 api/dispatcher.cpp；这里只负责 httplib 请求/响应
// 与 ApiRequest/ApiResponse 之间的互转。
void RegisterRoutes(httplib::Server& server, AppContext& ctx);

// 用 cpp-httplib 启动服务，正常运行时阻塞不返回
void RunServer(AppContext& ctx);

}  // namespace campus
