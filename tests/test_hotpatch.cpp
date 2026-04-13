#include <catch2/catch_test_macros.hpp>
#include "vm.h"
#include "filewatcher.h"
#include "hotpatch.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
namespace fs = std::filesystem;

using namespace zscript;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void write_file(const std::string& path, const std::string& contents) {
    std::ofstream f(path, std::ios::trunc);
    f << contents;
}

static std::string tmp_dir() {
    return (fs::temp_directory_path() / "zscript_phase4_test").string();
}

static void mkdir_p(const std::string& path) {
    fs::create_directories(path);
}

static void rm_rf(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec); // ignore errors (dir may not exist)
}

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
    size_t count_matching(const std::function<bool(const FileChange&)>& pred) {
        std::lock_guard<std::mutex> lk(mu);
        size_t n = 0;
        for (const auto& fc : events) {
            if (pred(fc)) ++n;
        }
        return n;
    }
    bool any_matching(const std::function<bool(const FileChange&)>& pred) {
        return count_matching(pred) > 0;
    }
    std::vector<FileChange> snapshot() {
        std::lock_guard<std::mutex> lk(mu);
        return events;
    }
};

static bool wait_until(const std::function<bool()>& pred,
                       int timeout_ms = 1000,
                       int poll_ms = 10) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }
    return pred();
}

// ===========================================================================
// FileWatcher tests
// ===========================================================================

TEST_CASE("FileWatcher backend_name before start is 'none'", "[filewatcher]") {
    FileWatcher fw;
    CHECK(std::string(fw.backend_name()) == "none");
}

TEST_CASE("FileWatcher is not running before watch()", "[filewatcher]") {
    FileWatcher fw;
    CHECK(!fw.is_running());
}

