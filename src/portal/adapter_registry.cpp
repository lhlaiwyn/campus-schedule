#include "campus/portal/adapter_registry.h"

#include "campus/portal/http_portal_adapter.h"
#include "campus/portal/mock_portal_adapter.h"

namespace campus {

AdapterRegistry& AdapterRegistry::Instance() {
    // C++11 起静态局部变量的初始化是线程安全的
    static AdapterRegistry registry;
    return registry;
}

void AdapterRegistry::Register(const std::string& name, Creator creator) {
    Instance().creators_[name] = std::move(creator);
}

std::unique_ptr<PortalAdapter> AdapterRegistry::Create(const std::string& name) {
    const auto& creators = Instance().creators_;
    const auto it = creators.find(name);
    if (it == creators.end()) {
        return nullptr;
    }
    return it->second();
}

std::vector<std::string> AdapterRegistry::Names() {
    std::vector<std::string> names;
    for (const auto& entry : Instance().creators_) {
        names.push_back(entry.first);
    }
    return names;
}

void RegisterBuiltinAdapters() {
    // mock：内存里的确定性实现，用于单元测试和默认演示
    AdapterRegistry::Register("mock", [] { return std::make_unique<MockPortalAdapter>(); });
    // http：真实的跨进程 HTTP 实现，对接模拟教务系统服务
    AdapterRegistry::Register("http", [] { return std::make_unique<HttpPortalAdapter>(); });
}

}  // namespace campus
