#include "Module/Modules/SelfNameTag.h"

#include <intrin.h>

#include <atomic>
#include <cstring>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Context.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"
#include "Utils/Obfusc.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;
namespace field = offsets::field;

constexpr int kFirstPerson = 0;
constexpr int kThirdPersonFront = 2;

constexpr float kMinCameraGap = 0.25f;
constexpr float kMaxCameraGap = 1024.0f;

Detour<void(__fastcall*)(void*, void*, const void*, float)> g_renderText;
Detour<void*(__fastcall*)(void*, void*)> g_getOffset;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_frontView{true};

thread_local bool t_aimAtCamera = false;
thread_local float t_camera[3]{};

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

void aimAtCamera(const void* entity) {
    t_aimAtCamera = false;

    if (!memory::isReadable(entity, field::entity::pos + sizeof(float) * 3))
        return;

    const auto* position =
        reinterpret_cast<const float*>(static_cast<const uint8_t*>(entity) + field::entity::pos);

    const auto* camera = reinterpret_cast<const float*>(memory::rva(func::g_cameraPos));
    if (!memory::isReadable(camera, sizeof(float) * 3))
        return;

    const float dx = camera[0] - position[0];
    const float dz = camera[2] - position[2];
    const float gap = dx * dx + dz * dz;

    if (gap < kMinCameraGap || gap > kMaxCameraGap)
        return;

    std::memcpy(t_camera, camera, sizeof(t_camera));
    t_aimAtCamera = true;
}

void* __fastcall onGetOffset(void* self, void* out) {
    void* origin = g_getOffset.call(self, out);

    if (t_aimAtCamera && origin &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
            memory::rva(func::BaseEntityRenderer_textBillboardReturn))
        std::memcpy(origin, t_camera, sizeof(t_camera));

    return origin;
}

void __fastcall onRenderText(void* self, void* entity, const void* text, float partialTicks) {
    const bool own = g_enabled.load(std::memory_order_relaxed) && entity &&
                     entity == sdk::Context::get().localPlayer;

    if (own && !wanted())
        return;

    if (own)
        aimAtCamera(entity);

    g_renderText.call(self, entity, text, partialTicks);

    t_aimAtCamera = false;
}

bool install() {
    g_renderText.attach("EntityRenderer::renderText", memory::rva(func::EntityRenderer_renderText),
                        &onRenderText);
    g_getOffset.attach("EntityRenderer::_getOffset", memory::rva(func::EntityRenderer_getOffset),
                       &onGetOffset);
    return true;
}

const hooks::Installer g_installer{"SelfNameTag", &install};

}

SelfNameTag::SelfNameTag()
    : Module("SelfNameTag", "Draws your own nametag in third person", Category::Visuals),
      m_patch(BytePatch::nops(AERIAL_STR("48 3B DF 0F 84 4E 01 00 00"), 9)) {
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
