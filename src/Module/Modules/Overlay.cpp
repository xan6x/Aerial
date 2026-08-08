#include "Module/Modules/Overlay.h"

#include "Render/Overlay.h"

namespace aerial::modules {

Overlay::Overlay()
    : Module("Overlay", "Draws the interface with the client's own renderer instead of the game's",
             Category::Client) {
    setEnabled(true);
}

std::string Overlay::suffix() const {
    if (!enabled())
        return "off";
    return render::Overlay::get().ready() ? "" : "unavailable";
}

void Overlay::onEnable() { render::Overlay::get().setEnabled(true); }

void Overlay::onDisable() { render::Overlay::get().setEnabled(false); }

}
