# The engine, built as a static library that games link against.

add_library(engine STATIC
    app.cpp
    debug/imgui_layer.cpp
    platform/input.cpp
    platform/window.cpp
    render/mesh.cpp
    render/renderer.cpp
)

# Includes are written from the repository root, e.g.
# #include "engine/core/math/vec.h", so it is always clear where a file lives.
target_include_directories(engine PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(engine PUBLIC SDL3::SDL3 imgui)

# Plenty of warnings: they catch real bugs, and the engine is ours to keep clean.
if(MSVC)
    target_compile_options(engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
