#include "Module/Modules/MotionBlur.h"

#include "Event/Events.h"
#include "Render/Overlay.h"
#include "Render/MotionBlur.h"

namespace aerial::modules {

MotionBlur::MotionBlur()
    : Module("MotionBlur", "Blends the frame with the ones before it, so fast movement smears",
             Category::Visuals) {
    m_amount = addFloat("Amount", "How long a trail every frame leaves behind", 0.40f, 0.05f, 1.0f,
                        0.05f);
    m_opacity = addFloat("Opacity", "How strongly the blurred image covers the frame", 1.0f, 0.1f,
                         1.0f, 0.05f);

    listen<Render2DEvent>(&MotionBlur::onRender);
}

std::string MotionBlur::suffix() const {
    if (!enabled())
        return {};

    if (render::Overlay::get().presentCount() == 0)
        return "no present hook";

    return render::MotionBlur::get().failed() ? render::MotionBlur::get().status() : std::string{};
}

void MotionBlur::push() {
    auto& blur = render::MotionBlur::get();
    blur.setAmount(m_amount->value);
    blur.setOpacity(m_opacity->value);
}

void MotionBlur::onRender(Render2DEvent& event) {
    (void)event;
    push();
}

void MotionBlur::onEnable() {
    push();
    render::MotionBlur::get().setEnabled(true);
}

void MotionBlur::onDisable() { render::MotionBlur::get().setEnabled(false); }

}
