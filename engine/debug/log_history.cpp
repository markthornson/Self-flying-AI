#include "engine/debug/log_history.h"

#include <SDL3/SDL_log.h>

namespace eng {

namespace {

// SDL calls this for every log message once install() has run. `userdata`
// is the LogHistory.
void SDLCALL capture(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    auto* history = static_cast<LogHistory*>(userdata);
    LogLevel level = priority >= SDL_LOG_PRIORITY_ERROR ? LogLevel::Error
                     : priority == SDL_LOG_PRIORITY_WARN ? LogLevel::Warn
                                                         : LogLevel::Info;
    history->add(level, message);
    // Then print it the way SDL would have without us.
    SDL_GetDefaultLogOutputFunction()(nullptr, category, priority, message);
}

} // namespace

void LogHistory::install() {
    if (installed_) return;
    SDL_SetLogOutputFunction(capture, this);
    installed_ = true;
}

void LogHistory::uninstall() {
    if (!installed_) return;
    SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
    installed_ = false;
}

void LogHistory::add(LogLevel level, std::string text) {
    std::lock_guard lock(mutex_);
    lines_.push_back({level, std::move(text)});
    while (lines_.size() > capacity_) lines_.pop_front();
    ++count_;
}

void LogHistory::clear() {
    std::lock_guard lock(mutex_);
    lines_.clear();
}

} // namespace eng
