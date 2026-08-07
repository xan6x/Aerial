#pragma once

// Every module Aerial ships, one header and one source file each.
//
// Adding a module is three steps:
//   1. create <Name>.h and <Name>.cpp beside these,
//   2. include the header below,
//   3. add<YourModule>() in ModuleManager::registerAll().

// Combat
#include "Module/Modules/ItemDelayFix.h"

// Movement
#include "Module/Modules/AutoSprint.h"
#include "Module/Modules/InventoryMove.h"

// Player
#include "Module/Modules/NoCamReset.h"
#include "Module/Modules/SensMultiplier.h"

// Render
#include "Module/Modules/ArrayList.h"
#include "Module/Modules/FogColor.h"
#include "Module/Modules/FullBright.h"
#include "Module/Modules/JavaFov.h"
#include "Module/Modules/ItemPhysics.h"
#include "Module/Modules/NoDynamicFov.h"
#include "Module/Modules/NoHurtCam.h"
#include "Module/Modules/NoVSync.h"
#include "Module/Modules/Notifications.h"
#include "Module/Modules/Watermark.h"

// Misc
#include "Module/Modules/ClickGuiModule.h"
#include "Module/Modules/Direct2D.h"
#include "Module/Modules/PacketLogger.h"
