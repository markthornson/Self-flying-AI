#pragma once

// Keeps the most recent log lines, for the debug console.
//
// Everything the engine logs goes through SDL_Log (see core/log.h). SDL lets
// us swap in our own *output function*, so install() puts this class between
// SDL_Log and the terminal: each line is stored here and then passed on to
// SDL's usual output, so the terminal still sees everything too.
//
// Old lines drop off the front once there are `capacity` of them. A lock
// guards the lines, because the audio thread may log while the main thread
// draws the console.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace eng {

enum class LogLevel { Info, Warn, Error, Command, Result };

struct LogLine {
    LogLevel level;
    std::string text;
};

class LogHistory {
public:
    // With capture_sdl_log, starts capturing at once (see install()).
    explicit LogHistory(std::size_t capacity = 1000, bool capture_sdl_log = false) : capacity_(capacity) {
        if (capture_sdl_log) install();
    }
    ~LogHistory() { uninstall(); }
    LogHistory(const LogHistory&) = delete;
    LogHistory& operator=(const LogHistory&) = delete;

    // Starts (or stops) capturing SDL_Log output.
    void install();
    void uninstall();

    void add(LogLevel level, std::string text);
    void clear();

    // Calls fn(const LogLine&) for each stored line, oldest first. Holds the
    // lock throughout, so fn must not log.
    template <typename Fn>
    void each(Fn&& fn) const {
        std::lock_guard lock(mutex_);
        for (const LogLine& line : lines_) fn(line);
    }

    // Goes up by one for every line added, so a viewer can tell when to
    // scroll to the bottom.
    std::uint64_t count() const {
        std::lock_guard lock(mutex_);
        return count_;
    }

private:
    mutable std::mutex mutex_;
    std::deque<LogLine> lines_;
    std::size_t capacity_;
    std::uint64_t count_ = 0;
    bool installed_ = false;
};

} // namespace eng
