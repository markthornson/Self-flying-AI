# Third-party dependencies, fetched from git at configure time.
#
# Each one is pinned to a release tag so every machine (and CI) builds the same
# code. The first configure clones them into build/_deps; later configures
# reuse that copy.
#
# The design plan names vcpkg for this. FetchContent is used in phase 1
# because it needs nothing installed beyond CMake and git; moving to a vcpkg
# manifest later only changes this file.

include(FetchContent)

# --- SDL3: window, input, and the SDL_GPU graphics API -----------------------
# Built as a static library so the sandbox runs without a DLL next to it.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE
)

# --- Dear ImGui: immediate-mode debug UI ---------------------------------------
# ImGui ships no CMake build, so we compile its sources ourselves below.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9
    GIT_SHALLOW    TRUE
)

# --- doctest: single-header unit testing ---------------------------------------
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(SDL3 imgui doctest)

# ImGui core plus the two backends we use: SDL3 for input and windowing, and
# SDL_GPU for drawing.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
