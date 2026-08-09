#pragma once

#include "Event/Events.h"
#include "Module/Module.h"
#include "Utils/Patch.h"

namespace aerial::modules {

class NoRender : public Module {
public:
    NoRender();

protected:
    void onDisable() override;

private:
    void onTick(TickEvent& event);
    void sync();

    BoolSetting* m_nameTags;
    BoolSetting* m_flame;
    BoolSetting* m_particles;
    BoolSetting* m_armor;
    BoolSetting* m_clouds;
    BoolSetting* m_stars;
    BoolSetting* m_sunMoon;

    BytePatch m_nameTagsPatch;
    BytePatch m_flamePatch;
    BytePatch m_particlesPatch;
    BytePatch m_particlesBlendPatch;
    BytePatch m_armorPatch;
    BytePatch m_cloudsPatch;
};

}
