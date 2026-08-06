#include "Module/Modules/Modules.h"

#include "Event/Events.h"
#include "SDK/Offsets.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

// The present path branches on a single byte. Resolve it through the pointer
// the function reads, checking every link - this runs every frame and the
// object does not exist before the renderer is up.
uint8_t* vsyncFlag() {
    auto* holder = memory::at<uint8_t*>(offsets::data::presentConfig);
    if (!memory::isReadable(holder, sizeof(void*)))
        return nullptr;

    uint8_t* config = *holder;
    if (!memory::isReadable(config, offsets::field::presentConfig::vsync + 1))
        return nullptr;

    return config + offsets::field::presentConfig::vsync;
}

} // namespace

VSync::VSync() : Module("VSync", "Waits for the display refresh; turn off to uncap the frame rate",
                        Category::Misc) {
    // Enforced every frame regardless of state: switching the module off has to
    // keep the flag clear, not just clear it once.
    listenAlways<Render2DEvent>(&VSync::onRender);

    setEnabled(true);
}

std::string VSync::suffix() const { return enabled() ? "" : "uncapped"; }

void VSync::onEnable() { apply(true); }

void VSync::onDisable() { apply(false); }

void VSync::onRender(Render2DEvent& event) {
    (void)event;
    apply(enabled());
}

void VSync::apply(bool enabled) {
    uint8_t* flag = vsyncFlag();
    if (!flag) {
        if (!m_warned) {
            m_warned = true;
            LOG_WARN("VSync", "present config not reachable yet - flag {:#x} still null",
                     offsets::data::presentConfig);
        }
        return;
    }

    const uint8_t wanted = enabled ? 1 : 0;
    if (*flag != wanted)
        *flag = wanted;
}

} // namespace aerial::modules
