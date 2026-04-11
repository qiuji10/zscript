#pragma once
#include "ast.h"
#include "json.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zscript {

// ---------------------------------------------------------------------------
// Symbol information collected from the AST
// ---------------------------------------------------------------------------
struct SymInfo {
    enum class Kind {
        Function,   // fn foo(...)
        Method,     // method inside a class/impl
        Class,      // class Foo
        Trait,      // trait Movable
        Variable,   // var / let at top level or local scope
        Parameter,  // function parameter
        Field,      // class field (var/let)
        Property,   // prop inside trait/impl
        Import,     // imported module
    };

    Kind        kind;
    std::string name;
    std::string owner;       // for Method/Field/Property: the owning class/trait name
    std::string type_str;    // human-readable type annotation, or ""
    std::string return_type; // for Function/Method
    std::string detail;      // full signature string for hover/completion detail
    SourceLoc   loc;

    // Parameter list for signature help (only for Function/Method)
    struct ParamInfo {
        std::string name;
        std::string type;
        bool        is_mut = false;
    };
    std::vector<ParamInfo> params;
};

// ---------------------------------------------------------------------------
// Symbol index — built from one parsed Program
// ---------------------------------------------------------------------------
struct SymIndex {
    std::vector<SymInfo> globals;       // all top-level symbols
    // class/trait name → member symbols
    std::unordered_map<std::string, std::vector<SymInfo>> members;
    // function name → local symbols (params + body locals)
    std::unordered_map<std::string, std::vector<SymInfo>> fn_locals;
    // variable/param name → inferred class type (for dot-completion)
    // Keyed by "varname" for globals, "FnName::varname" for function locals.
    std::unordered_map<std::string, std::string> var_types;
};

// ---------------------------------------------------------------------------
// LSP server — reads JSON-RPC from stdin, writes to stdout.
// Launch via: zsc lsp
// ---------------------------------------------------------------------------
class LspServer {
public:
    void run();

private:
    // ── JSON-RPC transport ────────────────────────────────────────────────
    bool read_message(Json& out);
    void send_message(const Json& msg);
    void send_response(const Json& id, Json result);
    void send_error(const Json& id, int code, const std::string& msg);
    void send_notification(const std::string& method, Json params);

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
    void on_signature_help(const Json& id, const Json& params);

    // ── diagnostics ──────────────────────────────────────────────────────
    void publish_diagnostics(const std::string& uri, const std::string& text);
    Json compute_diagnostics(const std::string& uri, const std::string& text);

    // ── symbol analysis ──────────────────────────────────────────────────
    // Build a full symbol index from source text
    SymIndex build_index(const std::string& text);
    // Build index from an already-parsed Program
    SymIndex build_index_from_prog(const Program& prog);

    // Collect local symbols (params + var/let decls) from a function body.
    // fn_key is used to scope var_types entries: "FnName" or "ClassName.methodName".
    void collect_fn_locals(const FnDecl& fn, std::vector<SymInfo>& out,
                           const std::string& fn_key, SymIndex& idx);

    // Semantic pass: emit diagnostics for references to undeclared identifiers
    void check_undefined_symbols(
        const Program& prog, const SymIndex& idx,
        std::function<void(int, int, int, int, const std::string&)> add_diag);

    // Find which function the given (line, col) position is inside, or ""
    std::string fn_at_position(const Program& prog, int line, int col);

    // Find which class the position is inside, or ""
    std::string class_at_position(const Program& prog, int line, int col);

    // ── helpers ───────────────────────────────────────────────────────────
    // word at (line, col) 0-based LSP position
    std::string word_at(const std::string& text, int line, int col);
    // word immediately before (line, col), stopping at non-word chars
    std::string word_before_dot(const std::string& text, int line, int col);
    // Returns true if the cursor is preceded by a dot access expr  (e.g. "foo.")
    bool is_after_dot(const std::string& text, int line, int col);

    // Convert SymInfo::Kind to LSP CompletionItemKind integer
    static int lsp_completion_kind(SymInfo::Kind k);

    // Type expression → human-readable string
    static std::string type_str(const TypeExpr* t);
    // Build signature string from params + return type
    static std::string make_signature(const std::string& name,
                                      const std::vector<Param>& params,
                                      const TypeExpr* ret);
    // Infer a type name from an initializer expression (returns "" if unknown)
    // e.g.  Stack()  → "Stack",   math.Vec2()  → "Vec2",  42 → ""
    static std::string infer_type(const Expr* init);

    // ── state ─────────────────────────────────────────────────────────────
    bool shutdown_requested_ = false;
    std::unordered_map<std::string, std::string> docs_;      // uri → text
    std::unordered_map<std::string, SymIndex>    indexes_;   // uri → index
};

} // namespace zscript
