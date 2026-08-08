cmake_minimum_required(VERSION 3.21)
project(AerialClient LANGUAGES CXX VERSION 0.1.0)

if(NOT MSVC)
    message(FATAL_ERROR "AerialClient must be built with MSVC (ABI compatibility with the game binary).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "AerialClient must be built for x64. Configure with -A x64.")
endif()

option(AERIAL_OBFUSCATE "Obfuscate cold code with an OLLVM clang-cl toolchain (Arkari)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

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

file(GLOB_RECURSE AERIAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h)

add_library(AerialClient SHARED ${AERIAL_SOURCES} ${CMAKE_SOURCE_DIR}/src/Assets/Resources.rc)

source_group(TREE ${CMAKE_SOURCE_DIR}/src PREFIX src FILES ${AERIAL_SOURCES})

target_include_directories(AerialClient PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(AerialClient PRIVATE
    minhook
    nlohmann_json::nlohmann_json
    user32 shell32 ole32
    winmm                      # PlaySound, for the notification chime
    # the Direct2D overlay: dxguid for effect CLSIDs, d3dcompiler for the
    # composite shaders that blend our texture over the game's frame
    d2d1 dwrite d3d11 dxgi dxguid d3dcompiler
    windowscodecs)   # WIC, to decode the embedded PNGs

target_compile_definitions(AerialClient PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE _UNICODE
    AERIAL_VERSION="${PROJECT_VERSION}"
    $<$<CONFIG:Debug>:AERIAL_LOGGING>)

# 4100: unreferenced parameter (event handlers ignore their event)
# 4201: nameless struct/union
# 4624: deleted destructor - deliberate on the SDK wrappers, which are never
#       constructed or destroyed by us, only reinterpret_cast from game pointers
target_compile_options(AerialClient PRIVATE
    /W4 /wd4100 /wd4201 /wd4624 /permissive- /EHsc /Zc:preprocessor /utf-8
    /GR- /Gy /Gw)
target_link_options(AerialClient PRIVATE
    $<$<CONFIG:Debug>:/DEBUG>
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /OPT:LBR /INCREMENTAL:NO>)

set_target_properties(AerialClient PROPERTIES
    OUTPUT_NAME "AerialClient"
    PREFIX ""
    SUFFIX ".dll")

if(AERIAL_OBFUSCATE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "AERIAL_OBFUSCATE needs a Clang/clang-cl toolchain (e.g. Arkari); "
            "current compiler is ${CMAKE_CXX_COMPILER_ID}. Configure with the 'obf' preset.")
    endif()

    set(AERIAL_OBF_FLAGS
        -mllvm -irobf-fla     # control flow flattening
        -mllvm -irobf-cse     # constant string encryption
        -mllvm -irobf-cie     # constant integer encryption
        -mllvm -irobf-cfe)    # constant float encryption

    set(AERIAL_HOT_REGEX "/src/Render/|/src/Hooks/GameHooks\\.cpp$")

    foreach(src IN LISTS AERIAL_SOURCES)
        if(src MATCHES "\\.cpp$" AND NOT src MATCHES "${AERIAL_HOT_REGEX}")
            set_source_files_properties(${src} PROPERTIES COMPILE_OPTIONS "${AERIAL_OBF_FLAGS}")
        endif()
    endforeach()

    message(STATUS "AerialClient: obfuscation ON (render + GameHooks kept clean for FPS)")
endif()

add_subdirectory(injector)
