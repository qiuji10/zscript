#include <catch2/catch_test_macros.hpp>
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "gc.h"
#include "binding.h"
#include <cmath>

using namespace zscript;

// ---------------------------------------------------------------------------
// Ctx helper
// ---------------------------------------------------------------------------
struct Ctx {
    VM vm;
    Ctx() { vm.open_stdlib(); }

    bool run(const std::string& src) {
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        Program prog = parser.parse();
        if (parser.has_errors()) return false;
        Compiler compiler;
        auto chunk = compiler.compile(prog, "<test>");
        if (compiler.has_errors()) return false;
        return vm.execute(*chunk);
    }
    Value g(const std::string& n) { return vm.get_global(n); }
};

// ===========================================================================
// GC tests
// ===========================================================================

TEST_CASE("GC tracks objects", "[gc]") {
    GC gc;
    auto str = std::make_shared<ZString>("hello");
    gc.track(str.get(), 32);
    CHECK(gc.num_objects() == 1);
    CHECK(gc.bytes_allocated() == 32);
}

TEST_CASE("GC tracks multiple objects", "[gc]") {
    GC gc;
    auto s1 = std::make_shared<ZString>("one");
    auto s2 = std::make_shared<ZString>("two");
    auto s3 = std::make_shared<ZString>("three");
    gc.track(s1.get(), 16);
    gc.track(s2.get(), 16);
    gc.track(s3.get(), 16);
    CHECK(gc.num_objects() == 3);
    CHECK(gc.bytes_allocated() == 48);
}

TEST_CASE("GC collects unreachable objects", "[gc]") {
    GC gc;
    gc.step_alloc_limit = 1;
    {
        auto str = std::make_shared<ZString>("temp");
        gc.track(str.get(), 32);
        REQUIRE(gc.num_objects() == 1);
        gc.collect([](GC&) {});
        CHECK(gc.num_objects() == 0);
    }
}

TEST_CASE("GC keeps pinned objects alive", "[gc]") {
    GC gc;
    auto str = std::make_shared<ZString>("pinned");
    gc.track(str.get(), 32);
    gc.pin(str.get());
    gc.collect([](GC&) {});
    CHECK(gc.num_objects() == 1);  // still alive
    gc.unpin(str.get());
    gc.collect([](GC&) {});
    CHECK(gc.num_objects() == 0);  // swept after unpin
}

TEST_CASE("GC pin multiple: only pinned survive", "[gc]") {
    GC gc;
    auto s1 = std::make_shared<ZString>("a");
    auto s2 = std::make_shared<ZString>("b");
    gc.track(s1.get(), 16);
    gc.track(s2.get(), 16);
    gc.pin(s1.get());
    gc.collect([](GC&) {});
    CHECK(gc.num_objects() == 1);
    gc.unpin(s1.get());
    gc.collect([](GC&) {});
    CHECK(gc.num_objects() == 0);
}

TEST_CASE("GC marks roots via callback", "[gc]") {
    GC gc;
    auto str = std::make_shared<ZString>("root");
    gc.track(str.get(), 32);
    Value v = Value::from_string("root");
    gc.collect([&](GC& g) { g.mark_value(v); });
    // v was created independently; the tracked ptr is different
    CHECK(gc.num_objects() == 0);
}

TEST_CASE("GC table traversal keeps children alive", "[gc]") {
    GC gc;
    auto tbl = std::make_shared<ZTable>();
    auto str = std::make_shared<ZString>("child");
    gc.track(tbl.get(), 64);
    gc.track(str.get(), 32);

    Value sv; sv.tag = Value::Tag::String; sv.str_ptr = str;
    tbl->set("key", sv);
    Value tv; tv.tag = Value::Tag::Table; tv.table_ptr = tbl;

    gc.collect([&](GC& g) { g.mark_value(tv); });
    CHECK(gc.num_objects() == 2);  // table + child string survive
}

TEST_CASE("GC table: orphan child is swept", "[gc]") {
    GC gc;
    auto tbl = std::make_shared<ZTable>();
    auto str = std::make_shared<ZString>("orphan");
    gc.track(tbl.get(), 64);
    gc.track(str.get(), 32);
    // str NOT in tbl — orphan
    Value tv; tv.tag = Value::Tag::Table; tv.table_ptr = tbl;
    gc.collect([&](GC& g) { g.mark_value(tv); });
    CHECK(gc.num_objects() == 1);  // only table survives
}

