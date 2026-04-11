#pragma once
#include "chunk.h"
#include "gc.h"
#include "module.h"
#include "value.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zscript {

class HotpatchManager; // forward declaration — defined in hotpatch.h

// ---------------------------------------------------------------------------
// Runtime error
// ---------------------------------------------------------------------------
struct RuntimeError {
    std::string message;
    std::string trace;
};

// ---------------------------------------------------------------------------
// CallFrame
// ---------------------------------------------------------------------------
struct CallFrame {
    Proto*    proto       = nullptr;
    size_t    pc          = 0;
    uint8_t   base_reg    = 0;
    uint8_t   num_results = 1;    // how many return values the caller expects
    ZClosure* closure     = nullptr; // nullptr for native/top-level frames
};

// ---------------------------------------------------------------------------
// VM
// ---------------------------------------------------------------------------
class VM {
public:
    VM();
    ~VM();

    // --- setup ---
    void set_engine(EngineMode mode) { engine_ = mode; }
    EngineMode engine_mode() const   { return engine_; }

    void register_function(const std::string& name, NativeFunction::Fn fn);
    void open_stdlib();

    // --- module system ---
    ModuleLoader& loader() { return loader_; }
    // Import a module by name; its exports are merged into globals.
    bool import_module(const std::string& name);
    // Execute a Module's compiled chunk in its own namespace (called by loader).
    bool execute_module(Module& mod, std::string& error_msg);
    // Execute a compiled chunk under a module name (used by hotpatch).
    bool execute_module(Chunk& chunk, const std::string& mod_name);

    // --- execution ---
    bool  execute(Chunk& chunk);
    bool  load_file(const std::string& path);
    Value call_global(const std::string& name, std::vector<Value> args = {});
    // Call any callable Value (closure or native). Returns first return value or nil.
    Value call_value(const Value& fn, std::vector<Value> args = {});

    // --- hotpatch ---
    // Start watching dir for .zs file changes. Returns false on failure.
    bool enable_hotpatch(const std::string& dir);
    // Apply pending hotpatches at a safe point (between frames). Returns reload count.
    int  poll();

    // --- globals ---
    void  set_global(const std::string& name, Value v);
    Value get_global(const std::string& name) const;
    const std::unordered_map<std::string, Value>& globals() const { return globals_; }

    // --- GC ---
    GC& gc() { return gc_; }
    void gc_collect();
    void track(GcObject* obj, size_t sz = 64) { gc_.track(obj, sz); }

    const RuntimeError& last_error() const { return last_error_; }

private:
    bool run();
    bool call(uint8_t base_reg, uint8_t num_args, uint8_t num_results);
    bool call_method(uint8_t base_reg, uint8_t user_args, uint8_t num_results);

    static constexpr size_t MAX_REGS   = 1024;
    static constexpr size_t MAX_FRAMES = 200;

    Value regs_[MAX_REGS];
    std::vector<CallFrame> frames_;

    CallFrame& cur_frame() { return frames_.back(); }

    // Open upvalue cells: absolute register index → shared cell.
    // When a closure captures a local that is still on the stack, both the
    // register and the shared_ptr cell point at the same Value.  When the
    // enclosing scope exits we "close" the upvalue by leaving the value only
    // in the cell.
    std::unordered_map<uint16_t, std::shared_ptr<Value>> open_upvals_;

    std::shared_ptr<Value> capture_reg(uint16_t abs_reg);
    void close_upvals_above(uint8_t base); // called when a frame returns

    std::unordered_map<std::string, Value> globals_;
    EngineMode    engine_ = EngineMode::None;

    // Type method tables — looked up when GetField is called on a non-table value.
    // Each entry is a native function that expects self as first arg.
    std::unordered_map<std::string, Value> string_methods_;
    std::unordered_map<std::string, Value> table_methods_;
    std::unordered_map<std::string, Value> int_methods_;
    std::unordered_map<std::string, Value> float_methods_;

    // Build a self-bound wrapper: a native that prepends `self_val` to args
    // before calling the original native function.
    Value make_bound_method(const Value& method, Value self_val);
    GC            gc_;
    ModuleLoader  loader_;
    RuntimeError  last_error_;

    std::unique_ptr<HotpatchManager> hotpatch_;

    // Stack of pending constructors: each entry records the result register and
    // instance for one class being constructed.  Nested constructors (e.g.
    // Vec2 called inside Player.init) push on top; each init Return pops one.
    struct PendingCtor {
        uint8_t result_reg = 0;
        Value   inst;
    };
    std::vector<PendingCtor> ctor_stack_;

    // Try-catch support
    struct TryFrame {
        size_t  frame_count; // number of CallFrames when PushTry was executed
        size_t  catch_pc;    // absolute PC in the frame to jump to on error
        uint8_t base_reg;    // base_reg of the catching frame
        uint8_t catch_reg;   // register to store the caught error value
    };
    std::vector<TryFrame> try_stack_;
    Value thrown_value_;     // last value passed to Op::Throw

    void mark_roots(GC& gc);
    void runtime_error(const std::string& msg);
    std::string format_trace() const;
};

} // namespace zscript
