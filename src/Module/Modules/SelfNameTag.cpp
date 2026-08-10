#include "Module/Modules/SelfNameTag.h"

#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

#include "Event/Events.h"
#include "Module/Modules/FreeLook.h"
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

struct Colour4 {
    float r, g, b, a;
};

using FontDrawFn = void(__fastcall*)(void*, const void*, float, float, const Colour4*, bool, bool,
                                     void*, int32_t, bool);

constexpr uintptr_t kTessConstColour = 0x192AE08;

Detour<void(__fastcall*)(void*, void*, const void*, float)> g_renderText;
Detour<void*(__fastcall*)(void*, void*)> g_getOffset;
Detour<FontDrawFn> g_fontDraw;
Detour<void(__fastcall*)(void*, float, float, float)> g_matrixScale;
Detour<void(__fastcall*)(void*, char, int)> g_tessBegin;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_frontView{true};

std::atomic<bool> g_ignoreColour{false};
std::atomic<bool> g_textOn{false};
std::atomic<bool> g_bgOn{false};
std::atomic<bool> g_scaleOn{false};
std::atomic<float> g_textR{1.0f}, g_textG{1.0f}, g_textB{1.0f};
std::atomic<float> g_bgR{0.0f}, g_bgG{0.0f}, g_bgB{0.0f}, g_bgA{1.0f};
std::atomic<float> g_scale{1.0f};

thread_local bool t_aimAtCamera = false;
thread_local bool t_own = false;
thread_local bool t_bgDone = false;
thread_local float t_camera[3]{};

