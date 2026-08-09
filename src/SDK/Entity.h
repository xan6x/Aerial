#pragma once

#include <string>

#include "SDK/Offsets.h"
#include "SDK/Types.h"
#include "Utils/Memory.h"

namespace aerial::sdk {

namespace ev = offsets::vidx::entity;
namespace ef = offsets::field::entity;

class Entity {
public:
    Entity() = delete;
    ~Entity() = delete;

    Vec3& pos() { return fieldAt<Vec3>(this, ef::pos); }
    const Vec3& pos() const { return fieldAt<Vec3>(this, ef::pos); }

    Vec3& posOld() { return fieldAt<Vec3>(this, ef::posOld); }
    Vec3& velocity() { return fieldAt<Vec3>(this, ef::velocity); }

    Vec2& rot() { return fieldAt<Vec2>(this, ef::rot); }
    const Vec2& rot() const { return fieldAt<Vec2>(this, ef::rot); }
    Vec2& rotOld() { return fieldAt<Vec2>(this, ef::rotOld); }

    AABB worldAABB() const {
        return {fieldAt<Vec3>(this, ef::aabbMin), fieldAt<Vec3>(this, ef::aabbMax)};
    }

    Vec3 renderPos(float partialTicks) {
        Vec3 out;
        callVirtual<void>(this, ev::getPosExtrapolated, &out, partialTicks);
        return out;
    }

    float pitch() const { return rot().x; }
    float yaw() const { return rot().y; }

    Vec3 eyePos() const {
        Vec3 p = pos();
        p.y += const_cast<Entity*>(this)->eyeHeight();
        return p;
    }

    Vec3 lookDirection() const { return Vec3::fromAngles(rot()); }

    bool isAlive() const { return callVirtual<bool>(this, ev::isAlive); }
    bool isInWater() const { return callVirtual<bool>(this, ev::isInWater); }
    bool isInLava() const { return callVirtual<bool>(this, ev::isInLava); }
    bool isOnFire() const { return callVirtual<bool>(this, ev::isOnFire); }
    bool isInvisible() const { return callVirtual<bool>(this, ev::isInvisible); }
    bool isInWall() const { return callVirtual<bool>(this, ev::isInWall); }
    bool isImmobile() const { return callVirtual<bool>(this, ev::isImmobile); }
    bool isSprinting() const { return callVirtual<bool>(this, ev::isSprinting); }
    bool canShowNameTag() const { return callVirtual<bool>(this, ev::canShowNameTag); }

    void setSprinting(bool value) { callVirtual<void>(this, ev::setSprinting, value); }
    void setSneaking(bool value) { callVirtual<void>(this, ev::setSneaking, value); }
    void setOnFire(int seconds) { callVirtual<void>(this, ev::setOnFire, seconds); }

    float eyeHeight() { return callVirtual<float>(this, ev::getEyeHeight); }
    int dimensionId() { return callVirtual<int>(this, ev::getDimensionId); }
    int entityTypeId() { return callVirtual<int>(this, ev::getEntityTypeId); }
    float pickRadius() { return callVirtual<float>(this, ev::getPickRadius); }

    const std::string& nameTag() const {
        static const std::string empty;
        const auto* name = callVirtual<const std::string*>(this, ev::getNameTag);
        return name ? *name : empty;
    }

    void setPos(const Vec3& position) { callVirtual<void>(this, ev::setPos, &position); }
    void setRot(const Vec2& rotation) { callVirtual<void>(this, ev::setRot, &rotation); }
    void lerpMotion(const Vec3& motion) { callVirtual<void>(this, ev::lerpMotion, &motion); }
    void swing() { callVirtual<void>(this, ev::swing); }

    float distanceTo(const Entity& other) const { return pos().distance(other.pos()); }
    float distanceTo(const Vec3& point) const { return pos().distance(point); }

    bool isPlayer() { return entityTypeId() == 63; }

    AABB boundingBox() {
        const float radius = pickRadius() > 0.0f ? pickRadius() : 0.3f;
        const float rawHeight = fieldAt<float>(this, ef::bodyHeight);
        const float height = rawHeight > 0.0f ? rawHeight : 1.8f;
        const Vec3 p = pos();
        const float feet = p.y - eyeHeight();
        return {{p.x - radius, feet, p.z - radius}, {p.x + radius, feet + height, p.z + radius}};
    }

    Vec2 anglesFrom(const Vec3& from) {
        return from.anglesTo(pos() + Vec3{0.0f, eyeHeight() * 0.9f, 0.0f});
    }
};

class Mob : public Entity {
public:
    Mob() = delete;

    bool hasEffect(int effectId) {
        using Fn = bool(__fastcall*)(void*, int);
        return reinterpret_cast<Fn>(memory::rva(offsets::func::Mob_hasEffect))(this, effectId);
    }

    int armorValue() { return callVirtual<int>(this, ev::getArmor); }
};

class Player : public Mob {
public:
    Player() = delete;

    void* carriedItem() { return callVirtual<void*>(this, ev::getCarriedItem); }
    void openInventory() { callVirtual<void>(this, ev::openInventory); }
};

class LocalPlayer : public Player {
public:
    LocalPlayer() = delete;

    ClientInstance* clientInstance() const {
        return fieldAt<ClientInstance*>(this, offsets::field::localPlayer::clientInstance);
    }

    void displayClientMessage(const std::string& message) {
        callVirtual<void>(this, ev::displayClientMessage, &message);
    }
};

}
