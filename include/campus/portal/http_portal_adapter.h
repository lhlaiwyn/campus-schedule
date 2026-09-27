#pragma once

#include <string>
#include <vector>

#include "campus/portal/portal_adapter.h"

namespace campus {

// 通过 HTTP 与教务系统通信的适配器。
//
// 目标地址从环境变量读取：
//   CAMPUS_PORTAL_HOST（默认 127.0.0.1）
//   CAMPUS_PORTAL_PORT（默认 9090）
//
// 之所以用环境变量而不是构造参数，是因为适配器由 AdapterRegistry
// 用统一的工厂签名创建，注册表不应该为某个具体实现开特例。
class HttpPortalAdapter : public PortalAdapter {
public:
    HttpPortalAdapter() = default;
    HttpPortalAdapter(std::string host, int port);

    std::string Name() const override { return "http"; }

    Result<std::string> Login(const PortalCredentials& credentials) override;

    Result<std::vector<RawScheduleEntry>> FetchSchedule(const std::string& token,
                                                        const std::string& semester) override;

    void Logout(const std::string& token) override;

    // 便于日志和排查
    std::string Endpoint() const;

private:
    std::string host_ = "127.0.0.1";
    int port_ = 9090;
    int timeout_seconds_ = 5;
};

}  // namespace campus

