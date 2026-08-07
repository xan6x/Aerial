#include "Render/SkyCubemap.h"

#include <array>
#include <cstring>

#include "SDK/Context.h"
#include "SDK/Offsets.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::render {
namespace {

namespace func = offsets::func;

// ── The game's ResourceLocation ──────────────────────────────────────────────
//
// Never constructed by a call in the two places this was read from - both build
// the string, copy it in, zero the int and leave the second string empty. So it
// is an aggregate, and one this side can lay out itself.
//
// The std::string members are ours, passed to the game. Both binaries are MSVC
// x64 release builds and the layout has not moved in a decade - 16 bytes of
// small-string buffer, then size, then capacity - which is what makes that
// safe. The static_assert is the guard: if a future toolchain changes it, this
// stops compiling rather than handing the game a malformed string.
struct ResourceLocation {
    std::string path;
    int type = 0;
    std::string extra;
};
static_assert(sizeof(std::string) == 0x20, "the game expects MSVC's std::string layout");
static_assert(offsetof(ResourceLocation, type) == 0x20, "ResourceLocation.type moved");
static_assert(offsetof(ResourceLocation, extra) == 0x28, "ResourceLocation.extra moved");
static_assert(sizeof(ResourceLocation) == 0x48, "ResourceLocation is not 0x48 bytes");

using TexturePtrCtor = void*(__fastcall*)(void*, void*, const ResourceLocation*, int);
using TessBegin = void(__fastcall*)(void*, char, int);
using TessColor = void(__fastcall*)(void*, uint8_t, uint8_t, uint8_t, uint8_t);
using TessVertexUV = void(__fastcall*)(void*, float, float, float, float, float);
using TessDraw2 = void(__fastcall*)(void*, void*, void*);
using MatrixPush = void(__fastcall*)(void*, void*);
using MatrixScale = void(__fastcall*)(void*, float, float, float);

// The guard MatrixStack::push fills: the stack, then the new top matrix.
struct MatrixGuard {
    void* stack = nullptr;
    void* matrix = nullptr;
};

// Popping is inline everywhere in the game - mark the stack dirty and drop the
// top matrix - so there is no function to call for it.
void popMatrix(const MatrixGuard& guard) {
    if (!guard.stack || !memory::isReadable(guard.stack, 0x20))
        return;

    auto* bytes = static_cast<uint8_t*>(guard.stack);
    bytes[0x18] = 1;
    *reinterpret_cast<intptr_t*>(bytes + 8) -= 0x40;
}

// ── The cube ─────────────────────────────────────────────────────────────────
//
// Each face is given as the axis it faces along plus the two in-plane
// directions that carry its texture's u and v. Corners are then derived rather
// than written out, which is the point: six hand-written quads are six chances
// to wind one the wrong way round, and a face wound the wrong way is culled
// rather than drawn - a hole in the sky with whatever is behind it showing
// through. Derived this way they are all consistent by construction, and if the
// whole cube turns out inside-out it is one sign that fixes it, not six tables.
//
// Index order is the one packs are painted in, matching the menu panorama: the
// four sides going round, then up, then down.
struct Face {
    float normal[3];
    float right[3];   // +u
    float up[3];      // +v runs the other way, see below
};

constexpr Face kFaces[6] = {
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},   // 0 north
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},    // 1 east
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},     // 2 south
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},    // 3 west
    {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},     // 4 up
    {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},   // 5 down
};

// Texture coordinates, upper-left first and going round. The v axis is negated
// against `up` because image space runs downwards while the cube's does not.
constexpr float kU[4] = {0.0f, 1.0f, 1.0f, 0.0f};
constexpr float kV[4] = {0.0f, 0.0f, 1.0f, 1.0f};

// A corner of `face` for texture coordinate `i`: out along the normal, then
// across by u and down by v.
void corner(const Face& face, int i, float out[3]) {
    const float u = kU[i] * 2.0f - 1.0f;
    const float v = 1.0f - kV[i] * 2.0f;
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = face.normal[axis] + face.right[axis] * u + face.up[axis] * v;
}

