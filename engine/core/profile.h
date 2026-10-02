#pragma once

// Profiling macros, backed by Tracy.
//
// A profiler answers "where did this frame's 16 ms go?". Tracy does it with
// *zones*: put PROFILE_SCOPE("Physics") at the top of a block and every time
// the block runs, Tracy records when it started and ended, on which thread.
// The Tracy viewer (a separate program) connects to the running game over the
// network and draws those zones as a timeline, frame by frame, so a slow
// frame shows exactly which system ate the time.
//
//     void Physics::step(World& world, float dt) {
//         PROFILE_SCOPE("Physics::step");
//         ...
//     }
//
// Engine and game code use only these macros, never Tracy's own, so Tracy
// could be swapped for another profiler by changing this one file.
//
// Build with -DENGINE_PROFILE=ON to turn them on. Off (the default), Tracy's
// header defines its macros as nothing, so the zones cost nothing at all.

#include <tracy/Tracy.hpp>

// Times the enclosing scope under `name` (a string literal).
#define PROFILE_SCOPE(name) ZoneScopedN(name)
// Marks the end of a frame, so the viewer can split the timeline into frames.
#define PROFILE_FRAME() FrameMark
// Records a number over time, drawn as a graph in the viewer.
#define PROFILE_PLOT(name, value) TracyPlot(name, value)
