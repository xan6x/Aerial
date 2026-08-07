#pragma once

#include <span>

#include "SDK/Entity.h"
#include "SDK/Offsets.h"
#include "SDK/Types.h"

namespace aerial::sdk {

template <typename T>
struct VectorView {
    T* first = nullptr;
    T* last = nullptr;
    T* capacity = nullptr;

    size_t size() const { return first && last >= first ? static_cast<size_t>(last - first) : 0; }
    bool empty() const { return size() == 0; }
    T* begin() const { return first; }
    T* end() const { return last; }
    std::span<T> span() const { return {first, size()}; }
};

class GameMode {
public:
    GameMode() = delete;

    void attack(Player* player, Entity* target) {
        callVirtual<void>(this, offsets::vidx::gameMode::attack, player, target);
    }

    void interact(Player* player, Entity* target) {
        callVirtual<void>(this, offsets::vidx::gameMode::interact, player, target);
    }

    float pickRange() { return callVirtual<float>(this, offsets::vidx::gameMode::getPickRange); }

    void releaseUsingItem(Player* player) {
        callVirtual<void>(this, offsets::vidx::gameMode::releaseUsingItem, player);
    }
};

class Level {
public:
    Level() = delete;

    VectorView<Player*> players() const {
        return fieldAt<VectorView<Player*>>(this, offsets::field::level::players);
    }
};

}