std::string stripCodes(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (i + 2 < in.size() && static_cast<uint8_t>(in[i]) == 0xC2 &&
            static_cast<uint8_t>(in[i + 1]) == 0xA7) {
            i += 3;
            continue;
        }
        if (i + 1 < in.size() && static_cast<uint8_t>(in[i]) == 0xA7) {
            i += 2;
            continue;
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

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

    float cam[3];

    if (freelook::active()) {
        const float yaw = freelook::cameraYaw() * kDeg2Rad;
        const float pitch = freelook::cameraPitch() * kDeg2Rad;
        const float cp = std::cos(pitch);
        const float fx = -std::sin(yaw) * cp;
        const float fy = -std::sin(pitch);
        const float fz = std::cos(yaw) * cp;

        const float sign = perspective() == kThirdPersonFront ? 1.0f : -1.0f;
        constexpr float kDist = 20.0f;
        cam[0] = position[0] + fx * sign * kDist;
        cam[1] = position[1] + 1.62f + fy * sign * kDist;
        cam[2] = position[2] + fz * sign * kDist;
    } else {
        const auto* camera = reinterpret_cast<const float*>(memory::rva(func::g_cameraPos));
        if (!memory::isReadable(camera, sizeof(float) * 3))
            return;

        const float dx = camera[0] - position[0];
        const float dz = camera[2] - position[2];
        const float gap = dx * dx + dz * dz;
        if (gap < kMinCameraGap || gap > kMaxCameraGap)
            return;

        std::memcpy(cam, camera, sizeof(cam));
    }

    std::memcpy(t_camera, cam, sizeof(t_camera));
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

void __fastcall onFontDraw(void* self, const void* text, float x, float y, const Colour4* colour,
                          bool a6, bool a7, void* a8, int32_t a9, bool a10) {
    if (t_own && colour && g_textOn.load(std::memory_order_relaxed)) {
        Colour4 mine = *colour;
        mine.r = g_textR.load(std::memory_order_relaxed);
        mine.g = g_textG.load(std::memory_order_relaxed);
        mine.b = g_textB.load(std::memory_order_relaxed);
        g_fontDraw.call(self, text, x, y, &mine, a6, a7, a8, a9, a10);
        return;
    }
    g_fontDraw.call(self, text, x, y, colour, a6, a7, a8, a9, a10);
}

void __fastcall onTessBegin(void* tess, char mode, int hint) {
    if (t_own && !t_bgDone && g_bgOn.load(std::memory_order_relaxed)) {
        t_bgDone = true;
        auto* slot = reinterpret_cast<float*>(memory::rva(kTessConstColour));
        if (memory::isReadable(slot, sizeof(float) * 4)) {
            slot[0] = g_bgR.load(std::memory_order_relaxed);
            slot[1] = g_bgG.load(std::memory_order_relaxed);
            slot[2] = g_bgB.load(std::memory_order_relaxed);
            slot[3] = g_bgA.load(std::memory_order_relaxed);
        }
    }
    g_tessBegin.call(tess, mode, hint);
}

void __fastcall onMatrixScale(void* matrix, float x, float y, float z) {
    if (t_own && g_scaleOn.load(std::memory_order_relaxed)) {
        const float k = g_scale.load(std::memory_order_relaxed);
        x *= k;
        y *= k;
        z *= k;
    }
    g_matrixScale.call(matrix, x, y, z);
}

void __fastcall onRenderText(void* self, void* entity, const void* text, float partialTicks) {
    t_own = false;
    t_aimAtCamera = false;

    const bool own = g_enabled.load(std::memory_order_relaxed) && entity &&
                     entity == sdk::Context::get().localPlayer;

    if (own && !wanted())
        return;

    if (own)
        aimAtCamera(entity);

    t_own = own;
    t_bgDone = false;

    if (own && g_ignoreColour.load(std::memory_order_relaxed) && text) {
        std::string stripped = stripCodes(*reinterpret_cast<const std::string*>(text));
        g_renderText.call(self, entity, &stripped, partialTicks);
    } else {
        g_renderText.call(self, entity, text, partialTicks);
    }

    t_own = false;
    t_aimAtCamera = false;
}

bool install() {
    g_renderText.attach("EntityRenderer::renderText", memory::rva(func::EntityRenderer_renderText),
                        &onRenderText);
    g_getOffset.attach("EntityRenderer::_getOffset", memory::rva(func::EntityRenderer_getOffset),
                       &onGetOffset);
    return true;
}

void syncHotHooks(bool font, bool scale, bool bg) {
    static bool lastFont = false, lastScale = false, lastBg = false;

    if (font && !g_fontDraw.attached())
        g_fontDraw.attach("Font::drawCached", memory::rva(func::Font_drawCached), &onFontDraw);
    if (scale && !g_matrixScale.attached())
        g_matrixScale.attach("Matrix::scale", memory::rva(func::Matrix_scale), &onMatrixScale);
    if (bg && !g_tessBegin.attached())
        g_tessBegin.attach("Tessellator::begin", memory::rva(func::Tessellator_begin), &onTessBegin);

    if (font != lastFont) {
        g_fontDraw.setActive(font);
        lastFont = font;
    }
    if (scale != lastScale) {
        g_matrixScale.setActive(scale);
        lastScale = scale;
    }
    if (bg != lastBg) {
        g_tessBegin.setActive(bg);
        lastBg = bg;
    }
}

const hooks::Installer g_installer{"SelfNameTag", &install};

}

SelfNameTag::SelfNameTag()
    : Module("SelfNameTag", "Draws your own nametag in third person", Category::Visuals),
      m_patch(BytePatch::nops(AERIAL_STR("48 3B DF 0F 84 4E 01 00 00"), 9)) {
    m_frontView = addBool("Front view", "Also draw it when the camera faces you", true);
    m_ignoreColour = addBool("Ignore server coloring", "Strip the server's name colours", false);
    m_useText = addBool("Text color", "Override the name colour", false);
    m_textColour = addColour("Text", "Name colour", Colour::rgb(0xFFFFFF));
    m_textColour->onlyIf([this] { return m_useText->value; });
    m_useBg = addBool("Background color", "Override the backing panel colour", false);
    m_bgColour = addColour("Background", "Backing panel colour", Colour(0.0f, 0.0f, 0.0f, 0.35f));
    m_bgColour->onlyIf([this] { return m_useBg->value; });
    m_scale = addFloat("Scale", "Nametag size", 1.0f, 0.4f, 2.5f, 0.05f);

    listen<Render2DEvent>(&SelfNameTag::onRender);
}

std::string SelfNameTag::suffix() const { return {}; }

void SelfNameTag::onRender(Render2DEvent& event) {
    (void)event;
    g_frontView.store(m_frontView->value, std::memory_order_relaxed);

    g_ignoreColour.store(m_ignoreColour->value, std::memory_order_relaxed);

    g_textOn.store(m_useText->value, std::memory_order_relaxed);
    g_textR.store(m_textColour->value.r, std::memory_order_relaxed);
    g_textG.store(m_textColour->value.g, std::memory_order_relaxed);
    g_textB.store(m_textColour->value.b, std::memory_order_relaxed);

    g_bgOn.store(m_useBg->value, std::memory_order_relaxed);
    g_bgR.store(m_bgColour->value.r, std::memory_order_relaxed);
    g_bgG.store(m_bgColour->value.g, std::memory_order_relaxed);
    g_bgB.store(m_bgColour->value.b, std::memory_order_relaxed);
    g_bgA.store(m_bgColour->value.a, std::memory_order_relaxed);

    const float scale = m_scale->value;
    const bool scaleOn = scale < 0.999f || scale > 1.001f;
    g_scaleOn.store(scaleOn, std::memory_order_relaxed);
    g_scale.store(scale, std::memory_order_relaxed);

    const bool active = wanted();
    if (active) {
        if (!m_patch.applied())
            m_patch.apply();
    } else if (m_patch.applied()) {
        m_patch.revert();
    }

    syncHotHooks(active && m_useText->value, active && scaleOn, active && m_useBg->value);
}

void SelfNameTag::onEnable() { g_enabled.store(true, std::memory_order_relaxed); }

void SelfNameTag::onDisable() {
    g_enabled.store(false, std::memory_order_relaxed);
    syncHotHooks(false, false, false);
    m_patch.revert();
}

}
