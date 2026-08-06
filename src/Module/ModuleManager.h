#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Module/Module.h"

namespace aerial {

// Owns every module instance and routes keybinds.
//
// Registering a new module is a single line in ModuleManager::registerAll():
//     add<KillAura>();
class ModuleManager {
public:
    static ModuleManager& get();

    void registerAll();
    void shutdown();

    const std::vector<std::unique_ptr<Module>>& modules() const { return m_modules; }

    Module* find(std::string_view name) const;
    std::vector<Module*> byCategory(Category category) const;

    // Modules that are enabled, sorted the way the array list wants them.
    std::vector<Module*> activeModules() const;

    template <typename T>
    T* get() const {
        for (const auto& module : m_modules) {
            if (auto* typed = dynamic_cast<T*>(module.get()))
                return typed;
        }
        return nullptr;
    }

    // Called by the input layer; returns true when a bind consumed the key.
    bool handleKey(int key, bool down);

private:
    ModuleManager() = default;

    template <typename T, typename... Args>
    T* add(Args&&... args) {
        auto module = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = module.get();
        m_modules.push_back(std::move(module));
        return raw;
    }

    std::vector<std::unique_ptr<Module>> m_modules;
};

} // namespace aerial
