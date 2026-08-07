#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Minecraft: Windows 10 Edition 1.1.5 (x64) — RVAs relative to image base
// 0x140000000, extracted from ida/Minecraft.Windows.exe.i64.
//
// Names marked "verified" were confirmed by disassembling the function; the
// rest come from the r_* symbols in the database. If the game binary is ever
// swapped for another build every value here becomes wrong — Aerial checks the
// module size at startup and refuses to hook on a mismatch.
// ─────────────────────────────────────────────────────────────────────────────

namespace aerial::offsets {

// ── Functions ────────────────────────────────────────────────────────────────
namespace func {

// Client / game loop
inline constexpr uintptr_t ClientInstance_ctor          = 0x119730;
inline constexpr uintptr_t ClientInstance_onTick        = 0x11B2D0;
// The database labels these two the other way round. Disassembly settles it:
// 0x13D920 tests the mouse-grabbed flag at [this+0xB8] directly, while 0x11BE90
// reaches it through [this+0x30] - so 0x13D920 takes a MinecraftGame and
// 0x11BE90 takes a ClientInstance. Calling them with the wrong receiver walks a
// garbage pointer chain and faults.
inline constexpr uintptr_t MinecraftGame_releaseMouse   = 0x13D920;  // verified
inline constexpr uintptr_t ClientInstance_grabMouse     = 0x11BE90;  // verified
inline constexpr uintptr_t ClientInstance_leaveGame     = 0x119C80;
inline constexpr uintptr_t ClientInstance_tickBuildAction = 0x11C350;  // per-tick attack/build action
inline constexpr uintptr_t MinecraftGame_update         = 0x1312F0;
inline constexpr uintptr_t MinecraftGame_tickInput      = 0x134110;
// MinecraftGame::update runs the frame as: startFrame -> Minecraft::update ->
// updateGraphics (all rendering, 3D then UI) -> endFrame. The overlay is drawn
// at the end of updateGraphics, which is after everything else in the frame.
inline constexpr uintptr_t MinecraftGame_updateGraphics = 0x131D00;
inline constexpr uintptr_t MinecraftGame_startFrame     = 0x7FA2D0;
inline constexpr uintptr_t MinecraftGame_endFrame       = 0x131DD0;
inline constexpr uintptr_t MinecraftGame_onPlayerLoaded = 0x138140;
inline constexpr uintptr_t MinecraftGame_leaveGame      = 0x138450;

// Entity
inline constexpr uintptr_t Entity_getPos                = 0x9C1700;  // verified: lea rax,[rcx+0x88]
inline constexpr uintptr_t Entity_getPosOld             = 0x9C1710;  // verified: lea rax,[rcx+0x94]
inline constexpr uintptr_t Entity_getPosExtrapolated    = 0x9C1720;
inline constexpr uintptr_t Entity_getVelocity           = 0x9C17E0;  // verified: lea rax,[rcx+0xac]
inline constexpr uintptr_t Entity_setPos                = 0x9C1600;
inline constexpr uintptr_t Entity_setRot                = 0x9C1F00;  // verified: writes +0xb8/+0xbc
inline constexpr uintptr_t Entity_getViewVector         = 0x9C1AC0;
inline constexpr uintptr_t Entity_getEyeHeight          = 0x9CD9E0;  // verified: [rcx+0x19c] * k
inline constexpr uintptr_t Entity_getNameTag            = 0x9CBE10;  // verified: EntityData slot 4
inline constexpr uintptr_t Entity_setNameTag            = 0x9CBEA0;
inline constexpr uintptr_t Entity_isAlive               = 0x9C72E0;
inline constexpr uintptr_t Entity_isInWater             = 0x9BED70;
inline constexpr uintptr_t Entity_isInLava              = 0x9BEF00;
inline constexpr uintptr_t Entity_isOnFire              = 0x9CBC90;
inline constexpr uintptr_t Entity_isInvisible           = 0x9CBCE0;
inline constexpr uintptr_t Entity_isInWall              = 0x9BED10;
inline constexpr uintptr_t Entity_move                  = 0x9BF2D0;
inline constexpr uintptr_t Entity_moveRelative          = 0x9C4BE0;
inline constexpr uintptr_t Entity_teleportTo            = 0x9C4CD0;
inline constexpr uintptr_t Entity_lerpMotion            = 0x9C7320;
inline constexpr uintptr_t Entity_turn                  = 0x9C2020;
inline constexpr uintptr_t Entity_lookAt                = 0xA1FF10;
inline constexpr uintptr_t Entity_getDimensionId        = 0x9CF4C0;
inline constexpr uintptr_t Entity_normalTick            = 0x9C21D0;
inline constexpr uintptr_t Entity_baseTick              = 0x9C2680;
inline constexpr uintptr_t Entity_getEntityTypeId       = 0x4A6710;  // Player override

// Mob
inline constexpr uintptr_t Mob_normalTick               = 0xA19E20;
inline constexpr uintptr_t Mob_aiStep                   = 0xA1F050;
inline constexpr uintptr_t Mob_swing                    = 0xA20E70;
inline constexpr uintptr_t Mob_setSprinting             = 0xA20730;
inline constexpr uintptr_t Mob_setSneaking              = 0xA20EF0;
inline constexpr uintptr_t Mob_isGliding                = 0xA206D0;
inline constexpr uintptr_t Mob_hasEffect                = 0xA24D10;
inline constexpr uintptr_t Mob_addEffect                = 0xA23D50;
inline constexpr uintptr_t Mob_removeEffect             = 0xA23EC0;
inline constexpr uintptr_t Mob_getArmor                 = 0xA21320;
inline constexpr uintptr_t Mob_jumpFromGround           = 0xA1FD10;

// Player
inline constexpr uintptr_t Player_ctor                  = 0xA41620;
inline constexpr uintptr_t Player_normalTick            = 0xA437C0;
inline constexpr uintptr_t Player_aiStep                = 0xA44CC0;
inline constexpr uintptr_t Player_attack                = 0xA480F0;
inline constexpr uintptr_t Player_interact              = 0xA47E80;
inline constexpr uintptr_t Player_useItem               = 0xA44390;
inline constexpr uintptr_t Player_isUsingItem           = 0xA43D10;
inline constexpr uintptr_t Player_startUsingItem        = 0xA43D90;
inline constexpr uintptr_t Player_stopUsingItem         = 0xA43E90;
inline constexpr uintptr_t Player_releaseUsingItem      = 0xA44180;
inline constexpr uintptr_t Player_completeUsingItem     = 0xA44210;
inline constexpr uintptr_t Player_getCarriedItem        = 0xA4A280;
inline constexpr uintptr_t Player_jumpFromGround        = 0xA44940;
inline constexpr uintptr_t Player_startGliding          = 0xA4CE40;
inline constexpr uintptr_t Player_stopGliding           = 0xA4CEF0;
inline constexpr uintptr_t Player_canDestroy            = 0xA45810;
inline constexpr uintptr_t Player_getDestroySpeed       = 0xA454C0;
inline constexpr uintptr_t Player_setPlayerGameType     = 0xA4B990;

// LocalPlayer
inline constexpr uintptr_t LocalPlayer_ctor             = 0x4A99A0;
inline constexpr uintptr_t LocalPlayer_normalTick       = 0x4AA600;
inline constexpr uintptr_t LocalPlayer_tickWorld        = 0x4AA320;
inline constexpr uintptr_t LocalPlayer_aiStep           = 0x4AB930;
inline constexpr uintptr_t LocalPlayer_move             = 0x4AC980;
inline constexpr uintptr_t LocalPlayer_travel           = 0x4AD250;
inline constexpr uintptr_t LocalPlayer_swing            = 0x4AEBF0;
inline constexpr uintptr_t LocalPlayer_setSneaking      = 0x4AECE0;
inline constexpr uintptr_t LocalPlayer_setSprinting     = 0x4AEDA0;
inline constexpr uintptr_t LocalPlayer_isSprinting      = 0xA20960;  // verified: EntityData flag 3
inline constexpr uintptr_t LocalPlayer_sendPosition     = 0x4AFCD0;
inline constexpr uintptr_t LocalPlayer_respawn          = 0x4AE870;
inline constexpr uintptr_t LocalPlayer_displayClientMessage = 0x4AF670;  // verified: -> GuiData
inline constexpr uintptr_t LocalPlayer_openInventory    = 0x4AF460;
inline constexpr uintptr_t LocalPlayer_clearMovementState = 0x444050;
inline constexpr uintptr_t LocalPlayer_updateAutoJump   = 0x4AD4B0;

// GameMode
inline constexpr uintptr_t GameMode_tick                = 0xA5B930;
inline constexpr uintptr_t GameMode_attack              = 0xA59E20;  // verified: tail-calls Player::attack
inline constexpr uintptr_t GameMode_interact            = 0xA59E10;
inline constexpr uintptr_t GameMode_useItem             = 0xA5B350;
inline constexpr uintptr_t GameMode_useItemOn           = 0xA5B030;
inline constexpr uintptr_t GameMode_getPickRange        = 0xA5B530;
inline constexpr uintptr_t GameMode_startDestroyBlock   = 0xA59E50;
inline constexpr uintptr_t GameMode_destroyBlock        = 0xA5A0E0;
inline constexpr uintptr_t GameMode_continueDestroyBlock = 0xA5A540;
inline constexpr uintptr_t GameMode_stopDestroyBlock    = 0xA5A910;
inline constexpr uintptr_t GameMode_startBuildBlock     = 0xA5AAF0;
inline constexpr uintptr_t GameMode_buildBlock          = 0xA5AB70;
inline constexpr uintptr_t GameMode_releaseUsingItem    = 0xA5B5D0;
inline constexpr uintptr_t GameMode_getDestroyRate      = 0xA5B740;

// Level / world
inline constexpr uintptr_t Level_tick                   = 0xB9F660;
inline constexpr uintptr_t Level_getPrimaryLocalPlayer  = 0xBA4C20;
inline constexpr uintptr_t Level_fetchEntity            = 0xBA1E80;
inline constexpr uintptr_t Level_getRuntimeEntity       = 0xBA20B0;
inline constexpr uintptr_t Level_addParticle            = 0xBA1390;
inline constexpr uintptr_t Level_playSound              = 0xBA0D70;
inline constexpr uintptr_t Level_broadcastEntityEvent   = 0xBA3220;
inline constexpr uintptr_t BlockSource_getEntities      = 0xB74EF0;

// Input
inline constexpr uintptr_t MoveInputHandler_tick        = 0x443DC0;
// The database calls this LocalPlayer::clearMovementState, but it zeroes the
// fields at +8/+0x42/+0x65 that MoveInputHandler::tick maintains, so `this` is
// a MoveInputHandler. Not called by Aerial: the qword at +8 is a pointer, and
// clearing it every tick (rather than at the safe points the game uses) made
// the game fault once input resumed. The small state fields are zeroed inline
// in the MoveInputHandler::tick hook instead.
inline constexpr uintptr_t MoveInputHandler_clearState  = 0x444050;
inline constexpr uintptr_t InputHandler_tick            = 0x7147D0;
inline constexpr uintptr_t MinecraftInputHandler_updateInputMode = 0x41F250;
inline constexpr uintptr_t ClientInputCallbacks_handleBuildOrAttackButtonPress = 0x42A520;
inline constexpr uintptr_t ClientInputCallbacks_handleInteractButtonPress      = 0x42A4C0;
inline constexpr uintptr_t ScreenView_handleTextChar    = 0x3EE620;
inline constexpr uintptr_t ScreenView_handlePointerLocation = 0x3EDE50;
// UI event pump: (this, events*). The second argument shows up only at the call
// site (lea rdx, [rbp-0x10] in ScreenView::ScreenView), never in the prologue.
inline constexpr uintptr_t ScreenView_processEvents    = 0x3F2470;

// Options::getFloat(options, id) - an FNV-1a lookup that returns the float at
// [entry+0x14]. It serves every float option, not just sensitivity, which is
// why scaling its result unconditionally (as zutil's tail patch does) would
// touch unrelated settings. Only rcx and edx are inputs; r8/r9 are overwritten
// before use, so the two-argument signature is the complete one.
inline constexpr uintptr_t Options_getFloat            = 0x495600;

// Options::getGamma(this) -> float. Single argument; the body only reads rcx.
inline constexpr uintptr_t Options_getGamma            = 0x4885C0;

// LevelRendererPlayer::getFov(this, partialTicks, bool) -> float. The body
// reads xmm1 and r8b, so all three are real arguments.
inline constexpr uintptr_t LevelRendererPlayer_getFov  = 0x5B6830;

// Matrix::translate(matrix, x, y, z) - writes the 64-byte result back through
// rcx. Verified: it stores xmm1/xmm2/xmm3 into a local vector and copies 0x40
// bytes into the matrix it was handed.
inline constexpr uintptr_t Matrix_translate            = 0x15D390;

// ItemRenderer::render is virtual and has no direct call site, so its argument
// count cannot be read anywhere. It does not need to be: the one thing that
// identifies a dropped item's transform is the return address of the translate
// it performs, which is the instruction after the call at 0x56FE7E.
inline constexpr uintptr_t ItemRenderer_render         = 0x56FCC0;
inline constexpr uintptr_t ItemRenderer_translateReturn = 0x56FE83;

// glm::rotate(matrix, angle, x, y, z) - five arguments, the fifth on the stack
// at [rsp+0x20]. Both call sites below set it up identically.
inline constexpr uintptr_t Matrix_rotate               = 0x180010;

// ItemRenderer::render turns the item twice after the translate we hook, which
// is why our own orientation was being overridden:
//   0x570319 rotates about Y by an angle derived from a direction vector - the
//            billboard that makes a sprite follow the camera.
//   0x5704F5 rotates about Y by the bob-derived angle - the vanilla spin.
// These are the return addresses, which is how each call is identified.
inline constexpr uintptr_t ItemRenderer_billboardReturn = 0x57031E;
inline constexpr uintptr_t ItemRenderer_spinReturn      = 0x5704FA;

// LevelRendererCamera::setupFog computes the fog colour from the biome's RGBA
// at [biome+0xD0], scales it by brightness and stores it to [this+0x3C8].
// It reads rcx, rdx and r8; its one call site sets no stack arguments.
inline constexpr uintptr_t LevelRendererCamera_setupFog = 0x5AFE00;

// Networking
inline constexpr uintptr_t LoopbackPacketSender_send    = 0x77ABC0;
inline constexpr uintptr_t NetworkHandler_update        = 0x77B710;
inline constexpr uintptr_t ClientNetworkHandler_handle_TextPacket = 0x476000;

// UI / rendering
inline constexpr uintptr_t InGamePlayScreen_render      = 0x3528C0;  // (this, ScreenContext*)
inline constexpr uintptr_t Screen_render                = 0x3B6D80;
inline constexpr uintptr_t Screen_renderToolBar         = 0x3B7390;  // calls ScreenRenderer::fill/blit
inline constexpr uintptr_t ScreenRenderer_singleton     = 0x1D9620;  // verified
inline constexpr uintptr_t ScreenRenderer_fill          = 0x1DAF10;  // verified: (this,x0,y0,x1,y1,&colour)
inline constexpr uintptr_t ScreenRenderer_drawRect      = 0x1DB3E0;
inline constexpr uintptr_t ScreenRenderer_blit          = 0x1DA3E0;
inline constexpr uintptr_t ScreenRenderer_reloadResources = 0x1D9690;
inline constexpr uintptr_t MinecraftUIRenderContext_fillRectangle = 0x5D5CB0;  // verified
inline constexpr uintptr_t MinecraftUIRenderContext_drawText      = 0x5D3D20;
inline constexpr uintptr_t MinecraftUIRenderContext_drawImage     = 0x5D45E0;
inline constexpr uintptr_t MinecraftUIRenderContext_getLineLength = 0x5D3C90;
inline constexpr uintptr_t Font_drawShadow              = 0x1C55C0;
inline constexpr uintptr_t Font_drawCached              = 0x1C8B60;  // verified via HUD call site
inline constexpr uintptr_t Font_drawTransformed         = 0x1C53D0;
inline constexpr uintptr_t Font_getLineLength           = 0x1C6530;  // verified
inline constexpr uintptr_t Tessellator_begin            = 0x5D0660;
inline constexpr uintptr_t Tessellator_vertex           = 0x5D0A90;
inline constexpr uintptr_t Tessellator_vertexUV         = 0x5D0960;
inline constexpr uintptr_t Tessellator_colour           = 0x5D0890;
inline constexpr uintptr_t Tessellator_end              = 0x5D14F0;

// Chat / HUD
inline constexpr uintptr_t GuiData_displayClientMessage = 0x1CDD30;
inline constexpr uintptr_t GuiData_addMessage           = 0x1CD9D0;
inline constexpr uintptr_t GuiData_showTipMessage       = 0x1CE380;
inline constexpr uintptr_t GuiData_tick                 = 0x1CD630;

} // namespace func

// ── Static data / vtables ────────────────────────────────────────────────────
namespace data {

inline constexpr uintptr_t LocalPlayer_vtable     = 0x1724CD8;
inline constexpr uintptr_t Player_vtable          = 0x1766348;
inline constexpr uintptr_t Mob_vtable             = 0x1757908;
inline constexpr uintptr_t GameMode_vtable        = 0x176CA40;
inline constexpr uintptr_t MovePlayerPacket_vtable = 0x1725AC8;
inline constexpr uintptr_t GuiData_GuiScale       = 0x16EC0A4;
inline constexpr uintptr_t MAX_REACH              = 0x11F57BC;

// Pointer to the object the present path reads its vsync flag from. The
// function the database calls "HereIsVSyncMazafaka" (0x72EC00) branches on
// byte [*presentConfig + 0x30]: 1 presents with SyncInterval 1, anything else
// presents with SyncInterval 0 and DXGI_PRESENT_ALLOW_TEARING.
inline constexpr uintptr_t presentConfig          = 0x194BC78;

} // namespace data

// ── Virtual table indices ────────────────────────────────────────────────────
// Taken from the vtable dumps; index * 8 is the byte offset into the table.
namespace vidx {

namespace entity {  // shared prefix of Entity/Mob/Player/LocalPlayer
inline constexpr int reset            = 9;
inline constexpr int remove           = 11;
inline constexpr int setPos           = 12;
inline constexpr int getPos           = 13;
inline constexpr int getPosOld        = 14;
inline constexpr int getPosExtrapolated = 15;
inline constexpr int getVelocity      = 16;
inline constexpr int setRot           = 17;
inline constexpr int move             = 18;
inline constexpr int moveRelative     = 23;
inline constexpr int tryTeleportTo    = 25;
inline constexpr int lerpMotion       = 27;
inline constexpr int turn             = 28;
inline constexpr int normalTick       = 31;
inline constexpr int rideTick         = 33;
inline constexpr int startRiding      = 36;
inline constexpr int intersects       = 39;
inline constexpr int isInWall         = 42;
inline constexpr int isInvisible      = 43;
inline constexpr int canShowNameTag   = 44;
inline constexpr int getNameTag       = 47;
inline constexpr int setNameTag       = 49;
inline constexpr int isInWater        = 50;
inline constexpr int isInWaterOrRain  = 51;
inline constexpr int isInLava         = 52;
inline constexpr int isUnderLiquid    = 53;
inline constexpr int isSkyLit         = 60;
inline constexpr int getBrightness    = 61;
inline constexpr int isImmobile       = 67;
inline constexpr int isSilent         = 68;
inline constexpr int isAlive          = 69;
inline constexpr int isOnFire         = 76;
inline constexpr int handleEntityEvent = 101;
inline constexpr int getPickRadius    = 102;
inline constexpr int getEntityTypeId  = 116;
inline constexpr int setOnFire        = 120;
inline constexpr int getDimensionId   = 125;
inline constexpr int changeDimension  = 127;
inline constexpr int getEyeHeight     = 141;
inline constexpr int stopRiding       = 144;
inline constexpr int useItem          = 147;
inline constexpr int getDebugText     = 150;
inline constexpr int setSize          = 163;
inline constexpr int setSneaking      = 187;
inline constexpr int isSprinting      = 188;
inline constexpr int setSprinting     = 189;
inline constexpr int travel           = 204;
inline constexpr int aiStep           = 207;
inline constexpr int lookAt           = 209;
inline constexpr int getCarriedItem   = 216;
inline constexpr int swing            = 221;
inline constexpr int getArmor         = 246;
inline constexpr int getWaterSlowDown = 261;
inline constexpr int setOffhandSlot   = 262;
inline constexpr int getYHeadRot      = 267;
inline constexpr int jumpFromGround   = 270;
inline constexpr int updateAi         = 271;
inline constexpr int tickDeath        = 280;
inline constexpr int tickWorld        = 291;
inline constexpr int respawn          = 296;
inline constexpr int resetRot         = 297;
inline constexpr int resetPos         = 298;
inline constexpr int completeUsingItem = 301;
inline constexpr int openContainer    = 308;
inline constexpr int openInventory    = 325;
inline constexpr int displayClientMessage = 328;
inline constexpr int setPlayerGameType = 340;
} // namespace entity

namespace gameMode {
inline constexpr int destructor           = 0;
inline constexpr int startDestroyBlock    = 1;
inline constexpr int destroyBlock         = 2;
inline constexpr int continueDestroyBlock = 3;
inline constexpr int stopDestroyBlock     = 4;
inline constexpr int startBuildBlock      = 5;
inline constexpr int buildBlock           = 6;
inline constexpr int continueBuildBlock   = 7;
inline constexpr int tick                 = 9;
inline constexpr int getPickRange         = 10;
inline constexpr int useItem              = 11;
inline constexpr int useItemOn            = 12;
inline constexpr int interact             = 13;
inline constexpr int attack               = 14;
inline constexpr int releaseUsingItem     = 15;
} // namespace gameMode

namespace packet {
inline constexpr int destructor = 0;
inline constexpr int getId      = 1;
inline constexpr int getName    = 2;
inline constexpr int write      = 3;
inline constexpr int read       = 4;
inline constexpr int handle     = 5;
} // namespace packet

} // namespace vidx

// ── Member offsets ───────────────────────────────────────────────────────────
namespace field {

namespace entity {
inline constexpr ptrdiff_t pos           = 0x88;   // Vec3, verified
inline constexpr ptrdiff_t posOld        = 0x94;   // Vec3, verified
inline constexpr ptrdiff_t posExtrapolated = 0xA0; // Vec3
inline constexpr ptrdiff_t velocity      = 0xAC;   // Vec3, verified
inline constexpr ptrdiff_t rot           = 0xB8;   // Vec2 {pitch, yaw}, verified
inline constexpr ptrdiff_t rotOld        = 0xC0;   // Vec2
// Entity::save writes this through CompoundTag::putByte("OnGround") and the
// matching load writes the result straight back to it, so both directions name
// the same field.
inline constexpr ptrdiff_t onGround      = 0x12E;  // bool, verified
inline constexpr ptrdiff_t flags         = 0xEC;   // bitfield read by GameMode::attack
inline constexpr ptrdiff_t entityData    = 0xF0;   // vector<EntityDataItem*> begin/end
inline constexpr ptrdiff_t bodyHeight    = 0x19C;  // scaled by getEyeHeight
} // namespace entity

// The dropped-item entity. ItemRenderer::render takes the actor in rdx and
// immediately does `lea r12, [rdx + 0xE58]`, then reads the stack through r12.
namespace itemActor {
inline constexpr ptrdiff_t itemStack = 0xE58;
inline constexpr ptrdiff_t age       = 0xEA8;  // int, ticks; drives the bob
inline constexpr ptrdiff_t bobOffset = 0xEB4;  // float, per-item bob phase
inline constexpr ptrdiff_t noBob     = 0xEC0;  // bool; when set the game skips its own bob
} // namespace itemActor

// Read at 0x56FE83 onwards: the block pointer is tested first and the item
// pointer second, which is exactly how the renderer decides whether to draw a
// cube or a flat sprite.
namespace itemStack {
inline constexpr ptrdiff_t count = 0x00;  // byte
inline constexpr ptrdiff_t item  = 0x10;  // Item*
inline constexpr ptrdiff_t block = 0x18;  // Block*, null for non-block items
} // namespace itemStack

namespace localPlayer {
// LocalPlayer::displayClientMessage walks [this+0x15F0] -> [+0x30] -> [+0x170].
inline constexpr ptrdiff_t clientInstance = 0x15F0;
} // namespace localPlayer

namespace clientInstance {
inline constexpr ptrdiff_t minecraftGame = 0x30;   // from the chain above
inline constexpr ptrdiff_t options       = 0x38;   // read by ClientInstance::onTick
inline constexpr ptrdiff_t cameraTarget  = 0x50;   // InGamePlayScreen::render, falls back to +0x60
inline constexpr ptrdiff_t localPlayer   = 0x60;   // getCarriedItem (vidx 216) is called on it
} // namespace clientInstance

namespace minecraftGame {
inline constexpr ptrdiff_t font         = 0x88;    // HudPlayerPositionRenderer::render
inline constexpr ptrdiff_t guiData      = 0x170;   // HudVignetteRenderer::_renderVignette
inline constexpr ptrdiff_t options      = 0x168;   // ClientInputCallbacks read [+0x71] from it
inline constexpr ptrdiff_t mouseGrabbed = 0xB8;    // set by grabMouse, cleared by releaseMouse

// MinecraftGame::tickInput reaches the InputHandler as [[this+0x168]+0x50], and
// MinecraftScreenController::setSuspendInput walks the same pair - two
// independent confirmations of the chain.
inline constexpr ptrdiff_t inputHolder  = 0x168;
} // namespace minecraftGame

namespace inputHolder {
inline constexpr ptrdiff_t inputHandler = 0x50;
} // namespace inputHolder

namespace inputHandler {
// setSuspendInput writes its bool argument here. Non-zero means the game
// ignores gameplay input, which is how an open screen stops movement.
inline constexpr ptrdiff_t suspended = 0xFC;
} // namespace inputHandler

namespace moveInput {
// Fields MoveInputHandler::tick maintains. The two floats are the movement
// amounts it feeds the player, read at the end of the tick as [this+0x6C] and
// [this+0x70]; zeroing only the byte state left them intact, which is why
// movement still leaked through while the menu was open.
inline constexpr ptrdiff_t flagA     = 0x41;
inline constexpr ptrdiff_t direction = 0x42;   // word of direction bits
inline constexpr ptrdiff_t flagB     = 0x47;
inline constexpr ptrdiff_t state     = 0x65;   // dword
inline constexpr ptrdiff_t amountX   = 0x6C;   // float
inline constexpr ptrdiff_t amountY   = 0x70;   // float
} // namespace moveInput

namespace levelRendererCamera {
// Fog colour as four floats. setupFog writes it with a single movups, and both
// of its branches target the same field.
inline constexpr ptrdiff_t fogColour = 0x3C8;
} // namespace levelRendererCamera

namespace screen {
// Screen+0x30 is the ClientInstance every Screen::render implementation walks.
inline constexpr ptrdiff_t clientInstance = 0x30;
} // namespace screen

namespace gameMode {
inline constexpr ptrdiff_t owner = 0x10;           // read by GameMode::attack
} // namespace gameMode

namespace presentConfig {
inline constexpr ptrdiff_t vsync = 0x30;   // 1 = vsync on, 0 = uncapped
} // namespace presentConfig

namespace level {
// Level::getPrimaryLocalPlayer iterates [this+0x30] .. [this+0x38] as a
// vector<Player*>; +0x40 is the matching end-of-storage pointer.
inline constexpr ptrdiff_t players = 0x30;
} // namespace level

} // namespace field

} // namespace aerial::offsets
