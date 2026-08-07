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

struct MatrixGuard {
    void* stack = nullptr;
    void* matrix = nullptr;
};

void popMatrix(const MatrixGuard& guard) {
    if (!guard.stack || !memory::isReadable(guard.stack, 0x20))
        return;

    auto* bytes = static_cast<uint8_t*>(guard.stack);
    bytes[0x18] = 1;
    *reinterpret_cast<intptr_t*>(bytes + 8) -= 0x40;
}

struct Face {
    float normal[3];
    float right[3];
    float up[3];
};

constexpr Face kFaces[6] = {
    {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}},
    {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
};

constexpr float kU[4] = {0.0f, 1.0f, 1.0f, 0.0f};
constexpr float kV[4] = {0.0f, 0.0f, 1.0f, 1.0f};

void corner(const Face& face, int i, float out[3]) {
    const float u = kU[i] * 2.0f - 1.0f;
    const float v = 1.0f - kV[i] * 2.0f;
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = face.normal[axis] + face.right[axis] * u + face.up[axis] * v;
}

std::string facePath(int face) {
    return "textures/environment/overworld_cubemap/cubemap_" + std::to_string(face);
}

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

}

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
    // dropped, never destroyed: mce::TexturePtr's destructor address is unknown
    // and calling the wrong one frees textures the game is still drawing with.
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

    scale(guard.matrix, offsets::func::kPackCubeScale, offsets::func::kPackCubeScale,
          offsets::func::kPackCubeScale);

    for (int face = 0; face < 6; ++face) {
        void* texture = m_faces.data() + offsets::func::kTexturePtrSize * face;

        // required: begin() returns without resetting if a batch is still open,
        // and ours would then be appended to the game's.
        auto* state = static_cast<uint8_t*>(tessellator);
        state[0x170] = 0;
        state[0x125] = 0;

        begin(tessellator, 1, 8);

        colour(tessellator, 255, 255, 255, 255);

        for (int pass = 0; pass < 2; ++pass) {
            for (int step = 0; step < 4; ++step) {
                const int i = pass == 0 ? step : 3 - step;
                float position[3];
                corner(kFaces[face], i, position);
                vertexUV(tessellator, position[0], position[1], position[2], kU[i], kV[i]);
            }
        }

        const uint32_t emitted = *reinterpret_cast<const uint32_t*>(state + 0x168);
        if (emitted != 8 && !m_warnedBatch) {
            m_warnedBatch = true;
            LOG_WARN("Skybox", "the tessellator holds {} vertices where eight were emitted; "
                               "the batch is not ours alone",
                     emitted);
        }

        draw2(tessellator, material, texture);
    }

    if (!m_drewOnce) {
        m_drewOnce = true;
        LOG_INFO("Skybox", "cubemap drawn: six faces, both windings, sun material");
    }

    popMatrix(guard);
}

}
