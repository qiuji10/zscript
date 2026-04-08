#include "lsp.h"
#include "ast.h"
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace zscript {

// ============================================================================
// JSON-RPC transport
// ============================================================================

bool LspServer::read_message(Json& out) {
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
    Json id     = msg.contains("id")     ? msg["id"]     : Json(nullptr);
    Json params = msg.contains("params") ? msg["params"] : Json::object();

    if (method == "initialize")                { on_initialize(id, params);     return; }
    if (method == "initialized")               { return; }
    if (method == "shutdown")                  { on_shutdown(id);               return; }
    if (method == "exit")                      { std::exit(shutdown_requested_ ? 0 : 1); }
    if (method == "$/cancelRequest")           { return; }
    if (method == "textDocument/didOpen")      { on_did_open(params);           return; }
    if (method == "textDocument/didChange")    { on_did_change(params);         return; }
    if (method == "textDocument/didClose")     { on_did_close(params);          return; }
    if (method == "textDocument/completion")   { on_completion(id, params);     return; }
    if (method == "textDocument/definition")   { on_definition(id, params);     return; }
    if (method == "textDocument/hover")        { on_hover(id, params);          return; }
    if (method == "textDocument/signatureHelp"){ on_signature_help(id, params); return; }

    if (!id.is_null()) send_error(id, -32601, "method not found: " + method);
}

// ============================================================================
// LSP handlers — lifecycle
// ============================================================================

void LspServer::on_initialize(const Json& id, const Json& /*params*/) {
    Json caps = Json::object();

    // Full document sync
    caps["textDocumentSync"] = 1;

    // Completion — trigger on dot only (space causes too much noise)
    Json cp = Json::object();
    cp["triggerCharacters"] = Json(std::vector<Json>{"."});
    cp["resolveProvider"]   = false;
    caps["completionProvider"] = std::move(cp);

    // Go-to-definition
    caps["definitionProvider"] = true;

    // Hover
    caps["hoverProvider"] = true;

    // Signature help — trigger on '(' and ','
    Json sh = Json::object();
    sh["triggerCharacters"] = Json(std::vector<Json>{"(", ","});
    caps["signatureHelpProvider"] = std::move(sh);

    Json result = Json::object();
    result["capabilities"] = std::move(caps);

    Json serverInfo = Json::object();
    serverInfo["name"]    = "zscript-lsp";
    serverInfo["version"] = "0.2.0";
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
    docs_[uri]    = text;
    indexes_[uri] = build_index(text);
    publish_diagnostics(uri, text);
}

void LspServer::on_did_change(const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    const Json& changes = params["contentChanges"];
    if (changes.is_array() && changes.size() > 0) {
        std::string text = changes[changes.size() - 1]["text"].string_or("");
        docs_[uri]    = text;
        indexes_[uri] = build_index(text);
        publish_diagnostics(uri, text);
    }
}

void LspServer::on_did_close(const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    docs_.erase(uri);
    indexes_.erase(uri);
    Json p = Json::object();
    p["uri"]         = uri;
    p["diagnostics"] = Json::array();
    send_notification("textDocument/publishDiagnostics", std::move(p));
}

// ============================================================================
// LSP handlers — completion
// ============================================================================

void LspServer::on_completion(const Json& id, const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    int line = (int)params["position"]["line"].int_or(0);
    int col  = (int)params["position"]["character"].int_or(0);

    std::string text  = docs_.count(uri) ? docs_[uri] : "";
    std::string prefix = word_at(text, line, col);

    // Rebuild index if missing (e.g. first request before didOpen)
    if (!indexes_.count(uri)) indexes_[uri] = build_index(text);
    const SymIndex& idx = indexes_[uri];

    // Check for dot completion: "obj." → show members of obj's class
    bool after_dot = is_after_dot(text, line, col);
    std::string dot_object = after_dot ? word_before_dot(text, line, col) : "";

    Json items = Json::array();
    auto add_item = [&](const std::string& label, SymInfo::Kind kind,
                        const std::string& detail, const std::string& doc) {
        if (!prefix.empty() && label.rfind(prefix, 0) != 0) return;
        Json item = Json::object();
        item["label"]      = label;
        item["kind"]       = lsp_completion_kind(kind);
        item["insertText"] = label;   // explicit so VS Code doesn't use filterText
        item["sortText"]   = label;   // alphabetic sort within our items
        if (!detail.empty()) item["detail"] = detail;
        if (!doc.empty())    item["documentation"] = doc;
        items.push_back(std::move(item));
    };

    if (after_dot && !dot_object.empty()) {
        // Dot completion: show members of the named class/variable type
        // Look for a class with that name, or a variable whose type matches
        auto it = idx.members.find(dot_object);
        if (it != idx.members.end()) {
            for (auto& sym : it->second) {
                add_item(sym.name, sym.kind, sym.detail, "");
            }
        }
        // Also search globals for a class matching the object name (PascalCase)
        for (auto& sym : idx.globals) {
            if (sym.kind == SymInfo::Kind::Class && sym.name == dot_object) {
                auto mit = idx.members.find(sym.name);
                if (mit != idx.members.end()) {
                    for (auto& msym : mit->second) {
                        add_item(msym.name, msym.kind, msym.detail, "");
                    }
                }
            }
        }
    } else {
        // General completion: globals + current function locals + keywords

        // Globals
        for (auto& sym : idx.globals) {
            add_item(sym.name, sym.kind, sym.detail, "");
        }

        // Find if we're inside a function — add its locals
        if (!text.empty()) {
            Lexer lx(text, "<lsp>");
            auto toks = lx.tokenize();
            if (!lx.has_errors()) {
                Parser pr(std::move(toks), "<lsp>");
                Program prog = pr.parse();
                std::string fn_name = fn_at_position(prog, line, col);
                if (!fn_name.empty()) {
                    auto fit = idx.fn_locals.find(fn_name);
                    if (fit != idx.fn_locals.end()) {
                        for (auto& lsym : fit->second) {
                            add_item(lsym.name, lsym.kind, lsym.detail, "");
                        }
                    }
                }
                // If inside a class method, also add class members
                std::string cls_name = class_at_position(prog, line, col);
                if (!cls_name.empty()) {
                    auto cit = idx.members.find(cls_name);
                    if (cit != idx.members.end()) {
                        for (auto& csym : cit->second) {
                            add_item(csym.name, csym.kind, csym.detail, "");
                        }
                    }
                }
            }
        }

        // Keywords (LSP kind 14)
        static const std::vector<std::string> keywords = {
            "var", "let", "fn", "return", "if", "else", "while", "for", "in",
            "class", "trait", "impl", "self", "true", "false", "nil", "mut",
            "import", "as", "pub",
        };
        for (auto& kw : keywords) {
            if (prefix.empty() || kw.rfind(prefix, 0) == 0) {
                Json item = Json::object();
                item["label"]      = kw;
                item["kind"]       = 14; // Keyword
                item["insertText"] = kw;
                item["sortText"]   = "z" + kw; // sort keywords after symbols
                items.push_back(std::move(item));
            }
        }

        // Stdlib globals — functions and module tables
        struct Builtin { const char* name; const char* detail; int kind; };
        static const Builtin builtins[] = {
            // global functions  (kind 3 = Function)
            {"log",      "fn log(...) -> Nil",                   3},
            {"print",    "fn print(...) -> Nil",                  3},
            {"tostring", "fn tostring(v: Any) -> String",         3},
            {"tonumber", "fn tonumber(v: Any) -> Int|Float|Nil",  3},
            {"tobool",   "fn tobool(v: Any) -> Bool",             3},
            {"type",     "fn type(v: Any) -> String",             3},
            {"len",      "fn len(v: String|Table) -> Int",        3},
            {"assert",   "fn assert(cond, msg?: String) -> Any",  3},
            {"error",    "fn error(msg: String) -> Never",        3},
            {"max",      "fn max(...) -> Any",                    3},
            {"min",      "fn min(...) -> Any",                    3},
            {"range",    "fn range(stop) | range(start,stop,step?) -> Table", 3},
            // module tables  (kind 9 = Module)
            {"math",     "math — floor ceil round sqrt pow exp log sin cos tan clamp lerp sign rad deg pi huge", 9},
            {"string",   "string — len sub upper lower trim contains starts_with ends_with find replace split join format rep byte char", 9},
            {"table",    "table — len push pop insert remove sort copy keys values contains", 9},
            {"io",       "io — read_file write_file append_file read_line print_err exists", 9},
        };
        for (auto& b : builtins) {
            if (prefix.empty() || std::string(b.name).rfind(prefix, 0) == 0) {
                Json item = Json::object();
                item["label"]      = std::string(b.name);
                item["kind"]       = b.kind;
                item["insertText"] = std::string(b.name);
                item["detail"]     = std::string(b.detail);
                items.push_back(std::move(item));
            }
        }
    }

    // Deduplicate by label
    auto& arr = items.as_array();
    std::sort(arr.begin(), arr.end(), [](const Json& a, const Json& b) {
        return a["label"].string_or("") < b["label"].string_or("");
    });
    arr.erase(
        std::unique(arr.begin(), arr.end(), [](const Json& a, const Json& b) {
            return a["label"].string_or("") == b["label"].string_or("");
        }),
        arr.end()
    );

    Json result = Json::object();
    result["isIncomplete"] = false;
    result["items"]        = std::move(items);
    send_response(id, std::move(result));
}

// ============================================================================
// LSP handlers — go-to-definition
// ============================================================================

void LspServer::on_definition(const Json& id, const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    int line = (int)params["position"]["line"].int_or(0);
    int col  = (int)params["position"]["character"].int_or(0);

    std::string text = docs_.count(uri) ? docs_[uri] : "";
    std::string word = word_at(text, line, col);
    if (word.empty()) { send_response(id, Json(nullptr)); return; }

    if (!indexes_.count(uri)) indexes_[uri] = build_index(text);
    const SymIndex& idx = indexes_[uri];

    auto make_loc = [&](const SourceLoc& loc) {
        Json j = Json::object();
        j["uri"] = uri;
        Json range = Json::object();
        Json start = Json::object();
        start["line"]      = (int)loc.line - 1;
        start["character"] = (int)loc.column - 1;
        Json end = Json::object();
        end["line"]      = (int)loc.line - 1;
        end["character"] = (int)loc.column - 1 + (int)word.size();
        range["start"] = start;
        range["end"]   = end;
        j["range"] = range;
        return j;
    };

    // Search globals first
    for (auto& sym : idx.globals) {
        if (sym.name == word) {
            send_response(id, make_loc(sym.loc));
            return;
        }
    }

    // Search class members
    for (auto& [cls, members] : idx.members) {
        for (auto& sym : members) {
            if (sym.name == word) {
                send_response(id, make_loc(sym.loc));
                return;
            }
        }
    }

    // Search function locals (all functions)
    for (auto& [fn, locals] : idx.fn_locals) {
        for (auto& sym : locals) {
            if (sym.name == word) {
                send_response(id, make_loc(sym.loc));
                return;
            }
        }
    }

    send_response(id, Json(nullptr));
}

// ============================================================================
// LSP handlers — hover
// ============================================================================

void LspServer::on_hover(const Json& id, const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    int line = (int)params["position"]["line"].int_or(0);
    int col  = (int)params["position"]["character"].int_or(0);

    std::string text = docs_.count(uri) ? docs_[uri] : "";
    std::string word = word_at(text, line, col);
    if (word.empty()) { send_response(id, Json(nullptr)); return; }

    if (!indexes_.count(uri)) indexes_[uri] = build_index(text);
    const SymIndex& idx = indexes_[uri];

    auto make_hover = [&](const std::string& markdown) {
        Json contents = Json::object();
        contents["kind"]  = "markdown";
        contents["value"] = markdown;
        Json result = Json::object();
        result["contents"] = std::move(contents);
        return result;
    };

    // Search globals
    for (auto& sym : idx.globals) {
        if (sym.name == word) {
            std::string md;
            md += "```zscript\n" + sym.detail + "\n```";
            if (!sym.type_str.empty())
                md += "\n\n**Type:** `" + sym.type_str + "`";
            if (!sym.return_type.empty())
                md += "\n\n**Returns:** `" + sym.return_type + "`";
            send_response(id, make_hover(md));
            return;
        }
    }

    // Search class members
    for (auto& [cls, members] : idx.members) {
        for (auto& sym : members) {
            if (sym.name == word) {
                std::string md;
                md += "```zscript\n" + sym.detail + "\n```";
                if (!sym.owner.empty())
                    md += "\n\n*Member of* `" + sym.owner + "`";
                if (!sym.return_type.empty())
                    md += "\n\n**Returns:** `" + sym.return_type + "`";
                send_response(id, make_hover(md));
                return;
            }
        }
    }

    // Search function locals / params
    for (auto& [fn, locals] : idx.fn_locals) {
        for (auto& sym : locals) {
            if (sym.name == word) {
                std::string md = "```zscript\n";
                if (sym.kind == SymInfo::Kind::Parameter)
                    md += "param ";
                md += sym.name;
                if (!sym.type_str.empty())
                    md += ": " + sym.type_str;
                md += "\n```";
                send_response(id, make_hover(md));
                return;
            }
        }
    }

    send_response(id, Json(nullptr));
}

// ============================================================================
// LSP handlers — signature help
// ============================================================================

void LspServer::on_signature_help(const Json& id, const Json& params) {
    std::string uri = params["textDocument"]["uri"].string_or("");
    int line = (int)params["position"]["line"].int_or(0);
    int col  = (int)params["position"]["character"].int_or(0);

    std::string text = docs_.count(uri) ? docs_[uri] : "";

    // Find the function name being called: scan backwards from cursor to find
    // the nearest '(' and the identifier before it.
    auto& src = text;
    // Convert (line, col) 0-based to byte offset
    int cur_line = 0, cur_col = 0;
    size_t cursor = 0;
    for (size_t i = 0; i < src.size(); ++i) {
        if (cur_line == line && cur_col == col) { cursor = i; break; }
        if (src[i] == '\n') { ++cur_line; cur_col = 0; }
        else                { ++cur_col; }
    }

    // Count active argument index by counting commas after the opening paren
    int active_param = 0;
    int depth = 0;
    size_t paren_pos = std::string::npos;
    if (cursor > 0) {
        for (int i = (int)cursor - 1; i >= 0; --i) {
            char c = src[(size_t)i];
            if (c == ')') { ++depth; }
            else if (c == '(') {
                if (depth == 0) { paren_pos = (size_t)i; break; }
                --depth;
            } else if (c == ',' && depth == 0) {
                ++active_param;
            }
        }
    }

    if (paren_pos == std::string::npos) {
        send_response(id, Json(nullptr));
        return;
    }

    // Extract function name before the paren
    auto is_word = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    size_t name_end = paren_pos;
    while (name_end > 0 && is_word(src[name_end - 1])) --name_end;
    std::string fn_name = src.substr(name_end, paren_pos - name_end);

    if (fn_name.empty()) { send_response(id, Json(nullptr)); return; }

    if (!indexes_.count(uri)) indexes_[uri] = build_index(text);
    const SymIndex& idx = indexes_[uri];

    // Find matching function/method in globals or members
    auto find_sym = [&](const std::string& name) -> const SymInfo* {
        for (auto& sym : idx.globals) {
            if (sym.name == name &&
                (sym.kind == SymInfo::Kind::Function || sym.kind == SymInfo::Kind::Method))
                return &sym;
        }
        for (auto& [cls, members] : idx.members) {
            for (auto& sym : members) {
                if (sym.name == name &&
                    (sym.kind == SymInfo::Kind::Function || sym.kind == SymInfo::Kind::Method))
                    return &sym;
            }
        }
        return nullptr;
    };

    const SymInfo* sym = find_sym(fn_name);
    if (!sym) { send_response(id, Json(nullptr)); return; }

    // Build signature help response
    Json param_list = Json::array();
    for (auto& p : sym->params) {
        Json pi = Json::object();
        std::string label = (p.is_mut ? "mut " : "") + p.name + (p.type.empty() ? "" : ": " + p.type);
        pi["label"] = label;
        param_list.push_back(std::move(pi));
    }

    Json sig = Json::object();
    sig["label"]          = sym->detail;
    sig["parameters"]     = std::move(param_list);

    Json sigs = Json::array();
    sigs.push_back(std::move(sig));

    Json result = Json::object();
    result["signatures"]     = std::move(sigs);
    result["activeSignature"] = 0;
    result["activeParameter"] = active_param;

    send_response(id, std::move(result));
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

    // Semantic pass: warn about undefined identifier references
    if (!indexes_.count(uri)) indexes_[uri] = build_index_from_prog(prog);
    check_undefined_symbols(prog, indexes_[uri], add);

    return diags;
}

// ============================================================================
// Symbol indexing
// ============================================================================

/*static*/ std::string LspServer::type_str(const TypeExpr* t) {
    if (!t) return "";
    if (auto* n = dynamic_cast<const NamedType*>(t))    return n->name;
    if (auto* n = dynamic_cast<const NullableType*>(t)) return type_str(n->inner.get()) + "?";
    if (auto* g = dynamic_cast<const GenericType*>(t)) {
        std::string s = g->name + "<";
        for (size_t i = 0; i < g->args.size(); ++i) {
            if (i) s += ", ";
            s += type_str(g->args[i].get());
        }
        return s + ">";
    }
    return "?";
}

/*static*/ std::string LspServer::make_signature(const std::string& name,
                                                   const std::vector<Param>& params,
                                                   const TypeExpr* ret) {
    std::string sig = "fn " + name + "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) sig += ", ";
        if (params[i].is_mut) sig += "mut ";
        sig += params[i].name;
        if (params[i].type) sig += ": " + type_str(params[i].type.get());
    }
    sig += ")";
    if (ret) sig += " -> " + type_str(ret);
    return sig;
}

