#include "zscript_c.h"
#include "vm.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "chunk.h"
#include <cstring>
#include <string>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// Heap-allocated Value wrapper (ZsValue = ZsValueBox*)
// ---------------------------------------------------------------------------
// We wrap zscript::Value in a heap object so C# SafeHandle can manage
// lifetime with a finalizer. Each zs_value_* constructor allocates one;
// zs_value_free deletes it.
// ---------------------------------------------------------------------------
namespace {

using namespace zscript;

struct ZsValueBox {
    Value v;
    explicit ZsValueBox(Value val) : v(std::move(val)) {}
};

// Casting helpers — keep the casts in one place.
inline VM*         as_vm(ZsVM p)    { return static_cast<VM*>(p); }
inline ZsValueBox* as_box(ZsValue p){ return static_cast<ZsValueBox*>(p); }

// Write an error string safely into a caller-supplied buffer.
static void write_err(const std::string& msg, char* buf, int len) {
    if (!buf || len <= 0) return;
    std::strncpy(buf, msg.c_str(), (size_t)(len - 1));
    buf[len - 1] = '\0';
}

// Per-VM state for the C-function callback shim.
// Each registered ZsNativeFn is wrapped in a lambda that reconstructs
// the ZsValue* argv on the fly and calls the C function pointer.
struct CNativeFnCtx {
    ZsNativeFn fn;
    ZsVM       vm_handle; // stable — same pointer for the VM's lifetime
};

} // namespace

// ===========================================================================
// VM lifecycle
// ===========================================================================
ZsVM zs_vm_new(void) {
    return new zscript::VM();
}

void zs_vm_free(ZsVM vm) {
    delete as_vm(vm);
}

void zs_vm_open_stdlib(ZsVM vm) {
    as_vm(vm)->open_stdlib();
}

// ===========================================================================
// Tag system
// ===========================================================================
int zs_vm_add_tag(ZsVM vm, const char* tag) {
    return as_vm(vm)->add_tag(tag) ? 1 : 0;
}

void zs_vm_remove_tag(ZsVM vm, const char* tag) {
    as_vm(vm)->remove_tag(tag);
}

int zs_vm_has_tag(ZsVM vm, const char* tag) {
    return as_vm(vm)->has_tag(tag) ? 1 : 0;
}

// ===========================================================================
// Script execution
// ===========================================================================
int zs_vm_load_file(ZsVM vm, const char* path, char* err_buf, int err_len) {
    bool ok = as_vm(vm)->load_file(path);
    if (!ok) write_err(as_vm(vm)->last_error().message, err_buf, err_len);
    return ok ? 1 : 0;
}

int zs_vm_load_source(ZsVM vm, const char* name, const char* source,
                      char* err_buf, int err_len) {
    using namespace zscript;
    VM* v = as_vm(vm);
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        write_err("lexer errors in '" + std::string(name) + "'", err_buf, err_len);
        return 0;
    }
    Parser parser(std::move(tokens));
    Program prog = parser.parse();
    if (parser.has_errors()) {
        write_err("parse errors in '" + std::string(name) + "'", err_buf, err_len);
        return 0;
    }
    Compiler compiler(v->active_tags());
    auto chunk = compiler.compile(prog, name);
    if (compiler.has_errors()) {
        write_err("compile errors in '" + std::string(name) + "'", err_buf, err_len);
        return 0;
    }
    bool ok = v->execute(*chunk);
    // Transfer ownership so ZClosure::proto pointers stay valid.
    v->take_chunk(std::move(chunk));
    if (!ok) write_err(v->last_error().message, err_buf, err_len);
    return ok ? 1 : 0;
}

// ===========================================================================
// Function calls
// ===========================================================================
int zs_vm_call(ZsVM vm_handle, const char* name,
               int argc, ZsValue* argv,
               ZsValue* out_result,
               char* err_buf, int err_len) {
    using namespace zscript;
    VM* vm = as_vm(vm_handle);

    if (vm->get_global(name).is_nil()) {
        write_err(std::string("undefined global '") + name + "'", err_buf, err_len);
        return 0;
    }

    std::vector<Value> args;
    args.reserve((size_t)argc);
    for (int i = 0; i < argc; ++i)
        args.push_back(as_box(argv[i])->v);

    Value result = vm->call_global(name, std::move(args));

    if (!vm->last_error().message.empty()) {
        write_err(vm->last_error().message, err_buf, err_len);
        return 0;
    }
    if (out_result)
        *out_result = new ZsValueBox(std::move(result));
    return 1;
}