TEST_CASE("PollingBackend detects file modification", "[filewatcher]") {
    std::string dir = tmp_dir() + "/fw_poll";
    mkdir_p(dir);
    std::string file = dir + "/a.zs";
    write_file(file, "var a = 0");

    ChangeLog log;
    PollingBackend pb(50);
    bool ok = pb.start(dir, [&](const FileChange& fc) { log.push(fc); });
    CHECK(ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    write_file(file, "var a = 99");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pb.stop();

    CHECK(log.count() >= 1);
    rm_rf(dir);
}

TEST_CASE("FileWatcher detects file modification", "[filewatcher]") {
    std::string dir = tmp_dir() + "/fw_modify";
    mkdir_p(dir);
    std::string file = dir + "/script.zs";
    write_file(file, "var x = 1");

    ChangeLog log;
    FileWatcher fw(50);
    bool ok = fw.watch(dir, [&](const FileChange& fc) { log.push(fc); });
    CHECK(ok);
    CHECK(fw.is_running());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_file(file, "var x = 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    fw.stop();

    CHECK(!fw.is_running());
    CHECK(log.count() >= 1);
    rm_rf(dir);
}

TEST_CASE("FileWatcher reports an active backend after watch()", "[filewatcher]") {
    std::string dir = tmp_dir() + "/fw_backend_name";
    mkdir_p(dir);
    write_file(dir + "/script.zs", "var x = 1");

    FileWatcher fw(50);
    bool ok = fw.watch(dir, [&](const FileChange&) {});
    REQUIRE(ok);

    std::string backend = fw.backend_name();
    CHECK(backend != "none");
    CHECK((backend == "FSEvents" ||
           backend == "inotify" ||
           backend == "ReadDirectoryChangesW" ||
           backend == "polling"));

    fw.stop();
    rm_rf(dir);
}

TEST_CASE("FileWatcher debounce collapses rapid writes", "[filewatcher]") {
    std::string dir = tmp_dir() + "/fw_debounce";
    mkdir_p(dir);
    std::string file = dir + "/script.zs";
    write_file(file, "var x = 0");

    std::atomic<int> cb_count{0};
    FileWatcher fw(150);
    fw.watch(dir, [&](const FileChange&) { ++cb_count; });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int i = 1; i <= 5; ++i) {
        write_file(file, "var x = " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    fw.stop();

    CHECK(cb_count.load() >= 1);
    CHECK(cb_count.load() <= 2);  // debounce collapsed rapid writes
    rm_rf(dir);
}

TEST_CASE("FileWatcher debounce preserves separate write bursts", "[filewatcher]") {
    std::string dir = tmp_dir() + "/fw_two_bursts";
    mkdir_p(dir);
    std::string file = dir + "/script.zs";
    write_file(file, "var x = 0");

    std::atomic<int> cb_count{0};
    FileWatcher fw(120);
    bool ok = fw.watch(dir, [&](const FileChange& fc) {
        if (fs::path(fc.path).filename() == "script.zs") ++cb_count;
    });
    REQUIRE(ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    write_file(file, "var x = 1");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    write_file(file, "var x = 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(320));

    write_file(file, "var x = 3");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    write_file(file, "var x = 4");

    CHECK(wait_until([&] { return cb_count.load() >= 2; }, 1000));
    fw.stop();

    CHECK(cb_count.load() >= 2);
    rm_rf(dir);
}

TEST_CASE("FileWatcher rapid writes surface file events, not directory noise", "[filewatcher]") {
    std::string dir = tmp_dir() + "/fw_paths";
    mkdir_p(dir);
    std::string file = dir + "/script.zs";
    write_file(file, "var x = 0");

    ChangeLog log;
    FileWatcher fw(150);
    bool ok = fw.watch(dir, [&](const FileChange& fc) { log.push(fc); });
    REQUIRE(ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int i = 1; i <= 5; ++i) {
        write_file(file, "var x = " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    CHECK(wait_until([&] { return log.count() >= 1; }, 1000));
    fw.stop();

    auto events = log.snapshot();
    REQUIRE(!events.empty());
    for (const auto& fc : events) {
        CHECK(fs::path(fc.path).filename() == "script.zs");
    }
    rm_rf(dir);
}

TEST_CASE("PollingBackend detects create and delete events", "[filewatcher]") {
    std::string dir = tmp_dir() + "/fw_poll_create_delete";
    mkdir_p(dir);
    std::string file = dir + "/created_then_deleted.zs";

    ChangeLog log;
    PollingBackend pb(25);
    bool ok = pb.start(dir, [&](const FileChange& fc) { log.push(fc); });
    REQUIRE(ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    write_file(file, "var x = 1");
    CHECK(wait_until([&] {
        return log.any_matching([&](const FileChange& fc) {
            return fs::path(fc.path).filename() == "created_then_deleted.zs" &&
                   fc.event == WatchEvent::Created;
        });
    }, 1000));

    rm_rf(file);
    CHECK(wait_until([&] {
        return log.any_matching([&](const FileChange& fc) {
            return fs::path(fc.path).filename() == "created_then_deleted.zs" &&
                   fc.event == WatchEvent::Deleted;
        });
    }, 1000));

    pb.stop();
    rm_rf(dir);
}

TEST_CASE("PollingBackend detects nested file creation", "[filewatcher]") {
    std::string dir = tmp_dir() + "/fw_poll_nested";
    std::string subdir = dir + "/nested";
    mkdir_p(subdir);
    std::string file = subdir + "/inner.zs";

    ChangeLog log;
    PollingBackend pb(25);
    bool ok = pb.start(dir, [&](const FileChange& fc) { log.push(fc); });
    REQUIRE(ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    write_file(file, "var x = 1");

    CHECK(wait_until([&] {
        return log.any_matching([&](const FileChange& fc) {
            return fs::path(fc.path).filename() == "inner.zs" &&
                   fc.event == WatchEvent::Created;
        });
    }, 1000));

    pb.stop();
    rm_rf(dir);
}

// ===========================================================================
// HotpatchManager tests
// ===========================================================================

TEST_CASE("HotpatchManager enable and disable", "[hotpatch]") {
    std::string dir = tmp_dir() + "/hp_basic";
    mkdir_p(dir);

    VM vm;
    vm.open_stdlib();
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "mymod") return "var VERSION = 1";
        return "";
    });
    vm.import_module("mymod");

    HotpatchManager hpm(vm);
    bool ok = hpm.enable(dir);
    CHECK(ok);
    CHECK(hpm.is_enabled());

    hpm.disable();
    CHECK(!hpm.is_enabled());
    rm_rf(dir);
}

TEST_CASE("HotpatchManager reload_count starts at zero", "[hotpatch]") {
    std::string dir = tmp_dir() + "/hp_count";
    mkdir_p(dir);
    VM vm;
    vm.open_stdlib();
    HotpatchManager hpm(vm);
    hpm.enable(dir);
    CHECK(hpm.reload_count() == 0);
    hpm.disable();
    rm_rf(dir);
}

TEST_CASE("HotpatchManager apply_pending with no changes returns 0", "[hotpatch]") {
    std::string dir = tmp_dir() + "/hp_poll";
    mkdir_p(dir);
    VM vm;
    vm.open_stdlib();
    HotpatchManager hpm(vm);
    hpm.enable(dir);
    int reloaded = hpm.apply_pending();
    CHECK(reloaded == 0);
    hpm.disable();
    rm_rf(dir);
}

TEST_CASE("vm.enable_hotpatch and vm.poll API", "[hotpatch]") {
    std::string dir = tmp_dir() + "/vm_hp";
    mkdir_p(dir);
    VM vm;
    vm.open_stdlib();
    bool ok = vm.enable_hotpatch(dir);
    CHECK(ok);
    int n = vm.poll();
    CHECK(n == 0);
    rm_rf(dir);
}

TEST_CASE("file modification triggers hotpatch reload", "[hotpatch][integration]") {
    std::string dir = tmp_dir() + "/hp_reload";
    mkdir_p(dir);
    std::string file = dir + "/counter.zs";
    write_file(file, "var VALUE = 10");

    VM vm;
    vm.open_stdlib();
    vm.loader().add_search_path(dir);
    vm.import_module("counter");
    CHECK(vm.get_global("VALUE").as_int() == 10);

    bool ok = vm.enable_hotpatch(dir);
    CHECK(ok);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_file(file, "var VALUE = 42");

    // FSEvents on macOS CI can be slow; poll in a loop up to 10 s.
    int reloaded = 0;
    for (int i = 0; i < 100 && reloaded == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        reloaded = vm.poll();
    }
    CHECK(reloaded >= 1);
    CHECK(vm.get_global("VALUE").as_int() == 42);
    rm_rf(dir);
}

TEST_CASE("module version increments on hotpatch reload", "[hotpatch][integration]") {
    std::string dir = tmp_dir() + "/mod_ver";
    mkdir_p(dir);
    std::string file = dir + "/versmod.zs";
    write_file(file, "var V = 1");

    VM vm;
    vm.open_stdlib();
    vm.loader().add_search_path(dir);
    vm.import_module("versmod");

    Module* mod = vm.loader().find("versmod");
    REQUIRE(mod != nullptr);
    CHECK(mod->version == 0);

    vm.enable_hotpatch(dir);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    write_file(file, "var V = 2");

    // FSEvents on macOS CI can be slow; poll in a loop up to 10 s.
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (vm.poll() > 0) break;
    }

    Module* mod2 = vm.loader().find("versmod");
    REQUIRE(mod2 != nullptr);
    CHECK(mod2->version == 1);
    rm_rf(dir);
}

// ===========================================================================
// call_value tests
// ===========================================================================

TEST_CASE("call_value with native function", "[vm][call_value]") {
    VM vm;
    vm.open_stdlib();
    Value fn = Value::from_native("add", [](std::vector<Value> args) -> std::vector<Value> {
        int64_t a = args.size() > 0 ? args[0].as_int() : 0;
        int64_t b = args.size() > 1 ? args[1].as_int() : 0;
        return {Value::from_int(a + b)};
    });
    Value result = vm.call_value(fn, {Value::from_int(3), Value::from_int(4)});
    CHECK(result.as_int() == 7);
}