SymIndex LspServer::build_index(const std::string& text) {
    Lexer lx(text, "<lsp>");
    auto toks = lx.tokenize();
    if (lx.has_errors()) return {};
    Parser pr(std::move(toks), "<lsp>");
    Program prog = pr.parse();
    return build_index_from_prog(prog);
}

SymIndex LspServer::build_index_from_prog(const Program& prog) {
    SymIndex idx;

    // Seed stdlib module members so dot-completion works on math.X, string.X, etc.
    auto seed = [&](const std::string& mod, SymInfo::Kind k,
                    std::initializer_list<std::pair<const char*, const char*>> members) {
        auto& list = idx.members[mod];
        for (auto& [name, detail] : members) {
            SymInfo s;
            s.kind   = k;
            s.name   = name;
            s.owner  = mod;
            s.detail = detail;
            list.push_back(s);
        }
    };
    seed("math", SymInfo::Kind::Method, {
        {"pi",     "Float — 3.14159…"},
        {"huge",   "Float — infinity"},
        {"inf",    "Float — infinity"},
        {"floor",  "fn floor(x: Float) -> Int"},
        {"ceil",   "fn ceil(x: Float) -> Int"},
        {"round",  "fn round(x: Float) -> Int"},
        {"abs",    "fn abs(x) -> Number"},
        {"sqrt",   "fn sqrt(x: Float) -> Float"},
        {"pow",    "fn pow(base, exp: Float) -> Float"},
        {"exp",    "fn exp(x: Float) -> Float"},
        {"log",    "fn log(x, base?: Float) -> Float"},
        {"log10",  "fn log10(x: Float) -> Float"},
        {"sin",    "fn sin(x: Float) -> Float"},
        {"cos",    "fn cos(x: Float) -> Float"},
        {"tan",    "fn tan(x: Float) -> Float"},
        {"asin",   "fn asin(x: Float) -> Float"},
        {"acos",   "fn acos(x: Float) -> Float"},
        {"atan",   "fn atan(x, y?: Float) -> Float"},
        {"clamp",  "fn clamp(v, lo, hi: Float) -> Float"},
        {"lerp",   "fn lerp(lo, hi, t: Float) -> Float"},
        {"sign",   "fn sign(x: Float) -> Int"},
        {"rad",    "fn rad(deg: Float) -> Float"},
        {"deg",    "fn deg(rad: Float) -> Float"},
        {"min",    "fn min(...) -> Float"},
        {"max",    "fn max(...) -> Float"},
        {"fmod",   "fn fmod(x, y: Float) -> Float"},
        {"is_nan", "fn is_nan(x: Float) -> Bool"},
        {"is_inf", "fn is_inf(x: Float) -> Bool"},
    });
    seed("string", SymInfo::Kind::Method, {
        {"len",         "fn len(s: String) -> Int"},
        {"sub",         "fn sub(s: String, start: Int, end?: Int) -> String"},
        {"upper",       "fn upper(s: String) -> String"},
        {"lower",       "fn lower(s: String) -> String"},
        {"trim",        "fn trim(s: String) -> String"},
        {"trim_start",  "fn trim_start(s: String) -> String"},
        {"trim_end",    "fn trim_end(s: String) -> String"},
        {"contains",    "fn contains(s, sub: String) -> Bool"},
        {"starts_with", "fn starts_with(s, prefix: String) -> Bool"},
        {"ends_with",   "fn ends_with(s, suffix: String) -> Bool"},
        {"find",        "fn find(s, pattern: String) -> Int"},
        {"replace",     "fn replace(s, from, to: String) -> String"},
        {"split",       "fn split(s, delim: String) -> Table"},
        {"join",        "fn join(tbl: Table, sep: String) -> String"},
        {"rep",         "fn rep(s: String, n: Int) -> String"},
        {"byte",        "fn byte(s: String, i: Int) -> Int"},
        {"char",        "fn char(code: Int) -> String"},
        {"format",      "fn format(fmt: String, ...) -> String"},
        {"is_empty",    "fn is_empty(s: String) -> Bool"},
        {"reverse",     "fn reverse(s: String) -> String"},
    });
    seed("table", SymInfo::Kind::Method, {
        {"len",      "fn len(t: Table) -> Int"},
        {"push",     "fn push(t: Table, val: Any) -> Nil"},
        {"pop",      "fn pop(t: Table) -> Any"},
        {"insert",   "fn insert(t: Table, idx: Int, val: Any) -> Nil"},
        {"remove",   "fn remove(t: Table, idx?: Int) -> Any"},
        {"sort",     "fn sort(t: Table) -> Nil"},
        {"copy",     "fn copy(t: Table) -> Table"},
        {"keys",     "fn keys(t: Table) -> Table"},
        {"values",   "fn values(t: Table) -> Table"},
        {"contains", "fn contains(t: Table, key: String) -> Bool"},
    });
    seed("io", SymInfo::Kind::Method, {
        {"read_file",   "fn read_file(path: String) -> String|Nil"},
        {"write_file",  "fn write_file(path: String, data: String) -> Bool"},
        {"append_file", "fn append_file(path: String, data: String) -> Bool"},
        {"read_line",   "fn read_line() -> String|Nil"},
        {"print_err",   "fn print_err(...) -> Nil"},
        {"exists",      "fn exists(path: String) -> Bool"},
    });

    for (auto& decl : prog.decls) {
        // Top-level function
        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            SymInfo sym;
            sym.kind        = SymInfo::Kind::Function;
            sym.name        = fn->name;
            sym.detail      = make_signature(fn->name, fn->params, fn->return_type.get());
            sym.return_type = type_str(fn->return_type.get());
            sym.loc         = fn->loc;
            for (auto& p : fn->params) {
                SymInfo::ParamInfo pi;
                pi.name   = p.name;
                pi.type   = type_str(p.type.get());
                pi.is_mut = p.is_mut;
                sym.params.push_back(pi);
            }
            idx.globals.push_back(sym);

            // Collect locals for this function
            std::vector<SymInfo> locals;
            // Add params as locals too
            for (auto& p : fn->params) {
                SymInfo psym;
                psym.kind     = SymInfo::Kind::Parameter;
                psym.name     = p.name;
                psym.type_str = type_str(p.type.get());
                psym.detail   = (p.is_mut ? "mut " : "") + p.name +
                                (psym.type_str.empty() ? "" : ": " + psym.type_str);
                psym.loc      = p.loc;
                locals.push_back(psym);
            }
            collect_fn_locals(*fn, locals);
            idx.fn_locals[fn->name] = std::move(locals);
            continue;
        }

        // Top-level class
        if (auto* cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            SymInfo sym;
            sym.kind   = SymInfo::Kind::Class;
            sym.name   = cls->name;
            sym.detail = "class " + cls->name +
                         (cls->base.has_value() ? " : " + cls->base.value() : "");
            sym.loc    = cls->loc;
            idx.globals.push_back(sym);

            std::vector<SymInfo> members;
            for (auto& m : cls->members) {
                if (auto* field = dynamic_cast<const FieldDecl*>(m.get())) {
                    SymInfo fsym;
                    fsym.kind     = SymInfo::Kind::Field;
                    fsym.name     = field->name;
                    fsym.owner    = cls->name;
                    fsym.type_str = type_str(field->type.get());
                    fsym.detail   = (field->is_let ? "let " : "var ") + field->name +
                                   (fsym.type_str.empty() ? "" : ": " + fsym.type_str);
                    fsym.loc      = field->loc;
                    members.push_back(fsym);
                }
                if (auto* mfn = dynamic_cast<const FnDecl*>(m.get())) {
                    SymInfo msym;
                    msym.kind        = SymInfo::Kind::Method;
                    msym.name        = mfn->name;
                    msym.owner       = cls->name;
                    msym.detail      = make_signature(mfn->name, mfn->params, mfn->return_type.get());
                    msym.return_type = type_str(mfn->return_type.get());
                    msym.loc         = mfn->loc;
                    for (auto& p : mfn->params) {
                        SymInfo::ParamInfo pi;
                        pi.name   = p.name;
                        pi.type   = type_str(p.type.get());
                        pi.is_mut = p.is_mut;
                        msym.params.push_back(pi);
                    }
                    members.push_back(msym);

                    // Collect locals for this method under "ClassName.methodName"
                    std::vector<SymInfo> locals;
                    for (auto& p : mfn->params) {
                        SymInfo psym;
                        psym.kind     = SymInfo::Kind::Parameter;
                        psym.name     = p.name;
                        psym.type_str = type_str(p.type.get());
                        psym.detail   = (p.is_mut ? "mut " : "") + p.name +
                                        (psym.type_str.empty() ? "" : ": " + psym.type_str);
                        psym.loc      = p.loc;
                        locals.push_back(psym);
                    }
                    collect_fn_locals(*mfn, locals);
                    idx.fn_locals[cls->name + "." + mfn->name] = std::move(locals);
                }
            }
            idx.members[cls->name] = std::move(members);
            continue;
        }

        // Top-level trait
        if (auto* tr = dynamic_cast<const TraitDecl*>(decl.get())) {
            SymInfo sym;
            sym.kind   = SymInfo::Kind::Trait;
            sym.name   = tr->name;
            sym.detail = "trait " + tr->name;
            sym.loc    = tr->loc;
            idx.globals.push_back(sym);

            std::vector<SymInfo> members;
            for (auto& m : tr->members) {
                if (auto* prop = dynamic_cast<const PropDecl*>(m.get())) {
                    SymInfo psym;
                    psym.kind     = SymInfo::Kind::Property;
                    psym.name     = prop->name;
                    psym.owner    = tr->name;
                    psym.type_str = type_str(prop->type.get());
                    psym.detail   = "prop " + prop->name +
                                   (psym.type_str.empty() ? "" : ": " + psym.type_str);
                    psym.loc      = prop->loc;
                    members.push_back(psym);
                }
                if (auto* mfn = dynamic_cast<const FnDecl*>(m.get())) {
                    SymInfo msym;
                    msym.kind        = SymInfo::Kind::Method;
                    msym.name        = mfn->name;
                    msym.owner       = tr->name;
                    msym.detail      = make_signature(mfn->name, mfn->params, mfn->return_type.get());
                    msym.return_type = type_str(mfn->return_type.get());
                    msym.loc         = mfn->loc;
                    for (auto& p : mfn->params) {
                        SymInfo::ParamInfo pi;
                        pi.name = p.name;
                        pi.type = type_str(p.type.get());
                        msym.params.push_back(pi);
                    }
                    members.push_back(msym);
                }
            }
            idx.members[tr->name] = std::move(members);
            continue;
        }

        // Top-level impl
        if (auto* impl = dynamic_cast<const ImplDecl*>(decl.get())) {
            std::vector<SymInfo> members;
            for (auto& m : impl->members) {
                if (auto* prop = dynamic_cast<const PropDecl*>(m.get())) {
                    SymInfo psym;
                    psym.kind   = SymInfo::Kind::Property;
                    psym.name   = prop->name;
                    psym.owner  = impl->for_type;
                    psym.detail = "prop " + prop->name;
                    psym.loc    = prop->loc;
                    members.push_back(psym);
                }
                if (auto* mfn = dynamic_cast<const FnDecl*>(m.get())) {
                    SymInfo msym;
                    msym.kind        = SymInfo::Kind::Method;
                    msym.name        = mfn->name;
                    msym.owner       = impl->for_type;
                    msym.detail      = make_signature(mfn->name, mfn->params, mfn->return_type.get());
                    msym.return_type = type_str(mfn->return_type.get());
                    msym.loc         = mfn->loc;
                    for (auto& p : mfn->params) {
                        SymInfo::ParamInfo pi;
                        pi.name = p.name;
                        pi.type = type_str(p.type.get());
                        msym.params.push_back(pi);
                    }
                    members.push_back(msym);
                }
            }
            // Merge into existing members for this type (class may already have some)
            auto& existing = idx.members[impl->for_type];
            existing.insert(existing.end(), members.begin(), members.end());
            continue;
        }

        // Top-level import
        if (auto* imp = dynamic_cast<const ImportDecl*>(decl.get())) {
            SymInfo sym;
            sym.kind   = SymInfo::Kind::Import;
            sym.name   = imp->path;
            sym.detail = "import " + imp->path;
            sym.loc    = imp->loc;
            idx.globals.push_back(sym);
            continue;
        }

        // Top-level field declaration: var/let at module scope → FieldDecl
        if (auto* fd = dynamic_cast<const FieldDecl*>(decl.get())) {
            SymInfo sym;
            sym.kind     = SymInfo::Kind::Variable;
            sym.name     = fd->name;
            sym.type_str = type_str(fd->type.get());
            sym.detail   = (fd->is_let ? "let " : "var ") + fd->name +
                           (sym.type_str.empty() ? "" : ": " + sym.type_str);
            sym.loc      = fd->loc;
            idx.globals.push_back(sym);
            continue;
        }

        // Top-level statement (var/let — fallback for StmtDecl wrapping)
        if (auto* sd = dynamic_cast<const StmtDecl*>(decl.get())) {
            if (auto* vd = dynamic_cast<const VarDeclStmt*>(sd->stmt.get())) {
                SymInfo sym;
                sym.kind     = SymInfo::Kind::Variable;
                sym.name     = vd->name;
                sym.type_str = type_str(vd->type.get());
                sym.detail   = (vd->is_let ? "let " : "var ") + vd->name +
                               (sym.type_str.empty() ? "" : ": " + sym.type_str);
                sym.loc      = vd->loc;
                idx.globals.push_back(sym);
            }
        }
    }

    return idx;
}

