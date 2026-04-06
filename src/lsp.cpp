#include "lsp.h"
#include "ast.h"
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace zscript {

// ============================================================================
// JSON-RPC transport (Content-Length framing over stdin/stdout)
// ============================================================================

bool LspServer::read_message(Json& out) {
    // Read headers
    size_t content_length = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break; // blank line separates headers from body
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

void LspServer::send_message(const Json& msg) {
    std::string body = msg.dump();
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

void LspServer::send_response(const Json& id, Json result) {
    Json msg = Json::object();
    msg["jsonrpc"] = "2.0";
    msg["id"]      = id;
    msg["result"]  = std::move(result);
    send_message(msg);
}

void LspServer::send_error(const Json& id, int code, const std::string& message) {
    Json err = Json::object();
    err["code"]    = code;
    err["message"] = message;
    Json msg = Json::object();
    msg["jsonrpc"] = "2.0";
    msg["id"]      = id;
    msg["error"]   = std::move(err);
    send_message(msg);
}

void LspServer::send_notification(const std::string& method, Json params) {
    Json msg = Json::object();
    msg["jsonrpc"] = "2.0";
    msg["method"]  = method;
    msg["params"]  = std::move(params);
    send_message(msg);
}

// ============================================================================
// Main loop
// ============================================================================

void LspServer::run() {
    Json msg;
    while (!shutdown_requested_ && read_message(msg)) {
        handle(msg);
    }
}

void LspServer::handle(const Json& msg) {
    if (!msg.is_object()) return;

    std::string method = msg["method"].string_or("");
    Json id = msg.contains("id") ? msg["id"] : Json(nullptr);
    Json params = msg.contains("params") ? msg["params"] : Json::object();

    if (method == "initialize")             { on_initialize(id, params); return; }
    if (method == "initialized")            { return; } // notification, no response
    if (method == "shutdown")               { on_shutdown(id); return; }
    if (method == "exit")                   { std::exit(shutdown_requested_ ? 0 : 1); }
    if (method == "$/cancelRequest")        { return; }
    if (method == "textDocument/didOpen")   { on_did_open(params); return; }
    if (method == "textDocument/didChange") { on_did_change(params); return; }
    if (method == "textDocument/didClose")  { on_did_close(params); return; }
    if (method == "textDocument/completion"){ on_completion(id, params); return; }
    if (method == "textDocument/definition"){ on_definition(id, params); return; }
    if (method == "textDocument/hover")     { on_hover(id, params); return; }

    // Unknown request — send method-not-found error
    if (!id.is_null()) send_error(id, -32601, "method not found: " + method);
}

// ============================================================================
// LSP handlers
// ============================================================================

void LspServer::on_initialize(const Json& id, const Json& /*params*/) {
    Json caps = Json::object();

    // Full document sync
    caps["textDocumentSync"] = 1; // Full

    // Completion
    Json cp = Json::object();
    cp["triggerCharacters"] = Json(std::vector<Json>{".", " "});
    caps["completionProvider"] = std::move(cp);

    // Definition
    caps["definitionProvider"] = true;

    // Hover
    caps["hoverProvider"] = true;

    Json result = Json::object();
    result["capabilities"] = std::move(caps);

    Json serverInfo = Json::object();
    serverInfo["name"]    = "zscript-lsp";
    serverInfo["version"] = "0.1.0";
    result["serverInfo"] = std::move(serverInfo);

    send_response(id, std::move(result));
}

void LspServer::on_shutdown(const Json& id) {
    shutdown_requested_ = true;
    send_response(id, Json(nullptr));
}

void LspServer::on_did_open(const Json& params) {
    std::string uri  = params["textDocument"]["uri"].string_or("");
    std::string text = params["textDocument"]["text"].string_or("");
    docs_[uri] = text;
    publish_diagnostics(uri, text);
}

void LspServer::on_did_change(const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    // Full sync: take the last contentChanges entry
    const Json& changes = params["contentChanges"];
    if (changes.is_array() && changes.size() > 0) {
        std::string text = changes[changes.size() - 1]["text"].string_or("");
        docs_[uri] = text;
        publish_diagnostics(uri, text);
    }
}

void LspServer::on_did_close(const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    docs_.erase(uri);
    // Clear diagnostics
    Json p = Json::object();
    p["uri"]         = uri;
    p["diagnostics"] = Json::array();
    send_notification("textDocument/publishDiagnostics", std::move(p));
}

void LspServer::on_completion(const Json& id, const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    int line = (int)params["position"]["line"].int_or(0);
    int col  = (int)params["position"]["character"].int_or(0);

    std::string text = docs_.count(uri) ? docs_[uri] : "";
    std::string prefix = word_at(text, line, col);

    auto names = collect_names(text);

    // Built-in keywords and stdlib
    static const std::vector<std::string> builtins = {
        "var","let","fn","return","if","else","while","for","in","class",
        "self","true","false","nil","and","or","not","import","mut",
        "log","math","string","table","io"
    };
    for (auto& b : builtins) names.push_back(b);

    // Deduplicate
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    Json items = Json::array();
    for (auto& name : names) {
        if (prefix.empty() || name.rfind(prefix, 0) == 0) {
            Json item = Json::object();
            item["label"] = name;
            item["kind"]  = 6; // Variable (will refine per type later)
            items.push_back(std::move(item));
        }
    }

    Json result = Json::object();
    result["isIncomplete"] = false;
    result["items"]        = std::move(items);
    send_response(id, std::move(result));
}

void LspServer::on_definition(const Json& id, const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    int line = (int)params["position"]["line"].int_or(0);
    int col  = (int)params["position"]["character"].int_or(0);

    std::string text = docs_.count(uri) ? docs_[uri] : "";
    std::string word = word_at(text, line, col);
    if (word.empty()) { send_response(id, Json(nullptr)); return; }

    // Parse and look for definition of `word`
    Lexer lx(text, uri);
    auto toks = lx.tokenize();
    Parser pr(std::move(toks), uri);
    Program prog = pr.parse();

    // Walk top-level decls for fn/class/var declarations matching word
    auto make_loc = [&](const SourceLoc& loc) {
        Json j = Json::object();
        j["uri"] = uri;
        Json range = Json::object();
        Json start = Json::object(); start["line"] = (int)loc.line - 1; start["character"] = (int)loc.column - 1;
        Json end   = Json::object(); end["line"]   = (int)loc.line - 1; end["character"]   = (int)loc.column - 1 + (int)word.size();
        range["start"] = start; range["end"] = end;
        j["range"] = range;
        return j;
    };
    for (auto& decl : prog.decls) {
        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            if (fn->name == word) { send_response(id, make_loc(fn->loc)); return; }
        }
        if (auto* cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            if (cls->name == word) { send_response(id, make_loc(cls->loc)); return; }
        }
        if (auto* sd = dynamic_cast<const StmtDecl*>(decl.get())) {
            if (auto* vd = dynamic_cast<const VarDeclStmt*>(sd->stmt.get())) {
                if (vd->name == word) { send_response(id, make_loc(vd->loc)); return; }
            }
        }
    }

    send_response(id, Json(nullptr));
}

void LspServer::on_hover(const Json& id, const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    int line = (int)params["position"]["line"].int_or(0);
    int col  = (int)params["position"]["character"].int_or(0);

    std::string text = docs_.count(uri) ? docs_[uri] : "";
    std::string word = word_at(text, line, col);
    if (word.empty()) { send_response(id, Json(nullptr)); return; }

    // Parse and find fn/class with this name for a signature
    Lexer lx(text, uri);
    auto toks = lx.tokenize();
    Parser pr(std::move(toks), uri);
    Program prog = pr.parse();

    for (auto& decl : prog.decls) {
        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            if (fn->name == word) {
                std::string sig = "fn " + fn->name + "(";
                for (size_t i = 0; i < fn->params.size(); ++i) {
                    if (i) sig += ", ";
                    sig += fn->params[i].name;
                }
                sig += ")";
                Json contents = Json::object();
                contents["kind"]  = "markdown";
                contents["value"] = "```zscript\n" + sig + "\n```";
                Json result = Json::object();
                result["contents"] = std::move(contents);
                send_response(id, std::move(result));
                return;
            }
        }
        if (auto* cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            if (cls->name == word) {
                Json contents = Json::object();
                contents["kind"]  = "markdown";
                contents["value"] = "```zscript\nclass " + cls->name + "\n```";
                Json result = Json::object();
                result["contents"] = std::move(contents);
                send_response(id, std::move(result));
                return;
            }
        }
    }

    send_response(id, Json(nullptr));
}

// ============================================================================
// Diagnostics
// ============================================================================

void LspServer::publish_diagnostics(const std::string& uri, const std::string& text) {
    Json diags = compute_diagnostics(uri, text);
    Json p = Json::object();
    p["uri"]         = uri;
    p["diagnostics"] = std::move(diags);
    send_notification("textDocument/publishDiagnostics", std::move(p));
}

// Convert 1-based line/col to LSP 0-based range JSON
static Json make_range(int line, int col, int end_col) {
    Json start = Json::object(); start["line"] = line - 1; start["character"] = col - 1;
    Json end   = Json::object(); end["line"]   = line - 1; end["character"]   = end_col - 1;
    Json range = Json::object(); range["start"] = start; range["end"] = end;
    return range;
}

Json LspServer::compute_diagnostics(const std::string& uri, const std::string& text) {
    Json diags = Json::array();

    auto add = [&](int line, int col, int end_col, int severity, const std::string& msg) {
        Json d = Json::object();
        d["range"]    = make_range(line, col, end_col);
        d["severity"] = severity; // 1=error, 2=warning, 3=info, 4=hint
        d["source"]   = "zscript";
        d["message"]  = msg;
        diags.push_back(std::move(d));
    };

    Lexer lx(text, uri);
    auto toks = lx.tokenize();
    if (lx.has_errors()) {
        for (auto& e : lx.errors())
            add(e.loc.line, e.loc.column, e.loc.column + 1, 1, e.message);
        return diags;
    }

    Parser pr(std::move(toks), uri);
    Program prog = pr.parse();
    if (pr.has_errors()) {
        for (auto& e : pr.errors())
            add(e.loc.line, e.loc.column, e.loc.column + 1, 1, e.message);
        return diags;
    }

    Compiler comp(EngineMode::None);
    comp.compile(prog, uri);
    if (comp.has_errors()) {
        for (auto& e : comp.errors())
            add(e.loc.line, e.loc.column, e.loc.column + 1, 1, e.message);
    }

    return diags;
}

// ============================================================================
// Helpers
// ============================================================================

std::vector<std::string> LspServer::collect_names(const std::string& text) {
    std::vector<std::string> names;

    Lexer lx(text, "<lsp>");
    auto toks = lx.tokenize();
    if (lx.has_errors()) return names;

    Parser pr(std::move(toks), "<lsp>");
    Program prog = pr.parse();

    for (auto& decl : prog.decls) {
        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            names.push_back(fn->name);
        }
        if (auto* cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            names.push_back(cls->name);
            for (auto& m : cls->members) {
                if (auto* mfn = dynamic_cast<const FnDecl*>(m.get()))
                    names.push_back(mfn->name);
            }
        }
        if (auto* sd = dynamic_cast<const StmtDecl*>(decl.get())) {
            if (auto* vd = dynamic_cast<const VarDeclStmt*>(sd->stmt.get()))
                names.push_back(vd->name);
        }
    }

    return names;
}

std::string LspServer::word_at(const std::string& text, int line, int col) {
    // Convert (line, col) 0-based LSP coordinates to byte offset
    int cur_line = 0, cur_col = 0;
    size_t offset = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (cur_line == line && cur_col == col) { offset = i; break; }
        if (text[i] == '\n') { ++cur_line; cur_col = 0; }
        else { ++cur_col; }
    }

    auto is_word = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };

    // Expand left
    size_t start = offset;
    while (start > 0 && is_word(text[start - 1])) --start;
    // Expand right
    size_t end = offset;
    while (end < text.size() && is_word(text[end])) ++end;

    return text.substr(start, end - start);
}

} // namespace zscript
