#include "Module/ModuleManager.h"

#include <algorithm>

#include "Module/Modules/Modules.h"
#include "Utils/Logger.h"

namespace aerial {

ModuleManager& ModuleManager::get() {
    static ModuleManager instance;
    return instance;
}

void ModuleManager::registerAll() {
    if (!m_modules.empty())
        return;

    add<modules::AspectRatio>();
    add<modules::ChunkBorder>();
    add<modules::FogColor>();
    add<modules::FreeLook>();
    add<modules::FullBright>();
    add<modules::Hitboxes>();
    add<modules::ItemPhysics>();
    add<modules::JavaFov>();
    add<modules::MotionBlur>();
    add<modules::NoDynamicFov>();
    add<modules::NoHurtCam>();
    add<modules::NoRender>();
    add<modules::SelfNameTag>();
    add<modules::Skybox>();
    add<modules::Zoom>();

    add<modules::ArrayList>();
    add<modules::Notifications>();
    add<modules::Watermark>();

    add<modules::Aliases>();
    add<modules::AutoSprint>();
    add<modules::DiagonalSprint>();
    add<modules::ItemDelayFix>();
    add<modules::JavaHotkeys>();
    add<modules::NoCamReset>();
    add<modules::QuickSlots>();
    add<modules::SensMultiplier>();

    add<modules::ClickGuiModule>();
    add<modules::Overlay>();
    add<modules::NoVSync>();
    add<modules::PackSwitcher>();

    std::sort(m_modules.begin(), m_modules.end(),
              [](const auto& a, const auto& b) { return a->name() < b->name(); });

    LOG_INFO("Modules", "{} modules registered", m_modules.size());
}

void ModuleManager::shutdown() {
    for (auto& module : m_modules)
        module->setEnabled(false);
    m_modules.clear();
}

Module* ModuleManager::find(std::string_view name) const {
    const auto it = std::find_if(m_modules.begin(), m_modules.end(), [name](const auto& module) {
        if (module->name().size() != name.size())
            return false;
        return std::equal(name.begin(), name.end(), module->name().begin(),
                          [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    });
    return it != m_modules.end() ? it->get() : nullptr;
}

std::vector<Module*> ModuleManager::byCategory(Category category) const {
    std::vector<Module*> result;
    for (const auto& module : m_modules) {
        if (module->category() == category)
            result.push_back(module.get());
    }
    return result;
}

std::vector<Module*> ModuleManager::activeModules() const {
    std::vector<Module*> result;
    for (const auto& module : m_modules) {
        if (module->enabled())
            result.push_back(module.get());
    }
    return result;
}

bool ModuleManager::handleKey(int key, bool down) {
    if (!down || key == 0)
        return false;

    bool consumed = false;
    for (const auto& module : m_modules) {
        if (module->keybind() != key || module->holdBind())
            continue;

        module->toggle();
        consumed = true;
    }
    return consumed;
}

}
