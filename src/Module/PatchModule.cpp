#include "Module/PatchModule.h"

#include "Utils/Logger.h"

namespace aerial {

PatchModule::PatchModule(std::string name, std::string description, Category category,
                         BytePatch patch)
    : Module(std::move(name), std::move(description), category), m_patch(std::move(patch)) {}

std::string PatchModule::suffix() const {

    return enabled() && !m_patch.applied() ? "unavailable" : std::string{};
}

void PatchModule::onEnable() {
    if (!m_patch.apply())
        LOG_ERROR("Patch", "{} could not be applied", name());
}

void PatchModule::onDisable() { m_patch.revert(); }

}