TEST_CASE("GC maybe_collect: under limit skips collection", "[gc]") {
    GC gc;
    gc.step_alloc_limit = 100;
    auto s1 = std::make_shared<ZString>("a");
    gc.track(s1.get(), 50);
    bool collected = gc.maybe_collect([](GC&) {});
    CHECK(!collected);
}

TEST_CASE("GC maybe_collect: over limit triggers collection", "[gc]") {
    GC gc;
    gc.step_alloc_limit = 100;
    size_t initial = gc.num_collections();
    auto s1 = std::make_shared<ZString>("a");
    auto s2 = std::make_shared<ZString>("b");
    gc.track(s1.get(), 60);
    gc.track(s2.get(), 60);  // 120 > 100
    bool collected = gc.maybe_collect([](GC&) {});
    CHECK(collected);
    CHECK(gc.num_collections() == initial + 1);
}

TEST_CASE("GC collection count increments", "[gc]") {
    GC gc;
    size_t n0 = gc.num_collections();
    gc.collect([](GC&) {});
    CHECK(gc.num_collections() == n0 + 1);
    gc.collect([](GC&) {});
    CHECK(gc.num_collections() == n0 + 2);
}

TEST_CASE("GC bytes decrease after collecting unreachable", "[gc]") {
    GC gc;
    auto str = std::make_shared<ZString>("data");
    gc.track(str.get(), 100);
    CHECK(gc.bytes_allocated() == 100);
    gc.collect([](GC&) {});  // no roots → swept
    CHECK(gc.bytes_allocated() == 0);
}

// ===========================================================================
// Module tests
// ===========================================================================

TEST_CASE("module source provider exports globals", "[module]") {
    VM vm;
    vm.open_stdlib();
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "mylib") return "fn greet() -> nil { log(\"hi\") }  var VERSION = 42";
        return "";
    });
    bool ok = vm.import_module("mylib");
    CHECK(ok);
    CHECK(vm.get_global("VERSION").is_int());
    CHECK(vm.get_global("VERSION").as_int() == 42);
    CHECK(vm.get_global("greet").is_closure());
}

TEST_CASE("module exports multiple functions", "[module]") {
    VM vm;
    vm.open_stdlib();
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "mathlib") return R"(
            fn add(a: Int, b: Int) -> Int { return a + b }
            fn mul(a: Int, b: Int) -> Int { return a * b }
            var ZERO = 0
        )";
        return "";
    });
    REQUIRE(vm.import_module("mathlib"));
    CHECK(vm.get_global("add").is_closure());
    CHECK(vm.get_global("mul").is_closure());
    CHECK(vm.get_global("ZERO").as_int() == 0);
}

TEST_CASE("module is cached after first load", "[module]") {
    VM vm;
    vm.open_stdlib();
    int load_count = 0;
    vm.loader().set_source_provider([&](const std::string& name) -> std::string {
        if (name == "counter") { ++load_count; return "var N = 1"; }
        return "";
    });
    vm.import_module("counter");
    vm.import_module("counter");
    CHECK(load_count == 1);
}

TEST_CASE("module function is callable after import", "[module]") {
    VM vm;
    vm.open_stdlib();
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "utils") return "fn double(n: Int) -> Int { return n * 2 }";
        return "";
    });
    REQUIRE(vm.import_module("utils"));
    Value result = vm.call_global("double", {Value::from_int(21)});
    CHECK(result.is_int());
    CHECK(result.as_int() == 42);
}

TEST_CASE("native module with value exports", "[module]") {
    VM vm;
    vm.open_stdlib();
    std::unordered_map<std::string, Value> exports;
    exports["PI"]  = Value::from_float(3.14159265358979);
    exports["TAU"] = Value::from_float(6.28318530717959);
    vm.loader().register_native("mathconst", std::move(exports));
    REQUIRE(vm.import_module("mathconst"));
    Value pi = vm.get_global("PI");
    CHECK(pi.is_float());
    CHECK(std::abs(pi.to_float() - 3.14159265358979) < 1e-10);
}

TEST_CASE("native module with function export", "[module]") {
    VM vm;
    vm.open_stdlib();
    std::unordered_map<std::string, Value> exports;
    exports["square"] = Value::from_native("square", [](std::vector<Value> args) -> std::vector<Value> {
        int64_t n = args.empty() ? 0 : args[0].as_int();
        return {Value::from_int(n * n)};
    });
    vm.loader().register_native("mymath", std::move(exports));
    REQUIRE(vm.import_module("mymath"));
    Value sq = vm.call_global("square", {Value::from_int(7)});
    CHECK(sq.is_int());
    CHECK(sq.as_int() == 49);
}

