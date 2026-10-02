# The engine, built as a static library that games link against.

add_library(engine STATIC
    app.cpp
    assets/assets.cpp
    assets/file_watcher.cpp
    assets/gltf.cpp
    audio/audio.cpp
    debug/debug_tools.cpp
    debug/imgui_layer.cpp
    debug/inspector.cpp
    debug/log_history.cpp
    physics/collision.cpp
    physics/physics.cpp
    platform/input.cpp
    platform/window.cpp
    render/mesh.cpp
    render/renderer.cpp
    script/scripting.cpp
    world/ecs.cpp
    world/systems.cpp
)

# Includes are written from the repository root, e.g.
# #include "engine/core/math/vec.h", so it is always clear where a file lives.
target_include_directories(engine PUBLIC ${PROJECT_SOURCE_DIR})

# Games see SDL and ImGui through the engine's headers, and Tracy through
# core/profile.h so they can time their own code. fastgltf, miniaudio, Lua and
# sol2 are private: only the engine's .cpp files include them, so a game never
# compiles their (large) headers.
target_link_libraries(engine
    PUBLIC SDL3::SDL3 imgui Tracy::TracyClient
    PRIVATE fastgltf::fastgltf miniaudio sol2_headers
)

# Hot reload watches asset files for changes. It's for development: phase 5's
# packaged builds turn it off, since a shipped game's files don't change.
option(ENGINE_HOT_RELOAD "Reload assets when their files change on disk" ON)
if(ENGINE_HOT_RELOAD)
    target_compile_definitions(engine PRIVATE ENGINE_HOT_RELOAD)
endif()

# Plenty of warnings: they catch real bugs, and the engine is ours to keep clean.
if(MSVC)
    # /bigobj: sol2's templates make more sections than MSVC allows by default.
    target_compile_options(engine PRIVATE /W4 /permissive- /bigobj)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
