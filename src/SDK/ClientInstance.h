#pragma once

#include "SDK/Entity.h"
#include "SDK/Offsets.h"
#include "SDK/Types.h"

namespace aerial::sdk {

class GuiData {
public:
    GuiData() = delete;

    void displayClientMessage(const std::string& message) {
        using Fn = void(__fastcall*)(void*, const std::string*);
        reinterpret_cast<Fn>(memory::rva(offsets::func::GuiData_displayClientMessage))(this, &message);
    }
};

class Font {
public:
    Font() = delete;

    void drawRaw(const std::string& text, float x, float y, const Color& colour) {
        using Fn = void(__fastcall*)(void*, const std::string*, float, float, const Color*, bool, bool,
                                     void*, int32_t, bool);
        reinterpret_cast<Fn>(memory::rva(offsets::func::Font_drawCached))(
            this, &text, x, y, &colour, false, false, nullptr, -1, false);
    }

    void draw(const std::string& text, float x, float y, const Color& colour, bool shadow = true) {
        if (shadow) {
            const Color backdrop{colour.r * 0.25f, colour.g * 0.25f, colour.b * 0.25f, colour.a};
            drawRaw(text, x + 1.0f, y + 1.0f, backdrop);
        }
        drawRaw(text, x, y, colour);
    }

    float width(const std::string& text, float scale = 1.0f) {
        using Fn = int(__fastcall*)(void*, const std::string*, float, bool);
        return static_cast<float>(
            reinterpret_cast<Fn>(memory::rva(offsets::func::Font_getLineLength))(this, &text, scale, false));
    }
};

class MinecraftGame {
public:
    MinecraftGame() = delete;

    Font* font() const { return fieldAt<Font*>(this, offsets::field::minecraftGame::font); }
    GuiData* guiData() const { return fieldAt<GuiData*>(this, offsets::field::minecraftGame::guiData); }

    bool mouseGrabbed() const {
        return fieldAt<uint8_t>(this, offsets::field::minecraftGame::mouseGrabbed) != 0;
    }

    void releaseMouse() {
        using Fn = void(__fastcall*)(void*);
        reinterpret_cast<Fn>(memory::rva(offsets::func::MinecraftGame_releaseMouse))(this);
    }
};

class ClientInstance {
public:
    ClientInstance() = delete;

    MinecraftGame* game() const {
        return fieldAt<MinecraftGame*>(this, offsets::field::clientInstance::minecraftGame);
    }

    LocalPlayer* localPlayer() const {
        return fieldAt<LocalPlayer*>(this, offsets::field::clientInstance::localPlayer);
    }

    Font* font() const {
        auto* game = this->game();
        return game ? game->font() : nullptr;
    }

    GuiData* guiData() const {
        auto* game = this->game();
        return game ? game->guiData() : nullptr;
    }

    bool mouseGrabbed() const {
        auto* game = this->game();
        return game && memory::isReadable(game, 0x200) && game->mouseGrabbed();
    }

    void releaseMouse() {
        auto* game = this->game();
        if (game && memory::isReadable(game, 0x200))
            game->releaseMouse();
    }

    void grabMouse() {
        using Fn = void(__fastcall*)(void*);
        reinterpret_cast<Fn>(memory::rva(offsets::func::ClientInstance_grabMouse))(this);
    }
};

}
