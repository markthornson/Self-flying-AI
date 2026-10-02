cmake_minimum_required(VERSION 3.25)
project(GameEngine LANGUAGES C CXX)

# C++20 everywhere, no compiler-specific extensions, so the same code builds
# with MSVC, Clang and GCC.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put every executable in one folder so the sandbox and tests are easy to find.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(ENGINE_BUILD_TESTS "Build the doctest unit tests" ON)

# Third-party libraries (SDL3, Dear ImGui, doctest), pinned to exact tags.
include(cmake/Dependencies.cmake)

add_subdirectory(engine)
add_subdirectory(games/sandbox)
add_subdirectory(games/coin_hunt)

if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
