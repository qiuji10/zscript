#pragma once
#include "chunk.h"
#include "gc.h"
#include "module.h"
#include "value.h"
#include <functional>
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
// ZCoroutine — stackful coroutine
//
// Defined here (not in value.h) because it depends on CallFrame and the
// VM's private TryFrame / PendingCtor types.  value.h forward-declares it.
//
// Lifetime: shared_ptr held by the Value that represents the coroutine.
// The GC traces saved_regs through VM::mark_roots so values inside a
// suspended coroutine are not collected.
// ---------------------------------------------------------------------------
struct ZCoroutine : GcObject {
    enum class Status { Suspended, Running, Dead };
    Status status = Status::Suspended;

    Value fn;  // initial closure; nil once the first resume frame is set up

    // --- saved execution state (populated on yield, cleared on resume) ---
    std::vector<CallFrame> saved_frames;  // absolute base_regs at save time
    std::vector<Value>     saved_regs;   // register snapshot from regs_base onward
    uint16_t               regs_base = 0; // abs index of first saved register

    // --- yield result delivery ---
    // On yield, these record where the next resume's args should be written.
    uint16_t resume_result_offset = 0;  // result_reg - regs_base at yield time
    uint8_t  resume_result_count  = 1;  // num_results of the yielding Call
};

// ---------------------------------------------------------------------------
// VM
// ---------------------------------------------------------------------------
class VM {
public:
    VM();
    ~VM();

    // --- setup ---
    // Tag-based conditional compilation. Add tags before execute()/load_file().
    // @tag { } blocks are emitted only when their tag is in the active set.
    // add_tag() returns false and is a no-op if the name is not a valid identifier.
    bool           add_tag(const std::string& tag) {
                       if (!is_valid_tag(tag)) return false;
                       tags_.insert(tag); return true;
                   }
    void           remove_tag(const std::string& tag) { tags_.erase(tag); }
    bool           has_tag(const std::string& tag) const { return tags_.count(tag) > 0; }
    const TagSet&  active_tags() const { return tags_; }

    void register_function(const std::string& name, NativeFunction::Fn fn);
    void open_stdlib();
    void open_io();   // file I/O (opt-in — not called by open_stdlib)
    void open_os();   // OS / path utilities (opt-in — not called by open_stdlib)

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

    // --- annotation registry ---
    // Annotations are populated from Chunk metadata when a chunk is executed.
    // They are read-only from the host side; scripts have no access to them.
    const std::vector<CompiledAnnotation>& get_annotations(const std::string& class_name) const;
    const std::unordered_map<std::string, std::vector<CompiledAnnotation>>& all_annotations() const {
        return annotations_;
    }

    // --- GC ---
    GC& gc() { return gc_; }
    void gc_collect();
    void track(GcObject* obj, size_t sz = 64) { gc_.track(obj, sz); }

    // Transfer ownership of a compiled chunk to the VM so that ZClosure::proto
    // raw pointers into it remain valid for the VM's lifetime.
    // Called by load_file() internally and by the C API after zs_vm_load_source().
    void take_chunk(std::unique_ptr<Chunk> chunk) {
        owned_chunks_.push_back(std::move(chunk));
    }

    // --- Object handle system (Unity / engine plugin bridge) ---
    // Register the callback fired when a proxy table is GC'd.
    // Called once by the host (C API / C# ZsObjectPool) before any handles are created.
    using HandleReleaseFn = std::function<void(int64_t id)>;
    void set_object_handle_release(HandleReleaseFn fn) {
        handle_release_ = std::move(fn);
    }

    // Create a ZScript proxy table for a Unity object with the given integer handle.
    // The table has:
    //   __handle  = id (int)
    //   gc_hook   = fires handle_release_(id) when the table is collected
    // Type-specific metamethods (__index, __newindex, …) are NOT set here;
    // per-type binding functions (generated or hand-written) set them afterward.
    Value push_object_handle(int64_t id);

    // Extract the integer handle from a proxy table created by push_object_handle().
    // Returns -1 if val is not a handle-bearing table.
    int64_t get_object_handle(const Value& val) const;

    const RuntimeError& last_error() const { return last_error_; }

    // --- debugger ---
    // Line hook: called each time execution enters a new source line.
    // Arguments: source file path, 1-based line number, call depth (0 = top level).
    // The hook may block internally (e.g. to wait for step/continue from DAP).
    using LineHook = std::function<void(const std::string& source, int line, int depth)>;
    void set_line_hook(LineHook hook) { line_hook_ = std::move(hook); }

    // Per-frame debug info snapshot — call from inside a line hook callback.
    struct DebugFrame {
        std::string name;    // function/proto name
        std::string source;  // source file path
        int         line;    // current source line (1-based)
        std::vector<std::pair<std::string, Value>> locals; // slot name → value
    };
    std::vector<DebugFrame> debug_frames() const;

private:
    bool run(size_t stop_depth = 0);
    bool call(uint8_t base_reg, uint8_t num_args, uint8_t num_results);
    bool call_method(uint8_t base_reg, uint8_t user_args, uint8_t num_results);
    // Re-entrant closure call from within a native function.
    std::vector<Value> invoke_from_native(Value fn, std::vector<Value> args);

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
    TagSet        tags_;
    std::unordered_map<std::string, std::vector<CompiledAnnotation>> annotations_;

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
    HandleReleaseFn handle_release_; // fired by gc_hook when a proxy table is collected

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

    // Active coroutine tracking — set for the duration of a coroutine's resume.
    ZCoroutine* active_coro_      = nullptr;
    size_t      coro_frame_base_  = 0;   // frames_.size() at the point resume called run()

    void mark_roots(GC& gc);
    void runtime_error(const std::string& msg);
    std::string format_trace() const;

    // String interning — returns (or creates) the canonical Value for a string.
    Value intern_string(std::string s);
    // Walk all proto constant tables in a chunk and replace string Values with
    // their interned counterparts so identical strings across protos share one object.
    void  intern_chunk_strings(Chunk& chunk);

    // Chunks compiled from load_file() / zs_vm_load_source() — kept alive so
    // ZClosure::proto raw pointers into them remain valid for the VM's lifetime.
    std::vector<std::unique_ptr<Chunk>> owned_chunks_;

    // Intern table: string data → weak_ptr so entries expire when unreferenced.
    std::unordered_map<std::string, std::weak_ptr<ZString>> intern_table_;

    // Debugger hook state
    LineHook    line_hook_;
    std::string source_file_;      // set by execute(), used in line hook calls
    uint32_t    last_hook_line_ = 0; // suppress duplicate fires for the same line
};

} // namespace zscript
