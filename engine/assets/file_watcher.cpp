#include "engine/assets/file_watcher.h"

#include <algorithm>
#include <system_error>

namespace eng {

FileWatcher::Stamp FileWatcher::stamp(const std::filesystem::path& path) {
    // The error_code overloads don't throw: a file that vanished mid-save is
    // an ordinary event here, not an exception.
    std::error_code ec;
    Stamp s;
    s.time = std::filesystem::last_write_time(path, ec);
    if (ec) return {};
    s.size = std::filesystem::file_size(path, ec);
    if (ec) return {};
    s.exists = true;
    return s;
}

void FileWatcher::watch(const std::filesystem::path& path) {
    if (watching(path)) return;
    files_.push_back({path, stamp(path)});
}

void FileWatcher::unwatch(const std::filesystem::path& path) {
    std::erase_if(files_, [&](const File& f) { return f.path == path; });
}

bool FileWatcher::watching(const std::filesystem::path& path) const {
    return std::any_of(files_.begin(), files_.end(), [&](const File& f) { return f.path == path; });
}

std::vector<std::filesystem::path> FileWatcher::poll(double now) {
    std::vector<std::filesystem::path> changed;
    if (now - last_poll_ < poll_seconds_) return changed;
    last_poll_ = now;

    for (File& f : files_) {
        Stamp current = stamp(f.path);
        if (current != f.seen) {
            // Still changing: (re)start the settle timer.
            f.seen = current;
            f.pending = true;
            f.changed_at = now;
        } else if (f.pending && current.exists && now - f.changed_at >= settle_seconds_) {
            // Unchanged for long enough: the save is finished.
            f.pending = false;
            changed.push_back(f.path);
        }
        // A file that disappeared stays pending until it comes back; a
        // deleted asset keeps its last good version in memory.
    }
    return changed;
}

} // namespace eng