// ============================================================================
// Semantic analysis — undefined symbol checking
// ============================================================================

// Known stdlib builtins (registered by VM::open_stdlib)
static const std::unordered_set<std::string>& stdlib_builtins() {
    static const std::unordered_set<std::string> s = {
        // stdlib functions
        "print", "log", "tostring", "tonumber", "type", "assert", "max", "min",
        // math table
        "math",
        // literals / keywords that appear as identifiers
        "self", "true", "false", "nil",
    };
    return s;
}

// Forward declarations for mutual recursion
using ReportFn = std::function<void(int, int, int, const std::string&)>;
static void walk_expr_undef(const Expr* e,
    const std::unordered_set<std::string>& known, const ReportFn& report);
static void walk_stmts_undef(const StmtList& stmts,
    const std::unordered_set<std::string>& known, const ReportFn& report);

static void walk_stmt_undef(const Stmt* s,
    const std::unordered_set<std::string>& known, const ReportFn& report) {
    if (!s) return;
    if (auto* vd = dynamic_cast<const VarDeclStmt*>(s)) {
        if (vd->init) walk_expr_undef(vd->init.get(), known, report);
    } else if (auto* es = dynamic_cast<const ExprStmt*>(s)) {
        walk_expr_undef(es->expr.get(), known, report);
    } else if (auto* rs = dynamic_cast<const ReturnStmt*>(s)) {
        if (rs->value) walk_expr_undef(rs->value.get(), known, report);
    } else if (auto* ifs = dynamic_cast<const IfStmt*>(s)) {
        walk_expr_undef(ifs->cond.get(), known, report);
        walk_stmts_undef(ifs->then_block.stmts, known, report);
        if (ifs->else_clause) walk_stmt_undef(ifs->else_clause.get(), known, report);
    } else if (auto* blk = dynamic_cast<const BlockStmt*>(s)) {
        walk_stmts_undef(blk->block.stmts, known, report);
    } else if (auto* ws = dynamic_cast<const WhileStmt*>(s)) {
        walk_expr_undef(ws->cond.get(), known, report);
        walk_stmts_undef(ws->body.stmts, known, report);
    } else if (auto* fs = dynamic_cast<const ForStmt*>(s)) {
        walk_expr_undef(fs->iterable.get(), known, report);
        walk_stmts_undef(fs->body.stmts, known, report);
    } else if (auto* eb = dynamic_cast<const EngineBlock*>(s)) {
        walk_stmts_undef(eb->body.stmts, known, report);
    }
}

