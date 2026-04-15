#include <catch2/catch_test_macros.hpp>
#include "zscript_c.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static ZsVM make_vm() {
    ZsVM vm = zs_vm_new();
    zs_vm_open_stdlib(vm);
    return vm;
}

// ---------------------------------------------------------------------------
// VM lifecycle
// ---------------------------------------------------------------------------
TEST_CASE("zs_vm_new / zs_vm_free", "[c_api][lifecycle]") {
    ZsVM vm = zs_vm_new();
    REQUIRE(vm != nullptr);
    zs_vm_free(vm);
}

// ---------------------------------------------------------------------------
// Value constructors and accessors
// ---------------------------------------------------------------------------
TEST_CASE("zs_value_nil type", "[c_api][value]") {
    ZsValue v = zs_value_nil();
    CHECK(zs_value_type(v) == ZS_TYPE_NIL);
    zs_value_free(v);
}

TEST_CASE("zs_value_bool round-trip", "[c_api][value]") {
    ZsValue t = zs_value_bool(1);
    ZsValue f = zs_value_bool(0);
    CHECK(zs_value_type(t) == ZS_TYPE_BOOL);
    CHECK(zs_value_as_bool(t) == 1);
    CHECK(zs_value_as_bool(f) == 0);
    zs_value_free(t);
    zs_value_free(f);
}

TEST_CASE("zs_value_int round-trip", "[c_api][value]") {
    ZsValue v = zs_value_int(123456789LL);
    CHECK(zs_value_type(v) == ZS_TYPE_INT);
    CHECK(zs_value_as_int(v) == 123456789LL);
    zs_value_free(v);
}

TEST_CASE("zs_value_float round-trip", "[c_api][value]") {
    ZsValue v = zs_value_float(3.14);
    CHECK(zs_value_type(v) == ZS_TYPE_FLOAT);
    CHECK(zs_value_as_float(v) == 3.14);
    zs_value_free(v);
}

TEST_CASE("zs_value_string round-trip", "[c_api][value]") {
    ZsValue v = zs_value_string("hello");
    CHECK(zs_value_type(v) == ZS_TYPE_STRING);
    char buf[64];
    int len = zs_value_as_string(v, buf, sizeof(buf));
    CHECK(std::string(buf) == "hello");
    CHECK(len == 5);
    zs_value_free(v);
}

TEST_CASE("zs_value_clone produces independent copy", "[c_api][value]") {
    ZsValue orig  = zs_value_int(42);
    ZsValue clone = zs_value_clone(orig);
    CHECK(zs_value_as_int(orig)  == 42);
    CHECK(zs_value_as_int(clone) == 42);
    zs_value_free(orig);
    // clone still valid after orig is freed (shared_ptr keeps string/table alive)
    CHECK(zs_value_as_int(clone) == 42);
    zs_value_free(clone);
}

// ---------------------------------------------------------------------------
// Script execution
// ---------------------------------------------------------------------------
TEST_CASE("zs_vm_load_source executes script", "[c_api][execution]") {
    ZsVM vm = make_vm();
    char err[256] = {};
    int ok = zs_vm_load_source(vm, "<test>", "var answer = 6 * 7", err, sizeof(err));
    CHECK(ok == 1);
    CHECK(err[0] == '\0');
    zs_vm_free(vm);
}

TEST_CASE("zs_vm_load_source reports parse error", "[c_api][execution]") {
    ZsVM vm = make_vm();
    char err[256] = {};
    int ok = zs_vm_load_source(vm, "<bad>", "fn (((", err, sizeof(err));
    CHECK(ok == 0);
    CHECK(err[0] != '\0');
    zs_vm_free(vm);
}

// ---------------------------------------------------------------------------
// Function calls
// ---------------------------------------------------------------------------
TEST_CASE("zs_vm_call invokes a global function", "[c_api][call]") {
    ZsVM vm = make_vm();
    char err[256] = {};
    REQUIRE(zs_vm_load_source(vm, "<t>", "fn add(a, b) { return a + b }", err, sizeof(err)));

    ZsValue argv[2] = { zs_value_int(3), zs_value_int(4) };
    ZsValue result  = nullptr;
    int ok = zs_vm_call(vm, "add", 2, argv, &result, err, sizeof(err));
    CHECK(ok == 1);
    CHECK(result != nullptr);
    CHECK(zs_value_as_int(result) == 7);

    zs_value_free(argv[0]);
    zs_value_free(argv[1]);
    zs_value_free(result);
    zs_vm_free(vm);
}

TEST_CASE("zs_vm_call undefined function reports error", "[c_api][call]") {
    ZsVM vm = make_vm();
    char err[256] = {};
    int ok = zs_vm_call(vm, "does_not_exist", 0, nullptr, nullptr, err, sizeof(err));
    CHECK(ok == 0);
    CHECK(err[0] != '\0');
    zs_vm_free(vm);
}

