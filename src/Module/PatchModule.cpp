#include "Module/PatchModule.h"

#include "Utils/Logger.h"

namespace aerial {

PatchModule::PatchModule(std::string name, std::string description, Category category,
                         BytePatch patch)
    : Module(std::move(name), std::move(description), category), m_patch(std::move(patch)) {}

std::string PatchModule::suffix() const {
    // Say so in the menu rather than sitting there pretending to work: a
    // signature that does not resolve means this build is not the one the
    // pattern was taken from.
    return enabled() && !m_patch.applied() ? "unavailable" : std::string{};
}

void PatchModule::onEnable() {
    if (!m_patch.apply())
        LOG_ERROR("Patch", "{} could not be applied", name());
}

void PatchModule::onDisable() { m_patch.revert(); }

} // namespace aerial
