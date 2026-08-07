#include "Hooks/Hooks.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "Hooks/HookRegistry.h"
#include "Render/D2DOverlay.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"

namespace aerial::hooks {
namespace {

struct Registration {
    const char* name;
    bool (*install)();
};

std::vector<Registration>& registrations() {
    static std::vector<Registration> list;
    return list;
}

}

Installer::Installer(const char* name, bool (*install)()) {
    registrations().push_back({name, install});
}

bool installAll() {
    if (!HookManager::get().init())
        return false;

    auto& list = registrations();
    std::sort(list.begin(), list.end(), [](const Registration& a, const Registration& b) {
        return std::string_view(a.name) < std::string_view(b.name);
    });

    bool ok = true;
    for (const Registration& entry : list) {
        if (entry.install())
            continue;

        LOG_ERROR("Hooks", "{} could not install its hooks", entry.name);
        ok = false;
    }

    if (!ok)
        return false;

    LOG_INFO("Hooks", "{} hook groups installed", list.size());
    return HookManager::get().enableAll();
}

void removeAll() {
    render::D2DOverlay::get().setEnabled(false);
    HookManager::get().shutdown();
}

}
