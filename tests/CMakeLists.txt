# Unit tests. Run them with `ctest --test-dir build` after building.

add_executable(engine_tests
    main.cpp
    test_fixed_step.cpp
    test_input.cpp
    test_math.cpp
    test_mesh.cpp
)
target_link_libraries(engine_tests PRIVATE engine doctest::doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(engine_tests)