TEST_CASE("second module load returns cached module", "[module]") {
    VM vm;
    vm.open_stdlib();
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "circ") return "var X = 1";
        return "";
    });
    std::string err;
    Module* mod  = vm.loader().load("circ", vm, err);
    REQUIRE(mod != nullptr);
    CHECK(err.empty());
    Module* mod2 = vm.loader().load("circ", vm, err);
    CHECK(mod2 == mod);  // same cached pointer
}

TEST_CASE("empty module loads without error", "[module]") {
    VM vm;
    vm.open_stdlib();
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "empty") return "";
        return "";
    });
    CHECK(vm.import_module("empty"));
}

// ===========================================================================
// C++ Binding API
// ===========================================================================

struct Counter {
    int value = 0;
    explicit Counter(int start = 0) : value(start) {}
    void increment()         { ++value; }
    void decrement()         { --value; }
    void add(int n)          { value += n; }
    void reset()             { value = 0; }
    int  get() const         { return value; }
    bool is_positive() const { return value > 0; }
    std::string to_str() const {
        return "Counter(" + std::to_string(value) + ")";
    }
};

TEST_CASE("binding: constructor creates native in globals", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("get", &Counter::get)
        .commit();
    Value proto = vm.get_global("Counter");
    REQUIRE(proto.is_table());
    Value ctor = proto.as_table()->get("new");
    CHECK(ctor.is_native());
}

TEST_CASE("binding: default constructor gives zero value", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<>()
        .method("get", &Counter::get)
        .commit();
    Value proto = vm.get_global("Counter");
    Value ctor  = proto.as_table()->get("new");
    REQUIRE(ctor.is_native());
    auto instances = ctor.as_native()->fn({});
    REQUIRE(!instances.empty());
    Value inst   = instances[0];
    Value get_fn = inst.as_table()->get("get");
    auto res     = get_fn.as_native()->fn({inst});
    CHECK(res[0].as_int() == 0);
}

TEST_CASE("binding: method call increment", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("increment", &Counter::increment)
        .method("get",       &Counter::get)
        .commit();
    Value proto = vm.get_global("Counter");
    Value ctor  = proto.as_table()->get("new");
    REQUIRE(ctor.is_native());
    auto instances = ctor.as_native()->fn({Value::from_int(5)});
    REQUIRE(!instances.empty());
    Value inst   = instances[0];
    REQUIRE(inst.is_table());
    Value get_fn = inst.as_table()->get("get");
    REQUIRE(get_fn.is_native());
    auto results = get_fn.as_native()->fn({inst});
    REQUIRE(!results.empty());
    CHECK(results[0].as_int() == 5);

    Value inc_fn = inst.as_table()->get("increment");
    REQUIRE(inc_fn.is_native());
    inc_fn.as_native()->fn({inst});
    results = get_fn.as_native()->fn({inst});
    CHECK(results[0].as_int() == 6);
}

TEST_CASE("binding: decrement method", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("decrement", &Counter::decrement)
        .method("get",       &Counter::get)
        .commit();
    Value proto  = vm.get_global("Counter");
    Value ctor   = proto.as_table()->get("new");
    Value inst   = ctor.as_native()->fn({Value::from_int(10)})[0];
    Value dec_fn = inst.as_table()->get("decrement");
    REQUIRE(dec_fn.is_native());
    dec_fn.as_native()->fn({inst});
    dec_fn.as_native()->fn({inst});
    dec_fn.as_native()->fn({inst});
    Value get_fn = inst.as_table()->get("get");
    auto res     = get_fn.as_native()->fn({inst});
    CHECK(res[0].as_int() == 7);
}

TEST_CASE("binding: add method", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("add", &Counter::add)
        .method("get", &Counter::get)
        .commit();
    Value proto  = vm.get_global("Counter");
    Value ctor   = proto.as_table()->get("new");
    Value inst   = ctor.as_native()->fn({Value::from_int(10)})[0];
    Value add_fn = inst.as_table()->get("add");
    REQUIRE(add_fn.is_native());
    add_fn.as_native()->fn({inst, Value::from_int(7)});
    Value get_fn = inst.as_table()->get("get");
    auto results = get_fn.as_native()->fn({inst});
    CHECK(results[0].as_int() == 17);
}