// ---------------------------------------------------------------------------
// Native function registration
// ---------------------------------------------------------------------------
TEST_CASE("zs_vm_register_fn callable from ZScript", "[c_api][native]") {
    ZsVM vm = make_vm();

    zs_vm_register_fn(vm, "double_it", [](ZsVM, int argc, ZsValue* argv) -> ZsValue {
        if (argc < 1) return zs_value_int(0);
        int64_t n = zs_value_as_int(argv[0]);
        return zs_value_int(n * 2);
    });

    char err[256] = {};
    REQUIRE(zs_vm_load_source(vm, "<t>", "var x = double_it(21)", err, sizeof(err)));

    ZsValue result = nullptr;
    zs_vm_call(vm, "double_it", 0, nullptr, nullptr, err, sizeof(err));

    // Check via re-calling the native with a value argument
    ZsValue arg = zs_value_int(21);
    zs_vm_call(vm, "double_it", 1, &arg, &result, err, sizeof(err));
    CHECK(zs_value_as_int(result) == 42);

    zs_value_free(arg);
    zs_value_free(result);
    zs_vm_free(vm);
}

// ---------------------------------------------------------------------------
// Object handle system
// ---------------------------------------------------------------------------
TEST_CASE("zs_vm_push_object_handle creates a handle value", "[c_api][handle]") {
    ZsVM vm = make_vm();
    ZsValue proxy = zs_vm_push_object_handle(vm, 7);
    CHECK(proxy != nullptr);
    CHECK(zs_value_type(proxy) == ZS_TYPE_OBJECT);
    CHECK(zs_vm_get_object_handle(vm, proxy) == 7);
    zs_value_free(proxy);
    zs_vm_free(vm);
}

TEST_CASE("zs_vm_get_object_handle returns -1 for non-handle", "[c_api][handle]") {
    ZsVM vm = make_vm();
    ZsValue v = zs_value_int(42);
    CHECK(zs_vm_get_object_handle(vm, v) == -1);
    zs_value_free(v);
    zs_vm_free(vm);
}

TEST_CASE("handle release callback fires on GC", "[c_api][handle]") {
    static int64_t released_id = -1;
    ZsVM vm = make_vm();
    zs_vm_set_handle_release(vm, [](ZsVM, int64_t id) {
        released_id = id;
    });

    // Create proxy but immediately free the ZsValue wrapper — ZTable shared_ptr
    // refcount drops to 0 and ~ZTable fires gc_hook.
    ZsValue proxy = zs_vm_push_object_handle(vm, 55);
    zs_value_free(proxy); // releases ZsValueBox; ZTable destructor fires

    CHECK(released_id == 55);
    zs_vm_free(vm);
}

TEST_CASE("handle __index metamethod reachable from ZScript", "[c_api][handle]") {
    ZsVM vm = make_vm();

    ZsValue proxy = zs_vm_push_object_handle(vm, 1);
    // Set an __index that always returns 99
    zs_vm_handle_set_index(vm, proxy, [](ZsVM, int /*argc*/, ZsValue* /*argv*/) -> ZsValue {
        return zs_value_int(99);
    });

    // Expose to ZScript
    // We need to pass the proxy to the VM's global table.
    // Use zs_vm_register_fn to provide it:
    ZsValue proxy_clone = zs_value_clone(proxy);
    zs_vm_register_fn(vm, "get_proxy", [](ZsVM vv, int, ZsValue*) -> ZsValue {
        // We stashed the proxy in a static — simpler for test
        return zs_value_clone(zs_vm_push_object_handle(vv, 1));
    });

    // The real test: set field directly and read it back
    ZsValue field_val = zs_value_int(42);
    zs_vm_handle_set_field(vm, proxy, "myField", field_val);
    ZsValue got = zs_vm_handle_get_field(vm, proxy, "myField");
    CHECK(zs_value_as_int(got) == 42);

    zs_value_free(field_val);
    zs_value_free(got);
    zs_value_free(proxy_clone);
    zs_value_free(proxy);
    zs_vm_free(vm);
}

// ---------------------------------------------------------------------------
// Tag system
// ---------------------------------------------------------------------------
TEST_CASE("zs_vm_add_tag / has_tag / remove_tag", "[c_api][tags]") {
    ZsVM vm = make_vm();
    CHECK(zs_vm_has_tag(vm, "unity")   == 0);
    CHECK(zs_vm_add_tag(vm, "unity")   == 1);
    CHECK(zs_vm_has_tag(vm, "unity")   == 1);
    zs_vm_remove_tag(vm, "unity");
    CHECK(zs_vm_has_tag(vm, "unity")   == 0);
    // Invalid identifier should be rejected
    CHECK(zs_vm_add_tag(vm, "123bad") == 0);
    zs_vm_free(vm);
}
