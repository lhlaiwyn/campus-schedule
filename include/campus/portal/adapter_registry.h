#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "campus/portal/portal_adapter.h"

namespace campus {

// 适配器注册表。
//
// 上层按名字取适配器，不接触任何具体类型；
// 新增一所学校只需要 Register 一次，符合开闭原则。
class AdapterRegistry {
public:
    using Creator = std::function<std::unique_ptr<PortalAdapter>()>;

    static AdapterRegistry& Instance();

    // 重复注册同名适配器会覆盖旧实现，方便测试替换
    static void Register(const std::string& name, Creator creator);

    // 名字不存在时返回 nullptr
    static std::unique_ptr<PortalAdapter> Create(const std::string& name);

    static std::vector<std::string> Names();

private:
    AdapterRegistry() = default;

    std::map<std::string, Creator> creators_;
};

// 进程启动时调用一次，注册内置适配器
void RegisterBuiltinAdapters();

}  // namespace campus

