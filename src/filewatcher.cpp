#include "filewatcher.h"
#include <chrono>
#include <cstring>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------
#if defined(__APPLE__)
#  define ZS_FSEVENTS 1
#elif defined(__linux__)
#  define ZS_INOTIFY 1
#elif defined(_WIN32)
#  define ZS_WINAPI 1
#else
#  define ZS_POLLING 1
#endif

// ---------------------------------------------------------------------------
// macOS — FSEvents backend (uses GCD dispatch queue — no CFRunLoop race)
// ---------------------------------------------------------------------------
#if ZS_FSEVENTS
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

namespace zscript {

class FSEventsBackend : public IWatcherBackend {
public:
    ~FSEventsBackend() override { stop(); }

    bool start(const std::string& dir, WatchCallback cb) override {
        cb_ = std::move(cb);

        CFStringRef path_str = CFStringCreateWithCString(
            kCFAllocatorDefault, dir.c_str(), kCFStringEncodingUTF8);
        CFArrayRef paths = CFArrayCreate(
            kCFAllocatorDefault, (const void**)&path_str, 1, &kCFTypeArrayCallBacks);
        CFRelease(path_str);

        FSEventStreamContext ctx{};
        ctx.info = this;

        stream_ = FSEventStreamCreate(
            kCFAllocatorDefault,
            &FSEventsBackend::event_cb,
            &ctx,
            paths,
            kFSEventStreamEventIdSinceNow,
            0.05,   // 50ms latency hint (debounce is done at FileWatcher level too)
            kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer
        );
        CFRelease(paths);
        if (!stream_) return false;

        queue_ = dispatch_queue_create("zscript.fsevent", DISPATCH_QUEUE_SERIAL);
        FSEventStreamSetDispatchQueue(stream_, queue_);
        FSEventStreamStart(stream_);
        running_ = true;
        return true;
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (stream_) {
            FSEventStreamStop(stream_);
            // Drain any pending callbacks before invalidating
            dispatch_sync(queue_, ^{});
            FSEventStreamInvalidate(stream_);
            FSEventStreamRelease(stream_);
            stream_ = nullptr;
        }
        if (queue_) {
            dispatch_release(queue_);
            queue_ = nullptr;
        }
    }

    const char* backend_name() const override { return "FSEvents"; }

private:
    static void event_cb(ConstFSEventStreamRef, void* info,
                         size_t num_events,
                         void* event_paths_void,
                         const FSEventStreamEventFlags* flags,
                         const FSEventStreamEventId*) {
        auto* self = static_cast<FSEventsBackend*>(info);
        if (!self->running_) return;
        char** paths = static_cast<char**>(event_paths_void);
        for (size_t i = 0; i < num_events; ++i) {
            FileChange fc;
            fc.path = paths[i];
            FSEventStreamEventFlags f = flags[i];
            if (f & kFSEventStreamEventFlagItemRemoved)
                fc.event = WatchEvent::Deleted;
            else if (f & kFSEventStreamEventFlagItemCreated)
                fc.event = WatchEvent::Created;
            else
                fc.event = WatchEvent::Modified;
            self->cb_(fc);
        }
    }

    WatchCallback        cb_;
    FSEventStreamRef     stream_ = nullptr;
    dispatch_queue_t     queue_  = nullptr;
    std::atomic<bool>    running_{false};
};

std::unique_ptr<IWatcherBackend> make_watcher_backend() {
    return std::make_unique<FSEventsBackend>();
}

} // namespace zscript

// ---------------------------------------------------------------------------
// Linux — inotify backend
// ---------------------------------------------------------------------------
#elif ZS_INOTIFY
#include <dirent.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace zscript {

class InotifyBackend : public IWatcherBackend {
public:
    ~InotifyBackend() override { stop(); }

    bool start(const std::string& dir, WatchCallback cb) override {
        cb_  = std::move(cb);
        ifd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (ifd_ < 0) return false;

        add_watch(dir);

        running_ = true;
        thread_  = std::thread([this] { read_loop(); });
        return true;
    }

    void stop() override {
        if (!running_) return;
        running_ = false;
        // Write to pipe to unblock poll/read
        if (ifd_ >= 0) { close(ifd_); ifd_ = -1; }
        if (thread_.joinable()) thread_.join();
        std::lock_guard<std::mutex> lk(mu_);
        wd_to_dir_.clear();
    }