static void walk_stmts_undef(const StmtList& stmts,
    const std::unordered_set<std::string>& known, const ReportFn& report) {
    for (auto& s : stmts) walk_stmt_undef(s.get(), known, report);
}

static void walk_expr_undef(const Expr* e,
    const std::unordered_set<std::string>& known, const ReportFn& report) {
    if (!e) return;
    if (auto* id = dynamic_cast<const IdentExpr*>(e)) {
        if (!known.count(id->name)) {
            report(id->loc.line, id->loc.column,
                   id->loc.column + (int)id->name.size(), id->name);
        }
        return;
    }
    if (auto* be = dynamic_cast<const BinaryExpr*>(e)) {
        walk_expr_undef(be->left.get(), known, report);
        walk_expr_undef(be->right.get(), known, report);
        return;
    }
    if (auto* ue = dynamic_cast<const UnaryExpr*>(e)) {
        walk_expr_undef(ue->operand.get(), known, report);
        return;
    }
    if (auto* ae = dynamic_cast<const AssignExpr*>(e)) {
        walk_expr_undef(ae->target.get(), known, report);
        walk_expr_undef(ae->value.get(), known, report);
        return;
    }
    if (auto* fe = dynamic_cast<const FieldExpr*>(e)) {
        // Only check the object; field name is a member reference resolved at runtime
        walk_expr_undef(fe->object.get(), known, report);
        return;
    }
    if (auto* ce = dynamic_cast<const CallExpr*>(e)) {
        walk_expr_undef(ce->callee.get(), known, report);
        for (auto& arg : ce->args) walk_expr_undef(arg.get(), known, report);
        return;
    }
    if (auto* ie = dynamic_cast<const IndexExpr*>(e)) {
        walk_expr_undef(ie->object.get(), known, report);
        walk_expr_undef(ie->index.get(), known, report);
        return;
    }
    if (auto* ge = dynamic_cast<const GroupExpr*>(e)) {
        walk_expr_undef(ge->inner.get(), known, report);
        return;
    }
    if (auto* si = dynamic_cast<const StringInterpExpr*>(e)) {
        for (auto& p : si->parts) walk_expr_undef(p.get(), known, report);
        return;
    }
    // LambdaExpr: skip — captures from outer scope are hard to track statically
    // LitExpr, SelfExpr: nothing to check
}

