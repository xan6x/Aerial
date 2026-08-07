#include "Module/Modules/Notifications.h"

#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Module/ModuleManager.h"
#include "Render/DrawUtils.h"

namespace aerial::modules {
namespace {

using render::DrawUtils;

constexpr float kToastHeight = 34.0f;
constexpr float kToastGap = 6.0f;
constexpr float kMargin = 12.0f;
constexpr float kToastTextSize = 13.0f;
constexpr int kSampleRate = 44100;

Notifications* g_instance = nullptr;

// A short two-note chime, synthesised rather than shipped as an asset: a pure
// sine is harsh, so each note gets a soft attack, an exponential decay and a
// quiet octave above it for warmth. The two notes are a perfect fifth apart,
// which is what makes it read as pleasant rather than as an alert.
std::vector<uint8_t> buildChime(bool rising, float volume) {
    constexpr float kNoteSeconds = 0.075f;
    constexpr float kTotalSeconds = kNoteSeconds * 2.0f;

    const float low = 880.0f;    // A5
    const float high = 1318.5f;  // E6
    const float first = rising ? low : high;
    const float second = rising ? high : low;

    const int frames = static_cast<int>(kSampleRate * kTotalSeconds);
    const int noteFrames = static_cast<int>(kSampleRate * kNoteSeconds);

    std::vector<int16_t> samples(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        const bool secondNote = i >= noteFrames;
        const int local = secondNote ? i - noteFrames : i;
        const float frequency = secondNote ? second : first;
        const float t = static_cast<float>(local) / kSampleRate;

        // 4 ms attack keeps the onset from clicking; the decay tail fades it out.
        const float attack = std::min(1.0f, t / 0.004f);
        const float decay = std::exp(-t * 26.0f);
        const float envelope = attack * decay;

        const float phase = 2.0f * kPi * frequency * t;
        const float wave = std::sin(phase) + 0.22f * std::sin(phase * 2.0f);

        const float value = wave * envelope * 0.38f * std::clamp(volume, 0.0f, 1.0f);
        samples[static_cast<size_t>(i)] =
            static_cast<int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
    }

    // Minimal 16-bit mono PCM WAV around the samples, for PlaySound(SND_MEMORY).
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    std::vector<uint8_t> wav(44 + dataBytes);

    auto put32 = [&wav](size_t offset, uint32_t value) { std::memcpy(&wav[offset], &value, 4); };
    auto put16 = [&wav](size_t offset, uint16_t value) { std::memcpy(&wav[offset], &value, 2); };

    std::memcpy(&wav[0], "RIFF", 4);
    put32(4, 36 + dataBytes);
    std::memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);                                    // fmt chunk size
    put16(20, 1);                                     // PCM
    put16(22, 1);                                     // mono
    put32(24, kSampleRate);
    put32(28, kSampleRate * 2);                       // byte rate
    put16(32, 2);                                     // block align
    put16(34, 16);                                    // bits per sample
    std::memcpy(&wav[36], "data", 4);
    put32(40, dataBytes);
    std::memcpy(&wav[44], samples.data(), dataBytes);

    return wav;
}

} // namespace

Notifications::Notifications()
    : Module("Notifications", "Toast popups with a soft chime", Category::Interface) {
    m_sound = addBool("Sound", "Play a chime with each notification", true);
    m_volume = addFloat("Volume", "Chime loudness", 0.5f, 0.0f, 1.0f, 0.05f);
    m_volume->onlyIf([this] { return m_sound->value; });
    m_duration = addFloat("Duration", "Seconds a toast stays on screen", 3.0f, 1.0f, 10.0f, 0.5f);
    m_maxVisible = addInt("Max", "How many toasts to stack", 5, 1, 10);
    m_corner = addEnum("Corner", "Where toasts appear", {"Bottom right", "Top right", "Bottom left"}, 0);
    m_moduleToggles = addBool("Module toggles", "Notify when a module is switched", true);

    g_instance = this;

    listenAlways<Render2DEvent>(&Notifications::onRender);
    listenAlways<ModuleToggleEvent>(&Notifications::onModuleToggle);

    setEnabled(true);
}

void Notifications::push(std::string text, Level level) {
    if (g_instance && g_instance->enabled())
        g_instance->add(std::move(text), level);
}

void Notifications::add(std::string text, Level level) {
    Toast toast;
    toast.text = std::move(text);
    toast.level = level;
    toast.born = gui::clockSeconds();
    toast.slide.set(1.0f);
    m_toasts.push_back(std::move(toast));

    while (m_toasts.size() > static_cast<size_t>(m_maxVisible->value) + 2)
        m_toasts.pop_front();

    if (m_sound->value)
        playChime(level != Level::Error && level != Level::Warning);
}

