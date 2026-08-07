#include "Module/Modules/NoVSync.h"

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

NoVSync::NoVSync()
    : Module("NoVSync", "Uncaps the frame rate by not waiting for the display refresh",
             Category::Client) {
    // Enforced every frame: the game rewrites the flag from its own settings,
    // so setting it once on enable would not hold.
    listen<Render2DEvent>(&NoVSync::onRender);
}

void NoVSync::onRender(Render2DEvent& event) {
    (void)event;

    uint8_t* flag = vsyncFlag();
    if (!flag) {
        if (!m_warned) {
            m_warned = true;
            LOG_WARN("NoVSync", "present config not reachable yet");
        }
        return;
    }

    // Remember what the game wanted the first time, so switching this off
    // restores its setting instead of guessing.
    if (*flag != 0) {
        m_hadVsync = true;
        *flag = 0;
    }
}

void NoVSync::onDisable() {
    if (uint8_t* flag = vsyncFlag(); flag && m_hadVsync)
        *flag = 1;
}

} // namespace aerial::modules
