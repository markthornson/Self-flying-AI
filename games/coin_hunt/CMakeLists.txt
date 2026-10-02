add_executable(coin_hunt main.cpp)
target_link_libraries(coin_hunt PRIVATE engine)

# Where the game finds its scripts and assets. Pointing at the source folder
# means saving game.lua (or a model) while the game runs picks up the change
# with no rebuild.
# Packaging (phase 5) will copy them next to the executable instead.
target_compile_definitions(coin_hunt PRIVATE COIN_HUNT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(coin_hunt PRIVATE /W4 /permissive-)
else()
    target_compile_options(coin_hunt PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
