#pragma once

// Noticing when files change on disk, for hot reload.
//
// Every OS has an API that pushes change notifications (inotify on Linux,
// ReadDirectoryChangesW on Windows, FSEvents on macOS), but each works
// differently and all have corner cases. For a few dozen asset files there is
// a much simpler way that works everywhere: every quarter of a second, ask
// for each file's modification time and size, and compare with last time.
// That's a few dozen tiny system calls, far below a millisecond.
//
// One subtlety: programs don't save files in one instant. Blender writing a
// .glb, or an editor saving through a temporary file, can leave the file
// half-written or briefly missing. Reloading at the first sign of change
// would read a broken file. So a change is only reported once the file has
// stopped changing for `settle_seconds`: it has to look the same on two polls
// in a row. This is called *debouncing*.
//
// Free of SDL and the rest of the engine; the caller passes the time in,
// which keeps it easy to test.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace eng {

class FileWatcher {
public:
    explicit FileWatcher(double poll_seconds = 0.25, double settle_seconds = 0.3)
        : poll_seconds_(poll_seconds), settle_seconds_(settle_seconds) {}

    // Starts watching a file. Watching it again does nothing.
    void watch(const std::filesystem::path& path);
    void unwatch(const std::filesystem::path& path);
    bool watching(const std::filesystem::path& path) const;

    // Checks the files if poll_seconds have passed since the last check, and
    // returns those that changed and have since settled. `now` is any clock
    // in seconds that only goes forward.
    std::vector<std::filesystem::path> poll(double now);

    std::size_t size() const { return files_.size(); }

private:
    // What a file looks like from outside. Any difference counts as a change.
    struct Stamp {
        bool exists = false;
        std::filesystem::file_time_type time{};
        std::uintmax_t size = 0;
        friend bool operator==(const Stamp&, const Stamp&) = default;
    };
    struct File {
        std::filesystem::path path;
        Stamp seen;              // as of the last poll
        bool pending = false;    // changed, waiting to settle
        double changed_at = 0.0; // when it last looked different
    };

    static Stamp stamp(const std::filesystem::path& path);

    std::vector<File> files_;
    double poll_seconds_;
    double settle_seconds_;
    double last_poll_ = -1e9;
};

} // namespace eng
