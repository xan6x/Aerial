#include "Module/Modules/PotCounter.h"

#include <cstdint>
#include <string>

#include "Event/Events.h"
#include "GUI/HudStyle.h"
#include "GUI/Theme.h"
#include "Render/DrawUtils.h"
#include "SDK/Context.h"
#include "SDK/Offsets.h"
#include "Utils/Guard.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

using render::DrawUtils;

namespace field = offsets::field;

void* derefAt(void* p, ptrdiff_t off) {
    if (!p || !memory::isReadable(static_cast<uint8_t*>(p) + off, sizeof(void*)))
        return nullptr;
    return *reinterpret_cast<void**>(static_cast<uint8_t*>(p) + off);
}

std::string readStdString(const void* base) {
    const auto* p = static_cast<const uint8_t*>(base);
    if (!memory::isReadable(p, 0x20))
        return {};
    const size_t size = *reinterpret_cast<const size_t*>(p + 0x10);
    const size_t cap = *reinterpret_cast<const size_t*>(p + 0x18);
    if (size == 0 || size > 128)
        return {};
    const char* data =
        cap >= 16 ? *reinterpret_cast<const char* const*>(p) : reinterpret_cast<const char*>(p);
    if (!memory::isReadable(data, size))
        return {};
    return std::string(data, size);
}

int countPots(void* player) {
    void* proxy = derefAt(player, field::localPlayer::inventoryProxy);
    void* container = derefAt(proxy, 0x70);
    if (!container || !memory::isReadable(container, 0x100))
        return 0;

    auto** vt = *reinterpret_cast<void***>(container);
    if (!memory::isReadable(vt, 0x50))
        return 0;

    using SizeFn = int(__fastcall*)(void*);
    using ItemFn = void*(__fastcall*)(void*, int);
    const auto getSize = reinterpret_cast<SizeFn>(vt[9]);
    const auto getItem = reinterpret_cast<ItemFn>(vt[3]);

    const int size = getSize(container);

    int start = size >= 45 ? 9 : 0;
    int end = start + 36;
    if (end > size)
        end = size;

    int pots = 0;
    for (int i = start; i < end; ++i) {
        void* stack = getItem(container, i);
        if (!stack || !memory::isReadable(stack, field::itemStack::item + sizeof(void*)))
            continue;
        void* item = *reinterpret_cast<void**>(static_cast<uint8_t*>(stack) + field::itemStack::item);
        if (!item || !memory::isReadable(item, 0x58))
            continue;
        const std::string name = readStdString(static_cast<uint8_t*>(item) + 0x38);
        if (name.find("splash_potion") != std::string::npos)
            ++pots;
    }
    return pots;
}

}

PotCounter::PotCounter()
    : Module("PotCounter", "Counts the splash potions in your inventory", Category::Interface) {
    m_colour = addColour("Colour", "Number colour", Colour::rgb(0x6C8CFF));
    m_rainbow = addBool("Rainbow", "Cycle the number colour", false);
    m_colour->onlyIf([this] { return !m_rainbow->value; });
    m_background = addBool("Background", "Draw a panel behind the text", true);
    m_rounding = addFloat("Rounding", "Corner radius", 8.0f, 0.0f, 14.0f, 0.5f);

    m_posX = addFloat("PosX", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posY = addFloat("PosY", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posX->onlyIf([] { return false; });
    m_posY->onlyIf([] { return false; });
    m_drag.bind(m_posX, m_posY);

    listen<TickEvent>(&PotCounter::onTick);
    listen<Render2DEvent>(&PotCounter::onRender);
}

void PotCounter::onTick(TickEvent& event) {
    (void)event;
    auto* player = sdk::Context::get().localPlayer;
    if (!player) {
        m_pots.store(0, std::memory_order_relaxed);
        return;
    }
    int result = 0;
    guarded("pot counter", [&] { result = countPots(player); });
    m_pots.store(result, std::memory_order_relaxed);
}

void PotCounter::onRender(Render2DEvent& event) {
    const auto& theme = gui::Theme::get();
    const float s = DrawUtils::uiScale();

    const std::string number = std::to_string(m_pots.load(std::memory_order_relaxed));
    const std::string suffix = "POTS";
    const float numSize = 15.0f * s;
    const float labelSize = 10.0f * s;
    const float innerGap = 5.0f * s;
    const float pad = 7.0f * s;

    const float numW = DrawUtils::textWidth(number, numSize, DrawUtils::Weight::Bold);
    const float labelW = DrawUtils::textWidth(suffix, labelSize, DrawUtils::Weight::Medium);
    const float contentW = numW + innerGap + labelW;
    const float contentH = DrawUtils::textHeight(numSize);

    const Vec2 anchor = m_drag.place({contentW + pad * 2.0f, contentH + pad * 2.0f}, event.screenSize);
    const float radius = m_rounding->value * s;
    const Colour accent = m_rainbow->value ? theme.rainbowAt(0) : m_colour->value;

    if (m_background->value)
        hud::panel({anchor.x, anchor.y, anchor.x + contentW + pad * 2.0f,
                    anchor.y + contentH + pad * 2.0f},
                   radius + 2.0f * s, s);

    const float x = anchor.x + pad;
    const float y = anchor.y + pad;
    DrawUtils::text(number, {x, y}, accent, numSize, DrawUtils::Weight::Bold, DrawUtils::Align::Left);
    DrawUtils::text(suffix, {x + numW + innerGap, y + (numSize - labelSize) * 0.5f + 1.0f * s},
                    theme.textDim, labelSize, DrawUtils::Weight::Medium, DrawUtils::Align::Left);
}

}