// ===========================================================================
// Native function registration
// ===========================================================================
void zs_vm_register_fn(ZsVM vm_handle, const char* name, ZsNativeFn fn) {
    using namespace zscript;
    VM* vm = as_vm(vm_handle);
    // Capture fn and vm_handle by value — both live for the VM's lifetime.
    vm->register_function(name, [fn, vm_handle](std::vector<Value> args) -> std::vector<Value> {
        // Convert args to ZsValue array on the stack.
        std::vector<ZsValueBox> boxes;
        boxes.reserve(args.size());
        for (auto& a : args) boxes.emplace_back(a);

        std::vector<ZsValue> argv;
        argv.reserve(boxes.size());
        for (auto& b : boxes) argv.push_back(&b);

        ZsValue ret = fn(vm_handle, (int)argv.size(), argv.data());
        if (!ret) return {Value::nil()};
        Value v = as_box(ret)->v;
        delete as_box(ret); // fn returned a new box; caller owns it here
        return {std::move(v)};
    });
}

// ===========================================================================
// Object handle system
// ===========================================================================
void zs_vm_set_handle_release(ZsVM vm_handle, ZsHandleReleaseFn fn) {
    as_vm(vm_handle)->set_object_handle_release([fn, vm_handle](int64_t id) {
        fn(vm_handle, id);
    });
}

ZsValue zs_vm_push_object_handle(ZsVM vm_handle, int64_t handle_id) {
    Value proxy = as_vm(vm_handle)->push_object_handle(handle_id);
    return new ZsValueBox(std::move(proxy));
}

int64_t zs_vm_get_object_handle(ZsVM vm_handle, ZsValue val) {
    return as_vm(vm_handle)->get_object_handle(as_box(val)->v);
}

void zs_vm_handle_set_field(ZsVM /*vm*/, ZsValue handle_val,
                             const char* name, ZsValue field_val) {
    zscript::Value& proxy = as_box(handle_val)->v;
    if (!proxy.is_table()) return;
    proxy.as_table()->set(name, as_box(field_val)->v);
}

ZsValue zs_vm_handle_get_field(ZsVM /*vm*/, ZsValue handle_val, const char* name) {
    zscript::Value& proxy = as_box(handle_val)->v;
    if (!proxy.is_table()) return new ZsValueBox(zscript::Value::nil());
    return new ZsValueBox(proxy.as_table()->get(name));
}

// Helper: wrap a ZsNativeFn into a zscript::Value::from_native
static zscript::Value wrap_native(const char* debug_name, ZsNativeFn fn, ZsVM vm_handle) {
    return zscript::Value::from_native(debug_name,
        [fn, vm_handle](std::vector<zscript::Value> args) -> std::vector<zscript::Value> {
            std::vector<ZsValueBox> boxes;
            boxes.reserve(args.size());
            for (auto& a : args) boxes.emplace_back(a);

            std::vector<ZsValue> argv;
            argv.reserve(boxes.size());
            for (auto& b : boxes) argv.push_back(&b);

            ZsValue ret = fn(vm_handle, (int)argv.size(), argv.data());
            if (!ret) return {zscript::Value::nil()};
            zscript::Value v = as_box(ret)->v;
            delete as_box(ret);
            return {std::move(v)};
        });
}

void zs_vm_handle_set_index(ZsVM vm_handle, ZsValue handle_val, ZsNativeFn fn) {
    zscript::Value& proxy = as_box(handle_val)->v;
    if (!proxy.is_table()) return;
    proxy.as_table()->set("__index", wrap_native("__index", fn, vm_handle));
}

void zs_vm_handle_set_newindex(ZsVM vm_handle, ZsValue handle_val, ZsNativeFn fn) {
    zscript::Value& proxy = as_box(handle_val)->v;
    if (!proxy.is_table()) return;
    proxy.as_table()->set("__newindex", wrap_native("__newindex", fn, vm_handle));
}

void zs_vm_handle_set_call(ZsVM vm_handle, ZsValue handle_val, ZsNativeFn fn) {
    zscript::Value& proxy = as_box(handle_val)->v;
    if (!proxy.is_table()) return;
    proxy.as_table()->set("__call", wrap_native("__call", fn, vm_handle));
}

