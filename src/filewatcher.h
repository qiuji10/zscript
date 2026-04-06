#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace zscript {

// ---------------------------------------------------------------------------
// FileWatcher — platform-abstracted directory watcher
//
// Runs a background thread that monitors a directory for file changes.
// On change, enqueues a notification that the main thread drains via poll().
//
// Platform backends (selected at compile time):
//   macOS:   FSEvents
//   Linux:   inotify
//   Windows: ReadDirectoryChangesW
//   Default: polling fallback (iOS, Android, unknown platforms)
//
// The 50ms debounce timer is applied per-file: a burst of rapid writes to
// the same file produces exactly one notification.
// ---------------------------------------------------------------------------

enum class WatchEvent { Modified, Created, Deleted };

struct FileChange {
    std::string path;
    WatchEvent  event;
};

// Callback type invoked on the watcher thread; implementations must be
// thread-safe (typically they just push onto a queue).
using WatchCallback = std::function<void(const FileChange&)>;

// ---------------------------------------------------------------------------
// IWatcherBackend — internal interface implemented per platform
// ---------------------------------------------------------------------------
class IWatcherBackend {
public:
    virtual ~IWatcherBackend() = default;
    virtual bool start(const std::string& dir, WatchCallback cb) = 0;
    virtual void stop() = 0;
    virtual const char* backend_name() const = 0;
};

// Factory — selects the right backend for the current platform
std::unique_ptr<IWatcherBackend> make_watcher_backend();

// ---------------------------------------------------------------------------
// FileWatcher — public API
// ---------------------------------------------------------------------------
class FileWatcher {
public:
    // debounce_ms: minimum quiet time after last change before firing callback
    explicit FileWatcher(int debounce_ms = 50);
    ~FileWatcher();

    // Start watching a directory (recursive). Spawns background thread.
    // Returns false if the backend could not be started.
    bool watch(const std::string& dir, WatchCallback on_change);

    // Stop watching and join the background thread.
    void stop();

    bool is_running() const { return running_; }
    const char* backend_name() const;

private:
    void on_raw_event(const FileChange& change);
    void debounce_thread();

    std::unique_ptr<IWatcherBackend> backend_;

    // Debounce state — guards with mutex
    struct Pending {
        FileChange  change;
        std::chrono::steady_clock::time_point deadline;
    };
    std::mutex                            mu_;
    std::unordered_map<std::string, Pending> pending_;  // path → latest event
    WatchCallback                         user_cb_;
    int                                   debounce_ms_;

    std::thread     debounce_thread_;
    std::atomic<bool> running_{false};
};

// ---------------------------------------------------------------------------
// Polling fallback — works everywhere, stat()-based
// ---------------------------------------------------------------------------
class PollingBackend : public IWatcherBackend {
public:
    explicit PollingBackend(int interval_ms = 200) : interval_ms_(interval_ms) {}
    bool start(const std::string& dir, WatchCallback cb) override;
    void stop() override;
    const char* backend_name() const override { return "polling"; }

private:
    void poll_loop();
    void scan(const std::string& dir);

    std::string   dir_;
    WatchCallback cb_;
    int           interval_ms_;
    std::thread   thread_;
    std::atomic<bool> running_{false};

    // last-seen mtimes
    std::unordered_map<std::string, int64_t> mtimes_;
    std::mutex mu_;
};

} // namespace zscript
