#pragma once
#include "value.h"
#include "vm.h"
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace zscript {

// ===========================================================================
// ZScript ↔ C++ type bridge helpers
// ===========================================================================

// Userdata wrapper: wraps a C++ object as a ZScript table with a hidden pointer.
// The object is stored as a shared_ptr in a ZTable under the key "__ptr__"
// (as a native function that returns the pointer on call — a simple trick).
// For real GC integration (Phase 5), this would use a proper userdata tag.
// ---------------------------------------------------------------------------

template<typename T>
struct Userdata : GcObject {
    std::shared_ptr<T> ptr;
    explicit Userdata(std::shared_ptr<T> p) : ptr(std::move(p)) {}
};

// Store a C++ object in a ZScript Value (as a table with __ptr__ entry).
template<typename T>
Value make_userdata(std::shared_ptr<T> obj) {
    Value tbl = Value::from_table();
    // Store a sentinel native function whose closure holds the shared_ptr.
    // Scripts call it to get the object (not normally needed — binding methods
    // extract it in C++ before calling the script).
    auto raw = obj.get();
    tbl.as_table()->set("__type__", Value::from_string(typeid(T).name()));
    // We store the shared_ptr in a lambda capture in a native function.
    tbl.as_table()->set("__ptr__", Value::from_native("__ptr__",
        [obj](std::vector<Value>) -> std::vector<Value> {
            (void)obj; // keep alive
            return {};
        }));
    return tbl;
}

// Extract the C++ object from a userdata table.
// Returns nullptr if the value is not a userdata for T.
template<typename T>
std::shared_ptr<T> extract_userdata(const Value& v) {
    if (!v.is_table()) return nullptr;
    Value type_val = v.as_table()->get("__type__");
    if (!type_val.is_string()) return nullptr;
    if (type_val.as_string() != typeid(T).name()) return nullptr;
    Value ptr_fn = v.as_table()->get("__ptr__");
    if (!ptr_fn.is_native()) return nullptr;
    // The shared_ptr lives in the lambda capture — we can't extract it directly
    // without UB. Instead, use a side channel: store raw ptr alongside.
    Value raw_val = v.as_table()->get("__raw__");
    if (raw_val.is_native()) {
        // raw_val stores the pointer encoded as a native function
        // (this is a limitation of the current Value system — Phase 3+ adds proper userdata)
    }
    return nullptr; // placeholder; see ClassBuilder for the real path
}

// ===========================================================================
// Argument extraction helpers (Value → C++ type)
// ===========================================================================

template<typename T> struct ArgOf;

template<> struct ArgOf<int64_t> {
    static int64_t get(const Value& v, int idx) {
        if (!v.is_int()) throw RuntimeError{"arg " + std::to_string(idx) + " expected int", ""};
        return v.as_int();
    }
};
template<> struct ArgOf<int> {
    static int get(const Value& v, int idx) {
        return (int)ArgOf<int64_t>::get(v, idx);
    }
};
template<> struct ArgOf<double> {
    static double get(const Value& v, int idx) {
        if (!v.is_number()) throw RuntimeError{"arg " + std::to_string(idx) + " expected number", ""};
        return v.to_float();
    }
};
template<> struct ArgOf<float> {
    static float get(const Value& v, int idx) { return (float)ArgOf<double>::get(v, idx); }
};
template<> struct ArgOf<bool> {
    static bool get(const Value& v, int /*idx*/) { return v.truthy(); }
};
template<> struct ArgOf<std::string> {
    static std::string get(const Value& v, int idx) {
        if (!v.is_string()) throw RuntimeError{"arg " + std::to_string(idx) + " expected string", ""};
        return v.as_string();
    }
};
template<> struct ArgOf<const std::string&> {
    static std::string get(const Value& v, int idx) {
        return ArgOf<std::string>::get(v, idx);
    }
};
template<> struct ArgOf<Value> {
    static Value get(const Value& v, int /*idx*/) { return v; }
};

// ===========================================================================
// Return value helpers (C++ type → Value)
// ===========================================================================

template<typename T> struct RetOf {
    static Value make(T v);
};
template<> struct RetOf<void>         { static Value make()            { return Value::nil(); } };
template<> struct RetOf<int64_t>      { static Value make(int64_t v)   { return Value::from_int(v); } };
template<> struct RetOf<int>          { static Value make(int v)        { return Value::from_int(v); } };
template<> struct RetOf<double>       { static Value make(double v)     { return Value::from_float(v); } };
template<> struct RetOf<float>        { static Value make(float v)      { return Value::from_float((double)v); } };
template<> struct RetOf<bool>         { static Value make(bool v)       { return Value::from_bool(v); } };
template<> struct RetOf<std::string>  { static Value make(std::string v){ return Value::from_string(std::move(v)); } };
template<> struct RetOf<Value>        { static Value make(Value v)      { return v; } };

// ===========================================================================
// ClassBuilder<T> — fluent builder for binding a C++ class
//
// Usage:
//   vm.register_class<RigidBody>("RigidBody")
//     .constructor<float>()
//     .method("apply_force", &RigidBody::apply_force)
//     .property("mass", &RigidBody::mass);
//
// This produces a ZScript table ("class prototype") stored as a global.
// Instances are created via ClassName.new(args...) which returns a table
// with bound methods.
// ===========================================================================

template<typename T>
class ClassBuilder {
public:
    ClassBuilder(VM& vm, std::string class_name)
        : vm_(vm), class_name_(std::move(class_name)) {
        proto_tbl_ = Value::from_table();
    }

    // ---- constructor ----
    // Bind a constructor taking Args...
    template<typename... Args>
    ClassBuilder& constructor() {
        proto_tbl_.as_table()->set("new", Value::from_native(
            class_name_ + ".new",
            [this](std::vector<Value> args) -> std::vector<Value> {
                return {make_instance<Args...>(args, std::index_sequence_for<Args...>{})};
            }
        ));
        return *this;
    }

    // Default constructor
    ClassBuilder& constructor() {
        proto_tbl_.as_table()->set("new", Value::from_native(
            class_name_ + ".new",
            [this](std::vector<Value>) -> std::vector<Value> {
                auto obj = std::make_shared<T>();
                return {wrap(obj)};
            }
        ));
        return *this;
    }

    // ---- method binding ----

    // Non-const member function returning non-void
    template<typename Ret, typename... Args>
    ClassBuilder& method(const std::string& name, Ret(T::*fn)(Args...)) {
        std::string full = class_name_ + "." + name;
        proto_tbl_.as_table()->set(name, Value::from_native(full,
            [fn, full](std::vector<Value> args) -> std::vector<Value> {
                if (args.empty()) throw RuntimeError{"method '" + full + "' requires self", ""};
                auto obj = get_self(args[0], full);
                if (!obj) throw RuntimeError{"method '" + full + "' called on wrong type", ""};
                std::vector<Value> rest(args.begin() + 1, args.end());
                if constexpr (std::is_void_v<Ret>) {
                    call_method(obj.get(), fn, rest, std::index_sequence_for<Args...>{});
                    return {};
                } else {
                    Ret r = call_method(obj.get(), fn, rest, std::index_sequence_for<Args...>{});
                    return {RetOf<Ret>::make(std::move(r))};
                }
            }
        ));
        return *this;
    }

    // Const member function
    template<typename Ret, typename... Args>
    ClassBuilder& method(const std::string& name, Ret(T::*fn)(Args...) const) {
        return method(name, reinterpret_cast<Ret(T::*)(Args...)>(fn));
    }

    // Free function taking shared_ptr<T> as first arg
    template<typename Ret, typename... Args>
    ClassBuilder& method(const std::string& name,
                         std::function<Ret(std::shared_ptr<T>, Args...)> fn) {
        std::string full = class_name_ + "." + name;
        proto_tbl_.as_table()->set(name, Value::from_native(full,
            [fn, full](std::vector<Value> args) -> std::vector<Value> {
                if (args.empty()) throw RuntimeError{"method '" + full + "' requires self", ""};
                auto obj = get_self(args[0], full);
                std::vector<Value> rest(args.begin() + 1, args.end());
                if constexpr (std::is_void_v<Ret>) {
                    call_free(obj, fn, rest, std::index_sequence_for<Args...>{});
                    return {};
                } else {
                    Ret r = call_free(obj, fn, rest, std::index_sequence_for<Args...>{});
                    return {RetOf<Ret>::make(std::move(r))};
                }
            }
        ));
        return *this;
    }

    // ---- property binding (member pointer) ----
    template<typename Prop>
    ClassBuilder& property(const std::string& name, Prop T::*member) {
        // getter
        std::string get_name = "get_" + name;
        proto_tbl_.as_table()->set(get_name, Value::from_native(
            class_name_ + "." + get_name,
            [member, name](std::vector<Value> args) -> std::vector<Value> {
                auto obj = get_self(args.empty() ? Value::nil() : args[0], name);
                if (!obj) return {Value::nil()};
                return {RetOf<Prop>::make(obj.get()->*member)};
            }
        ));
        // setter
        std::string set_name = "set_" + name;
        proto_tbl_.as_table()->set(set_name, Value::from_native(
            class_name_ + "." + set_name,
            [member, name](std::vector<Value> args) -> std::vector<Value> {
                if (args.size() < 2) throw RuntimeError{"set_" + name + " requires self and value", ""};
                auto obj = get_self(args[0], name);
                if (!obj) throw RuntimeError{"set_" + name + " called on wrong type", ""};
                obj.get()->*member = ArgOf<Prop>::get(args[1], 1);
                return {};
            }
        ));
        return *this;
    }

    // ---- finalize: register the prototype table as a global ----
    void commit() {
        vm_.set_global(class_name_, proto_tbl_);
    }

    // Allow chaining: automatically commits when the builder is destroyed
    ~ClassBuilder() {
        if (!committed_) commit();
        committed_ = true;
    }

private:
    VM&         vm_;
    std::string class_name_;
    Value       proto_tbl_;
    bool        committed_ = false;

    // Store the shared_ptr in a side table keyed by raw ptr address
    // so get_self can retrieve it.
    static std::unordered_map<void*, std::shared_ptr<T>>& registry() {
        static std::unordered_map<void*, std::shared_ptr<T>> r;
        return r;
    }

    Value wrap(std::shared_ptr<T> obj) {
        Value inst = Value::from_table();
        // Register so get_self can find it
        registry()[obj.get()] = obj;

        // Copy methods from prototype into instance
        auto* proto = proto_tbl_.as_table();
        auto* inst_tbl = inst.as_table();
        for (auto& [k, v] : proto->hash) {
            if (k != "new") inst_tbl->set(k, v);
        }

        // Store raw pointer as an int (safe: we keep the shared_ptr alive in registry)
        uintptr_t raw = (uintptr_t)obj.get();
        inst_tbl->set("__raw__", Value::from_int((int64_t)raw));
        inst_tbl->set("__type__", Value::from_string(class_name_));
        return inst;
    }

    static std::shared_ptr<T> get_self(const Value& v, const std::string& ctx) {
        if (!v.is_table()) throw RuntimeError{"method '" + ctx + "' requires a table self", ""};
        Value raw_v = v.as_table()->get("__raw__");
        if (!raw_v.is_int()) throw RuntimeError{"method '" + ctx + "' self missing __raw__", ""};
        void* raw = (void*)(uintptr_t)raw_v.as_int();
        auto& reg = registry();
        auto it = reg.find(raw);
        if (it == reg.end()) throw RuntimeError{"method '" + ctx + "' self is dead object", ""};
        return it->second;
    }

    template<typename... Args, size_t... I>
    Value make_instance(const std::vector<Value>& args, std::index_sequence<I...>) {
        auto obj = std::make_shared<T>(
            ArgOf<Args>::get(I < args.size() ? args[I] : Value::nil(), (int)I)...
        );
        return wrap(obj);
    }

    template<typename Ret, typename... Args, size_t... I>
    static Ret call_method(T* obj, Ret(T::*fn)(Args...),
                           const std::vector<Value>& args, std::index_sequence<I...>) {
        return (obj->*fn)(
            ArgOf<Args>::get(I < args.size() ? args[I] : Value::nil(), (int)I)...
        );
    }

    template<typename Ret, typename... Args, size_t... I>
    static Ret call_free(std::shared_ptr<T> obj,
                         std::function<Ret(std::shared_ptr<T>, Args...)> fn,
                         const std::vector<Value>& args, std::index_sequence<I...>) {
        return fn(obj, ArgOf<Args>::get(I < args.size() ? args[I] : Value::nil(), (int)I)...);
    }
};

// ===========================================================================
// VM extension: register_class<T>
// (free function that takes a VM reference)
// ===========================================================================
template<typename T>
ClassBuilder<T> register_class(VM& vm, const std::string& name) {
    return ClassBuilder<T>(vm, name);
}

// ===========================================================================
// MACRO-BASED self-registering pattern
//
//   ZSCRIPT_CLASS(MyClass)
//     ZSCRIPT_CTOR()
//     ZSCRIPT_METHOD(do_thing)
//     ZSCRIPT_PROPERTY(health)
//   ZSCRIPT_END()
//
//   Then: ZScript::register_all(vm)
// ===========================================================================

using RegisterFn = std::function<void(VM&)>;

// Global registry of register functions (populated via static initializers)
inline std::vector<RegisterFn>& global_registry() {
    static std::vector<RegisterFn> r;
    return r;
}

struct AutoRegister {
    explicit AutoRegister(RegisterFn fn) { global_registry().push_back(std::move(fn)); }
};

// Register all auto-registered classes/functions into a VM
inline void register_all(VM& vm) {
    for (auto& fn : global_registry()) fn(vm);
}

} // namespace zscript

// ---------------------------------------------------------------------------
// Convenience macros
// ---------------------------------------------------------------------------
#define ZSCRIPT_CLASS(Type) \
    namespace { static zscript::AutoRegister _zs_reg_##Type([]( zscript::VM& vm) { \
        zscript::register_class<Type>(vm, #Type)

#define ZSCRIPT_CTOR(...) \
        .constructor<__VA_ARGS__>()

#define ZSCRIPT_METHOD(name) \
        .method(#name, &Type::name)

#define ZSCRIPT_PROPERTY(name) \
        .property(#name, &Type::name)

#define ZSCRIPT_END() \
        .commit(); }); }
