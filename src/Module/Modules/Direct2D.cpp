#include "Module/Modules/Direct2D.h"

#include "Render/D2DOverlay.h"

namespace aerial::modules {

Direct2D::Direct2D()
    : Module("Direct2D", "Draws the interface with Direct2D instead of the game's renderer",
             Category::Client) {
    setEnabled(true);
}

std::string Direct2D::suffix() const {
    if (!enabled())
        return "off";
    return render::D2DOverlay::get().ready() ? "" : "unavailable";
}

void Direct2D::onEnable() { render::D2DOverlay::get().setEnabled(true); }

void Direct2D::onDisable() { render::D2DOverlay::get().setEnabled(false); }

} // namespace aerial::modules
