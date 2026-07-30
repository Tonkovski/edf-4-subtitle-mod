cmake_minimum_required(VERSION 3.24)

project(edf41_subtitles VERSION 0.1.1 LANGUAGES CXX)

if(NOT WIN32 OR NOT MINGW)
    message(FATAL_ERROR "edf41_subtitles currently requires a 64-bit MinGW-w64 toolchain.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "EDF 4.1 is 64-bit; configure this project with an x86_64 compiler.")
endif()

include(FetchContent)
include(CTest)

# These are the revisions used by the known-working EDF5 subtitle plugin.
FetchContent_Declare(
    detours
    GIT_REPOSITORY https://github.com/microsoft/Detours.git
    GIT_TAG 734ac64899c44933151c1335f6ef54a590219221
    GIT_SHALLOW FALSE
)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 713e034d95f6cc8a57ca504d52cb4f60da6359d9
    GIT_SHALLOW FALSE
)

FetchContent_MakeAvailable(detours imgui)

file(GLOB DETOURS_SOURCES CONFIGURE_DEPENDS "${detours_SOURCE_DIR}/src/*.cpp")
list(FILTER DETOURS_SOURCES EXCLUDE REGEX "uimports\\.cpp$")

add_library(detours STATIC ${DETOURS_SOURCES})
target_include_directories(detours PUBLIC "${detours_SOURCE_DIR}/src")
target_compile_definitions(detours PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(detours PRIVATE -w)

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
)
target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_compile_definitions(imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(imgui PRIVATE -w)
target_link_libraries(imgui PRIVATE d3d11 dwmapi)

add_library(edf41_subtitles SHARED
    Source/Config.cpp
    Source/GameHooks.cpp
    Source/Log.cpp
    Source/Mod.cpp
    Source/Renderer.cpp
    Source/SubtitleController.cpp
    Source/SubtitleDatabase.cpp
    Source/exports.def
)

target_compile_features(edf41_subtitles PRIVATE cxx_std_20)
target_compile_definitions(edf41_subtitles PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
target_compile_options(edf41_subtitles PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)
target_include_directories(edf41_subtitles PRIVATE Source)
target_link_libraries(edf41_subtitles PRIVATE
    detours
    imgui
    d3d11
    d3dcompiler
    dxgi
    psapi
    user32
    gdi32
    dwmapi
)
target_link_options(edf41_subtitles PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
    $<$<CONFIG:Release>:-s>
)

set_target_properties(edf41_subtitles PROPERTIES
    PREFIX ""
    OUTPUT_NAME "edf41_subtitles"
)

if(BUILD_TESTING)
    add_executable(edf41_subtitles_tests
        Tests/SubtitleTests.cpp
        Source/Log.cpp
        Source/SubtitleController.cpp
        Source/SubtitleDatabase.cpp
    )
    target_compile_features(edf41_subtitles_tests PRIVATE cxx_std_20)
    target_compile_definitions(edf41_subtitles_tests PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        _WIN32_WINNT=0x0A00
        EDF41_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_options(edf41_subtitles_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    target_include_directories(edf41_subtitles_tests PRIVATE Source)
    target_link_options(edf41_subtitles_tests PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
    target_link_libraries(edf41_subtitles_tests PRIVATE user32)
    add_test(NAME subtitle_core COMMAND edf41_subtitles_tests)
endif()

install(TARGETS edf41_subtitles
    RUNTIME DESTINATION Mods/Plugins
    LIBRARY DESTINATION Mods/Plugins
)
install(FILES
    dist/subtitles.toml
    subtitles.txt
    subtitles_en.txt
    subtitles_zh.txt
    cuelength.txt
    DESTINATION Mods/Plugins
)
install(FILES
    README.md
    THIRD_PARTY_NOTICES.md
    DESTINATION Mods/Plugins
)
install(FILES
    "${detours_SOURCE_DIR}/LICENSE.md"
    "${imgui_SOURCE_DIR}/LICENSE.txt"
    DESTINATION Mods/Plugins/licenses
)
