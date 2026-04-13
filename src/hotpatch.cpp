#include "hotpatch.h"
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#if defined(_WIN32)
#  include <windows.h>
#  include <direct.h>
#else
#  include <limits.h>
#  include <sys/stat.h>
#endif

namespace zscript {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Return the file's last-modification time in nanoseconds since the Unix epoch.
// Returns -1 on error.
static int64_t path_mtime_ns(const std::string& path) {
#if defined(_WIN32)
    // _stat() has 1-second resolution on Windows — use GetFileAttributesExA
    // instead to get FILETIME (100-ns resolution) so writes 100 ms apart are
    // distinguishable.  FILETIME epoch is 1601-01-01; convert to Unix epoch by
    // subtracting 116444736000000000 × 100 ns intervals.
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attr)) return -1;
    ULARGE_INTEGER ft;
    ft.LowPart  = attr.ftLastWriteTime.dwLowDateTime;
    ft.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    // Convert 100-ns FILETIME intervals to nanoseconds, shifting to Unix epoch.
    return (int64_t)((ft.QuadPart - 116444736000000000ULL) * 100LL);
#elif defined(__APPLE__)
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return -1;
    return (int64_t)st.st_mtimespec.tv_sec * 1000000000LL
         + (int64_t)st.st_mtimespec.tv_nsec;
#else
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return -1;
    return (int64_t)st.st_mtim.tv_sec * 1000000000LL
         + (int64_t)st.st_mtim.tv_nsec;