void LspServer::check_undefined_symbols(
    const Program& prog, const SymIndex& idx,
    std::function<void(int, int, int, int, const std::string&)> add_diag)
{
    const auto& builtins = stdlib_builtins();

    // Build the global known-name set
    std::unordered_set<std::string> global_known(builtins.begin(), builtins.end());
    for (auto& sym : idx.globals) global_known.insert(sym.name);
    for (auto& [cls, _] : idx.members) global_known.insert(cls);

    for (auto& decl : prog.decls) {
        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            // Per-function known set = globals + this function's params/locals
            auto known = global_known;
            auto it = idx.fn_locals.find(fn->name);
            if (it != idx.fn_locals.end())
                for (auto& sym : it->second) known.insert(sym.name);

            ReportFn report = [&](int line, int col, int end_col,
                                  const std::string& name) {
                add_diag(line, col, end_col, 2,
                         "undefined symbol: '" + name + "'");
            };
            walk_stmts_undef(fn->body.stmts, known, report);
            continue;
        }

        if (auto* cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            // Add all class members to the class-level known set
            auto cls_known = global_known;
            cls_known.insert("self");
            auto mit = idx.members.find(cls->name);
            if (mit != idx.members.end())
                for (auto& sym : mit->second) cls_known.insert(sym.name);

            for (auto& m : cls->members) {
                if (auto* mfn = dynamic_cast<const FnDecl*>(m.get())) {
                    auto known = cls_known;
                    auto it = idx.fn_locals.find(cls->name + "." + mfn->name);
                    if (it != idx.fn_locals.end())
                        for (auto& sym : it->second) known.insert(sym.name);

                    ReportFn report = [&](int line, int col, int end_col,
                                          const std::string& name) {
                        add_diag(line, col, end_col, 2,
                                 "undefined symbol: '" + name + "'");
                    };
                    walk_stmts_undef(mfn->body.stmts, known, report);
                }
            }
        }
    }
}

