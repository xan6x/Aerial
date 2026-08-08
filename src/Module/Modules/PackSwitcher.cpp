#include "Module/Modules/PackSwitcher.h"

#include "Utils/Logger.h"

namespace aerial::modules {

PackSwitcher::PackSwitcher()
    : Module("PackSwitcher", "Lets you change resource packs from inside a world or server",
             Category::Client),
      m_availableTile(BytePatch::nops("75 38 48 8B 12 E8 ? ? ? ? 48 8B 8B 10 05 00 00", 2)),
      m_selectedTile(BytePatch::nops("75 38 48 8B 12 E8 ? ? ? ? 48 8B 8B 08 05 00 00", 2)),
      m_commit(BytePatch::nops("0F 85 E3 01 00 00 49 8B 8E 28 05", 6)) {}

std::string PackSwitcher::suffix() const {
    if (!enabled())
        return {};
    const bool ok = m_availableTile.applied() && m_selectedTile.applied() && m_commit.applied();
    return ok ? std::string{} : "unavailable";
}

void PackSwitcher::onEnable() {
    const bool available = m_availableTile.apply();
    const bool selected = m_selectedTile.apply();
    const bool commit = m_commit.apply();

    if (available && selected && commit)
        return;

    LOG_ERROR("PackSwitcher", "could not unlock the pack list; reverting");
    onDisable();
}

void PackSwitcher::onDisable() {
    m_availableTile.revert();
    m_selectedTile.revert();
    m_commit.revert();
}

}
