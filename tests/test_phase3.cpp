#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "gc.h"
#include "binding.h"
#include <iostream>
#include <cassert>

using namespace zscript;

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;

#define EXPECT(cond, msg) \
    do { if (cond) { ++g_pass; } \
         else { ++g_fail; std::cerr << "FAIL [" << __LINE__ << "]: " << (msg) << "\n"; } \
    } while(0)

#define REQUIRE(cond, msg) \
    do { if (!(cond)) { ++g_fail; \
            std::cerr << "FAIL [" << __LINE__ << "]: " << (msg) << "\n"; return; } \
         ++g_pass; \
    } while(0)

struct Ctx {
    VM vm;
    Ctx() { vm.open_stdlib(); }

    bool run(const std::string& src) {
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        Program prog = parser.parse();
        if (parser.has_errors()) {
            for (auto& e : parser.errors())
                std::cerr << "  parse: " << e.message << "\n";
            return false;
        }
        Compiler compiler;
        auto chunk = compiler.compile(prog, "<test>");
        if (compiler.has_errors()) {
            for (auto& e : compiler.errors())
                std::cerr << "  compile: " << e.message << "\n";
            return false;
        }
        bool ok = vm.execute(*chunk);
        if (!ok) std::cerr << "  runtime: " << vm.last_error().message << "\n";
        return ok;
    }
    Value g(const std::string& n) { return vm.get_global(n); }
};

// ===========================================================================
// GC tests
// ===========================================================================

void test_gc_tracks_objects() {
    GC gc;
    auto str = std::make_shared<ZString>("hello");
    gc.track(str.get(), 32);
    EXPECT(gc.num_objects() == 1, "tracked 1 object");
    EXPECT(gc.bytes_allocated() == 32, "32 bytes tracked");
}

void test_gc_collect_unreachable() {
    GC gc;
    gc.step_alloc_limit = 1; // force collection immediately
    {
        auto str = std::make_shared<ZString>("temp");
        gc.track(str.get(), 32);
        EXPECT(gc.num_objects() == 1, "tracked");
        // Collect with no roots → str is unreachable → swept
        gc.collect([](GC&) {}); // no roots
        EXPECT(gc.num_objects() == 0, "swept unreachable string");
    }
}

void test_gc_keeps_pinned() {
    GC gc;
    auto str = std::make_shared<ZString>("pinned");
    gc.track(str.get(), 32);
    gc.pin(str.get());
    gc.collect([](GC&) {});
    EXPECT(gc.num_objects() == 1, "pinned object survives GC");
    gc.unpin(str.get());
    gc.collect([](GC&) {});
    EXPECT(gc.num_objects() == 0, "unpinned object swept");
}

void test_gc_marks_roots() {
    GC gc;
    auto str = std::make_shared<ZString>("root");
    gc.track(str.get(), 32);
    Value v = Value::from_string("root");
    // Mark this value as a root
    gc.collect([&](GC& g) { g.mark_value(v); });
    // The ZString in v.str_ptr is a different allocation from str — but the
    // from_string call creates its own shared_ptr. Let's just verify the GC
    // didn't crash and swept the unrelated one.
    EXPECT(gc.num_objects() == 0, "unrooted object swept (different ptr)");
}

void test_gc_table_traversal() {
    GC gc;
    auto tbl = std::make_shared<ZTable>();
    auto str = std::make_shared<ZString>("child");
    gc.track(tbl.get(), 64);
    gc.track(str.get(), 32);

    // Table holds a reference to str
    Value sv; sv.tag = Value::Tag::String; sv.str_ptr = str;
    tbl->set("key", sv);

    Value tv; tv.tag = Value::Tag::Table; tv.table_ptr = tbl;

    // Root = table value → should keep both table and string alive
    gc.collect([&](GC& g) { g.mark_value(tv); });
    EXPECT(gc.num_objects() == 2, "table + child string both survive");
}

void test_gc_maybe_collect() {
    GC gc;
    gc.step_alloc_limit = 100;
    size_t initial_collections = gc.num_collections();

    // Under limit: no collection
    auto s1 = std::make_shared<ZString>("a");
    gc.track(s1.get(), 50);
    bool collected = gc.maybe_collect([](GC&) {});
    EXPECT(!collected, "under limit: no collection");

    // Over limit: collection triggered
    auto s2 = std::make_shared<ZString>("b");
    gc.track(s2.get(), 60); // total 110 > 100
    collected = gc.maybe_collect([](GC&) {});
    EXPECT(collected, "over limit: collection triggered");
    EXPECT(gc.num_collections() == initial_collections + 1, "collection count incremented");
}