void Notifications::onModuleToggle(ModuleToggleEvent& event) {
    if (!enabled() || !m_moduleToggles->value || !event.module)
        return;

    // Do not announce ourselves, and do not announce the menu opening.
    if (event.module == this || !event.module->persistEnabled())
        return;

    add(event.module->name() + (event.enabled ? " enabled" : " disabled"),
        event.enabled ? Level::Success : Level::Info);
}

void Notifications::playChime(bool rising) {
    // Rebuild only when the volume changed; PlaySound keeps reading the buffer
    // asynchronously, so it has to outlive the call.
    if (m_builtVolume != m_volume->value) {
        PlaySoundW(nullptr, nullptr, 0);
        m_chimeUp = buildChime(true, m_volume->value);
        m_chimeDown = buildChime(false, m_volume->value);
        m_builtVolume = m_volume->value;
    }

    const auto& wav = rising ? m_chimeUp : m_chimeDown;
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

Colour Notifications::colourFor(Level level) const {
    switch (level) {
    case Level::Success: return Colour::rgb(0x34C759);
    case Level::Warning: return Colour::rgb(0xFFB020);
    case Level::Error:   return Colour::rgb(0xFF453A);
    case Level::Info:    break;
    }
    return gui::Theme::get().accent;
}

void Notifications::onRender(Render2DEvent& event) {
    if (m_toasts.empty())
        return;

    const auto& theme = gui::Theme::get();
    const float now = gui::clockSeconds();
    const bool left = m_corner->is("Bottom left");
    const bool top = m_corner->is("Top right");

    // Retire expired toasts once they have slid out.
    for (auto& toast : m_toasts) {
        if (!toast.expiring && now - toast.born > m_duration->value) {
            toast.expiring = true;
            toast.slide.set(0.0f);
        }
    }
    while (!m_toasts.empty() && m_toasts.front().expiring && m_toasts.front().slide.value() < 0.01f)
        m_toasts.pop_front();

    const float s = DrawUtils::uiScale();
    const float height = kToastHeight * s;
    const float margin = kMargin * s;
    const float radius = 9.0f * s;
    const float textSize = kToastTextSize * s;

    const int visible = std::min<int>(m_maxVisible->value, static_cast<int>(m_toasts.size()));
    const size_t firstIndex = m_toasts.size() - static_cast<size_t>(visible);

    float offset = 0.0f;
    for (size_t i = m_toasts.size(); i-- > firstIndex;) {
        Toast& toast = m_toasts[i];
        const float slide = toast.slide.update(theme.animationSpeed);
        if (slide < 0.01f)
            continue;

        const Colour accent = colourFor(toast.level);
        const float width = DrawUtils::textWidth(toast.text, textSize, DrawUtils::Weight::Medium) +
                            34.0f * s;

        // Slide in from the screen edge and fade with the same value.
        const float hidden = (1.0f - slide) * (width + margin);
        const float x = left ? margin - hidden : event.screenSize.x - margin - width + hidden;
        const float y = top ? margin + offset : event.screenSize.y - margin - height - offset;

        const Rect box{x, y, x + width, y + height};

        DrawUtils::shadow(box, Colour::rgb(0x000000, 0.45f * slide), 10.0f * s, radius,
                          {0.0f, 3.0f * s});
        DrawUtils::blurBehind(box, radius, 12.0f);
        DrawUtils::fill(box, theme.panel.withAlpha(theme.panel.a * slide), radius);
        DrawUtils::outline(box, theme.outline.withAlpha(theme.outline.a * slide), 1.0f * s, radius);

        // Accent dot instead of a bar: reads better on a rounded card.
        const float dot = 4.0f * s;
        const float cy = box.top + height * 0.5f;
        DrawUtils::fill({box.left + 12.0f * s - dot, cy - dot, box.left + 12.0f * s + dot, cy + dot},
                        accent.withAlpha(slide), dot);

        DrawUtils::text(toast.text, {box.left + 24.0f * s, cy - DrawUtils::textHeight(textSize) * 0.5f},
                        theme.textActive.withAlpha(slide), textSize, DrawUtils::Weight::Medium);

        // Remaining lifetime along the bottom edge.
        const float life = std::clamp(1.0f - (now - toast.born) / m_duration->value, 0.0f, 1.0f);
        if (life > 0.0f)
            DrawUtils::fill({box.left + radius, box.bottom - 2.0f * s,
                             box.left + radius + (width - radius * 2.0f) * life, box.bottom - 1.0f * s},
                            accent.withAlpha(0.5f * slide), 1.0f * s);

        offset += height + kToastGap * s;
    }
}

} // namespace aerial::modules
