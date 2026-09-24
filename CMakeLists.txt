cmake_minimum_required(VERSION 3.20)
# Versioning: MAJOR.MINOR.PATCH
#   MAJOR  0 = beta, 1 = first official release, +1 only for rare huge changes
#   MINOR  updates: new features, changes, semi-big stuff
#   PATCH  small updates and fixes
# Release tags are "v" + this, e.g. v0.2.1. The app shows a BETA badge while MAJOR is 0.
project(iconger VERSION 0.2.2 LANGUAGES CXX RC)
set(ICONGER_VERSION_LABEL "${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -------------------------------------------------------------------
# Dependencies (all pinned so builds are reproducible)
# -------------------------------------------------------------------
include(FetchContent)

# Dear ImGui 1.92 - dynamic font atlas (any glyph, any size, crisp at any DPI)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY  https://github.com/ocornut/imgui.git
    GIT_TAG         v1.92.9b
    GIT_SHALLOW     TRUE
)
FetchContent_MakeAvailable(imgui)

# stb_image / stb_image_resize2 / stb_image_write - image import + ICO building
FetchContent_Declare(
    stb
    GIT_REPOSITORY  https://github.com/nothings/stb.git
    GIT_TAG         2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_MakeAvailable(stb)

# Lucide icon font (ISC license), embedded into the exe as a resource
set(LUCIDE_VERSION 1.48.0)
set(LUCIDE_TTF ${CMAKE_BINARY_DIR}/lucide-${LUCIDE_VERSION}.ttf)
if(NOT EXISTS ${LUCIDE_TTF})
    file(DOWNLOAD
        https://cdn.jsdelivr.net/npm/lucide-static@${LUCIDE_VERSION}/font/lucide.ttf
        ${LUCIDE_TTF}
        EXPECTED_HASH SHA256=eaf30a2a0d2ec0cfbf7ab69ed6dc6e39b7e5e2f0255c17856dcb0b73a15584bb
        SHOW_PROGRESS)
endif()
configure_file(src/iconger.rc.in ${CMAKE_BINARY_DIR}/iconger.rc @ONLY)

# -------------------------------------------------------------------
# Core library: everything that is not UI, so tests can link it too
# -------------------------------------------------------------------
add_library(iconger_core STATIC
    src/app_paths.cpp
    src/shell_link.cpp
    src/icon_backup.cpp
    src/icon_utils.cpp
    src/explorer_refresh.cpp
    src/settings.cpp
    src/stb_impl.cpp
)
target_include_directories(iconger_core PUBLIC src ${stb_SOURCE_DIR})
target_compile_definitions(iconger_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(iconger_core PUBLIC ole32 oleaut32 shell32 shlwapi user32 gdi32 rstrtmgr)

# -------------------------------------------------------------------
# Executable
# -------------------------------------------------------------------
add_executable(iconger WIN32
    src/main.cpp
    src/app.cpp
    src/gfx.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
    src/iconger.manifest
    ${CMAKE_BINARY_DIR}/iconger.rc
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(iconger PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(iconger PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    ICONGER_VERSION="${ICONGER_VERSION_LABEL}")
target_link_libraries(iconger PRIVATE iconger_core d3d11 dxgi dwmapi comdlg32 imm32)

# -------------------------------------------------------------------
# Tests (plain asserts, no framework): ctest -C Release
# -------------------------------------------------------------------
enable_testing()
add_executable(iconger_tests tests/test_main.cpp)
target_link_libraries(iconger_tests PRIVATE iconger_core)
add_test(NAME iconger_tests COMMAND iconger_tests)

if(MSVC)
    foreach(t iconger_core iconger iconger_tests)
        target_compile_options(${t} PRIVATE /W4 /utf-8 /permissive-)
    endforeach()
    # third-party sources are not ours to lint
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
endif()
