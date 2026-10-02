# Unit tests. Run them with `ctest --test-dir build` after building.

add_executable(engine_tests
    main.cpp
    test_assets.cpp
    test_audio.cpp
    test_collision.cpp
    test_ecs.cpp
    test_fixed_step.cpp
    test_gltf.cpp
    test_input.cpp
    test_log_history.cpp
    test_math.cpp
    test_mesh.cpp
    test_physics.cpp
    test_script.cpp
)
target_link_libraries(engine_tests PRIVATE engine doctest::doctest)
# The glTF and asset tests load the game's models from the source tree.
target_compile_definitions(engine_tests PRIVATE ENGINE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(engine_tests)
