#pragma once

#include <string>

#include "campus/api/app_context.h"
#include "campus/api/http_types.h"
#include "campus/api/route_table.h"

namespace campus {

// 协议无关的业务分发：ApiRequest -> ApiResponse。
//
// 这里是全项目唯一的路由表：方法 + 路径 -> 处理函数。
// 两个 HTTP 引擎都只是把各自的请求结构转成 ApiRequest 再调它，
// 因此「换 HTTP 库」不会影响任何一条接口的行为。
//
// 内部已经把异常兜住了：任何异常都会变成 500 JSON，不会漏给调用方。
ApiResponse DispatchRequest(const ApiRequest& request, AppContext& ctx);

// 当前进程用的是哪个 HTTP 引擎（"net" = 自研 epoll 网络库，httplib = 对照组）。
// 只用于 /api/version 上报和压测脚本自检——脚本据此确认自己连的确实是预期引擎，
// 避免「压测打到了上一个没退干净的进程」这类假数据。
void SetHttpEngineName(std::string name);
const std::string& HttpEngineName();

}  // namespace campus