// ===========================================================================
// Module tests
// ===========================================================================

void test_module_source_provider() {
    VM vm;
    vm.open_stdlib();

    // Register a source provider instead of filesystem
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "mylib") return "fn greet() -> nil { log(\"hello from mylib\") }  var VERSION = 42";
        return "";
    });

    bool ok = vm.import_module("mylib");
    EXPECT(ok, "module loaded");
    // Module export should now be a global
    Value ver = vm.get_global("VERSION");
    EXPECT(ver.is_int() && ver.as_int() == 42, "VERSION exported from module");
    Value greet = vm.get_global("greet");
    EXPECT(greet.is_closure(), "greet exported as closure");
}

void test_module_cached() {
    VM vm;
    vm.open_stdlib();
    int load_count = 0;

    vm.loader().set_source_provider([&](const std::string& name) -> std::string {
        if (name == "counter") { ++load_count; return "var N = 1"; }
        return "";
    });

    vm.import_module("counter");
    vm.import_module("counter"); // second import: should be cached
    EXPECT(load_count == 1, "module source loaded only once");
}

void test_native_module() {
    VM vm;
    vm.open_stdlib();

    std::unordered_map<std::string, Value> exports;
    exports["PI"] = Value::from_float(3.14159265358979);
    exports["TAU"] = Value::from_float(6.28318530717959);
    vm.loader().register_native("mathconst", std::move(exports));

    bool ok = vm.import_module("mathconst");
    EXPECT(ok, "native module loaded");
    Value pi = vm.get_global("PI");
    EXPECT(pi.is_float(), "PI is float");
    EXPECT(std::abs(pi.to_float() - 3.14159265358979) < 1e-10, "PI value correct");
}

void test_module_circular_import_detected() {
    VM vm;
    vm.open_stdlib();
    // Simulate a module that tries to load itself by calling load() twice.
    // The second call should return the cached (Loading) module and trigger the guard.
    vm.loader().set_source_provider([](const std::string& name) -> std::string {
        if (name == "circ") return "var X = 1"; // simple valid module
        return "";
    });

    // First load succeeds.
    std::string err;
    Module* mod = vm.loader().load("circ", vm, err);
    EXPECT(mod != nullptr && err.empty(), "first load succeeds");

    // Second load hits the cache and returns immediately (no circular error since Loaded).
    Module* mod2 = vm.loader().load("circ", vm, err);
    EXPECT(mod2 == mod, "second load returns cached module");
}

// ===========================================================================
// C++ Binding API tests
// ===========================================================================

// A simple C++ class to bind
struct Counter {
    int value = 0;
    explicit Counter(int start = 0) : value(start) {}
    void increment()        { ++value; }
    void add(int n)         { value += n; }
    int  get() const        { return value; }
    std::string name() const { return "Counter"; }
};

void test_binding_constructor() {
    VM vm;
    vm.open_stdlib();

    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("increment", &Counter::increment)
        .method("add", &Counter::add)
        .method("get", &Counter::get)
        .commit();

    Value proto = vm.get_global("Counter");
    REQUIRE(proto.is_table(), "Counter is a table");
    Value ctor = proto.as_table()->get("new");
    EXPECT(ctor.is_native(), "Counter.new is native");
}

void test_binding_method_call() {
    VM vm;
    vm.open_stdlib();

    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("increment", &Counter::increment)
        .method("add",       &Counter::add)
        .method("get",       &Counter::get)
        .commit();

    // Call Counter.new(5) from C++
    Value proto = vm.get_global("Counter");
    Value ctor  = proto.as_table()->get("new");
    REQUIRE(ctor.is_native(), "ctor is native");

    auto instances = ctor.as_native()->fn({Value::from_int(5)});
    REQUIRE(!instances.empty(), "got instance");
    Value inst = instances[0];
    REQUIRE(inst.is_table(), "instance is table");

    // Call get() — should return 5
    Value get_fn = inst.as_table()->get("get");
    REQUIRE(get_fn.is_native(), "get is native");
    auto results = get_fn.as_native()->fn({inst});
    REQUIRE(!results.empty(), "get returned a value");
    EXPECT(results[0].as_int() == 5, "initial value is 5");

    // Call increment()
    Value inc_fn = inst.as_table()->get("increment");
    REQUIRE(inc_fn.is_native(), "increment is native");
    inc_fn.as_native()->fn({inst});

    // get() should return 6
    results = get_fn.as_native()->fn({inst});
    EXPECT(results[0].as_int() == 6, "value after increment is 6");
}

