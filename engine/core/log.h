#pragma once

// Logging. Thin wrappers over SDL_Log, which prints to the console on every
// platform (and to the debugger output on Windows). printf-style formatting.

#include <SDL3/SDL_log.h>

#define ENGINE_LOG_INFO(...)  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define ENGINE_LOG_WARN(...)  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define ENGINE_LOG_ERROR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
