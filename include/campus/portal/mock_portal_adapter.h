#pragma once

#include <string>
#include <vector>

#include "campus/portal/portal_adapter.h"

namespace campus {

// 模拟教务系统适配器。
//
// 不做任何网络请求，数据是确定的，因此可以用来：
//   1. 在没有真实教务系统的情况下把同步链路跑通；
//   2. 给单元测试提供稳定的输入，避免测试依赖外部服务。
//
// 真实学校的适配器（HttpPortalAdapter）实现同一个接口，
// 上层代码不需要知道自己拿到的是哪一个。
//
// 注意：这个实现把已登录的令牌存在实例内部，因此**实例必须被复用**。
// 每请求新建一个实例会让登录拿到的令牌立刻失效——
// 所以适配器由 main 在启动时创建一次，请求处理只做查找。
class MockPortalAdapter : public PortalAdapter {
public:
    std::string Name() const override { return "mock"; }

    Result<std::string> Login(const PortalCredentials& credentials) override;

    Result<std::vector<RawScheduleEntry>> FetchSchedule(const std::string& token,
                                                        const std::string& semester) override;

    void Logout(const std::string& token) override;

private:
    std::vector<std::string> active_tokens_;
};

}  // namespace campus
