#include "Module/Modules/ViewModel.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "Event/Events.h"
#include "Hooks/FovHooks.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

constexpr float kDegToRad = 0.017453292519943295f;

Detour<void(__fastcall*)(void*, char, float)> g_render;

std::atomic<bool> g_on{false};
std::atomic<float> g_posX{0.0f}, g_posY{0.0f}, g_posZ{0.0f};
std::atomic<float> g_rotX{0.0f}, g_rotY{0.0f}, g_rotZ{0.0f};
std::atomic<float> g_scaleX{1.0f}, g_scaleY{1.0f}, g_scaleZ{1.0f};

float* topMatrix() {
    auto* stack = memory::at<uint8_t>(func::g_skyMatrixStack);
    if (!memory::isReadable(stack + 8, sizeof(uintptr_t)))
        return nullptr;
    const uintptr_t end = *reinterpret_cast<uintptr_t*>(stack + 8);
    if (!end)
        return nullptr;
    auto* top = reinterpret_cast<float*>(end - 0x40);
    if (!memory::isReadable(top, 0x40))
        return nullptr;
    return top;
}

void applyTransform(float* top) {
    using Translate = void(__fastcall*)(void*, float, float, float);
    using Rotate = void(__fastcall*)(void*, float, float, float, float);
    using Scale = void(__fastcall*)(void*, float, float, float);

    auto translate = reinterpret_cast<Translate>(memory::rva(func::Matrix_translate));
    auto rotate = reinterpret_cast<Rotate>(memory::rva(func::Matrix_rotate));
    auto scale = reinterpret_cast<Scale>(memory::rva(func::Matrix_scale));

    const float px = g_posX.load(std::memory_order_relaxed);
    const float py = g_posY.load(std::memory_order_relaxed);
    const float pz = g_posZ.load(std::memory_order_relaxed);
    if (px != 0.0f || py != 0.0f || pz != 0.0f)
        translate(top, px, py, pz);

    const float rx = g_rotX.load(std::memory_order_relaxed);
    if (rx != 0.0f)
        rotate(top, rx * kDegToRad, 1.0f, 0.0f, 0.0f);
    const float ry = g_rotY.load(std::memory_order_relaxed);
    if (ry != 0.0f)
        rotate(top, ry * kDegToRad, 0.0f, 1.0f, 0.0f);
    const float rz = g_rotZ.load(std::memory_order_relaxed);
    if (rz != 0.0f)
        rotate(top, rz * kDegToRad, 0.0f, 0.0f, 1.0f);

    const float sx = g_scaleX.load(std::memory_order_relaxed);
    const float sy = g_scaleY.load(std::memory_order_relaxed);
    const float sz = g_scaleZ.load(std::memory_order_relaxed);
    if (sx != 1.0f || sy != 1.0f || sz != 1.0f)
        scale(top, sx, sy, sz);
}

void __fastcall onRender(void* self, char a2, float partialTicks) {
    if (!g_on.load(std::memory_order_relaxed)) {
        g_render.call(self, a2, partialTicks);
        return;
    }

    float* top = topMatrix();
    if (!top) {
        g_render.call(self, a2, partialTicks);
        return;
    }

    float saved[16];
    std::memcpy(saved, top, sizeof(saved));

    applyTransform(top);
    g_render.call(self, a2, partialTicks);

    std::memcpy(top, saved, sizeof(saved));
}

bool install() {
    g_render.attach("ItemInHandRenderer::render", memory::rva(func::ItemInHandRenderer_render),
                    &onRender);
    return true;
}

const hooks::Installer g_installer{"ViewModel", &install};

}

ViewModel::ViewModel()
    : Module("ViewModel", "Modify how the item in your hand looks", Category::Visuals) {
    m_itemFov =
        addFloat("Item FOV", "Changes the field of view of the held item", 70.0f, 1.0f, 179.0f, 1.0f);
    m_posX = addFloat("Position X", "Moves the item left/right", 0.0f, -4.0f, 4.0f, 0.01f);
    m_posY = addFloat("Position Y", "Moves the item up/down", 0.0f, -4.0f, 4.0f, 0.01f);
    m_posZ = addFloat("Position Z", "Moves the item forward/backward", 0.0f, -4.0f, 4.0f, 0.01f);
    m_rotX = addFloat("Rotation X", "Rotates around the X axis (pitch)", 0.0f, -180.0f, 180.0f, 1.0f);
    m_rotY = addFloat("Rotation Y", "Rotates around the Y axis (yaw)", 0.0f, -180.0f, 180.0f, 1.0f);
    m_rotZ = addFloat("Rotation Z", "Rotates around the Z axis (roll)", 0.0f, -180.0f, 180.0f, 1.0f);
    m_scaleX = addFloat("Scale X", "Scales the item on the X axis", 1.0f, -3.0f, 3.0f, 0.01f);
    m_scaleY = addFloat("Scale Y", "Scales the item on the Y axis", 1.0f, -3.0f, 3.0f, 0.01f);
    m_scaleZ = addFloat("Scale Z", "Scales the item on the Z axis", 1.0f, -3.0f, 3.0f, 0.01f);

    listenAlways<TickEvent>(&ViewModel::onTick);
}

void ViewModel::onTick(TickEvent&) { sync(); }

void ViewModel::sync() {
    const bool on = enabled();
    g_on.store(on, std::memory_order_relaxed);
    g_posX.store(m_posX->value, std::memory_order_relaxed);
    g_posY.store(m_posY->value, std::memory_order_relaxed);
    g_posZ.store(m_posZ->value, std::memory_order_relaxed);
    g_rotX.store(m_rotX->value, std::memory_order_relaxed);
    g_rotY.store(m_rotY->value, std::memory_order_relaxed);
    g_rotZ.store(m_rotZ->value, std::memory_order_relaxed);
    g_scaleX.store(m_scaleX->value, std::memory_order_relaxed);
    g_scaleY.store(m_scaleY->value, std::memory_order_relaxed);
    g_scaleZ.store(m_scaleZ->value, std::memory_order_relaxed);

    hooks::setItemFov(on ? m_itemFov->value : 0.0f);
}

void ViewModel::onEnable() { sync(); }

void ViewModel::onDisable() {
    g_on.store(false, std::memory_order_relaxed);
    hooks::setItemFov(0.0f);
}

}
