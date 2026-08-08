#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "Utils/Math.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace aerial::render {

class D3DRenderer {
public:
    enum class Weight { Regular, Medium, SemiBold, Bold };

    static D3DRenderer& get();

    bool beginFrame(ID3D11Device* device, ID3D11DeviceContext* context,
                    ID3D11RenderTargetView* target, float width, float height);
    void endFrame();

    void releaseDeviceResources();

    void fillRect(const Rect& area, const Colour& colour, float radius = 0.0f);
    void outlineRect(const Rect& area, const Colour& colour, float thickness, float radius = 0.0f);
    void gradientRect(const Rect& area, const Colour& from, const Colour& to, bool vertical,
                      float radius = 0.0f);
    void image(ID3D11ShaderResourceView* texture, const Rect& dest, float opacity);
    void polygon(const Vec2* points, size_t count, const Colour& colour);

    void text(const std::string& value, float x, float y, const Colour& colour, float size,
              Weight weight);
    float measure(const std::string& value, float size, Weight weight);
    float lineHeight(float size) const;

    void pushScissor(const Rect& area);
    void popScissor();

    bool ready() const { return m_ready; }
    Vec2 size() const { return {m_width, m_height}; }

private:
    D3DRenderer() = default;

    bool ensureResources(ID3D11Device* device);

    bool m_ready = false;
    float m_width = 0.0f;
    float m_height = 0.0f;
};

}
