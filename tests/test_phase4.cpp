#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "filewatcher.h"
#include "hotpatch.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>

using namespace zscript;

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;

#define EXPECT(cond, msg) \
    do { if (cond) { ++g_pass; } \
         else { ++g_fail; std::cerr << "FAIL [" << __LINE__ << "]: " << (msg) << "\n"; } \
    } while(0)

// ---------------------------------------------------------------------------
// FileChange accumulator — thread-safe
// ---------------------------------------------------------------------------
struct ChangeLog {
    std::mutex mu;
    std::vector<FileChange> events;

    void push(const FileChange& fc) {
        std::lock_guard<std::mutex> lk(mu);
        events.push_back(fc);
    }

    size_t count() {
        std::lock_guard<std::mutex> lk(mu);
        return events.size();
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void write_file(const std::string& path, const std::string& contents) {
    std::ofstream f(path, std::ios::trunc);
    f << contents;
}

static std::string tmp_dir() {
    // Use /tmp for simplicity — tests clean up after themselves.
    return "/tmp/zscript_phase4_test";
}

static void mkdir_p(const std::string& path) {
    std::string cmd = "mkdir -p " + path;
    (void)std::system(cmd.c_str());
}

static void rm_rf(const std::string& path) {
    std::string cmd = "rm -rf " + path;
    (void)std::system(cmd.c_str());
}

// ===========================================================================
// FileWatcher tests
// ===========================================================================

void test_filewatcher_backend_name() {
    FileWatcher fw;
    // Before watch() is called, backend_name() returns "none"
    EXPECT(std::string(fw.backend_name()) == "none", "backend_name before start is 'none'");
}

void test_filewatcher_is_not_running_initially() {
    FileWatcher fw;
    EXPECT(!fw.is_running(), "FileWatcher not running before watch()");
}

void test_filewatcher_detects_modify() {
    std::string dir = tmp_dir() + "/fw_modify";
    mkdir_p(dir);
    std::string file = dir + "/script.zs";
    write_file(file, "var x = 1");

    ChangeLog log;
    FileWatcher fw(50); // 50ms debounce
    bool ok = fw.watch(dir, [&](const FileChange& fc) { log.push(fc); });
    EXPECT(ok, "FileWatcher started");
    EXPECT(fw.is_running(), "FileWatcher is running");

    // Wait briefly for watcher to settle, then modify the file
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_file(file, "var x = 2");

    // Wait for debounce + callback
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    fw.stop();
    EXPECT(!fw.is_running(), "FileWatcher stopped");

    size_t n = log.count();
    EXPECT(n >= 1, "at least one change detected (got " + std::to_string(n) + ")");

    rm_rf(dir);
}

void test_filewatcher_debounce() {
    std::string dir = tmp_dir() + "/fw_debounce";
    mkdir_p(dir);
    std::string file = dir + "/script.zs";
    write_file(file, "var x = 0");

    std::atomic<int> cb_count{0};
    FileWatcher fw(150); // 150ms debounce
    fw.watch(dir, [&](const FileChange&) { ++cb_count; });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Write 5 times rapidly — debounce should collapse to 1 callback
    for (int i = 1; i <= 5; ++i) {
        write_file(file, "var x = " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Wait for debounce window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    fw.stop();

    EXPECT(cb_count.load() <= 2, "debounce collapsed rapid writes (got " +
           std::to_string(cb_count.load()) + ")");
    EXPECT(cb_count.load() >= 1, "at least one callback fired");

    rm_rf(dir);
}

void test_polling_backend_directly() {
    std::string dir = tmp_dir() + "/fw_poll";
    mkdir_p(dir);
    std::string file = dir + "/a.zs";
    write_file(file, "var a = 0");

    ChangeLog log;
    PollingBackend pb(50); // 50ms poll interval
    bool ok = pb.start(dir, [&](const FileChange& fc) { log.push(fc); });
    EXPECT(ok, "PollingBackend started");

    // Initial scan fired Created for existing file
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    write_file(file, "var a = 99");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pb.stop();

    EXPECT(log.count() >= 1, "polling detected change");

    rm_rf(dir);
}

// ===========================================================================
// HotpatchManager tests
// ===========================================================================

void test_hotpatch_enable_disable() {
    std::string dir = tmp_dir() + "/hp_basic";
    mkdir_p(dir);

    VM vm;
    vm.open_stdlib();

    // Register a dummy module
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "mymod") return "var VERSION = 1";
        return "";
    });
    vm.import_module("mymod");

    HotpatchManager hpm(vm);
    bool ok = hpm.enable(dir);
    EXPECT(ok, "HotpatchManager enabled");
    EXPECT(hpm.is_enabled(), "is_enabled() true");

    hpm.disable();
    EXPECT(!hpm.is_enabled(), "is_enabled() false after disable");

    rm_rf(dir);
}

void test_hotpatch_reload_count_starts_zero() {
    std::string dir = tmp_dir() + "/hp_count";
    mkdir_p(dir);

    VM vm;
    vm.open_stdlib();
    HotpatchManager hpm(vm);
    hpm.enable(dir);

    EXPECT(hpm.reload_count() == 0, "reload_count starts at 0");

    hpm.disable();
    rm_rf(dir);
}

void test_hotpatch_poll_noop_when_no_changes() {
    std::string dir = tmp_dir() + "/hp_poll";
    mkdir_p(dir);

    VM vm;
    vm.open_stdlib();
    HotpatchManager hpm(vm);
    hpm.enable(dir);

    int reloaded = hpm.apply_pending();
    EXPECT(reloaded == 0, "poll with no changes returns 0");

    hpm.disable();
    rm_rf(dir);
}

void test_vm_enable_hotpatch_api() {
    std::string dir = tmp_dir() + "/vm_hp";
    mkdir_p(dir);

    VM vm;
    vm.open_stdlib();
    bool ok = vm.enable_hotpatch(dir);
    EXPECT(ok, "vm.enable_hotpatch() returns true");

    int n = vm.poll();
    EXPECT(n == 0, "vm.poll() with no changes returns 0");

    rm_rf(dir);
}

void test_hotpatch_file_triggers_reload() {
    std::string dir = tmp_dir() + "/hp_reload";
    mkdir_p(dir);

    // Write initial module file
    std::string file = dir + "/counter.zs";
    write_file(file, "var VALUE = 10");

    VM vm;
    vm.open_stdlib();
    // Point module search to our tmp dir
    vm.loader().add_search_path(dir);
    vm.import_module("counter");

    EXPECT(vm.get_global("VALUE").as_int() == 10, "initial VALUE = 10");

    // Enable hotpatch
    bool ok = vm.enable_hotpatch(dir);
    EXPECT(ok, "hotpatch enabled");

    // Wait for watcher to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Modify the file
    write_file(file, "var VALUE = 42");

    // Wait for debounce + recompile + queue
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Apply at safe point
    int reloaded = vm.poll();
    EXPECT(reloaded >= 1, "at least one module reloaded (got " + std::to_string(reloaded) + ")");
    EXPECT(vm.get_global("VALUE").as_int() == 42, "VALUE updated to 42 after reload");

    rm_rf(dir);
}

// ===========================================================================
// Module version counter tests
// ===========================================================================

void test_module_version_increments_on_reload() {
    std::string dir = tmp_dir() + "/mod_ver";
    mkdir_p(dir);

    std::string file = dir + "/versmod.zs";
    write_file(file, "var V = 1");

    VM vm;
    vm.open_stdlib();
    vm.loader().add_search_path(dir);
    vm.import_module("versmod");

    Module* mod = vm.loader().find("versmod");
    EXPECT(mod != nullptr, "module found in registry");
    EXPECT(mod->version == 0, "initial version is 0");

    vm.enable_hotpatch(dir);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    write_file(file, "var V = 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    vm.poll();

    Module* mod2 = vm.loader().find("versmod");
    EXPECT(mod2 != nullptr, "module still in registry after reload");
    EXPECT(mod2->version == 1, "version incremented to 1");

    rm_rf(dir);
}

// ===========================================================================
// call_value tests
// ===========================================================================

void test_call_value_native() {
    VM vm;
    vm.open_stdlib();
    Value fn = Value::from_native("add", [](std::vector<Value> args) -> std::vector<Value> {
        int64_t a = args.size() > 0 ? args[0].as_int() : 0;
        int64_t b = args.size() > 1 ? args[1].as_int() : 0;
        return {Value::from_int(a + b)};
    });
    Value result = vm.call_value(fn, {Value::from_int(3), Value::from_int(4)});
    EXPECT(result.as_int() == 7, "call_value native: 3+4=7");
}

// ===========================================================================
// main
// ===========================================================================
int main() {
    // FileWatcher
    test_filewatcher_backend_name();
    test_filewatcher_is_not_running_initially();
    test_polling_backend_directly();
    test_filewatcher_debounce();
    test_filewatcher_detects_modify();

    // HotpatchManager API
    test_hotpatch_enable_disable();
    test_hotpatch_reload_count_starts_zero();
    test_hotpatch_poll_noop_when_no_changes();
    test_vm_enable_hotpatch_api();

    // call_value
    test_call_value_native();

    // Integration: file change triggers reload
    test_hotpatch_file_triggers_reload();
    test_module_version_increments_on_reload();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail > 0 ? 1 : 0;
}
