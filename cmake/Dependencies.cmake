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

# --- Phase 2 libraries ---------------------------------------------------------

# fastgltf: a fast glTF 2.0 parser. We use it only to read the file; turning
# glTF's data into our own MeshData is the engine's job (engine/assets/).
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
FetchContent_Declare(fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG        v0.9.0
    GIT_SHALLOW    TRUE
    SYSTEM         # its headers' warnings are not ours to fix
)

# Lua 5.4: the scripting language. The official sources ship no CMake build,
# so like ImGui we compile them ourselves below. This is the Lua team's own
# GitHub mirror, tagged per release.
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake # nothing to add_subdirectory(); just download
)

# sol2: header-only C++ bindings for Lua, so exposing a C++ function to
# scripts is one line instead of a page of manual stack pushing.
FetchContent_Declare(sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.5.0
    GIT_SHALLOW    TRUE
)

# miniaudio: a single-file audio library that talks to every OS's sound API
# and decodes WAV, FLAC and MP3. The mixer buses are built on top of it.
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.23
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake # we compile its one source file ourselves below
)

FetchContent_MakeAvailable(SDL3 imgui doctest fastgltf lua sol2 miniaudio)

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

# Lua: every lua/*.c file except the stand-alone interpreter (lua.c), the
# all-in-one build (onelua.c) and the Lua team's internal test hooks (ltests.c).
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(REMOVE_ITEM LUA_SOURCES
    ${lua_SOURCE_DIR}/lua.c
    ${lua_SOURCE_DIR}/onelua.c
    ${lua_SOURCE_DIR}/ltests.c
)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua SYSTEM PUBLIC ${lua_SOURCE_DIR})
# Lua's own code is C, so compile it as C even though we link it from C++.
set_target_properties(lua PROPERTIES LINKER_LANGUAGE C)
if(UNIX AND NOT APPLE)
    # Lets Lua's os and io libraries use the POSIX functions.
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua PUBLIC m dl)
elseif(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
endif()

# sol2 is header only; its include folder is all we need.
add_library(sol2_headers INTERFACE)
target_include_directories(sol2_headers SYSTEM INTERFACE ${sol2_SOURCE_DIR}/include)
target_link_libraries(sol2_headers INTERFACE lua)
# Turn on all of sol2's argument and type checks: a script passing the wrong
# thing gets a clear Lua error instead of undefined behaviour.
target_compile_definitions(sol2_headers INTERFACE SOL_ALL_SAFETIES_ON=1)

# miniaudio: its one implementation file, as a small static library.
add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
# We only decode and play sounds, so leave out the parts we don't use:
# encoding (writing audio files) and the waveform and noise generators.
target_compile_definitions(miniaudio PUBLIC MA_NO_ENCODING MA_NO_GENERATION)
# miniaudio loads the OS sound libraries (ALSA, PulseAudio, Core Audio,
# WASAPI) at runtime, so nothing else needs linking except on Linux, where it
# needs threads and the dynamic loader.
if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio PUBLIC pthread m dl)
endif()
