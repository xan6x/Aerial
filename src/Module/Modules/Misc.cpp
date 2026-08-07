#include "Module/Modules/Modules.h"

#include <Windows.h>
#include <format>

#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "Render/D2DOverlay.h"
#include "SDK/Context.h"
#include "Utils/Logger.h"

namespace aerial::modules {

// ── ClickGui ─────────────────────────────────────────────────────────────────

ClickGuiModule::ClickGuiModule()
    : Module("ClickGui", "Opens the settings interface", Category::Misc, 'Y') {
    m_character = addEnum("Character", "Artwork behind the module list",
                          {"None", "Rei", "Asuka"}, 1);
    m_characterOpacity = addFloat("Character opacity", "How strongly the artwork shows", 1.0f, 0.1f,
                                  1.0f, 0.05f);
    m_characterOpacity->onlyIf([this] { return !m_character->is("None"); });

    // The GUI must react to input while it is open, so these listen regardless
    // of the module's own enabled state and check isOpen() themselves.
    listenAlways<Render2DEvent>(&ClickGuiModule::onRender, kPriorityHighest);
    listenAlways<MouseEvent>(&ClickGuiModule::onMouse, kPriorityHighest);
    listenAlways<KeyEvent>(&ClickGuiModule::onKey, kPriorityHighest);
}

void ClickGuiModule::onEnable() { gui::ClickGui::get().open(); }

void ClickGuiModule::onDisable() { gui::ClickGui::get().close(); }

void ClickGuiModule::onRender(Render2DEvent& event) {
    // Keep the module flag and the GUI in sync if the GUI closed itself.
    if (enabled() && !gui::ClickGui::get().isOpen())
        setEnabled(false);

    gui::ClickGui::get().render(event);
}

void ClickGuiModule::onMouse(MouseEvent& event) { gui::ClickGui::get().onMouse(event); }

void ClickGuiModule::onKey(KeyEvent& event) { gui::ClickGui::get().onKey(event); }

// ── Direct2D ─────────────────────────────────────────────────────────────────

Direct2D::Direct2D()
    : Module("Direct2D", "Draws the interface with Direct2D instead of the game's renderer",
             Category::Misc) {
    setEnabled(true);
}

std::string Direct2D::suffix() const {
    if (!enabled())
        return "off";
    return render::D2DOverlay::get().ready() ? "" : "unavailable";
}

void Direct2D::onEnable() { render::D2DOverlay::get().setEnabled(true); }

void Direct2D::onDisable() { render::D2DOverlay::get().setEnabled(false); }

// ── PacketLogger ─────────────────────────────────────────────────────────────

PacketLogger::PacketLogger()
    : Module("PacketLogger", "Logs outgoing packet ids - useful while reversing", Category::Misc) {
    m_toChat = addBool("Chat", "Also print into the in-game chat", false);
    listen<PacketSendEvent>(&PacketLogger::onPacket);
}

void PacketLogger::onPacket(PacketSendEvent& event) {
    const std::string line = std::format("out packet id {} ({:#x})", event.packetId, event.packetId);
    LOG_DEBUG("Packet", "{}", line);

    if (m_toChat->value)
        sdk::Context::get().chat("\xC2\xA7" "7[Aerial] " + line);
}

} // namespace aerial::modules
