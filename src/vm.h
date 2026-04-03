#pragma once
#include "chunk.h"
#include "value.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zscript {

// ---------------------------------------------------------------------------
// Runtime error
// ---------------------------------------------------------------------------
struct RuntimeError {
    std::string message;
    std::string trace; // simple stack trace string
};

// ---------------------------------------------------------------------------
// CallFrame — one function invocation on the call stack
// ---------------------------------------------------------------------------
struct CallFrame {
    Proto*   proto    = nullptr;
    size_t   pc       = 0;     // program counter (index into proto->code)
    uint8_t  base_reg = 0;     // base register (offset into VM register window)
};

// ---------------------------------------------------------------------------
// VM
// ---------------------------------------------------------------------------
class VM {
public:
    VM();

    // --- setup ---
    void set_engine(EngineMode mode) { engine_ = mode; }
    void register_function(const std::string& name, NativeFunction::Fn fn);
    void open_stdlib();  // register built-in functions

    // --- execution ---
    // Run a compiled chunk. Returns true on success.
    bool execute(Chunk& chunk);

    // Call a global function by name. Returns the result value.
    // Throws RuntimeError on failure.
    Value call_global(const std::string& name, std::vector<Value> args = {});

    // --- globals access ---
    void  set_global(const std::string& name, Value v);
    Value get_global(const std::string& name) const;

    const RuntimeError& last_error() const { return last_error_; }

private:
    bool run();       // interpreter loop
    bool call(uint8_t base_reg, uint8_t num_args, uint8_t num_results);

    // Register window — flat array, indexed by frame.base_reg + local_reg
    static constexpr size_t MAX_REGS   = 1024;
    static constexpr size_t MAX_FRAMES = 200;

    Value regs_[MAX_REGS];
    std::vector<CallFrame> frames_;

    CallFrame& cur_frame() { return frames_.back(); }
    Value& reg(uint8_t r) { return regs_[cur_frame().base_reg + r]; }

    std::unordered_map<std::string, Value> globals_;
    EngineMode                             engine_ = EngineMode::None;
    RuntimeError                           last_error_;

    void runtime_error(const std::string& msg);
    std::string format_trace() const;
};

} // namespace zscript
