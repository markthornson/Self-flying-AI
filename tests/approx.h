#pragma once

// Float comparisons for tests: exact equality is wrong for computed floats.

#include "engine/core/math/math.h"

#include <doctest/doctest.h>

inline void check_near(eng::Vec3 a, eng::Vec3 b, double eps = 1e-5) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
    CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
    CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
}

inline void check_near(eng::Vec4 a, eng::Vec4 b, double eps = 1e-5) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
    CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
    CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
    CHECK(a.w == doctest::Approx(b.w).epsilon(eps));
}
