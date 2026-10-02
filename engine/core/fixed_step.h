#pragma once

// The fixed-timestep accumulator at the heart of the game loop.
//
// Rendering runs as fast as the screen allows (maybe 30 fps, maybe 144 fps),
// but the simulation should always advance in identical slices, here 1/60 s.
// Identical slices make physics stable and gameplay the same on every machine.
//
// Each frame we add the real elapsed time to an "accumulator" and then run as
// many whole simulation steps as fit in it. Whatever is left over (less than
// one step) carries into the next frame. The leftover, as a fraction of a
// step, is `alpha`: how far we are between the previous simulation state and
// the current one. The renderer blends the two by alpha so motion looks smooth.
//
// See Glenn Fiedler's "Fix Your Timestep!" for the classic write-up.

namespace eng {

class FixedStep {
public:
    explicit FixedStep(double step_seconds = 1.0 / 60.0) : step_(step_seconds) {}

    // Feed in how long the last frame took; returns how many simulation steps
    // to run this frame (often 1, sometimes 0 or 2).
    int advance(double frame_seconds) {
        // After a long stall (a breakpoint, dragging the window) don't try to
        // catch up on seconds of simulation at once.
        if (frame_seconds > kMaxFrameSeconds) frame_seconds = kMaxFrameSeconds;
        if (frame_seconds < 0.0) frame_seconds = 0.0;
        accumulator_ += frame_seconds;

        int steps = 0;
        while (accumulator_ >= step_ && steps < kMaxStepsPerFrame) {
            accumulator_ -= step_;
            ++steps;
        }
        // If simulating is slower than real time we would fall further behind
        // every frame (the "spiral of death"). Drop the excess instead: the
        // game runs in slow motion rather than freezing.
        if (steps == kMaxStepsPerFrame && accumulator_ > step_) accumulator_ = 0.0;
        return steps;
    }

    // 0..1: how far between the previous and current simulation state to draw.
    float alpha() const { return static_cast<float>(accumulator_ / step_); }

    double step_seconds() const { return step_; }

    static constexpr double kMaxFrameSeconds = 0.25;
    static constexpr int kMaxStepsPerFrame = 8;

private:
    double step_;
    double accumulator_ = 0.0;
};

} // namespace eng