// ===========================================================================
// Hotpatch
// ===========================================================================
int zs_vm_enable_hotpatch(ZsVM vm, const char* dir) {
    return as_vm(vm)->enable_hotpatch(dir) ? 1 : 0;
}

int zs_vm_poll(ZsVM vm) {
    return as_vm(vm)->poll();
}

// ===========================================================================
// Error query
// ===========================================================================
void zs_vm_last_error(ZsVM vm, char* buf, int len) {
    write_err(as_vm(vm)->last_error().message, buf, len);
}

// ===========================================================================
// Value constructors
// ===========================================================================
ZsValue zs_value_nil(void) {
    return new ZsValueBox(zscript::Value::nil());
}

ZsValue zs_value_bool(int b) {
    return new ZsValueBox(zscript::Value::from_bool(b != 0));
}

ZsValue zs_value_int(int64_t n) {
    return new ZsValueBox(zscript::Value::from_int(n));
}

ZsValue zs_value_float(double f) {
    return new ZsValueBox(zscript::Value::from_float(f));
}

ZsValue zs_value_string(const char* s) {
    return new ZsValueBox(zscript::Value::from_string(s ? s : ""));
}

ZsValue zs_value_object(ZsVM vm_handle, int64_t handle_id) {
    return zs_vm_push_object_handle(vm_handle, handle_id);
}

// ===========================================================================
// Value accessors
// ===========================================================================
ZsType zs_value_type(ZsValue val) {
    using T = zscript::Value::Tag;
    if (!val) return ZS_TYPE_NIL;
    switch (as_box(val)->v.tag) {
        case T::Nil:       return ZS_TYPE_NIL;
        case T::Bool:      return ZS_TYPE_BOOL;
        case T::Int:       return ZS_TYPE_INT;
        case T::Float:     return ZS_TYPE_FLOAT;
        case T::String:    return ZS_TYPE_STRING;
        case T::Table: {
            // Distinguish plain tables from object handles
            auto* tbl = as_box(val)->v.as_table();
            return tbl->get("__handle").is_int() ? ZS_TYPE_OBJECT : ZS_TYPE_TABLE;
        }
        case T::Closure:   return ZS_TYPE_CLOSURE;
        case T::Native:    return ZS_TYPE_NATIVE;
        case T::Delegate:  return ZS_TYPE_DELEGATE;
        case T::Coroutine: return ZS_TYPE_COROUTINE;
    }
    return ZS_TYPE_NIL;
}

int zs_value_as_bool(ZsValue val) {
    if (!val) return 0;
    auto& v = as_box(val)->v;
    return v.is_bool() ? (v.as_bool() ? 1 : 0) : (v.truthy() ? 1 : 0);
}

int64_t zs_value_as_int(ZsValue val) {
    if (!val) return 0;
    auto& v = as_box(val)->v;
    if (v.is_int())   return v.as_int();
    if (v.is_float()) return (int64_t)v.as_float();
    return 0;
}

double zs_value_as_float(ZsValue val) {
    if (!val) return 0.0;
    auto& v = as_box(val)->v;
    if (v.is_float()) return v.as_float();
    if (v.is_int())   return (double)v.as_int();
    return 0.0;
}

int zs_value_as_string(ZsValue val, char* buf, int len) {
    if (!val || !buf || len <= 0) return 0;
    std::string s = as_box(val)->v.to_string();
    int copy_len = (int)s.size() < len - 1 ? (int)s.size() : len - 1;
    std::memcpy(buf, s.c_str(), (size_t)copy_len);
    buf[copy_len] = '\0';
    return (int)s.size();
}

int64_t zs_value_as_object(ZsValue val) {
    if (!val) return -1;
    auto& v = as_box(val)->v;
    if (!v.is_table()) return -1;
    zscript::Value h = v.as_table()->get("__handle");
    return h.is_int() ? h.as_int() : -1;
}

// ===========================================================================
// Value lifetime
// ===========================================================================
void zs_value_free(ZsValue val) {
    delete as_box(val);
}

ZsValue zs_value_clone(ZsValue val) {
    if (!val) return new ZsValueBox(zscript::Value::nil());
    return new ZsValueBox(as_box(val)->v); // Value copy ctor handles shared_ptr refcount
}
