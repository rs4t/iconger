cmake_minimum_required(VERSION 3.20)
project(iconger VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -------------------------------------------------------------------
# Dependencies via FetchContent
# -------------------------------------------------------------------
include(FetchContent)

# Dear ImGui – the UI library
FetchContent_Declare(
    imgui
    GIT_REPOSITORY  https://github.com/ocornut/imgui.git
    GIT_TAG         v1.91.0-docking
)
FetchContent_MakeAvailable(imgui)
set(imgui_sources
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

# stb_image – PNG decoding
FetchContent_Declare(
    stb
    GIT_REPOSITORY  https://github.com/nothings/stb.git
    GIT_TAG         master
)
FetchContent_MakeAvailable(stb)

# -------------------------------------------------------------------
# Sources
# -------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/shell_link.cpp
    src/icon_utils.cpp
    src/explorer_refresh.cpp
    src/taskbar_enum.cpp
    src/stb_impl.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

set(HEADERS
    src/shell_link.h
    src/icon_utils.h
    src/explorer_refresh.h
    src/taskbar_enum.h
)

# -------------------------------------------------------------------
# Executable
# -------------------------------------------------------------------
add_executable(iconger WIN32 ${SOURCES} ${HEADERS})

target_include_directories(iconger PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${stb_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(iconger PRIVATE
    d3d11
    dxgi
    dcomp
    ole32
    oleacc
    oleaut32
    shell32
    shlwapi
    comctl32
    imm32
    user32
    gdi32
    psapi
)

# imgui backends need access to imgui internal headers for the backend files
target_include_directories(iconger PRIVATE
    ${imgui_SOURCE_DIR}/backends
)

# -------------------------------------------------------------------
# Compiler flags
# -------------------------------------------------------------------
if(MSVC)
    target_compile_options(iconger PRIVATE /W4 /utf-8)
else()
    target_compile_options(iconger PRIVATE -Wall -Wextra)
endif()