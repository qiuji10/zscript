#pragma once
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace zscript {

struct Proto;
struct VM;

// ---------------------------------------------------------------------------
// GC object header (Phase 3: GC will traverse these)
// For now, objects are reference-counted via shared_ptr.
// ---------------------------------------------------------------------------
struct GcObject {
    virtual ~GcObject() = default;
};

// ---------------------------------------------------------------------------
// String — interned by the VM (Phase 3), plain heap string for now
// ---------------------------------------------------------------------------
struct ZString : GcObject {
    std::string data;
    explicit ZString(std::string s) : data(std::move(s)) {}
};

// ---------------------------------------------------------------------------
// Table — ZScript's universal map/array type
// ---------------------------------------------------------------------------
struct Value; // forward

struct ZTable : GcObject {
    std::vector<Value>                         array;  // integer keys 1..n
    std::unordered_map<std::string, Value>     hash;   // string keys
    // (other key types can be added for Phase 3)

    Value get(const std::string& key) const;
    void  set(const std::string& key, Value val);
    Value get_index(int64_t idx) const;
    void  set_index(int64_t idx, Value val);
};

// ---------------------------------------------------------------------------
// Closure — a Proto + captured upvalues (upvalues are Phase 3; slot here)
// ---------------------------------------------------------------------------
struct ZClosure : GcObject {
    Proto*             proto = nullptr;
    // upvalues: Phase 3
    explicit ZClosure(Proto* p) : proto(p) {}
};

// ---------------------------------------------------------------------------
// NativeFunction — C++ function callable from ZScript
// ---------------------------------------------------------------------------
struct NativeFunction : GcObject {
    std::string name;
    // args: values passed by the call instruction
    // returns: vector of return values
    using Fn = std::function<std::vector<Value>(std::vector<Value>)>;
    Fn fn;
    NativeFunction(std::string n, Fn f) : name(std::move(n)), fn(std::move(f)) {}
};

// ---------------------------------------------------------------------------
// Value — tagged union for all ZScript runtime values
//   nil | bool | int64 | double | ZString* | ZTable* | ZClosure* | NativeFunction*
// ---------------------------------------------------------------------------
struct Value {
    enum class Tag : uint8_t {
        Nil,
        Bool,
        Int,
        Float,
        String,
        Table,
        Closure,
        Native,
    };

    Tag tag = Tag::Nil;

    union {
        bool                    b;
        int64_t                 i;
        double                  f;
        std::shared_ptr<ZString>*          str;
        std::shared_ptr<ZTable>*           table;
        std::shared_ptr<ZClosure>*         closure;
        std::shared_ptr<NativeFunction>*   native;
    };

    // We store shared_ptrs on the heap to keep the union trivially copyable-ish.
    // A cleaner approach would use std::variant; we use the union + tag pattern
    // here because it maps naturally to what a real GC value cell looks like.

    std::shared_ptr<ZString>          str_ptr;
    std::shared_ptr<ZTable>           table_ptr;
    std::shared_ptr<ZClosure>         closure_ptr;
    std::shared_ptr<NativeFunction>   native_ptr;

    // --- constructors ---
    Value() : tag(Tag::Nil) { i = 0; }

    static Value nil()                          { return Value(); }
    static Value from_bool(bool v)              { Value x; x.tag = Tag::Bool;  x.b = v; return x; }
    static Value from_int(int64_t v)            { Value x; x.tag = Tag::Int;   x.i = v; return x; }
    static Value from_float(double v)           { Value x; x.tag = Tag::Float; x.f = v; return x; }
    static Value from_string(std::string s)     {
        Value x; x.tag = Tag::String;
        x.str_ptr = std::make_shared<ZString>(std::move(s));
        return x;
    }
    static Value from_table() {
        Value x; x.tag = Tag::Table;
        x.table_ptr = std::make_shared<ZTable>();
        return x;
    }
    static Value from_closure(Proto* p) {
        Value x; x.tag = Tag::Closure;
        x.closure_ptr = std::make_shared<ZClosure>(p);
        return x;
    }
    static Value from_native(std::string name, NativeFunction::Fn fn) {
        Value x; x.tag = Tag::Native;
        x.native_ptr = std::make_shared<NativeFunction>(std::move(name), std::move(fn));
        return x;
    }

    // --- type queries ---
    bool is_nil()     const { return tag == Tag::Nil; }
    bool is_bool()    const { return tag == Tag::Bool; }
    bool is_int()     const { return tag == Tag::Int; }
    bool is_float()   const { return tag == Tag::Float; }
    bool is_number()  const { return tag == Tag::Int || tag == Tag::Float; }
    bool is_string()  const { return tag == Tag::String; }
    bool is_table()   const { return tag == Tag::Table; }
    bool is_closure() const { return tag == Tag::Closure; }
    bool is_native()  const { return tag == Tag::Native; }
    bool is_callable()const { return tag == Tag::Closure || tag == Tag::Native; }

    // --- accessors ---
    bool    as_bool()   const { assert(is_bool());    return b; }
    int64_t as_int()    const { assert(is_int());     return i; }
    double  as_float()  const { assert(is_float());   return f; }
    double  to_float()  const {
        if (is_float()) return f;
        if (is_int())   return (double)i;
        assert(false); return 0;
    }
    const std::string& as_string() const { assert(is_string()); return str_ptr->data; }
    ZTable*     as_table()   const { assert(is_table());   return table_ptr.get(); }
    ZClosure*   as_closure() const { assert(is_closure()); return closure_ptr.get(); }
    NativeFunction* as_native() const { assert(is_native()); return native_ptr.get(); }

    // --- truthiness: nil and false are falsy, everything else truthy ---
    bool truthy() const {
        if (is_nil())  return false;
        if (is_bool()) return b;
        return true;
    }

    // --- equality ---
    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const { return !(*this == o); }

    // --- display ---
    std::string to_string() const;
    std::string type_name() const;
};

// ---------------------------------------------------------------------------
// ZTable implementation (needs full Value definition)
// ---------------------------------------------------------------------------
inline Value ZTable::get(const std::string& key) const {
    auto it = hash.find(key);
    return (it != hash.end()) ? it->second : Value::nil();
}

inline void ZTable::set(const std::string& key, Value val) {
    hash[key] = std::move(val);
}

inline Value ZTable::get_index(int64_t idx) const {
    // 0-based internally
    if (idx >= 0 && (size_t)idx < array.size()) return array[(size_t)idx];
    return Value::nil();
}

inline void ZTable::set_index(int64_t idx, Value val) {
    if (idx >= 0) {
        if ((size_t)idx >= array.size()) array.resize((size_t)idx + 1);
        array[(size_t)idx] = std::move(val);
    }
}

} // namespace zscript