// The pack path a face is loaded from. Not a printf: the game is handed this
// string directly, so it is built the boring way.
std::string facePath(int face) {
    return "textures/environment/overworld_cubemap/cubemap_" + std::to_string(face);
}

// [[ClientInstance + 0x30] + 0x80], guarded at every step. A null here is not a
// failure worth remembering - it only means the client is not far enough into
// startup to have one yet.
void* textureGroup() {
    namespace field = offsets::field::clientInstance;

    void* client = sdk::Context::get().client;
    if (!client || !memory::isReadable(client, field::textureContainer + sizeof(void*)))
        return nullptr;

    void* container = *reinterpret_cast<void**>(static_cast<uint8_t*>(client) + field::textureContainer);
    if (!container || !memory::isReadable(container, field::textureGroup + sizeof(void*)))
        return nullptr;

    return *reinterpret_cast<void**>(static_cast<uint8_t*>(container) + field::textureGroup);
}

} // namespace

SkyCubemap& SkyCubemap::get() {
    static SkyCubemap instance;
    return instance;
}

bool SkyCubemap::load() {
    if (m_ready)
        return true;
    if (m_failed)
        return false;

    void* group = textureGroup();
    if (!group) {
        // Not an error: no world, no ClientInstance, nothing to hang textures
        // off yet. Try again next frame.
        m_status = "waiting for the game's texture group";
        return false;
    }

    auto ctor = reinterpret_cast<TexturePtrCtor>(memory::rva(func::TexturePtr_ctor));

    m_faces.assign(offsets::func::kTexturePtrSize * 6, 0);

    for (int face = 0; face < 6; ++face) {
        ResourceLocation location;
        location.path = facePath(face);

        void* slot = m_faces.data() + offsets::func::kTexturePtrSize * face;
        ctor(slot, group, &location, 0);
    }

    m_ready = true;
    m_status = "ready";
    LOG_INFO("Skybox", "six cubemap faces resolved through the game's texture group");
    return true;
}

void SkyCubemap::unload() {
    // The handles are dropped without being destroyed on purpose.
    //
    // mce::TexturePtr holds a reference into the texture group, and releasing
    // it means calling a destructor whose address is a guess - there is no call
    // site to read it off, because every owner of one is a long-lived renderer
    // that never lets go. Calling the wrong function there frees something the
    // game is still drawing with. Leaking six references does not: the group
    // keeps six images alive until it is reloaded, which is bounded, invisible,
    // and costs a pack's worth of memory that was already loaded anyway.
    m_faces.clear();
    m_faces.shrink_to_fit();
    m_ready = false;
    m_status = "not loaded";
}