    const char* backend_name() const override { return "inotify"; }

private:
    void add_watch(const std::string& path) {
        uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE |
                        IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR;
        int wd = inotify_add_watch(ifd_, path.c_str(), mask);
        if (wd < 0) return;
        {
            std::lock_guard<std::mutex> lk(mu_);
            wd_to_dir_[wd] = path;
        }
        // Recurse into subdirectories
        DIR* d = opendir(path.c_str());
        if (!d) return;
        while (auto* ent = readdir(d)) {
            if (ent->d_name[0] == '.') continue;
            if (ent->d_type == DT_DIR) {
                add_watch(path + "/" + ent->d_name);
            }
        }
        closedir(d);
    }

    void read_loop() {
        constexpr size_t BUF = 4096;
        alignas(struct inotify_event) char buf[BUF];
        while (running_) {
            ssize_t n = read(ifd_, buf, BUF);
            if (n <= 0) {
                if (running_) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            for (char* p = buf; p < buf + n; ) {
                auto* ev = reinterpret_cast<struct inotify_event*>(p);
                p += sizeof(struct inotify_event) + ev->len;

                std::string dir;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    auto it = wd_to_dir_.find(ev->wd);
                    if (it == wd_to_dir_.end()) continue;
                    dir = it->second;
                }

                std::string name = ev->len ? ev->name : "";
                std::string full = name.empty() ? dir : (dir + "/" + name);

                FileChange fc;
                fc.path = full;
                if (ev->mask & (IN_DELETE | IN_MOVED_FROM))
                    fc.event = WatchEvent::Deleted;
                else if (ev->mask & (IN_CREATE | IN_MOVED_TO))
                    fc.event = WatchEvent::Created;
                else
                    fc.event = WatchEvent::Modified;

                cb_(fc);

                // Watch new subdirectories
                if ((ev->mask & IN_CREATE) && (ev->mask & IN_ISDIR))
                    add_watch(full);
            }
        }
    }

    WatchCallback cb_;
    int ifd_ = -1;
    std::mutex mu_;
    std::unordered_map<int, std::string> wd_to_dir_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

std::unique_ptr<IWatcherBackend> make_watcher_backend() {
    return std::make_unique<InotifyBackend>();
}

} // namespace zscript

// ---------------------------------------------------------------------------
// Windows — ReadDirectoryChangesW backend
// ---------------------------------------------------------------------------
#elif ZS_WINAPI
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace zscript {

class WinWatchBackend : public IWatcherBackend {
public:
    ~WinWatchBackend() override { stop(); }

    bool start(const std::string& dir, WatchCallback cb) override {
        cb_ = std::move(cb);
        handle_ = CreateFileA(dir.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        stop_event_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        running_    = true;
        thread_     = std::thread([this, dir] { read_loop(dir); });
        return true;
    }

    void stop() override {
        if (!running_) return;
        running_ = false;
        if (stop_event_) { SetEvent(stop_event_); }
        if (thread_.joinable()) thread_.join();
        if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
        if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
    }

    const char* backend_name() const override { return "ReadDirectoryChangesW"; }

private:
    void read_loop(const std::string& base) {
        constexpr DWORD BUF = 65536;
        std::vector<char> buf(BUF);
        OVERLAPPED ov{};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        while (running_) {
            DWORD bytes = 0;
            ResetEvent(ov.hEvent);
            BOOL ok = ReadDirectoryChangesW(
                handle_,
                buf.data(), BUF,
                TRUE, // subtree
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_DIR_NAME,
                &bytes, &ov, nullptr);
            if (!ok && GetLastError() != ERROR_IO_PENDING) break;

            HANDLE evts[2] = {ov.hEvent, stop_event_};
            DWORD wait = WaitForMultipleObjects(2, evts, FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0) break; // stop or error

            GetOverlappedResult(handle_, &ov, &bytes, TRUE);
            if (!bytes) continue;

            for (auto* p = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf.data());;) {
                // Convert wide path to UTF-8
                char narrow[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0,
                    p->FileName, p->FileNameLength / sizeof(WCHAR),
                    narrow, MAX_PATH, nullptr, nullptr);
                narrow[p->FileNameLength / sizeof(WCHAR)] = '\0';

                FileChange fc;
                fc.path = base + "\\" + narrow;
                switch (p->Action) {
                case FILE_ACTION_REMOVED:
                case FILE_ACTION_RENAMED_OLD_NAME:
                    fc.event = WatchEvent::Deleted; break;
                case FILE_ACTION_ADDED:
                case FILE_ACTION_RENAMED_NEW_NAME:
                    fc.event = WatchEvent::Created; break;
                default:
                    fc.event = WatchEvent::Modified;
                }
                cb_(fc);
                if (!p->NextEntryOffset) break;
                p = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<char*>(p) + p->NextEntryOffset);
            }
        }
        CloseHandle(ov.hEvent);
    }

    WatchCallback     cb_;
    HANDLE            handle_     = INVALID_HANDLE_VALUE;
    HANDLE            stop_event_ = nullptr;
    std::thread       thread_;
    std::atomic<bool> running_{false};
};

