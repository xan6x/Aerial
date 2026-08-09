#pragma once

#include <cstdint>
#include <string>

#include "Module/Module.h"

namespace aerial {
struct TickEvent;
}

namespace aerial::modules {

class Skybox final : public Module {
public:
    Skybox();

    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onTick(TickEvent& event);

    enum class Found { Nothing, Cubemap, EndSky };
    Found m_found = Found::Nothing;
    uint32_t m_lastGeneration = 0;
};

}
