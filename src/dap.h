#pragma once
#include "json.h"
#include "value.h"
#include "vm.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zscript {

// ---------------------------------------------------------------------------
// Breakpoint
// ---------------------------------------------------------------------------
struct Breakpoint {
    int         id     = 0;
    std::string source;   // file path
    int         line   = 0;
    bool        verified = false;
};

// ---------------------------------------------------------------------------
// DAP server — Debug Adapter Protocol over stdin/stdout.
// Integrates with the ZScript VM via a single-step callback.
// Launch via: zsc dap
// ---------------------------------------------------------------------------
class DapServer {
public:
    // Run the DAP loop. Returns exit code.
    int run();

private:
    // ── transport ─────────────────────────────────────────────────────────
    bool read_message(Json& out);
    void send_message(const Json& msg);
    void send_response(int seq, const std::string& command, bool success, Json body = Json::object());
    void send_event(const std::string& event, Json body = Json::object());

    // ── request handlers ──────────────────────────────────────────────────
    void handle(const Json& msg);
    void on_initialize(int seq, const Json& args);
    void on_launch(int seq, const Json& args);
    void on_attach(int seq, const Json& args);
    void on_set_breakpoints(int seq, const Json& args);
    void on_set_exception_breakpoints(int seq, const Json& args);
    void on_threads(int seq);
    void on_stack_trace(int seq, const Json& args);
    void on_scopes(int seq, const Json& args);
    void on_variables(int seq, const Json& args);
    void on_continue(int seq, const Json& args);
    void on_next(int seq, const Json& args);       // step over
    void on_step_in(int seq, const Json& args);
    void on_step_out(int seq, const Json& args);
    void on_pause(int seq, const Json& args);
    void on_disconnect(int seq, const Json& args);
    void on_evaluate(int seq, const Json& args);

    // ── VM execution with debug hooks ─────────────────────────────────────
    void launch_script(const std::string& path);
    bool should_stop(const std::string& source, int line);

    // ── variable reference store ──────────────────────────────────────────
    // Maps a reference id → (frameId, scope_name)
    struct VarRef { int frame_id; std::string scope; };
    int  new_var_ref(int frame_id, const std::string& scope);
    void clear_var_refs();

    // ── state ─────────────────────────────────────────────────────────────
    int    seq_           = 1;
    bool   initialized_   = false;
    bool   terminated_    = false;
    bool   running_       = false;

    // Breakpoints: source path → set of lines
    std::unordered_map<std::string, std::unordered_set<int>> breakpoints_;
    std::vector<Breakpoint> bp_list_;
    int next_bp_id_ = 1;

    // Variable references (reset on each stop)
    std::vector<VarRef> var_refs_;

    // The VM used for launch mode
    std::unique_ptr<VM> vm_;
    std::string launch_path_;

    // Step control
    enum class StepMode { Run, StepOver, StepIn, StepOut, Pause };
    StepMode   step_mode_   = StepMode::Run;
    int        step_depth_  = 0; // call depth at step-over start
    std::string stop_reason_;

    // Captured frame info at stop point (for stack/variables queries)
    struct FrameInfo {
        std::string source;
        int         line   = 0;
        std::string name;
        std::vector<std::pair<std::string, Value>> locals;
    };
    std::vector<FrameInfo> frames_;
};

} // namespace zscript
