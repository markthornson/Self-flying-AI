#include <doctest/doctest.h>

#include "engine/core/log.h"
#include "engine/debug/log_history.h"

#include <string>
#include <vector>

using namespace eng;

TEST_CASE("the log history keeps the newest lines") {
    LogHistory log(/*capacity=*/3);
    for (int i = 1; i <= 5; ++i) log.add(LogLevel::Info, "line " + std::to_string(i));
    std::vector<std::string> lines;
    log.each([&](const LogLine& l) { lines.push_back(l.text); });
    CHECK(lines == std::vector<std::string>{"line 3", "line 4", "line 5"});
    CHECK(log.count() == 5); // counts every line ever added, for auto-scroll
}

TEST_CASE("the log history captures SDL_Log while installed") {
    LogHistory log;
    log.install();
    ENGINE_LOG_WARN("a warning %d", 42);
    log.uninstall();
    ENGINE_LOG_INFO("not captured");

    std::vector<LogLine> lines;
    log.each([&](const LogLine& l) { lines.push_back(l); });
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text == "a warning 42");
    CHECK(lines[0].level == LogLevel::Warn);
}
