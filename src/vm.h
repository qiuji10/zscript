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
    Proto*   proto    = nullptr;
    size_t   pc       = 0;
    uint8_t  base_reg = 0;
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

    static constexpr size_t MAX_REGS   = 1024;
    static constexpr size_t MAX_FRAMES = 200;

    Value regs_[MAX_REGS];
    std::vector<CallFrame> frames_;

    CallFrame& cur_frame() { return frames_.back(); }

    std::unordered_map<std::string, Value> globals_;
    EngineMode    engine_ = EngineMode::None;
    GC            gc_;
    ModuleLoader  loader_;
    RuntimeError  last_error_;

    std::unique_ptr<HotpatchManager> hotpatch_;

    void mark_roots(GC& gc);
    void runtime_error(const std::string& msg);
    std::string format_trace() const;
};

} // namespace zscript
