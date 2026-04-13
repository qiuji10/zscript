#pragma once
// ---------------------------------------------------------------------------
// AST pretty-printer
//
// Usage:
//   #include "ast_dump.h"
//   dump_ast(std::cout, program);      // whole program
//   dump_expr(std::cout, *expr, 0);    // single expression
//   dump_stmt(std::cout, *stmt, 0);    // single statement
// ---------------------------------------------------------------------------
#include "ast.h"
#include "token.h"
#include <ostream>
#include <string>

namespace zscript {

// Forward declarations (all defined below)
inline void dump_expr (std::ostream& out, const Expr*    e, int depth);
inline void dump_stmt (std::ostream& out, const Stmt*    s, int depth);
inline void dump_decl (std::ostream& out, const Decl*    d, int depth);
inline void dump_block(std::ostream& out, const Block&   b, int depth);
inline void dump_ast  (std::ostream& out, const Program& p);

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace ast_dump_detail {

inline std::string ind(int depth) { return std::string(depth * 2, ' '); }

inline void dump_type(std::ostream& out, const TypeExpr* t) {
    if (!t) { out << "<?>"; return; }
    if (auto* n = dynamic_cast<const NamedType*>(t))    { out << n->name; return; }
    if (auto* n = dynamic_cast<const NullableType*>(t)) { dump_type(out, n->inner.get()); out << '?'; return; }
    if (auto* g = dynamic_cast<const GenericType*>(t))  {
        out << g->name << '<';
        for (size_t i = 0; i < g->args.size(); ++i) {
            if (i) out << ", ";
            dump_type(out, g->args[i].get());
        }
        out << '>';
        return;
    }
    out << "<type>";
}

inline void dump_params(std::ostream& out, const std::vector<Param>& params, int depth) {
    for (auto& p : params) {
        out << ind(depth) << "Param";
        if (p.is_mut)    out << " mut";
        if (p.is_vararg) out << " ...";
        out << " '" << p.name << "'";
        if (p.type) { out << ": "; dump_type(out, p.type.get()); }
        out << '\n';
        if (p.default_val) {
            out << ind(depth + 1) << "default:\n";
            dump_expr(out, p.default_val.get(), depth + 2);
        }
    }
}

} // namespace ast_dump_detail

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
inline void dump_expr(std::ostream& out, const Expr* e, int depth) {
    using namespace ast_dump_detail;
    if (!e) { out << ind(depth) << "<null-expr>\n"; return; }

    if (auto* x = dynamic_cast<const LitExpr*>(e)) {
        const char* kind = "Lit";
        switch (x->kind) {
            case LitExpr::Kind::Int:    kind = "Int";    break;
            case LitExpr::Kind::Float:  kind = "Float";  break;
            case LitExpr::Kind::String: kind = "String"; break;
            case LitExpr::Kind::True:   kind = "true";   break;
            case LitExpr::Kind::False:  kind = "false";  break;
            case LitExpr::Kind::Nil:    kind = "nil";    break;
        }
        out << ind(depth) << "Lit(" << kind;
        if (x->kind == LitExpr::Kind::Int || x->kind == LitExpr::Kind::Float ||
            x->kind == LitExpr::Kind::String)
            out << " \"" << x->value << '"';
        out << ")\n";
        return;
    }
    if (auto* x = dynamic_cast<const StringInterpExpr*>(e)) {
        out << ind(depth) << "StringInterp\n";
        for (auto& p : x->parts) dump_expr(out, p.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const IdentExpr*>(e)) {
        out << ind(depth) << "Ident '" << x->name << "'\n";
        return;
    }
    if (dynamic_cast<const SelfExpr*>(e)) {
        out << ind(depth) << "self\n";
        return;
    }
    if (auto* x = dynamic_cast<const BinaryExpr*>(e)) {
        out << ind(depth) << "Binary " << token_kind_name(x->op) << '\n';
        dump_expr(out, x->left.get(),  depth + 1);
        dump_expr(out, x->right.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const UnaryExpr*>(e)) {
        out << ind(depth) << "Unary " << token_kind_name(x->op) << '\n';
        dump_expr(out, x->operand.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const AssignExpr*>(e)) {
        out << ind(depth) << "Assign " << token_kind_name(x->op) << '\n';
        dump_expr(out, x->target.get(), depth + 1);
        dump_expr(out, x->value.get(),  depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const FieldExpr*>(e)) {
        const char* acc = x->access == FieldExpr::Access::Safe  ? "?."
                        : x->access == FieldExpr::Access::Force ? "!." : ".";
        out << ind(depth) << "Field " << acc << "'" << x->field << "'\n";
        dump_expr(out, x->object.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const CallExpr*>(e)) {
        out << ind(depth) << "Call";
        if (!x->type_args.empty()) {
            out << '<';
            for (size_t i = 0; i < x->type_args.size(); ++i) {
                if (i) out << ", ";
                ast_dump_detail::dump_type(out, x->type_args[i].get());
            }
            out << '>';
        }
        out << '\n';
        dump_expr(out, x->callee.get(), depth + 1);
        for (auto& a : x->args)       dump_expr(out, a.get(), depth + 1);
        for (auto& n : x->named_args) {
            out << ind(depth + 1) << "NamedArg '" << n.name << "':\n";
            dump_expr(out, n.value.get(), depth + 2);
        }
        return;
    }
    if (auto* x = dynamic_cast<const IndexExpr*>(e)) {
        const char* acc = x->access == IndexExpr::Access::Safe  ? "?."
                        : x->access == IndexExpr::Access::Force ? "!." : "";
        out << ind(depth) << "Index" << acc << '\n';
        dump_expr(out, x->object.get(), depth + 1);
        dump_expr(out, x->index.get(),  depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const LambdaExpr*>(e)) {
        out << ind(depth) << "Lambda";
        if (x->return_type) { out << " -> "; ast_dump_detail::dump_type(out, x->return_type.get()); }
        out << '\n';
        ast_dump_detail::dump_params(out, x->params, depth + 1);
        dump_block(out, x->body, depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const GroupExpr*>(e)) {
        out << ind(depth) << "Group\n";
        dump_expr(out, x->inner.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const IfExpr*>(e)) {
        out << ind(depth) << "IfExpr\n";
        out << ind(depth + 1) << "cond:\n";
        dump_expr(out, x->cond.get(), depth + 2);
        out << ind(depth + 1) << "then:\n";
        dump_block(out, x->then_block, depth + 2);
        out << ind(depth + 1) << "else:\n";
        dump_block(out, x->else_block, depth + 2);
        return;
    }
    if (auto* x = dynamic_cast<const ArrayExpr*>(e)) {
        out << ind(depth) << "Array[" << x->elements.size() << "]\n";
        for (auto& el : x->elements) dump_expr(out, el.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const TableExpr*>(e)) {
        out << ind(depth) << "Table{" << x->fields.size() << "}\n";
        for (auto& f : x->fields) {
            out << ind(depth + 1) << "'" << f.key << "':\n";
            dump_expr(out, f.value.get(), depth + 2);
        }
        return;
    }
    out << ind(depth) << "<unknown-expr>\n";
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
inline void dump_stmt(std::ostream& out, const Stmt* s, int depth) {
    using namespace ast_dump_detail;
    if (!s) { out << ind(depth) << "<null-stmt>\n"; return; }

    if (auto* x = dynamic_cast<const ExprStmt*>(s)) {
        out << ind(depth) << "ExprStmt\n";
        dump_expr(out, x->expr.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const VarDeclStmt*>(s)) {
        out << ind(depth) << (x->is_let ? "let" : "var") << " '" << x->name << "'";
        if (x->type) { out << ": "; dump_type(out, x->type.get()); }
        out << '\n';
        if (x->init) dump_expr(out, x->init.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const MultiVarDeclStmt*>(s)) {
        out << ind(depth) << "MultiVar [";
        for (size_t i = 0; i < x->names.size(); ++i) {
            if (i) out << ", ";
            out << (x->is_let[i] ? "let " : "var ") << x->names[i];
        }
        out << "]\n";
        dump_expr(out, x->init.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const DestructureStmt*>(s)) {
        out << ind(depth) << (x->is_let ? "let" : "var") << " destructure "
            << (x->kind == DestructureStmt::Kind::Array ? "[]" : "{}") << '\n';
        for (auto& b : x->bindings) {
            out << ind(depth + 1) << '\'' << b.name << '\'';
            if (!b.key.empty() && b.key != b.name) out << " <- '" << b.key << "'";
            if (b.is_rest) out << " (rest)";
            out << '\n';
        }
        dump_expr(out, x->init.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const ReturnStmt*>(s)) {
        out << ind(depth) << "return\n";
        for (auto& v : x->values) dump_expr(out, v.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const IfStmt*>(s)) {
        out << ind(depth) << "if\n";
        out << ind(depth + 1) << "cond:\n";
        dump_expr(out, x->cond.get(), depth + 2);
        out << ind(depth + 1) << "then:\n";
        dump_block(out, x->then_block, depth + 2);
        if (x->else_clause) {
            out << ind(depth + 1) << "else:\n";
            dump_stmt(out, x->else_clause.get(), depth + 2);
        }
        return;
    }
    if (auto* x = dynamic_cast<const BlockStmt*>(s)) {
        dump_block(out, x->block, depth);
        return;
    }
    if (auto* x = dynamic_cast<const WhileStmt*>(s)) {
        out << ind(depth) << "while\n";
        out << ind(depth + 1) << "cond:\n";
        dump_expr(out, x->cond.get(), depth + 2);
        dump_block(out, x->body, depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const ForStmt*>(s)) {
        out << ind(depth) << "for " << (x->binding_is_let ? "let" : "var");
        if (!x->key_name.empty()) out << " '" << x->var_name << "', '" << x->key_name << "'";
        else                      out << " '" << x->var_name << "'";
        out << " in\n";
        dump_expr(out, x->iterable.get(), depth + 1);
        dump_block(out, x->body, depth + 1);
        return;
    }
    if (dynamic_cast<const BreakStmt*>(s))    { out << ind(depth) << "break\n";    return; }
    if (dynamic_cast<const ContinueStmt*>(s)) { out << ind(depth) << "continue\n"; return; }
    if (auto* x = dynamic_cast<const ThrowStmt*>(s)) {
        out << ind(depth) << "throw\n";
        dump_expr(out, x->value.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const TryCatchStmt*>(s)) {
        out << ind(depth) << "try\n";
        dump_block(out, x->try_block, depth + 1);
        out << ind(depth) << "catch '" << x->catch_var << "'\n";
        dump_block(out, x->catch_block, depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const EngineBlock*>(s)) {
        out << ind(depth) << "@" << x->engine << "\n";
        dump_block(out, x->body, depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const MatchStmt*>(s)) {
        out << ind(depth) << "match\n";
        dump_expr(out, x->subject.get(), depth + 1);
        for (auto& arm : x->arms) {
            if (arm.is_wild) out << ind(depth + 1) << "_ =>\n";
            else { out << ind(depth + 1) << "pattern:\n"; dump_expr(out, arm.pattern.get(), depth + 2); }
            dump_stmt(out, arm.body.get(), depth + 2);
        }
        return;
    }
    out << ind(depth) << "<unknown-stmt>\n";
}

// ---------------------------------------------------------------------------
// Block
// ---------------------------------------------------------------------------
inline void dump_block(std::ostream& out, const Block& b, int depth) {
    for (auto& s : b.stmts) dump_stmt(out, s.get(), depth);
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------
inline void dump_decl(std::ostream& out, const Decl* d, int depth) {
    using namespace ast_dump_detail;
    if (!d) { out << ind(depth) << "<null-decl>\n"; return; }

    // Annotations
    for (auto& ann : d->annotations) {
        out << ind(depth) << '@';
        for (size_t i = 0; i < ann.path.size(); ++i) {
            if (i) out << '.';
            out << ann.path[i];
        }
        out << '\n';
    }

    if (auto* x = dynamic_cast<const FnDecl*>(d)) {
        out << ind(depth) << "fn '" << x->name << "'";
        if (!x->type_params.empty()) {
            out << '<';
            for (size_t i = 0; i < x->type_params.size(); ++i) {
                if (i) out << ", ";
                out << x->type_params[i];
            }
            out << '>';
        }
        if (x->is_static) out << " static";
        if (x->return_type) { out << " -> "; dump_type(out, x->return_type.get()); }
        out << '\n';
        dump_params(out, x->params, depth + 1);
        dump_block(out, x->body, depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const FieldDecl*>(d)) {
        out << ind(depth) << (x->is_let ? "let" : "var");
        if (x->is_static) out << " static";
        out << " '" << x->name << "'";
        if (x->type) { out << ": "; dump_type(out, x->type.get()); }
        out << '\n';
        if (x->init) dump_expr(out, x->init.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const PropDecl*>(d)) {
        out << ind(depth) << "prop '" << x->name << "'\n";
        for (auto& acc : x->accessors) {
            out << ind(depth + 1) << acc.kind;
            if (!acc.param.empty()) out << '(' << acc.param << ')';
            out << '\n';
            if (acc.body_expr) dump_expr(out, acc.body_expr.get(), depth + 2);
        }
        return;
    }
    if (auto* x = dynamic_cast<const ClassDecl*>(d)) {
        out << ind(depth) << "class '" << x->name << "'";
        if (x->base) out << " : '" << *x->base << "'";
        out << '\n';
        for (auto& m : x->members) dump_decl(out, m.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const TraitDecl*>(d)) {
        out << ind(depth) << "trait '" << x->name << "'\n";
        for (auto& m : x->members) dump_decl(out, m.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const ImplDecl*>(d)) {
        out << ind(depth) << "impl";
        if (x->trait_name) out << " '" << *x->trait_name << "' for";
        out << " '" << x->for_type << "'\n";
        for (auto& m : x->members) dump_decl(out, m.get(), depth + 1);
        return;
    }
    if (auto* x = dynamic_cast<const ImportDecl*>(d)) {
        out << ind(depth) << "import '" << x->path << "'";
        if (!x->alias.empty()) out << " as '" << x->alias << "'";
        out << '\n';
        return;
    }
    if (auto* x = dynamic_cast<const EnumDecl*>(d)) {
        out << ind(depth) << "enum '" << x->name << "'\n";
        for (auto& v : x->variants) {
            out << ind(depth + 1) << v.name;
            if (v.value) out << " = " << *v.value;
            out << '\n';
        }
        return;
    }
    if (auto* x = dynamic_cast<const StmtDecl*>(d)) {
        dump_stmt(out, x->stmt.get(), depth);
        return;
    }
    out << ind(depth) << "<unknown-decl>\n";
}

// ---------------------------------------------------------------------------
// Program root
// ---------------------------------------------------------------------------
inline void dump_ast(std::ostream& out, const Program& prog) {
    out << "Program\n";
    for (auto& d : prog.decls) dump_decl(out, d.get(), 1);
}

} // namespace zscript
