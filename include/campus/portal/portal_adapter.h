#pragma once

#include <string>
#include <vector>

#include "campus/domain/result.h"
#include "campus/portal/portal_types.h"

namespace campus {

// 教务系统适配器接口。
//
// 设计意图：不同学校的教务系统在登录方式、验证码、课表字段、
// 周次表达上都不同，但「登录 -> 拉取课表」这个流程是一样的。
// 把这些差异收敛到接口的实现类里，上层的同步流程就完全不用改。
//
// 新增一所学校 = 新写一个实现类 + 注册到 AdapterRegistry，
// 不需要修改任何已有代码。
class PortalAdapter {
public:
    PortalAdapter() = default;
    virtual ~PortalAdapter() = default;

    // 适配器只表示身份，不做拷贝：持有的是多态资源
    PortalAdapter(const PortalAdapter&) = delete;
    PortalAdapter& operator=(const PortalAdapter&) = delete;

    // 适配器名字，用于日志和配置选择，例如 "mock"
    virtual std::string Name() const = 0;

    // 登录成功返回会话令牌，后续请求都带着它
    virtual Result<std::string> Login(const PortalCredentials& credentials) = 0;

    // 拉取指定学期的原始课表
    virtual Result<std::vector<RawScheduleEntry>> FetchSchedule(
        const std::string& token, const std::string& semester) = 0;

    // 退出登录。失败不影响主流程，所以不返回 Result
    virtual void Logout(const std::string& token) = 0;
};

}  // namespace campus