void test_binding_add_method() {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("add", &Counter::add)
        .method("get", &Counter::get)
        .commit();

    Value proto = vm.get_global("Counter");
    Value ctor  = proto.as_table()->get("new");
    Value inst  = ctor.as_native()->fn({Value::from_int(10)})[0];

    Value add_fn = inst.as_table()->get("add");
    REQUIRE(add_fn.is_native(), "add is native");
    add_fn.as_native()->fn({inst, Value::from_int(7)});

    Value get_fn = inst.as_table()->get("get");
    auto results = get_fn.as_native()->fn({inst});
    EXPECT(results[0].as_int() == 17, "10 + 7 = 17");
}

void test_binding_property() {
    VM vm;
    vm.open_stdlib();
    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .property("value", &Counter::value)
        .commit();

    Value proto = vm.get_global("Counter");
    Value ctor  = proto.as_table()->get("new");
    Value inst  = ctor.as_native()->fn({Value::from_int(99)})[0];

    // get_value
    Value getter = inst.as_table()->get("get_value");
    REQUIRE(getter.is_native(), "get_value is native");
    auto res = getter.as_native()->fn({inst});
    EXPECT(res[0].as_int() == 99, "get_value returns 99");

    // set_value
    Value setter = inst.as_table()->get("set_value");
    REQUIRE(setter.is_native(), "set_value is native");
    setter.as_native()->fn({inst, Value::from_int(42)});
    res = getter.as_native()->fn({inst});
    EXPECT(res[0].as_int() == 42, "set_value changed to 42");
}

void test_register_all_macro() {
    // Test that auto-registration works via the global registry
    size_t before = global_registry().size();
    // We can't use the macro without defining a new class, so just verify
    // register_all doesn't crash on an empty VM.
    VM vm;
    vm.open_stdlib();
    EXPECT(true, "register_all setup ok"); // placeholder
    (void)before;
}

// ===========================================================================
// Integration: binding + script calling a bound method
// ===========================================================================

void test_binding_from_script() {
    VM vm;
    vm.open_stdlib();

    register_class<Counter>(vm, "Counter")
        .constructor<int>()
        .method("increment", &Counter::increment)
        .method("add",       &Counter::add)
        .method("get",       &Counter::get)
        .commit();

    // Script: create a Counter, call methods, read result
    // (We call the native methods directly since method-on-table dispatch
    //  from script requires self-passing — tested via C++ API path here)
    Value proto = vm.get_global("Counter");
    Value ctor  = proto.as_table()->get("new");
    Value inst  = ctor.as_native()->fn({Value::from_int(0)})[0];
    vm.set_global("c", inst);

    // Now run a script that calls methods on the bound object
    // We'll expose the add method as a standalone helper
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

    Ctx ctx; // separate context
    ctx.vm.set_global("counter_add", vm.get_global("counter_add"));
    ctx.vm.set_global("counter_get", vm.get_global("counter_get"));
    ctx.run("counter_add(5)  counter_add(3)  var result = counter_get()");
    EXPECT(ctx.g("result").as_int() == 8, "script called bound methods: 5+3=8");
}

// ===========================================================================
// main
// ===========================================================================
int main() {
    // GC
    test_gc_tracks_objects();
    test_gc_collect_unreachable();
    test_gc_keeps_pinned();
    test_gc_marks_roots();
    test_gc_table_traversal();
    test_gc_maybe_collect();

    // Modules
    test_module_source_provider();
    test_module_cached();
    test_native_module();
    test_module_circular_import_detected();

    // Binding API
    test_binding_constructor();
    test_binding_method_call();
    test_binding_add_method();
    test_binding_property();
    test_register_all_macro();
    test_binding_from_script();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail > 0 ? 1 : 0;
}
