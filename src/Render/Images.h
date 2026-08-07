#pragma once

#include "Utils/Math.h"

struct ID2D1Bitmap;

namespace aerial::render {

// Decodes PNGs embedded in the DLL into Direct2D bitmaps, once each.
//
// The bitmaps belong to a device context, so the whole cache is dropped when
// the overlay's device goes away (a resize, or a lost device) and rebuilt
// lazily on the next request.
namespace images {

// Null until the Direct2D overlay is up, or if the resource fails to decode.
ID2D1Bitmap* get(int resourceId);

// Pixel size of the decoded image, {0,0} if unavailable.
Vec2 size(int resourceId);

void releaseAll();

} // namespace images
} // namespace aerial::render