void LspServer::collect_fn_locals(const FnDecl& fn, std::vector<SymInfo>& out) {
    // Walk the body's statements for VarDeclStmt
    std::function<void(const StmtList&)> walk_stmts = [&](const StmtList& stmts) {
        for (auto& s : stmts) {
            if (auto* vd = dynamic_cast<const VarDeclStmt*>(s.get())) {
                SymInfo sym;
                sym.kind     = SymInfo::Kind::Variable;
                sym.name     = vd->name;
                sym.type_str = type_str(vd->type.get());
                sym.detail   = (vd->is_let ? "let " : "var ") + vd->name +
                               (sym.type_str.empty() ? "" : ": " + sym.type_str);
                sym.loc      = vd->loc;
                out.push_back(sym);
            } else if (auto* ifs = dynamic_cast<const IfStmt*>(s.get())) {
                walk_stmts(ifs->then_block.stmts);
                if (ifs->else_clause) {
                    if (auto* blk = dynamic_cast<const BlockStmt*>(ifs->else_clause.get()))
                        walk_stmts(blk->block.stmts);
                    else if (auto* elif = dynamic_cast<const IfStmt*>(ifs->else_clause.get()))
                        walk_stmts(elif->then_block.stmts);
                }
            } else if (auto* ws = dynamic_cast<const WhileStmt*>(s.get())) {
                walk_stmts(ws->body.stmts);
            } else if (auto* fs = dynamic_cast<const ForStmt*>(s.get())) {
                // The loop variable
                SymInfo sym;
                sym.kind   = SymInfo::Kind::Variable;
                sym.name   = fs->var_name;
                sym.detail = (fs->binding_is_let ? "let " : "var ") + fs->var_name;
                sym.loc    = fs->loc;
                out.push_back(sym);
                walk_stmts(fs->body.stmts);
            } else if (auto* eb = dynamic_cast<const EngineBlock*>(s.get())) {
                walk_stmts(eb->body.stmts);
            }
        }
    };
    walk_stmts(fn.body.stmts);
}