std::unique_ptr<IWatcherBackend> make_watcher_backend() {
    return std::make_unique<WinWatchBackend>();
}

} // namespace zscript

// ---------------------------------------------------------------------------
// Polling fallback (all other platforms)
// ---------------------------------------------------------------------------
#else
#  define ZS_POLLING 1
#endif

// ---------------------------------------------------------------------------
// PollingBackend — used directly on fallback platforms and included everywhere
// ---------------------------------------------------------------------------
#include <dirent.h>

namespace zscript {

bool PollingBackend::start(const std::string& dir, WatchCallback cb) {
    dir_     = dir;
    cb_      = std::move(cb);
    running_ = true;
    // Initial scan to populate mtimes
    scan(dir_);
    thread_ = std::thread([this] { poll_loop(); });
    return true;
}

void PollingBackend::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void PollingBackend::poll_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms_));
        if (!running_) break;
        scan(dir_);
    }
}

void PollingBackend::scan(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;

    std::unordered_map<std::string, int64_t> seen;

    while (auto* ent = readdir(d)) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dir + "/" + ent->d_name;

        struct stat st{};
        if (::stat(full.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan(full); // recurse
            continue;
        }

        int64_t mtime = (int64_t)st.st_mtime;
        seen[full] = mtime;

        std::lock_guard<std::mutex> lk(mu_);
        auto it = mtimes_.find(full);
        if (it == mtimes_.end()) {
            mtimes_[full] = mtime;
            cb_({full, WatchEvent::Created});
        } else if (it->second != mtime) {
            it->second = mtime;
            cb_({full, WatchEvent::Modified});
        }
    }
    closedir(d);

    // Detect deletions
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = mtimes_.begin(); it != mtimes_.end(); ) {
        if (seen.find(it->first) == seen.end()) {
            cb_({it->first, WatchEvent::Deleted});
            it = mtimes_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// FileWatcher — debounce layer
// ---------------------------------------------------------------------------

FileWatcher::FileWatcher(int debounce_ms) : debounce_ms_(debounce_ms) {}

FileWatcher::~FileWatcher() { stop(); }

bool FileWatcher::watch(const std::string& dir, WatchCallback on_change) {
    user_cb_ = std::move(on_change);
    backend_ = make_watcher_backend();

    running_ = true;
    debounce_thread_ = std::thread([this] { debounce_thread(); });

    bool ok = backend_->start(dir, [this](const FileChange& fc) {
        on_raw_event(fc);
    });
    if (!ok) {
        running_ = false;
        if (debounce_thread_.joinable()) debounce_thread_.join();
        backend_.reset();
        return false;
    }
    return true;
}

void FileWatcher::stop() {
    if (!running_) return;
    running_ = false;
    if (backend_) backend_->stop();
    if (debounce_thread_.joinable()) debounce_thread_.join();
    backend_.reset();
}

const char* FileWatcher::backend_name() const {
    return backend_ ? backend_->backend_name() : "none";
}

void FileWatcher::on_raw_event(const FileChange& change) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(debounce_ms_);
    std::lock_guard<std::mutex> lk(mu_);
    pending_[change.path] = {change, deadline};
}

void FileWatcher::debounce_thread() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto now = std::chrono::steady_clock::now();

        std::vector<FileChange> fire;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto it = pending_.begin(); it != pending_.end(); ) {
                if (it->second.deadline <= now) {
                    fire.push_back(it->second.change);
                    it = pending_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& fc : fire) {
            if (user_cb_) user_cb_(fc);
        }
    }
}

} // namespace zscript
