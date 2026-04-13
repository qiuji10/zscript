#pragma once
#include "filewatcher.h"
#include "module.h"
#include "vm.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace zscript {

// ---------------------------------------------------------------------------
// HotpatchManager
//
// Watches a directory for .zs file changes, recompiles them in a background
// thread, then applies the new bytecode atomically at the next vm.poll().
//
// Thread model:
//   FileWatcher thread  → detects change, enqueues path
//   Recompile thread    → compiles new bytecode from source
//   VM main thread      → calls poll() at a safe point, swaps module in
//
// Module lifecycle:
//   v1 Module ptr ──────────┐ (kept alive until all frames using it drain)
//   v2 Module ptr ──new──── swap into loader registry
//
// The old Module is kept alive by the PendingReload entry until poll()
// confirms no frames reference it (conservative: we always keep one cycle).
// ---------------------------------------------------------------------------

struct PendingReload {
    std::string             module_name;   // logical module name (no .zs suffix)
    std::string             source_path;   // filesystem path that changed
    std::shared_ptr<Module> new_module;    // freshly compiled module
};

class HotpatchManager {
public:
    explicit HotpatchManager(VM& vm);
    ~HotpatchManager();

    // Start watching a directory. Returns false if the FileWatcher fails.
    bool enable(const std::string& dir);

    // Stop watching and join all threads.
    void disable();

    // Called by VM::poll() on the main thread.
    // Applies any pending hotpatches that are ready.
    // Returns the number of modules reloaded.
    int apply_pending();

    bool is_enabled() const { return enabled_; }

    // Diagnostic: total number of hotpatches applied since enable().
    size_t reload_count() const { return reload_count_.load(); }

private:
    // Called on FileWatcher thread — enqueues a recompile job.
    void on_file_changed(const FileChange& fc);

    // Recompile a source file and push a PendingReload.
    // Runs on the recompile thread (or inline if single-threaded).
    void recompile(const std::string& path);

    // Map a filesystem path to a module name (strips dir prefix and .zs suffix).
    std::string path_to_module_name(const std::string& path) const;

    // Fallback polling scan: stat every file in baseline_mtimes_ and enqueue
    // any whose mtime has advanced.  Called from the recompile thread every
    // ~500 ms so hotpatch works even when the OS event backend is slow.
    void scan_for_changes();

    VM&             vm_;
    std::string     watch_dir_;
    FileWatcher     watcher_;

    // Baseline mtime (nanoseconds since epoch) of every .zs file at enable()
    // time.  Events whose file mtime has not advanced past this baseline are
    // stale FSEvents deliveries for pre-hotpatch writes and are discarded.
    std::mutex                           baseline_mu_;
    std::unordered_map<std::string, int64_t> baseline_mtimes_;

    // Queue of paths that need recompilation (written by watcher thread)
    std::mutex              recompile_mu_;
    std::vector<std::string> recompile_queue_;

    // Queue of compiled modules ready to apply (written by recompile thread,
    // drained by main thread in poll())
    std::mutex                   ready_mu_;
    std::vector<PendingReload>   ready_queue_;

    // Background recompile thread
    std::thread          recompile_thread_;
    std::atomic<bool>    enabled_{false};

    std::atomic<size_t>  reload_count_{0};
};

} // namespace zscript
