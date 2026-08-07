#include "Module/Modules/ClickGuiModule.h"

#include <Windows.h>

#include "Event/Events.h"
#include "GUI/ClickGui.h"

namespace aerial::modules {

ClickGuiModule::ClickGuiModule()
    : Module("ClickGui", "Opens the settings interface", Category::Client, VK_INSERT) {
    m_character = addEnum("Character", "Artwork behind the module list", {"None", "Rei", "Asuka"}, 1);
    m_characterOpacity = addFloat("Character opacity", "How strongly the artwork shows", 1.0f, 0.1f,
                                  1.0f, 0.05f);
    m_characterOpacity->onlyIf([this] { return !m_character->is("None"); });

    listenAlways<Render2DEvent>(&ClickGuiModule::onRender, kPriorityHighest);
    listenAlways<MouseEvent>(&ClickGuiModule::onMouse, kPriorityHighest);
    listenAlways<KeyEvent>(&ClickGuiModule::onKey, kPriorityHighest);
}

void ClickGuiModule::onEnable() { gui::ClickGui::get().open(); }

void ClickGuiModule::onDisable() { gui::ClickGui::get().close(); }

void ClickGuiModule::onRender(Render2DEvent& event) {

    if (enabled() && !gui::ClickGui::get().isOpen())
        setEnabled(false);

    gui::ClickGui::get().render(event);
}

void ClickGuiModule::onMouse(MouseEvent& event) { gui::ClickGui::get().onMouse(event); }

void ClickGuiModule::onKey(KeyEvent& event) { gui::ClickGui::get().onKey(event); }

}
