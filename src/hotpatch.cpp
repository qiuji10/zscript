#include "hotpatch.h"
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <stdlib.h>

namespace zscript {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------
// HotpatchManager
// ---------------------------------------------------------------------------

HotpatchManager::HotpatchManager(VM& vm) : vm_(vm) {}

HotpatchManager::~HotpatchManager() { disable(); }

static std::string canonical(const std::string& path) {
    char buf[PATH_MAX];
    if (realpath(path.c_str(), buf)) return buf;
    return path;
}

bool HotpatchManager::enable(const std::string& dir) {
    if (enabled_) return true;
    watch_dir_ = canonical(dir);
    enabled_   = true;

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
    std::string source = read_file(path);
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
        // Execute the new module in the VM to populate its exports
        bool ok = vm_.execute_module(*pr.new_module->chunk, pr.module_name);
        if (!ok) {
            std::cerr << "[hotpatch] Runtime error reloading " << pr.module_name
                      << ": " << vm_.last_error().message << "\n";
            continue;
        }

        // Call on_reload(old) -> new if defined in new module
        // (The module's on_reload function, if present, is now a global)
        Value on_reload_fn = vm_.get_global("on_reload");
        if (on_reload_fn.is_closure() || on_reload_fn.is_native()) {
            // Pass nil as old state for now (full migration is Phase 5+)
            vm_.call_value(on_reload_fn, {Value::nil()});
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
