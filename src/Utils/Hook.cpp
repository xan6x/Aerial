#include "Utils/Hook.h"

#include <Windows.h>
#include <MinHook.h>
#include <algorithm>

namespace aerial {

HookManager& HookManager::get() {
    static HookManager instance;
    return instance;
}

bool HookManager::init() {
    if (m_initialised)
        return true;

    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR("Hook", "MH_Initialize failed: {}", MH_StatusToString(status));
        return false;
    }

    m_initialised = true;
    return true;
}

void HookManager::shutdown() {
    if (!m_initialised)
        return;

    // Disable everything in one transaction first: this suspends the game's
    // threads once and guarantees no thread is parked inside a trampoline.
    MH_DisableHook(MH_ALL_HOOKS);

    for (auto it = m_detours.rbegin(); it != m_detours.rend(); ++it)
        (*it)->detach();
    m_detours.clear();

    MH_Uninitialize();
    m_initialised = false;

    // Detours are removed, but a thread can still be executing code copied into
    // a trampoline. Give those frames time to unwind before the DLL is unmapped.
    Sleep(200);
}

bool HookManager::enableAll() {
    const MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        LOG_ERROR("Hook", "MH_EnableHook(ALL) failed: {}", MH_StatusToString(status));
        return false;
    }
    LOG_INFO("Hook", "{} hooks enabled", m_detours.size());
    return true;
}

void HookManager::registerDetour(DetourBase* detour) {
    if (std::find(m_detours.begin(), m_detours.end(), detour) == m_detours.end())
        m_detours.push_back(detour);
}

void HookManager::unregisterDetour(DetourBase* detour) {
    m_detours.erase(std::remove(m_detours.begin(), m_detours.end(), detour), m_detours.end());
}

namespace detail {

bool createHook(void* target, void* detour, void** original) {
    if (!HookManager::get().initialised() && !HookManager::get().init())
        return false;

    const MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        LOG_ERROR("Hook", "MH_CreateHook({}) failed: {}", target, MH_StatusToString(status));
        return false;
    }
    return true;
}

bool removeHook(void* target) {
    const MH_STATUS status = MH_RemoveHook(target);
    if (status != MH_OK && status != MH_ERROR_NOT_CREATED) {
        LOG_WARN("Hook", "MH_RemoveHook({}) failed: {}", target, MH_StatusToString(status));
        return false;
    }
    return true;
}

} // namespace detail
} // namespace aerial