TEST_CASE("binding: reset method", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("add",   &Counter::add)
        .method("reset", &Counter::reset)
        .method("get",   &Counter::get)
        .commit();
    Value proto    = vm.get_global("Counter");
    Value ctor     = proto.as_table()->get("new");
    Value inst     = ctor.as_native()->fn({Value::from_int(5)})[0];
    Value add_fn   = inst.as_table()->get("add");
    Value reset_fn = inst.as_table()->get("reset");
    Value get_fn   = inst.as_table()->get("get");

    add_fn.as_native()->fn({inst, Value::from_int(95)});
    auto r1 = get_fn.as_native()->fn({inst});
    CHECK(r1[0].as_int() == 100);

    reset_fn.as_native()->fn({inst});
    auto r2 = get_fn.as_native()->fn({inst});
    CHECK(r2[0].as_int() == 0);
}

TEST_CASE("binding: bool return from method", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("is_positive", &Counter::is_positive)
        .commit();
    Value proto    = vm.get_global("Counter");
    Value ctor     = proto.as_table()->get("new");
    Value pos_inst = ctor.as_native()->fn({Value::from_int(5)})[0];
    Value neg_inst = ctor.as_native()->fn({Value::from_int(-1)})[0];

    Value is_pos = pos_inst.as_table()->get("is_positive");
    REQUIRE(is_pos.is_native());
    auto r1 = is_pos.as_native()->fn({pos_inst});
    CHECK(r1[0].as_bool() == true);

    Value is_pos2 = neg_inst.as_table()->get("is_positive");
    auto r2 = is_pos2.as_native()->fn({neg_inst});
    CHECK(r2[0].as_bool() == false);
}

TEST_CASE("binding: property getter and setter", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .property("value", &Counter::value)
        .commit();
    Value proto  = vm.get_global("Counter");
    Value ctor   = proto.as_table()->get("new");
    Value inst   = ctor.as_native()->fn({Value::from_int(99)})[0];
    Value getter = inst.as_table()->get("get_value");
    REQUIRE(getter.is_native());
    auto res = getter.as_native()->fn({inst});
    CHECK(res[0].as_int() == 99);

    Value setter = inst.as_table()->get("set_value");
    REQUIRE(setter.is_native());
    setter.as_native()->fn({inst, Value::from_int(42)});
    res = getter.as_native()->fn({inst});
    CHECK(res[0].as_int() == 42);
}

TEST_CASE("binding: property with zero value", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .property("value", &Counter::value)
        .commit();
    Value proto  = vm.get_global("Counter");
    Value ctor   = proto.as_table()->get("new");
    Value inst   = ctor.as_native()->fn({Value::from_int(0)})[0];
    Value getter = inst.as_table()->get("get_value");
    auto res     = getter.as_native()->fn({inst});
    CHECK(res[0].as_int() == 0);
}

TEST_CASE("binding: multiple instances are independent", "[binding]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("add", &Counter::add)
        .method("get", &Counter::get)
        .commit();
    Value proto  = vm.get_global("Counter");
    Value ctor   = proto.as_table()->get("new");
    Value inst1  = ctor.as_native()->fn({Value::from_int(0)})[0];
    Value inst2  = ctor.as_native()->fn({Value::from_int(100)})[0];

    inst1.as_table()->get("add").as_native()->fn({inst1, Value::from_int(5)});
    inst2.as_table()->get("add").as_native()->fn({inst2, Value::from_int(10)});

    auto r1 = inst1.as_table()->get("get").as_native()->fn({inst1});
    auto r2 = inst2.as_table()->get("get").as_native()->fn({inst2});
    CHECK(r1[0].as_int() == 5);
    CHECK(r2[0].as_int() == 110);
}

// ===========================================================================
// Integration: binding called from script
// ===========================================================================

