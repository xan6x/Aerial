#pragma once

#include <string>

namespace aerial::packs {

struct SkyAssets {
    bool endSky = false;
    bool cubemapShader = false;
    bool faces = false;
    std::wstring facesDir;
};

SkyAssets scanActive();

}
