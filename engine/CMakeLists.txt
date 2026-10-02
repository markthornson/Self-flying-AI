# The engine, built as a static library that games link against.

add_library(engine STATIC
    app.cpp
    assets/gltf.cpp
    assets/library.cpp
    audio/audio.cpp
    debug/imgui_layer.cpp
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

# Games see SDL and ImGui through the engine's headers. fastgltf, miniaudio,
# Lua and sol2 are private: only the engine's .cpp files include them, so a
# game never compiles their (large) headers.
target_link_libraries(engine
    PUBLIC SDL3::SDL3 imgui
    PRIVATE fastgltf::fastgltf miniaudio sol2_headers
)

# Plenty of warnings: they catch real bugs, and the engine is ours to keep clean.
if(MSVC)
    # /bigobj: sol2's templates make more sections than MSVC allows by default.
    target_compile_options(engine PRIVATE /W4 /permissive- /bigobj)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
