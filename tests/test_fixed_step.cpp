#include "engine/core/fixed_step.h"

#include <doctest/doctest.h>

using eng::FixedStep;

TEST_CASE("fixed step runs whole steps and carries the remainder") {
    // Powers of two keep the float arithmetic exact, so the test is too.
    const double step = 1.0 / 64.0;
    FixedStep fs(step);
    CHECK(fs.advance(2.5 * step) == 2);
    CHECK(fs.alpha() == doctest::Approx(0.5f));
    CHECK(fs.advance(0.5 * step) == 1); // the half step left over plus this half makes one
    CHECK(fs.alpha() == doctest::Approx(0.0f));
    CHECK(fs.advance(0.25 * step) == 0); // a fast frame may run no steps at all
    CHECK(fs.alpha() == doctest::Approx(0.25f));
}

TEST_CASE("fixed step clamps long stalls") {
    FixedStep fs(1.0 / 60.0);
    // A 5 second hitch (a breakpoint) must not run 300 steps.
    int steps = fs.advance(5.0);
    CHECK(steps <= FixedStep::kMaxStepsPerFrame);
    CHECK(fs.alpha() < 1.0f);
}

TEST_CASE("fixed step keeps real time over many frames") {
    FixedStep fs(1.0 / 60.0);
    int total = 0;
    for (int i = 0; i < 144; ++i) total += fs.advance(1.0 / 144.0); // one second on a 144 Hz screen
    CHECK(total >= 59);
    CHECK(total <= 60);
}