void SkyCubemap::draw(void* camera) {
    if (!m_ready || !camera)
        return;

    namespace field = offsets::field::levelRendererCamera;
    if (!memory::isReadable(camera, field::moonMaterial + 0x40))
        return;

    // The sun's material, not the sky's.
    //
    // The sky material tiles. That is not something either side chooses through
    // texture coordinates - the game's own quad builder clamps u and v to [0,1]
    // before storing them, and so does Tessellator::vertexUV - so the repetition
    // has to be in the shader, and the End's starfield repeating across its cube
    // is the same effect working as intended. Handing it a 2048-pixel photograph
    // gets the photograph repeated instead, which is exactly what showed up.
    //
    // The sun's material draws one image on one quad, which is the whole
    // requirement here. It is reached the same way, off the camera, and the sun
    // is drawn in this very pass - so its blend and depth state are known to
    // work in world space at sky distance.
    void* material = static_cast<uint8_t*>(camera) + field::sunMaterial;
    void* tessellator = reinterpret_cast<void*>(memory::rva(func::g_tessellator));
    void* stack = reinterpret_cast<void*>(memory::rva(func::g_skyMatrixStack));

    if (!memory::isReadable(tessellator, 0x140) || !memory::isReadable(stack, 0x20))
        return;

    auto push = reinterpret_cast<MatrixPush>(memory::rva(func::MatrixStack_push));
    auto scale = reinterpret_cast<MatrixScale>(memory::rva(func::Matrix_scale));
    auto begin = reinterpret_cast<TessBegin>(memory::rva(func::Tessellator_begin));
    auto colour = reinterpret_cast<TessColor>(memory::rva(func::Tessellator_colour));
    auto vertexUV = reinterpret_cast<TessVertexUV>(memory::rva(func::Tessellator_vertexUV));
    auto draw2 = reinterpret_cast<TessDraw2>(memory::rva(func::Tessellator_draw2));

    MatrixGuard guard;
    push(stack, &guard);
    if (!guard.matrix) {
        popMatrix(guard);
        return;
    }

    // Negative, like the End cube's own scale, so model space is inverted and
    // the quads face the camera from the inside - and a little smaller than it,
    // so the two are not coincident.
    scale(guard.matrix, offsets::func::kPackCubeScale, offsets::func::kPackCubeScale,
          offsets::func::kPackCubeScale);

    for (int face = 0; face < 6; ++face) {
        void* texture = m_faces.data() + offsets::func::kTexturePtrSize * face;

        // Tessellator::begin does nothing at all if a batch is already open -
        // it compares [this+0x170] against zero and returns. The game leaves one
        // open by the time the sky is finished, so the first version of this
        // appended four vertices to whatever was already in the buffer and then
        // drew the lot with a cubemap face on it: the whole sky tiled over with
        // the same image, which is exactly what it looked like.
        //
        // Clearing the flag makes begin do its full reset and discard the stale
        // batch. Nothing is lost by that - the sky pass is over, and anything
        // still pending was never going to be drawn.
        auto* state = static_cast<uint8_t*>(tessellator);
        state[0x170] = 0;
        state[0x125] = 0;

        begin(tessellator, 1, 8);

        // Full white: the material multiplies by the vertex colour, and whatever
        // the previous draw left behind would tint the whole sky.
        colour(tessellator, 255, 255, 255, 255);

        // Both windings, one quad each, in the same place.
        //
        // A quad is only drawn from the side its winding faces, and which side
        // that is depends on the material: the sky's material wanted one, the
        // sun's wants the other, and picking wrong means the sky is simply not
        // there. Rather than keep guessing which - a question with no answer in
        // the binary, since it lives in render state a material carries around -
        // both are emitted. The two are coincident, so whichever the material
        // rejects costs four vertices and nothing else.
        for (int pass = 0; pass < 2; ++pass) {
            for (int step = 0; step < 4; ++step) {
                const int i = pass == 0 ? step : 3 - step;
                float position[3];
                corner(kFaces[face], i, position);
                vertexUV(tessellator, position[0], position[1], position[2], kU[i], kV[i]);
            }
        }

        // Eight vertices in, eight expected out - two quads, one per winding.
        // Anything else means the batch was not ours alone, which is worth
        // saying out loud because it looks on screen exactly like a texture
        // wrapping problem and cannot be told from one by eye.
        const uint32_t emitted = *reinterpret_cast<const uint32_t*>(state + 0x168);
        if (emitted != 8 && !m_warnedBatch) {
            m_warnedBatch = true;
            LOG_WARN("Skybox", "the tessellator holds {} vertices where eight were emitted; "
                               "the batch is not ours alone",
                     emitted);
        }

        draw2(tessellator, material, texture);
    }

    // Said once, so an empty sky can be told from code that never ran. Every
    // step above bails quietly on a pointer it does not like, and without this
    // there is no way to know from outside which of the two happened.
    if (!m_drewOnce) {
        m_drewOnce = true;
        LOG_INFO("Skybox", "cubemap drawn: six faces, both windings, sun material");
    }

    popMatrix(guard);
}

} // namespace aerial::render
