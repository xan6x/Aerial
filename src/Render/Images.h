#pragma once

#include "Utils/Math.h"

struct ID2D1Bitmap;

namespace aerial::render {

namespace images {

ID2D1Bitmap* get(int resourceId);

Vec2 size(int resourceId);

void releaseAll();

}
}