#endif
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string read_stable_file(const std::string& path) {
    std::string previous;
    std::string current;
    for (int attempt = 0; attempt < 5; ++attempt) {
        current = read_file(path);
        if (!current.empty() && current == previous) {
            return current;
        }
        previous = current;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return current;
}

// ---------------------------------------------------------------------------
// HotpatchManager
// ---------------------------------------------------------------------------

HotpatchManager::HotpatchManager(VM& vm) : vm_(vm) {}

HotpatchManager::~HotpatchManager() { disable(); }

static std::string canonical(const std::string& path) {
#if defined(_WIN32)
    char buf[MAX_PATH];
    if (_fullpath(buf, path.c_str(), MAX_PATH)) return buf;
    return path;
#else
    char buf[PATH_MAX];
    if (realpath(path.c_str(), buf)) return buf;
    return path;
#endif
}

bool HotpatchManager::enable(const std::string& dir) {
    if (enabled_) return true;
    watch_dir_ = canonical(dir);
    enabled_   = true;

    // Baseline scan: record the current mtime of every .zs file in the watched
    // directory.  FSEvents (and some other backends) can deliver "late" events
    // for writes that happened just before the stream was created.  We suppress
    // those stale events in on_file_changed() by ignoring any event whose file
    // mtime has not advanced past this baseline.
    {
        std::lock_guard<std::mutex> lk(baseline_mu_);
        std::error_code ec;
        for (auto& entry : std::filesystem::recursive_directory_iterator(watch_dir_, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec) || ec) continue;
            if (entry.path().extension() != ".zs") continue;
            std::string cpath = canonical(entry.path().string());
            int64_t mtime = path_mtime_ns(cpath);
            if (mtime >= 0) baseline_mtimes_[cpath] = mtime;
        }
    }

    // Start background recompile thread
    recompile_thread_ = std::thread([this] {
        while (enabled_) {
            std::vector<std::string> batch;
            {
                std::lock_guard<std::mutex> lk(recompile_mu_);
                batch.swap(recompile_queue_);
            }
            for (auto& p : batch) {
                recompile(p);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Start FileWatcher
    bool ok = watcher_.watch(dir, [this](const FileChange& fc) {
        on_file_changed(fc);
    });
    if (!ok) {
        enabled_ = false;
        if (recompile_thread_.joinable()) recompile_thread_.join();
        return false;
    }
    return true;
}

void HotpatchManager::disable() {
    if (!enabled_) return;
    enabled_ = false;
    watcher_.stop();
    if (recompile_thread_.joinable()) recompile_thread_.join();
}

// ---------------------------------------------------------------------------
// on_file_changed — FileWatcher thread
// ---------------------------------------------------------------------------

void HotpatchManager::on_file_changed(const FileChange& fc) {
    // Only care about .zs files that were modified or created
    if (fc.event == WatchEvent::Deleted) return;
    const std::string path = canonical(fc.path);
    if (path.size() < 3 || path.substr(path.size() - 3) != ".zs") return;

    // Suppress stale events: FSEvents (and similar) can deliver events for
    // writes that predated the stream's creation.  Skip if the file's mtime
    // hasn't advanced past the baseline recorded at enable() time.
    {
        int64_t mtime = path_mtime_ns(path);
        std::lock_guard<std::mutex> lk(baseline_mu_);
        auto it = baseline_mtimes_.find(path);
        int64_t baseline = (it != baseline_mtimes_.end()) ? it->second : 0;
        if (mtime >= 0 && mtime <= baseline) return;  // stale — ignore
        // Advance the baseline so the same mtime doesn't re-trigger later.
        baseline_mtimes_[path] = (mtime >= 0) ? mtime : baseline + 1;
    }

    std::lock_guard<std::mutex> lk(recompile_mu_);
    // Deduplicate: only enqueue if not already pending
    if (std::find(recompile_queue_.begin(), recompile_queue_.end(), path)
            == recompile_queue_.end()) {
        recompile_queue_.push_back(path);
    }
}

// ---------------------------------------------------------------------------
// recompile — background recompile thread
// ---------------------------------------------------------------------------

void HotpatchManager::recompile(const std::string& path) {
    std::string source = read_stable_file(path);
    if (source.empty()) return;

    std::string mod_name = path_to_module_name(path);

    // Only reload a module that is already known to the loader
    Module* existing = vm_.loader().find(mod_name);
    if (!existing) return;

    // Lex + parse
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    Program prog = parser.parse();
    if (parser.has_errors()) {
        std::cerr << "[hotpatch] Parse errors in " << path << ":\n";
        for (auto& e : parser.errors())
            std::cerr << "  " << e.message << "\n";
        return;
    }

    // Compile
    Compiler compiler;
    auto chunk = compiler.compile(prog, path);
    if (compiler.has_errors()) {
        std::cerr << "[hotpatch] Compile errors in " << path << ":\n";
        for (auto& e : compiler.errors())
            std::cerr << "  " << e.message << "\n";
        return;
    }

    // Build a new Module
    auto new_mod = std::make_shared<Module>();
    new_mod->name     = mod_name;
    new_mod->filepath = path;
    new_mod->state    = Module::State::Loaded;
    new_mod->chunk    = std::move(chunk);
    new_mod->exports  = existing->exports;
    // version counter: existing version + 1
    new_mod->version  = existing->version + 1;

    PendingReload pr;
    pr.module_name = mod_name;
    pr.source_path = path;
    pr.new_module  = std::move(new_mod);

    std::lock_guard<std::mutex> lk(ready_mu_);
    // Replace any stale pending reload for the same module
    for (auto& p : ready_queue_) {
        if (p.module_name == mod_name) {
            p = std::move(pr);
            return;
        }
    }
    ready_queue_.push_back(std::move(pr));
}

// ---------------------------------------------------------------------------
// apply_pending — VM main thread (safe point)
// ---------------------------------------------------------------------------

int HotpatchManager::apply_pending() {
    std::vector<PendingReload> batch;
    {
        std::lock_guard<std::mutex> lk(ready_mu_);
        batch.swap(ready_queue_);
    }

    int count = 0;
    for (auto& pr : batch) {
        // Execute the new module in an isolated namespace, then merge its
        // exports into the live VM globals once the reload succeeds.
        std::string error_msg;
        bool ok = vm_.execute_module(*pr.new_module, error_msg);
        if (!ok) {
            std::cerr << "[hotpatch] Runtime error reloading " << pr.module_name
                      << ": "
                      << (error_msg.empty() ? vm_.last_error().message : error_msg)
                      << "\n";
            continue;
        }

        // Call on_reload(old) -> new if defined in new module
        // (The module's on_reload function, if present, is now a global)
        Value on_reload_fn = vm_.get_global("on_reload");
        if (on_reload_fn.is_closure() || on_reload_fn.is_native()) {
            // Pass nil as old state for now (full migration is Phase 5+)
            vm_.call_value(on_reload_fn, {Value::nil()});
        }

        for (auto& [k, v] : pr.new_module->exports) {
            vm_.set_global(k, v);
        }

        // Swap the module in the loader registry
        vm_.loader().replace(pr.module_name, pr.new_module);

        std::cout << "[hotpatch] Reloaded module '" << pr.module_name
                  << "' (v" << pr.new_module->version << ")\n";
        ++count;
        ++reload_count_;
    }
    return count;
}

// ---------------------------------------------------------------------------
// path_to_module_name
// ---------------------------------------------------------------------------

std::string HotpatchManager::path_to_module_name(const std::string& path) const {
    // path must already be canonical (caller ensures this).
    // Strip the watch_dir_ prefix (with trailing slash).
    std::string rel = path;
    if (!watch_dir_.empty()) {
        std::string prefix = watch_dir_;
        if (prefix.back() != '/' && prefix.back() != '\\') prefix += '/';
        if (rel.substr(0, prefix.size()) == prefix)
            rel = rel.substr(prefix.size());
        else if (rel.substr(0, watch_dir_.size()) == watch_dir_)
            rel = rel.substr(watch_dir_.size() + 1);
    }
    // Strip .zs suffix
    if (rel.size() >= 3 && rel.substr(rel.size() - 3) == ".zs")
        rel = rel.substr(0, rel.size() - 3);
    // Replace path separators with dots
    for (char& c : rel)
        if (c == '/' || c == '\\') c = '.';
    return rel;
}

} // namespace zscript
