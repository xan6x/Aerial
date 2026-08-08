#pragma once

#include "Utils/Math.h"

struct ID3D11ShaderResourceView;

namespace aerial::render {

namespace images {

ID3D11ShaderResourceView* get(int resourceId);

Vec2 size(int resourceId);

void releaseAll();

}
}
