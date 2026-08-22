#pragma once
#include <string>
#include <vector>
#include <memory>

namespace me {

struct ModuleInfo {
    std::string id;
    std::string display_name;
    std::string icon;
    std::string description;
    std::string category;
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual ModuleInfo info() const = 0;
    virtual void activate() {}
    virtual void deactivate() {}
};

using ModulePtr = std::unique_ptr<IModule>;

class ModuleRegistry {
public:
    static ModuleRegistry& instance() {
        static ModuleRegistry r;
        return r;
    }
    void register_module(ModulePtr m) { modules_.push_back(std::move(m)); }
    const std::vector<ModulePtr>& modules() const { return modules_; }
private:
    std::vector<ModulePtr> modules_;
};

#define ME_REGISTER_MODULE(ClassName) \
    static bool _me_reg_##ClassName = []() { \
        me::ModuleRegistry::instance().register_module(std::make_unique<ClassName>()); \
        return true; \
    }();

} // namespace me
