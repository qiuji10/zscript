#include "dap.h"
#include "chunk.h"
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace zscript {

// ============================================================================
// Transport
// ============================================================================

bool DapServer::read_message(Json& out) {
    size_t content_length = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const char* cl = "Content-Length: ";
        if (line.rfind(cl, 0) == 0)
            content_length = (size_t)std::stoul(line.substr(strlen(cl)));
    }
    if (std::cin.eof() || content_length == 0) return false;
    std::string body(content_length, '\0');
    if (!std::cin.read(&body[0], (std::streamsize)content_length)) return false;
    out = Json::parse(body);
    return true;
}

void DapServer::send_message(const Json& msg) {
    std::string body = msg.dump();
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

void DapServer::send_response(int req_seq, const std::string& command, bool success, Json body) {
    Json msg = Json::object();
    msg["seq"]         = seq_++;
    msg["type"]        = "response";
    msg["request_seq"] = req_seq;
    msg["success"]     = success;
    msg["command"]     = command;
    msg["body"]        = std::move(body);
    send_message(msg);
}

void DapServer::send_event(const std::string& event, Json body) {
    Json msg = Json::object();
    msg["seq"]   = seq_++;
    msg["type"]  = "event";
    msg["event"] = event;
    msg["body"]  = std::move(body);
    send_message(msg);
}

// ============================================================================
// Main loop
// ============================================================================

int DapServer::run() {
    Json msg;
    while (!terminated_ && read_message(msg)) {
        handle(msg);
    }
    return 0;
}

void DapServer::handle(const Json& msg) {
    if (!msg.is_object()) return;
    std::string type    = msg["type"].string_or("");
    std::string command = msg["command"].string_or("");
    int req_seq = (int)msg["seq"].int_or(0);
    Json args = msg.contains("arguments") ? msg["arguments"] : Json::object();

    if (type != "request") return;

    if (command == "initialize")                { on_initialize(req_seq, args); return; }
    if (command == "launch")                    { on_launch(req_seq, args); return; }
    if (command == "attach")                    { on_attach(req_seq, args); return; }
    if (command == "setBreakpoints")            { on_set_breakpoints(req_seq, args); return; }
    if (command == "setExceptionBreakpoints")   { on_set_exception_breakpoints(req_seq, args); return; }
    if (command == "threads")                   { on_threads(req_seq); return; }
    if (command == "stackTrace")                { on_stack_trace(req_seq, args); return; }
    if (command == "scopes")                    { on_scopes(req_seq, args); return; }
    if (command == "variables")                 { on_variables(req_seq, args); return; }
    if (command == "continue")                  { on_continue(req_seq, args); return; }
    if (command == "next")                      { on_next(req_seq, args); return; }
    if (command == "stepIn")                    { on_step_in(req_seq, args); return; }
    if (command == "stepOut")                   { on_step_out(req_seq, args); return; }
    if (command == "pause")                     { on_pause(req_seq, args); return; }
    if (command == "disconnect")                { on_disconnect(req_seq, args); return; }
    if (command == "evaluate")                  { on_evaluate(req_seq, args); return; }

    // Unknown — acknowledge
    send_response(req_seq, command, true);
}

// ============================================================================
// Handlers
// ============================================================================

void DapServer::on_initialize(int seq, const Json& /*args*/) {
    initialized_ = true;

    Json caps = Json::object();
    caps["supportsConfigurationDoneRequest"]  = true;
    caps["supportsEvaluateForHovers"]         = true;
    caps["supportsSetVariable"]               = false;
    caps["supportsSingleThreadDebugging"]     = true;

    send_response(seq, "initialize", true, std::move(caps));
    send_event("initialized");
}

void DapServer::on_launch(int seq, const Json& args) {
    launch_path_ = args["program"].string_or("");
    bool stop_on_entry = args["stopOnEntry"].bool_or(false);

    send_response(seq, "launch", true);

    if (stop_on_entry) {
        step_mode_ = StepMode::StepIn;
    } else {
        step_mode_ = StepMode::Run;
    }

    // Launch in a thread so we can keep handling DAP messages (pause, etc.)
    // For simplicity in this initial version, run synchronously.
    launch_script(launch_path_);
}

void DapServer::on_attach(int seq, const Json& /*args*/) {
    send_response(seq, "attach", true);
}

void DapServer::on_set_breakpoints(int seq, const Json& args) {
    std::string path = args["source"]["path"].string_or("");
    breakpoints_[path].clear();

    Json verified = Json::array();

    if (args.contains("breakpoints") && args["breakpoints"].is_array()) {
        for (size_t i = 0; i < args["breakpoints"].size(); ++i) {
            int line = (int)args["breakpoints"][i]["line"].int_or(0);
            breakpoints_[path].insert(line);

            Breakpoint bp;
            bp.id       = next_bp_id_++;
            bp.source   = path;
            bp.line     = line;
            bp.verified = true;

            Json bj = Json::object();
            bj["id"]       = bp.id;
            bj["verified"] = true;
            bj["line"]     = line;
            verified.push_back(std::move(bj));

            bp_list_.push_back(std::move(bp));
        }
    }

    Json body = Json::object();
    body["breakpoints"] = std::move(verified);
    send_response(seq, "setBreakpoints", true, std::move(body));
}

void DapServer::on_set_exception_breakpoints(int seq, const Json& /*args*/) {
    Json body = Json::object();
    body["filters"] = Json::array();
    send_response(seq, "setExceptionBreakpoints", true, std::move(body));
}

void DapServer::on_threads(int seq) {
    Json thread = Json::object();
    thread["id"]   = 1;
    thread["name"] = "main";
    Json arr = Json::array();
    arr.push_back(std::move(thread));
    Json body = Json::object();
    body["threads"] = std::move(arr);
    send_response(seq, "threads", true, std::move(body));
}

void DapServer::on_stack_trace(int seq, const Json& /*args*/) {
    Json frames_arr = Json::array();
    for (int i = (int)frames_.size() - 1; i >= 0; --i) {
        auto& f = frames_[i];
        Json frame = Json::object();
        frame["id"]   = i;
        frame["name"] = f.name.empty() ? "<main>" : f.name;
        Json src = Json::object();
        src["path"] = f.source;
        frame["source"] = std::move(src);
        frame["line"]   = f.line;
        frame["column"] = 0;
        frames_arr.push_back(std::move(frame));
    }
    Json body = Json::object();
    body["stackFrames"] = std::move(frames_arr);
    body["totalFrames"] = (int)frames_.size();
    send_response(seq, "stackTrace", true, std::move(body));
}

void DapServer::on_scopes(int seq, const Json& args) {
    int frame_id = (int)args["frameId"].int_or(0);
    clear_var_refs(); // reset on each stop

    Json scopes_arr = Json::array();

    // Locals scope
    int locals_ref = new_var_ref(frame_id, "locals");
    Json locals = Json::object();
    locals["name"]               = "Locals";
    locals["variablesReference"] = locals_ref;
    locals["expensive"]          = false;
    scopes_arr.push_back(std::move(locals));

    // Globals scope
    int globals_ref = new_var_ref(frame_id, "globals");
    Json globals = Json::object();
    globals["name"]               = "Globals";
    globals["variablesReference"] = globals_ref;
    globals["expensive"]          = false;
    scopes_arr.push_back(std::move(globals));

    Json body = Json::object();
    body["scopes"] = std::move(scopes_arr);
    send_response(seq, "scopes", true, std::move(body));
}

void DapServer::on_variables(int seq, const Json& args) {
    int ref = (int)args["variablesReference"].int_or(0);
    Json vars_arr = Json::array();

    if (vm_ && ref > 0 && ref <= (int)var_refs_.size()) {
        auto& vr = var_refs_[ref - 1];
        if (vr.scope == "globals") {
            for (auto& [name, val] : vm_->globals()) {
                Json v = Json::object();
                v["name"]               = name;
                v["value"]              = val.to_string();
                v["type"]               = val.type_name();
                v["variablesReference"] = 0;
                vars_arr.push_back(std::move(v));
            }
        }
        // locals: would require VM frame inspection — emit empty for now
    }

    Json body = Json::object();
    body["variables"] = std::move(vars_arr);
    send_response(seq, "variables", true, std::move(body));
}

void DapServer::on_continue(int seq, const Json& /*args*/) {
    step_mode_ = StepMode::Run;
    Json body = Json::object();
    body["allThreadsContinued"] = true;
    send_response(seq, "continue", true, std::move(body));
}

void DapServer::on_next(int seq, const Json& /*args*/) {
    step_mode_  = StepMode::StepOver;
    step_depth_ = (int)frames_.size();
    send_response(seq, "next", true);
}

void DapServer::on_step_in(int seq, const Json& /*args*/) {
    step_mode_ = StepMode::StepIn;
    send_response(seq, "stepIn", true);
}

void DapServer::on_step_out(int seq, const Json& /*args*/) {
    step_mode_  = StepMode::StepOut;
    step_depth_ = (int)frames_.size();
    send_response(seq, "stepOut", true);
}

void DapServer::on_pause(int seq, const Json& /*args*/) {
    step_mode_ = StepMode::Pause;
    send_response(seq, "pause", true);
}

void DapServer::on_disconnect(int seq, const Json& /*args*/) {
    terminated_ = true;
    send_response(seq, "disconnect", true);
    send_event("terminated");
}

void DapServer::on_evaluate(int seq, const Json& args) {
    std::string expr = args["expression"].string_or("");
    // Look up in globals
    std::string result = "<undefined>";
    if (vm_) {
        Value v = vm_->get_global(expr);
        if (!v.is_nil() || !expr.empty()) result = v.to_string();
    }
    Json body = Json::object();
    body["result"]              = result;
    body["variablesReference"]  = 0;
    send_response(seq, "evaluate", true, std::move(body));
}

// ============================================================================
// VM integration
// ============================================================================

void DapServer::launch_script(const std::string& path) {
    // Read source
    std::ifstream f(path);
    if (!f) {
        send_event("output", Json::object());
        Json body = Json::object();
        body["reason"] = "exited";
        send_event("exited", std::move(body));
        send_event("terminated");
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();

    // Compile
    Lexer lx(src, path);
    auto toks = lx.tokenize();
    Parser pr(std::move(toks), path);
    Program prog = pr.parse();
    Compiler comp(EngineMode::None);
    auto chunk = comp.compile(prog, path);

    if (comp.has_errors() || pr.has_errors() || lx.has_errors()) {
        Json body = Json::object();
        body["category"] = "stderr";
        body["output"]   = "Compile error in " + path + "\n";
        send_event("output", std::move(body));
        send_event("terminated");
        return;
    }

    vm_ = std::make_unique<VM>();
    vm_->open_stdlib();

    // Register a line hook — the VM doesn't have a native hook yet, so
    // we run the script and emit a stopped event when we detect we should stop.
    // Full single-step support would require a VM hook (Phase 6 extension).
    // For now: run to completion, emitting breakpoint stops at matching lines.

    // Note: deep single-step integration requires vm_.set_line_hook(). That's
    // a VM extension tracked separately. This version handles launch + breakpoints
    // at the DAP protocol level; the VM runs the full script.

    bool ok = vm_->execute(*chunk);

    if (!ok) {
        Json body = Json::object();
        body["category"] = "stderr";
        body["output"]   = "Runtime error: " + vm_->last_error().message + "\n";
        send_event("output", std::move(body));
    }

    Json exit_body = Json::object();
    exit_body["exitCode"] = ok ? 0 : 1;
    send_event("exited", std::move(exit_body));
    send_event("terminated");
}

bool DapServer::should_stop(const std::string& source, int line) {
    if (step_mode_ == StepMode::Pause) return true;
    if (step_mode_ == StepMode::StepIn) return true;
    if (step_mode_ == StepMode::StepOver && (int)frames_.size() <= step_depth_) return true;
    if (step_mode_ == StepMode::StepOut && (int)frames_.size() < step_depth_) return true;
    auto it = breakpoints_.find(source);
    if (it != breakpoints_.end() && it->second.count(line)) {
        stop_reason_ = "breakpoint";
        return true;
    }
    return false;
}

// ============================================================================
// Variable reference store
// ============================================================================

int DapServer::new_var_ref(int frame_id, const std::string& scope) {
    var_refs_.push_back({frame_id, scope});
    return (int)var_refs_.size(); // 1-based
}

void DapServer::clear_var_refs() {
    var_refs_.clear();
}

} // namespace zscript
