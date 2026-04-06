#pragma once
#include "json.h"
#include <string>
#include <unordered_map>

namespace zscript {

// LSP server — reads JSON-RPC from stdin, writes to stdout.
// Launch via: zsc lsp
class LspServer {
public:
    // Runs the server loop until shutdown/exit.
    void run();

private:
    // ── JSON-RPC transport ────────────────────────────────────────────────
    bool        read_message(Json& out);
    void        send_message(const Json& msg);
    void        send_response(const Json& id, Json result);
    void        send_error(const Json& id, int code, const std::string& msg);
    void        send_notification(const std::string& method, Json params);

    // ── LSP protocol handlers ─────────────────────────────────────────────
    void handle(const Json& msg);
    void on_initialize(const Json& id, const Json& params);
    void on_shutdown(const Json& id);
    void on_did_open(const Json& params);
    void on_did_change(const Json& params);
    void on_did_close(const Json& params);
    void on_completion(const Json& id, const Json& params);
    void on_definition(const Json& id, const Json& params);
    void on_hover(const Json& id, const Json& params);

    // ── helpers ───────────────────────────────────────────────────────────
    void        publish_diagnostics(const std::string& uri, const std::string& text);
    Json        compute_diagnostics(const std::string& uri, const std::string& text);

    // Collect all top-level names from source (for completion)
    std::vector<std::string> collect_names(const std::string& text);

    // word at (line, col) in text
    std::string word_at(const std::string& text, int line, int col);

    // ── state ─────────────────────────────────────────────────────────────
    bool shutdown_requested_ = false;
    // uri → source text
    std::unordered_map<std::string, std::string> docs_;
};

} // namespace zscript