TEST_CASE("binding called from ZScript", "[binding][integration]") {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("add", &Counter::add)
        .method("get", &Counter::get)
        .commit();

    Value proto  = vm.get_global("Counter");
    Value ctor   = proto.as_table()->get("new");
    Value inst   = ctor.as_native()->fn({Value::from_int(0)})[0];
    vm.set_global("c", inst);

    Value add_fn = inst.as_table()->get("add");
    vm.set_global("counter_add", Value::from_native("counter_add",
        [add_fn, inst](std::vector<Value> args) mutable -> std::vector<Value> {
            std::vector<Value> call_args = {inst};
            call_args.insert(call_args.end(), args.begin(), args.end());
            return add_fn.as_native()->fn(call_args);
        }
    ));
    Value get_fn = inst.as_table()->get("get");
    vm.set_global("counter_get", Value::from_native("counter_get",
        [get_fn, inst](std::vector<Value>) mutable -> std::vector<Value> {
            return get_fn.as_native()->fn({inst});
        }
    ));

    Ctx ctx;
    ctx.vm.set_global("counter_add", vm.get_global("counter_add"));
    ctx.vm.set_global("counter_get", vm.get_global("counter_get"));
    REQUIRE(ctx.run("counter_add(5)  counter_add(3)  var result = counter_get()"));
    CHECK(ctx.g("result").as_int() == 8);
}

TEST_CASE("native function registration and script call", "[binding][integration]") {
    VM vm;
    vm.open_stdlib();
    vm.register_function("square", [](std::vector<Value> args) -> std::vector<Value> {
        int64_t n = args.empty() ? 0 : args[0].as_int();
        return {Value::from_int(n * n)};
    });

    Ctx ctx;
    ctx.vm.set_global("square", vm.get_global("square"));
    REQUIRE(ctx.run("var r = square(9)"));
    CHECK(ctx.g("r").as_int() == 81);
}

TEST_CASE("native function returning string", "[binding][integration]") {
    VM vm;
    vm.open_stdlib();
    vm.register_function("make_greeting", [](std::vector<Value> args) -> std::vector<Value> {
        std::string name = args.empty() ? "world" : args[0].as_string();
        return {Value::from_string("Hello, " + name + "!")};
    });

    Ctx ctx;
    ctx.vm.set_global("make_greeting", vm.get_global("make_greeting"));
    REQUIRE(ctx.run("var r = make_greeting(\"ZScript\")"));
    CHECK(ctx.g("r").as_string() == "Hello, ZScript!");
}

// ---------------------------------------------------------------------------
// Object handle system
// ---------------------------------------------------------------------------

TEST_CASE("push_object_handle creates proxy with __handle", "[handle]") {
    VM vm;
    vm.open_stdlib();
    Value proxy = vm.push_object_handle(42);
    CHECK(proxy.is_table());
    CHECK(vm.get_object_handle(proxy) == 42);
}

TEST_CASE("get_object_handle returns -1 for non-handle values", "[handle]") {
    VM vm;
    vm.open_stdlib();
    CHECK(vm.get_object_handle(Value::nil())       == -1);
    CHECK(vm.get_object_handle(Value::from_int(5)) == -1);
    CHECK(vm.get_object_handle(Value::from_table()) == -1); // plain table, no __handle
}

TEST_CASE("push_object_handle gc_hook fires on collection", "[handle]") {
    bool released = false;
    int64_t released_id = -1;
    {
        VM vm;
        vm.open_stdlib();
        vm.set_object_handle_release([&](int64_t id) {
            released    = true;
            released_id = id;
        });
        // Create proxy but don't keep any ZScript reference to it
        vm.push_object_handle(99);
        // proxy Value is a temporary — shared_ptr refcount drops to 0 here
    }
    // ZTable destructor fires gc_hook
    CHECK(released    == true);
    CHECK(released_id == 99);
}

TEST_CASE("proxy table accessible from ZScript via __handle field", "[handle]") {
    VM vm;
    vm.open_stdlib();
    Value proxy = vm.push_object_handle(7);
    vm.set_global("obj", proxy);

    // ZScript can read __handle directly
    Lexer lexer("var h = obj.__handle");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    Compiler compiler;
    auto chunk = compiler.compile(prog, "<test>");
    REQUIRE(vm.execute(*chunk));
    CHECK(vm.get_global("h").as_int() == 7);
}

TEST_CASE("proxy table supports metamethods set after handle creation", "[handle]") {
    VM vm;
    vm.open_stdlib();
    Value proxy = vm.push_object_handle(3);

    // Set a __index metamethod after handle creation (simulates type binding)
    proxy.as_table()->set("__index", Value::from_native("idx", [](std::vector<Value> args) -> std::vector<Value> {
        // Return a fixed string for any key access
        return {Value::from_string("bound_value")};
    }));
    vm.set_global("obj", proxy);

    Lexer lexer("var v = obj.anything");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    Compiler compiler;
    auto chunk = compiler.compile(prog, "<test>");
    REQUIRE(vm.execute(*chunk));
    CHECK(vm.get_global("v").as_string() == "bound_value");
}