// ============================================================================
// Position helpers
// ============================================================================

std::string LspServer::fn_at_position(const Program& prog, int line, int col) {
    // Find the innermost function whose loc contains the cursor
    // (Simple: check if cursor line is between fn start and body end)
    // We use block start/end lines as a proxy.
    std::string best;
    for (auto& decl : prog.decls) {
        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            int fn_line = (int)fn->loc.line - 1; // 0-based
            // Rough heuristic: fn starts at fn_line and ends somewhere after
            // We check if cursor is at or after the fn start and the fn has stmts
            if (line >= fn_line) {
                // Check if cursor is within the body
                if (!fn->body.stmts.empty()) {
                    int last_stmt_line = (int)fn->body.stmts.back()->loc.line - 1;
                    if (line <= last_stmt_line + 1) {
                        best = fn->name;
                    }
                } else {
                    // Empty body: just check if on the same line
                    if (line == fn_line) best = fn->name;
                }
            }
        }
        if (auto* cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            for (auto& m : cls->members) {
                if (auto* mfn = dynamic_cast<const FnDecl*>(m.get())) {
                    int fn_line = (int)mfn->loc.line - 1;
                    if (line >= fn_line) {
                        if (!mfn->body.stmts.empty()) {
                            int last = (int)mfn->body.stmts.back()->loc.line - 1;
                            if (line <= last + 1)
                                best = cls->name + "." + mfn->name;
                        }
                    }
                }
            }
        }
    }
    return best;
}

