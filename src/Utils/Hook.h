#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial {

class DetourBase {
public:
    virtual ~DetourBase() = default;
    virtual bool detach() = 0;
    virtual const std::string& name() const = 0;
    virtual uintptr_t targetAddress() const = 0;
};

class HookManager {
public:
    static HookManager& get();

    bool init();
    void shutdown();

    bool enableAll();

    void registerDetour(DetourBase* detour);
    void unregisterDetour(DetourBase* detour);

    std::vector<uintptr_t> hookedTargets() const;

    bool initialised() const { return m_initialised; }

private:
    HookManager() = default;

    std::vector<DetourBase*> m_detours;
    bool m_initialised = false;
};

namespace detail {

bool createHook(void* target, void* detour, void** original);
bool removeHook(void* target);
bool enableHook(void* target);
bool disableHook(void* target);
}

template <typename Fn>
class Detour final : public DetourBase {
public:
    Detour() = default;
    Detour(const Detour&) = delete;
    Detour& operator=(const Detour&) = delete;

    ~Detour() override {
        if (m_attached)
            detach();
        HookManager::get().unregisterDetour(this);
    }

    bool attach(std::string name, uintptr_t target, Fn detour) {
        m_name = std::move(name);

        if (!target) {
            LOG_ERROR("Hook", "{}: null target, skipped", m_name);
            return false;
        }

        m_target = reinterpret_cast<void*>(target);
        if (!detail::createHook(m_target, reinterpret_cast<void*>(detour),
                                reinterpret_cast<void**>(&m_original))) {
            LOG_ERROR("Hook", "{}: MinHook rejected target {:#x}", m_name, target);
            m_target = nullptr;
            return false;
        }

        m_attached = true;
        HookManager::get().registerDetour(this);
        LOG_DEBUG("Hook", "{} -> {:#x} (rva {:#x})", m_name, target, target - memory::base());
        return true;
    }

    bool detach() override {
        if (!m_attached)
            return true;
        const bool ok = detail::removeHook(m_target);
        m_attached = false;
        m_original = nullptr;
        m_target = nullptr;
        return ok;
    }

    void setActive(bool on) {
        if (!m_attached)
            return;
        if (on)
            detail::enableHook(m_target);
        else
            detail::disableHook(m_target);
    }

    template <typename... Args>
    auto call(Args&&... args) const -> decltype(std::declval<Fn>()(std::declval<Args>()...)) {
        return m_original(std::forward<Args>(args)...);
    }

    Fn original() const { return m_original; }
    bool attached() const { return m_attached; }
    const std::string& name() const override { return m_name; }
    uintptr_t targetAddress() const override { return reinterpret_cast<uintptr_t>(m_target); }

private:
    std::string m_name;
    void* m_target = nullptr;
    Fn m_original = nullptr;
    bool m_attached = false;
};

}
