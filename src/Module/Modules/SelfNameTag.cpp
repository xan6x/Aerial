#include "Module/Modules/SelfNameTag.h"

#include <atomic>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Context.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

constexpr int kFirstPerson = 0;
constexpr int kThirdPersonFront = 2;

Detour<void(__fastcall*)(void*, void*, const void*, float)> g_renderText;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_frontView{true};

int perspective() {
    using Get = int(__fastcall*)(void*);
    auto get = reinterpret_cast<Get>(memory::rva(func::Options_getPlayerViewPerspective));
    return get(nullptr);
}

bool wanted() {
    const int view = perspective();
    if (view == kFirstPerson)
        return false;
    return view != kThirdPersonFront || g_frontView.load(std::memory_order_relaxed);
}

void __fastcall onRenderText(void* self, void* entity, const void* text, float partialTicks) {
    if (g_enabled.load(std::memory_order_relaxed) && entity == sdk::Context::get().localPlayer &&
        !wanted())
        return;

    g_renderText.call(self, entity, text, partialTicks);
}

bool install() {
    g_renderText.attach("EntityRenderer::renderText", memory::rva(func::EntityRenderer_renderText),
                        &onRenderText);
    return true;
}

const hooks::Installer g_installer{"SelfNameTag", &install};

}

SelfNameTag::SelfNameTag()
    : Module("SelfNameTag", "Draws your own nametag in third person", Category::Visuals) {
    m_frontView = addBool("Front view", "Also draw it when the camera faces you", true);

    listen<Render2DEvent>(&SelfNameTag::onRender);
}

std::string SelfNameTag::suffix() const {
    if (!enabled())
        return {};
    return m_patch.applied() ? std::string{} : "unavailable";
}

void SelfNameTag::onRender(Render2DEvent& event) {
    (void)event;
    g_frontView.store(m_frontView->value, std::memory_order_relaxed);
}

void SelfNameTag::onEnable() {
    if (!m_patch.apply()) {
        LOG_ERROR("SelfNameTag", "the camera-target check could not be patched");
        return;
    }

    g_enabled.store(true, std::memory_order_relaxed);
}

void SelfNameTag::onDisable() {
    g_enabled.store(false, std::memory_order_relaxed);
    m_patch.revert();
}

}
