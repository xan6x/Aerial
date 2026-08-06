cmake_minimum_required(VERSION 3.21)
project(AerialClient LANGUAGES CXX VERSION 0.1.0)

# ── Target: Minecraft: Windows 10 Edition 1.1.5 (x64) ────────────────────────
# The client is an internal DLL injected into Minecraft.Windows.exe.
# Must be built x64 with MSVC: the SDK mirrors MSVC's ABI (vtables, std::string
# layout, __fastcall calling convention) of the game binary.

if(NOT MSVC)
    message(FATAL_ERROR "AerialClient must be built with MSVC (ABI compatibility with the game binary).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "AerialClient must be built for x64. Configure with -A x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Dependencies ─────────────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(minhook)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    ON)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# ── Client DLL ───────────────────────────────────────────────────────────────
file(GLOB_RECURSE AERIAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h)

add_library(AerialClient SHARED ${AERIAL_SOURCES})

source_group(TREE ${CMAKE_SOURCE_DIR}/src PREFIX src FILES ${AERIAL_SOURCES})

target_include_directories(AerialClient PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(AerialClient PRIVATE
    minhook
    nlohmann_json::nlohmann_json
    user32 shell32 ole32
    winmm)   # PlaySound, for the notification chime

target_compile_definitions(AerialClient PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE _UNICODE
    AERIAL_VERSION="${PROJECT_VERSION}")

# 4100: unreferenced parameter (event handlers ignore their event)
# 4201: nameless struct/union
# 4624: deleted destructor - deliberate on the SDK wrappers, which are never
#       constructed or destroyed by us, only reinterpret_cast from game pointers
target_compile_options(AerialClient PRIVATE
    /W4 /wd4100 /wd4201 /wd4624 /permissive- /EHsc /Zc:preprocessor /utf-8)
target_link_options(AerialClient PRIVATE /DEBUG)

set_target_properties(AerialClient PROPERTIES
    OUTPUT_NAME "AerialClient"
    PREFIX ""
    SUFFIX ".dll")

# ── Injector (UWP-aware) ─────────────────────────────────────────────────────
add_subdirectory(injector)
