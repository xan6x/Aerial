add_executable(AerialInjector main.cpp Download.cpp Download.h)

target_include_directories(AerialInjector PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(AerialInjector PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE _UNICODE)

target_compile_options(AerialInjector PRIVATE /W4 /permissive- /EHsc /utf-8 /GR- /Gy /Gw)

target_link_options(AerialInjector PRIVATE
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>)

target_link_libraries(AerialInjector PRIVATE advapi32 winhttp shell32 ole32)

set_target_properties(AerialInjector PROPERTIES
    OUTPUT_NAME "AerialInjector"
    VS_DPI_AWARE "PerMonitor")

target_link_options(AerialInjector PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