std::string LspServer::class_at_position(const Program& prog, int line, int /*col*/) {
    for (auto& decl : prog.decls) {
        if (auto* cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            int cls_line = (int)cls->loc.line - 1;
            if (line >= cls_line) {
                // Find the last member line
                int last_line = cls_line;
                for (auto& m : cls->members) {
                    if (!m) continue;
                    int ml = (int)m->loc.line - 1;
                    if (auto* mfn = dynamic_cast<const FnDecl*>(m.get())) {
                        if (!mfn->body.stmts.empty())
                            ml = (int)mfn->body.stmts.back()->loc.line - 1;
                    }
                    if (ml > last_line) last_line = ml;
                }
                if (line <= last_line + 1) return cls->name;
            }
        }
    }
    return "";
}

// ============================================================================
// Text helpers
// ============================================================================

std::string LspServer::word_at(const std::string& text, int line, int col) {
    int cur_line = 0, cur_col = 0;
    size_t offset = 0;
    bool found = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (cur_line == line && cur_col == col) { offset = i; found = true; break; }
        if (text[i] == '\n') { ++cur_line; cur_col = 0; }
        else                 { ++cur_col; }
    }
    if (!found) return "";

    auto is_word = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };

    size_t start = offset;
    while (start > 0 && is_word(text[start - 1])) --start;
    size_t end = offset;
    while (end < text.size() && is_word(text[end])) ++end;

    return text.substr(start, end - start);
}

bool LspServer::is_after_dot(const std::string& text, int line, int col) {
    int cur_line = 0, cur_col = 0;
    size_t offset = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (cur_line == line && cur_col == col) { offset = i; break; }
        if (text[i] == '\n') { ++cur_line; cur_col = 0; }
        else                 { ++cur_col; }
    }

    // Skip the current word backwards
    auto is_word = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    while (offset > 0 && is_word(text[offset - 1])) --offset;

    return (offset > 0 && text[offset - 1] == '.');
}

std::string LspServer::word_before_dot(const std::string& text, int line, int col) {
    int cur_line = 0, cur_col = 0;
    size_t offset = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (cur_line == line && cur_col == col) { offset = i; break; }
        if (text[i] == '\n') { ++cur_line; cur_col = 0; }
        else                 { ++cur_col; }
    }

    auto is_word = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };

    // Skip current partial word
    while (offset > 0 && is_word(text[offset - 1])) --offset;

    // Should be on the dot
    if (offset == 0 || text[offset - 1] != '.') return "";
    --offset; // skip dot

    size_t end = offset;
    while (offset > 0 && is_word(text[offset - 1])) --offset;
    return text.substr(offset, end - offset);
}

/*static*/ int LspServer::lsp_completion_kind(SymInfo::Kind k) {
    switch (k) {
        case SymInfo::Kind::Function:  return 3;  // Function
        case SymInfo::Kind::Method:    return 2;  // Method
        case SymInfo::Kind::Class:     return 7;  // Class
        case SymInfo::Kind::Trait:     return 8;  // Interface
        case SymInfo::Kind::Variable:  return 6;  // Variable
        case SymInfo::Kind::Parameter: return 6;  // Variable
        case SymInfo::Kind::Field:     return 5;  // Field
        case SymInfo::Kind::Property:  return 10; // Property
        case SymInfo::Kind::Import:    return 9;  // Module
    }
    return 6;
}

} // namespace zscript
