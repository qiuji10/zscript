#include "parser.h"
#include <cassert>
#include <stdexcept>

namespace zscript {

// ===========================================================================
// Constructor
// ===========================================================================
Parser::Parser(std::vector<Token> tokens, std::string filename)
    : tokens_(std::move(tokens)), filename_(std::move(filename)) {
    // Ensure there is always an Eof sentinel at the end.
    if (tokens_.empty() || tokens_.back().kind != TokenKind::Eof) {
        Token eof;
        eof.kind = TokenKind::Eof;
        tokens_.push_back(eof);
    }
}

// ===========================================================================
// Token navigation
// ===========================================================================
const Token& Parser::peek(int ahead) const {
    size_t i = pos_ + (size_t)ahead;
    if (i >= tokens_.size()) return tokens_.back(); // Eof
    return tokens_[i];
}

const Token& Parser::advance() {
    const Token& t = tokens_[pos_];
    if (t.kind != TokenKind::Eof) ++pos_;
    return t;
}

bool Parser::check(TokenKind k) const {
    return peek().kind == k;
}

// Check if current token is an Ident with a specific spelling (contextual kw)
bool Parser::check_ident(const char* name) const {
    return peek().kind == TokenKind::Ident && peek().lexeme == name;
}

bool Parser::match(TokenKind k) {
    if (!check(k)) return false;
    advance();
    return true;
}

const Token& Parser::expect(TokenKind k, const char* msg) {
    if (!check(k)) {
        record_error(msg);
        // Return the current token anyway so callers can inspect location;
        // we do NOT advance, allowing the recovery logic above to try again.
        return peek();
    }
    return advance();
}

SourceLoc Parser::cur_loc() const {
    return peek().loc;
}

// ===========================================================================
// Error handling
// ===========================================================================
ParseError Parser::make_error(const char* msg) {
    return { cur_loc(), std::string(msg) + " (got '" + std::string(token_kind_name(peek().kind)) + "')" };
}

void Parser::record_error(const char* msg) {
    errors_.push_back(make_error(msg));
}

// Skip to the next safe resync point.
void Parser::synchronize() {
    while (!check(TokenKind::Eof)) {
        switch (peek().kind) {
            case TokenKind::RBrace:
                advance(); // consume the closing brace
                return;
            case TokenKind::KwFn:
            case TokenKind::KwClass:
            case TokenKind::KwTrait:
            case TokenKind::KwImpl:
            case TokenKind::KwLet:
            case TokenKind::KwVar:
            case TokenKind::KwReturn:
            case TokenKind::KwIf:
            case TokenKind::KwWhile:
            case TokenKind::KwFor:
            case TokenKind::KwImport:
            case TokenKind::KwBreak:
            case TokenKind::KwContinue:
            case TokenKind::KwMatch:
                return;
            default:
                advance();
        }
    }
}

// ===========================================================================
// Public entry point
// ===========================================================================
Program Parser::parse() {
    Program prog;
    while (!check(TokenKind::Eof)) {
        try {
            // Engine block at top level: @unity { } / @unreal { }
            // Detect before parse_annotations() consumes the tokens.
            if (check(TokenKind::At) &&
                peek(1).kind == TokenKind::Ident &&
                peek(2).kind == TokenKind::LBrace) {
                auto node = std::make_unique<StmtDecl>();
                node->loc = cur_loc();
                node->stmt = parse_engine_block_stmt();
                prog.decls.push_back(std::move(node));
                continue;
            }
            auto annots = parse_annotations();
            if (check(TokenKind::Eof)) break;
            prog.decls.push_back(parse_decl(std::move(annots)));
        } catch (...) {
            synchronize();
        }
    }
    return prog;
}

// ===========================================================================
// Annotations  (@path.path)
// ===========================================================================
std::vector<Annotation> Parser::parse_annotations() {
    std::vector<Annotation> result;
    while (check(TokenKind::At)) {
        Annotation ann;
        ann.loc = cur_loc();
        advance(); // consume '@'
        const Token& id = expect(TokenKind::Ident, "expected identifier after '@'");
        ann.path.push_back(id.lexeme);
        while (match(TokenKind::Dot)) {
            // Could be @engine { } block — peeked later by statement parser;
            // here we just collect the dotted path.
            const Token& part = expect(TokenKind::Ident, "expected identifier in annotation path");
            ann.path.push_back(part.lexeme);
        }
        // Optional argument list: @unreal.uproperty(EditAnywhere) — skip for now
        if (check(TokenKind::LParen)) {
            advance();
            int depth = 1;
            while (!check(TokenKind::Eof) && depth > 0) {
                if (check(TokenKind::LParen)) ++depth;
                if (check(TokenKind::RParen)) --depth;
                advance();
            }
        }
        result.push_back(std::move(ann));
    }
    return result;
}

// ===========================================================================
// Top-level declaration dispatch
// ===========================================================================
DeclPtr Parser::parse_decl(std::vector<Annotation> annots) {
    switch (peek().kind) {
        case TokenKind::KwFn:     return parse_fn_decl(std::move(annots));
        case TokenKind::KwClass:  return parse_class_decl(std::move(annots));
        case TokenKind::KwTrait:  return parse_trait_decl(std::move(annots));
        case TokenKind::KwImpl:   return parse_impl_decl(std::move(annots));
        case TokenKind::KwEnum:   return parse_enum_decl(std::move(annots));
        case TokenKind::KwLet:
        case TokenKind::KwVar:    return parse_field_or_var_decl(std::move(annots), true);
        case TokenKind::KwImport: return parse_import_decl(std::move(annots));
        // Statement-starting tokens allowed at top level (script mode)
        case TokenKind::KwIf:
        case TokenKind::KwWhile:
        case TokenKind::KwFor:
        case TokenKind::KwReturn:
        case TokenKind::At: {
            auto node = std::make_unique<StmtDecl>();
            node->loc = cur_loc();
            node->annotations = std::move(annots);
            node->stmt = parse_stmt();
            return node;
        }
        default:
            // Anything else: try parsing as an expression statement
            if (!check(TokenKind::Eof)) {
                auto node = std::make_unique<StmtDecl>();
                node->loc = cur_loc();
                node->annotations = std::move(annots);
                node->stmt = parse_stmt();
                return node;
            }
            record_error("expected declaration");
            synchronize();
            auto imp = std::make_unique<ImportDecl>();
            imp->annotations = std::move(annots);
            imp->path = "<error>";
            return imp;
    }
}

// ===========================================================================
// Function declaration
//   fn name<T>(params) -> RetType { body }
// ===========================================================================
DeclPtr Parser::parse_fn_decl(std::vector<Annotation> annots) {
    auto node     = std::make_unique<FnDecl>();
    node->loc     = cur_loc();
    node->annotations = std::move(annots);

    expect(TokenKind::KwFn, "expected 'fn'");

    const Token& name = expect(TokenKind::Ident, "expected function name");
    node->name = name.lexeme;

    // Optional generic type parameters: <T, U>
    if (check(TokenKind::Lt)) {
        node->type_params = parse_type_params();
    }

    // Parameter list
    expect(TokenKind::LParen, "expected '(' in function declaration");
    if (!check(TokenKind::RParen)) {
        node->params = parse_param_list();
    }
    expect(TokenKind::RParen, "expected ')' after parameters");

    // Optional return type: -> Type
    if (match(TokenKind::Arrow)) {
        node->return_type = parse_type();
    }

    node->body = parse_block();
    return node;
}

// ===========================================================================
// Class declaration
//   class Name : Base { members }
// ===========================================================================
DeclPtr Parser::parse_class_decl(std::vector<Annotation> annots) {
    auto node     = std::make_unique<ClassDecl>();
    node->loc     = cur_loc();
    node->annotations = std::move(annots);

    expect(TokenKind::KwClass, "expected 'class'");
    node->name = expect(TokenKind::Ident, "expected class name").lexeme;

    // Optional base class: : Base
    if (match(TokenKind::Colon)) {
        node->base = expect(TokenKind::Ident, "expected base class name").lexeme;
    }

    expect(TokenKind::LBrace, "expected '{' to open class body");

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        auto member_annots = parse_annotations();
        bool is_static = match(TokenKind::KwStatic);
        if (check(TokenKind::KwFn)) {
            auto fn = std::unique_ptr<FnDecl>(
                static_cast<FnDecl*>(parse_fn_decl(std::move(member_annots)).release()));
            fn->is_static = is_static;
            node->members.push_back(std::move(fn));
        } else if (check(TokenKind::KwLet) || check(TokenKind::KwVar)) {
            auto fd = std::unique_ptr<FieldDecl>(
                static_cast<FieldDecl*>(parse_field_or_var_decl(std::move(member_annots), true).release()));
            fd->is_static = is_static;
            node->members.push_back(std::move(fd));
        } else if (check_ident("prop")) {
            node->members.push_back(parse_prop_decl(std::move(member_annots)));
        } else {
            record_error("expected class member (fn, let, var, prop, or static)");
            synchronize();
        }
    }

    expect(TokenKind::RBrace, "expected '}' to close class body");
    return node;
}

// ===========================================================================
// Trait declaration
//   trait Name { prop name: Type / fn name(params) -> RetType }
// ===========================================================================
DeclPtr Parser::parse_trait_decl(std::vector<Annotation> annots) {
    auto node     = std::make_unique<TraitDecl>();
    node->loc     = cur_loc();
    node->annotations = std::move(annots);

    expect(TokenKind::KwTrait, "expected 'trait'");
    node->name = expect(TokenKind::Ident, "expected trait name").lexeme;

    expect(TokenKind::LBrace, "expected '{' to open trait body");

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        auto member_annots = parse_annotations();
        if (check(TokenKind::KwFn)) {
            // Trait method — body is optional; if no '{', it's abstract.
            auto fn = std::make_unique<FnDecl>();
            fn->loc = cur_loc();
            fn->annotations = std::move(member_annots);
            expect(TokenKind::KwFn, "expected 'fn'");
            fn->name = expect(TokenKind::Ident, "expected method name").lexeme;
            if (check(TokenKind::Lt)) fn->type_params = parse_type_params();
            expect(TokenKind::LParen, "expected '('");
            if (!check(TokenKind::RParen)) fn->params = parse_param_list();
            expect(TokenKind::RParen, "expected ')'");
            if (match(TokenKind::Arrow)) fn->return_type = parse_type();
            if (check(TokenKind::LBrace)) fn->body = parse_block();
            node->members.push_back(std::move(fn));
        } else if (check_ident("prop")) {
            node->members.push_back(parse_prop_decl(std::move(member_annots)));
        } else {
            record_error("expected trait member (fn or prop)");
            synchronize();
        }
    }

    expect(TokenKind::RBrace, "expected '}' to close trait body");
    return node;
}

// ===========================================================================
// Impl declaration
//   impl Trait for Type { members }
//   impl Type { members }           (inherent impl — trait_name is null)
// ===========================================================================
DeclPtr Parser::parse_impl_decl(std::vector<Annotation> annots) {
    auto node     = std::make_unique<ImplDecl>();
    node->loc     = cur_loc();
    node->annotations = std::move(annots);

    expect(TokenKind::KwImpl, "expected 'impl'");

    std::string first = expect(TokenKind::Ident, "expected type or trait name").lexeme;

    // impl Trait for Type  vs  impl Type { }
    if (match(TokenKind::KwFor)) {
        node->trait_name = first;
        node->for_type   = expect(TokenKind::Ident, "expected type name after 'for'").lexeme;
    } else {
        node->for_type = first;
    }

    expect(TokenKind::LBrace, "expected '{' to open impl body");

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        auto member_annots = parse_annotations();
        if (check(TokenKind::KwFn)) {
            node->members.push_back(parse_fn_decl(std::move(member_annots)));
        } else if (check_ident("prop")) {
            node->members.push_back(parse_prop_decl(std::move(member_annots)));
        } else {
            record_error("expected impl member (fn or prop)");
            synchronize();
        }
    }

    expect(TokenKind::RBrace, "expected '}' to close impl body");
    return node;
}

// ===========================================================================
// Field / top-level variable declaration
//   let/var name: Type = init
// ===========================================================================
DeclPtr Parser::parse_field_or_var_decl(std::vector<Annotation> annots, bool /*allow_init*/) {
    SourceLoc loc = cur_loc();
    bool is_let = check(TokenKind::KwLet);
    advance(); // consume let/var

    // Top-level destructuring: let [a, b] = ... or let {x, y} = ...
    if (check(TokenKind::LBracket) || check(TokenKind::LBrace)) {
        auto stmt = parse_destructure_stmt(is_let, /*is_global=*/true);
        auto node = std::make_unique<StmtDecl>();
        node->loc  = loc;
        node->annotations = std::move(annots);
        node->stmt = std::move(stmt);
        return node;
    }

    std::string first_name = expect(TokenKind::Ident, "expected variable name").lexeme;

    // Multi-variable destructuring at top level: let a, b = foo()
    if (check(TokenKind::Comma)) {
        auto stmt = std::make_unique<MultiVarDeclStmt>();
        stmt->loc = loc;
        stmt->names.push_back(first_name);
        stmt->is_let.push_back(is_let);
        while (match(TokenKind::Comma)) {
            stmt->names.push_back(expect(TokenKind::Ident, "expected variable name").lexeme);
            stmt->is_let.push_back(is_let);
        }
        expect(TokenKind::Assign, "expected '=' in multi-variable declaration");
        stmt->init = parse_expr();
        auto node = std::make_unique<StmtDecl>();
        node->loc = loc;
        node->annotations = std::move(annots);
        node->stmt = std::move(stmt);
        return node;
    }

    auto node     = std::make_unique<FieldDecl>();
    node->loc     = loc;
    node->annotations = std::move(annots);
    node->is_let  = is_let;
    node->name    = first_name;
    if (match(TokenKind::Colon)) node->type = parse_type();
    if (match(TokenKind::Assign)) node->init = parse_expr();
    return node;
}

// ===========================================================================
// Property declaration (contextual keyword "prop")
//   prop name: Type              — abstract (in trait)
//   prop name: Type { get => … set(v) => … }   — concrete
//   prop name { get => … set(v) => … }          — in impl (type from trait)
// ===========================================================================
DeclPtr Parser::parse_prop_decl(std::vector<Annotation> annots) {
    auto node     = std::make_unique<PropDecl>();
    node->loc     = cur_loc();
    node->annotations = std::move(annots);

    advance(); // consume "prop" ident
    node->name = expect(TokenKind::Ident, "expected property name").lexeme;

    if (match(TokenKind::Colon)) {
        node->type = parse_type();
    }

    if (check(TokenKind::LBrace)) {
        advance(); // consume '{'
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            PropAccessor acc;
            acc.loc = cur_loc();
            if (!check(TokenKind::Ident)) {
                record_error("expected 'get' or 'set' in property accessor");
                synchronize();
                break;
            }
            acc.kind = advance().lexeme; // "get" or "set"

            // set(v) — optional param
            if (acc.kind == "set" && check(TokenKind::LParen)) {
                advance();
                acc.param = expect(TokenKind::Ident, "expected parameter name").lexeme;
                expect(TokenKind::RParen, "expected ')'");
            }

            expect(TokenKind::FatArrow, "expected '=>' in accessor");
            acc.body_expr = parse_expr();
            node->accessors.push_back(std::move(acc));
        }
        expect(TokenKind::RBrace, "expected '}' to close property");
    }

    return node;
}

// ===========================================================================
// Import declaration
//   import some.module
//   import some.module as alias
//   import "relative/path.zs"
//   import "relative/path.zs" as alias
// ===========================================================================
DeclPtr Parser::parse_import_decl(std::vector<Annotation> annots) {
    auto node     = std::make_unique<ImportDecl>();
    node->loc     = cur_loc();
    node->annotations = std::move(annots);

    expect(TokenKind::KwImport, "expected 'import'");

    std::string path;

    // String path: import "foo/bar.zs"
    if (check(TokenKind::LitString)) {
        path = advance().lexeme;   // already stripped of quotes by lexer
    } else {
        // Dotted identifier path: import some.module
        path = expect(TokenKind::Ident, "expected module name").lexeme;
        while (check(TokenKind::Dot) && peek(1).kind == TokenKind::Ident) {
            advance();
            path += '.';
            path += advance().lexeme;
        }
    }
    node->path = path;

    // Optional: as alias
    if (check(TokenKind::Ident) && tokens_[pos_].lexeme == "as") {
        advance(); // consume "as"
        node->alias = expect(TokenKind::Ident, "expected alias name").lexeme;
    }

    // Default alias: last dotted segment (or last path component without extension)
    if (node->alias.empty()) {
        auto last_dot = path.rfind('.');
        auto last_sep = path.rfind('/');
        size_t start = (last_sep != std::string::npos) ? last_sep + 1 : 0;
        std::string seg = (last_dot != std::string::npos && last_dot > start)
                          ? path.substr(start, last_dot - start)
                          : path.substr(start);
        // Strip .zs extension if present
        if (seg.size() > 3 && seg.substr(seg.size() - 3) == ".zs")
            seg = seg.substr(0, seg.size() - 3);
        node->alias = seg.empty() ? path : seg;
    }

    return node;
}

// ===========================================================================
// enum Direction { North, South, East, West }
// enum Status { Ok = 200, NotFound = 404 }
// ===========================================================================
DeclPtr Parser::parse_enum_decl(std::vector<Annotation> annots) {
    auto node = std::make_unique<EnumDecl>();
    node->loc  = cur_loc();
    node->annotations = std::move(annots);

    expect(TokenKind::KwEnum, "expected 'enum'");
    node->name = expect(TokenKind::Ident, "expected enum name").lexeme;
    expect(TokenKind::LBrace, "expected '{' after enum name");

    int64_t next_val = 0;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        EnumDecl::Variant v;
        v.name = expect(TokenKind::Ident, "expected variant name").lexeme;
        if (match(TokenKind::Assign)) {
            // Explicit value: must be an integer literal (or negated integer)
            bool neg = match(TokenKind::Minus);
            const Token& t = expect(TokenKind::LitInt, "expected integer value after '='");
            int64_t val = std::stoll(t.lexeme);
            v.value   = neg ? -val : val;
            next_val  = *v.value + 1;
        } else {
            v.value  = next_val++;
        }
        node->variants.push_back(std::move(v));
        match(TokenKind::Comma); // optional trailing comma
    }

    expect(TokenKind::RBrace, "expected '}' to close enum body");
    return node;
}

// ===========================================================================
// Shared: parameter list  (name: Type, mut name: Type, name: Type = default)
// ===========================================================================
std::vector<Param> Parser::parse_param_list() {
    std::vector<Param> params;
    do {
        Param p;
        p.loc = cur_loc();
        // vararg: ...name or just ...
        if (check(TokenKind::DotDotDot)) {
            advance();
            p.is_vararg = true;
            if (check(TokenKind::Ident))
                p.name = advance().lexeme;
            else
                p.name = "args";
            params.push_back(std::move(p));
            break; // vararg must be last
        }
        if (check(TokenKind::KwMut)) {
            p.is_mut = true;
            advance();
        }
        p.name = expect(TokenKind::Ident, "expected parameter name").lexeme;
        if (match(TokenKind::Colon)) {
            p.type = parse_type();
        }
        if (match(TokenKind::Assign)) {
            p.default_val = parse_expr();
        }
        params.push_back(std::move(p));
    } while (match(TokenKind::Comma) && !check(TokenKind::RParen));
    return params;
}

// ===========================================================================
// Shared: generic type parameter list  <T, U>
// ===========================================================================
std::vector<std::string> Parser::parse_type_params() {
    std::vector<std::string> params;
    expect(TokenKind::Lt, "expected '<'");
    do {
        params.push_back(expect(TokenKind::Ident, "expected type parameter").lexeme);
    } while (match(TokenKind::Comma) && !check(TokenKind::Gt));
    expect(TokenKind::Gt, "expected '>' to close type parameters");
    return params;
}

// ===========================================================================
// Shared: block  { stmts }
// ===========================================================================
Block Parser::parse_block() {
    Block b;
    b.loc = cur_loc();
    expect(TokenKind::LBrace, "expected '{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        try {
            b.stmts.push_back(parse_stmt());
        } catch (...) {
            synchronize();
        }
    }
    expect(TokenKind::RBrace, "expected '}'");
    return b;
}

// ===========================================================================
// Statements
// ===========================================================================
StmtPtr Parser::parse_stmt() {
    switch (peek().kind) {
        case TokenKind::KwLet:
        case TokenKind::KwVar:    return parse_var_decl_stmt();
        case TokenKind::KwReturn:   return parse_return_stmt();
        case TokenKind::KwIf:       return parse_if_stmt();
        case TokenKind::KwWhile:    return parse_while_stmt();
        case TokenKind::KwFor:      return parse_for_stmt();
        case TokenKind::At:         return parse_engine_block_stmt();
        case TokenKind::KwMatch:    return parse_match_stmt();
        case TokenKind::KwThrow:    return parse_throw_stmt();
        case TokenKind::KwTry:      return parse_try_catch_stmt();
        case TokenKind::KwBreak: {
            auto s = std::make_unique<BreakStmt>();
            s->loc = cur_loc();
            advance();
            return s;
        }
        case TokenKind::KwContinue: {
            auto s = std::make_unique<ContinueStmt>();
            s->loc = cur_loc();
            advance();
            return s;
        }
        case TokenKind::KwFn: {
            // Named inner function: fn name(params) { body }
            // Desugar to: var name = fn(params) { body }
            if (peek(1).kind == TokenKind::Ident) {
                auto s    = std::make_unique<VarDeclStmt>();
                s->loc    = cur_loc();
                s->is_let = false;
                advance(); // consume 'fn'
                s->name   = advance().lexeme; // consume name
                // Parse the rest as an anonymous lambda body
                auto lam  = std::make_unique<LambdaExpr>();
                lam->loc  = s->loc;
                expect(TokenKind::LParen, "expected '('");
                if (!check(TokenKind::RParen)) lam->params = parse_param_list();
                expect(TokenKind::RParen, "expected ')'");
                if (match(TokenKind::Arrow)) lam->return_type = parse_type();
                lam->body = parse_block();
                s->init   = std::move(lam);
                return s;
            }
            break; // anonymous fn-expression statement — fall through
        }
        default:
            break;
    }

    // Expression statement
    auto stmt  = std::make_unique<ExprStmt>();
    stmt->loc  = cur_loc();
    stmt->expr = parse_expr();
    return stmt;
}

// let [a, b, ...rest] = expr   or   let {x, y: alias} = obj
StmtPtr Parser::parse_destructure_stmt(bool is_let, bool is_global) {
    SourceLoc loc = cur_loc();
    auto stmt = std::make_unique<DestructureStmt>();
    stmt->loc       = loc;
    stmt->is_let    = is_let;
    stmt->is_global = is_global;

    if (check(TokenKind::LBracket)) {
        stmt->kind = DestructureStmt::Kind::Array;
        advance(); // consume '['
        while (!check(TokenKind::RBracket) && !check(TokenKind::Eof)) {
            DestructureStmt::Binding b;
            if (check(TokenKind::DotDotDot)) {
                advance(); // consume '...'
                b.name    = expect(TokenKind::Ident, "expected name after '...'").lexeme;
                b.is_rest = true;
                stmt->bindings.push_back(std::move(b));
                match(TokenKind::Comma); // optional trailing comma
                break;
            }
            b.name = expect(TokenKind::Ident, "expected variable name").lexeme;
            stmt->bindings.push_back(std::move(b));
            if (!check(TokenKind::RBracket)) expect(TokenKind::Comma, "expected ','");
        }
        expect(TokenKind::RBracket, "expected ']'");
    } else {
        // Table destructuring: {x, y}  or  {fieldName: varName}
        stmt->kind = DestructureStmt::Kind::Table;
        advance(); // consume '{'
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            DestructureStmt::Binding b;
            b.key  = expect(TokenKind::Ident, "expected field name").lexeme;
            b.name = b.key;
            if (match(TokenKind::Colon)) {
                b.name = expect(TokenKind::Ident, "expected variable name").lexeme;
            }
            stmt->bindings.push_back(std::move(b));
            if (!check(TokenKind::RBrace)) expect(TokenKind::Comma, "expected ','");
        }
        expect(TokenKind::RBrace, "expected '}'");
    }

    expect(TokenKind::Assign, "expected '='");
    stmt->init = parse_expr();
    return stmt;
}

// let/var name: Type = expr   or   let a, b, c = expr  (multi-decl)
StmtPtr Parser::parse_var_decl_stmt() {
    SourceLoc loc = cur_loc();
    bool is_let = check(TokenKind::KwLet);
    advance();

    // Destructuring: let [a, b] = ... or let {x, y} = ...
    if (check(TokenKind::LBracket) || check(TokenKind::LBrace)) {
        return parse_destructure_stmt(is_let, /*is_global=*/false);
    }

    std::string first_name = expect(TokenKind::Ident, "expected variable name").lexeme;

    // Multi-variable destructuring: let a, b = foo()
    if (check(TokenKind::Comma)) {
        auto stmt    = std::make_unique<MultiVarDeclStmt>();
        stmt->loc    = loc;
        stmt->names.push_back(first_name);
        stmt->is_let.push_back(is_let);
        while (match(TokenKind::Comma)) {
            stmt->names.push_back(expect(TokenKind::Ident, "expected variable name").lexeme);
            stmt->is_let.push_back(is_let);
        }
        expect(TokenKind::Assign, "expected '=' in multi-variable declaration");
        stmt->init = parse_expr();
        return stmt;
    }

    auto stmt    = std::make_unique<VarDeclStmt>();
    stmt->loc    = loc;
    stmt->is_let = is_let;
    stmt->name   = first_name;
    if (match(TokenKind::Colon)) stmt->type = parse_type();
    if (match(TokenKind::Assign)) stmt->init = parse_expr();
    return stmt;
}

// return expr?  or  return x, y, z
StmtPtr Parser::parse_return_stmt() {
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->loc = cur_loc();
    expect(TokenKind::KwReturn, "expected 'return'");
    // Value(s) optional — if next token starts an expression, parse it.
    if (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        stmt->values.push_back(parse_expr());
        while (check(TokenKind::Comma)) {
            advance();
            stmt->values.push_back(parse_expr());
        }
    }
    return stmt;
}

// if cond { } else if cond { } else { }
StmtPtr Parser::parse_if_stmt() {
    auto stmt = std::make_unique<IfStmt>();
    stmt->loc = cur_loc();
    expect(TokenKind::KwIf, "expected 'if'");
    stmt->cond       = parse_expr();
    stmt->then_block = parse_block();

    if (match(TokenKind::KwElse)) {
        if (check(TokenKind::KwIf)) {
            stmt->else_clause = parse_if_stmt();
        } else {
            auto blk = std::make_unique<BlockStmt>();
            blk->loc   = cur_loc();
            blk->block = parse_block();
            stmt->else_clause = std::move(blk);
        }
    }
    return stmt;
}

// while cond { }
StmtPtr Parser::parse_while_stmt() {
    auto stmt = std::make_unique<WhileStmt>();
    stmt->loc = cur_loc();
    expect(TokenKind::KwWhile, "expected 'while'");
    stmt->cond = parse_expr();
    stmt->body = parse_block();
    return stmt;
}

// for let/var name in iterable { }
// for let/var k, v in table { }   (key-value iteration)
StmtPtr Parser::parse_for_stmt() {
    auto stmt = std::make_unique<ForStmt>();
    stmt->loc = cur_loc();
    expect(TokenKind::KwFor, "expected 'for'");

    // Optional let/var binding
    if (check(TokenKind::KwLet) || check(TokenKind::KwVar)) {
        stmt->binding_is_let = check(TokenKind::KwLet);
        advance();
    } else {
        stmt->binding_is_let = true;
    }

    stmt->var_name = expect(TokenKind::Ident, "expected loop variable name").lexeme;

    // Optional second variable: for k, v in table
    if (check(TokenKind::Comma)) {
        advance();
        stmt->key_name = expect(TokenKind::Ident, "expected value variable name").lexeme;
    }

    expect(TokenKind::KwIn, "expected 'in'");
    stmt->iterable = parse_expr();
    stmt->body     = parse_block();
    return stmt;
}

// @unity { } / @unreal { }  as a statement
StmtPtr Parser::parse_engine_block_stmt() {
    auto stmt = std::make_unique<EngineBlock>();
    stmt->loc = cur_loc();
    expect(TokenKind::At, "expected '@'");
    const Token& eng = expect(TokenKind::Ident, "expected engine name ('unity' or 'unreal')");
    stmt->engine = eng.lexeme;
    stmt->body   = parse_block();
    return stmt;
}

StmtPtr Parser::parse_match_stmt() {
    // match <expr> { pattern => body ... }
    auto stmt = std::make_unique<MatchStmt>();
    stmt->loc = cur_loc();
    expect(TokenKind::KwMatch, "expected 'match'");
    stmt->subject = parse_expr();
    expect(TokenKind::LBrace, "expected '{' after match subject");

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        MatchArm arm;
        // wildcard: _
        if (check(TokenKind::Ident) && peek().lexeme == "_") {
            advance();
            arm.is_wild = true;
        } else {
            arm.pattern = parse_expr();
            arm.is_wild = false;
        }
        expect(TokenKind::FatArrow, "expected '=>' after match pattern");

        // Body: either a block { } or a single statement
        if (check(TokenKind::LBrace)) {
            auto bs   = std::make_unique<BlockStmt>();
            bs->loc   = cur_loc();
            bs->block = parse_block();
            arm.body  = std::move(bs);
        } else {
            arm.body = parse_stmt();
        }
        stmt->arms.push_back(std::move(arm));
    }
    expect(TokenKind::RBrace, "expected '}' to close match");
    return stmt;
}

// throw expr
StmtPtr Parser::parse_throw_stmt() {
    auto stmt = std::make_unique<ThrowStmt>();
    stmt->loc = cur_loc();
    expect(TokenKind::KwThrow, "expected 'throw'");
    stmt->value = parse_expr();
    return stmt;
}

// try { } catch name { }
StmtPtr Parser::parse_try_catch_stmt() {
    auto stmt = std::make_unique<TryCatchStmt>();
    stmt->loc = cur_loc();
    expect(TokenKind::KwTry, "expected 'try'");
    stmt->try_block = parse_block();
    expect(TokenKind::KwCatch, "expected 'catch' after try block");
    stmt->catch_var = expect(TokenKind::Ident, "expected catch variable name").lexeme;
    stmt->catch_block = parse_block();
    return stmt;
}

// ===========================================================================
// Type parsing
//   Base: Ident  or  Ident<Type, Type>
//   Nullable suffix: ?
// ===========================================================================
TypePtr Parser::parse_type() {
    TypePtr base;
    SourceLoc loc = cur_loc();

    // Accept keywords that are valid in type position: nil, true, false
    std::string name;
    if (check(TokenKind::Ident) || check(TokenKind::LitNil) ||
        check(TokenKind::LitTrue) || check(TokenKind::LitFalse)) {
        name = advance().lexeme;
    } else {
        record_error("expected type name");
        name = "<error>";
    }

    // Generic type: List<Int> — in type position < is unambiguous
    if (check(TokenKind::Lt)) {
        auto gen  = std::make_unique<GenericType>();
        gen->loc  = loc;
        gen->name = name;
        advance(); // '<'
        do {
            gen->args.push_back(parse_type());
        } while (match(TokenKind::Comma) && !check(TokenKind::Gt));
        expect(TokenKind::Gt, "expected '>' to close generic type");
        base = std::move(gen);
    } else {
        auto named  = std::make_unique<NamedType>();
        named->loc  = loc;
        named->name = name;
        base = std::move(named);
    }

    // Nullable: T?
    if (match(TokenKind::Question)) {
        auto nullable    = std::make_unique<NullableType>();
        nullable->loc    = loc;
        nullable->inner  = std::move(base);
        return nullable;
    }
    return base;
}

// ===========================================================================
// Generic-call lookahead
//   After an expression, if we see '<' we check whether it's a generic call
//   or a less-than operator.
//   Heuristic: scan < Ident (, Ident)* > (  → generic
// ===========================================================================
bool Parser::is_generic_call_lookahead() const {
    // peek(0) must be '<'
    size_t i = 1; // offset from current pos
    // Skip type identifiers and commas
    while (true) {
        TokenKind k = tokens_[pos_ + i].kind;
        if (k == TokenKind::Ident) { ++i; continue; }
        if (k == TokenKind::Comma) { ++i; continue; }
        if (k == TokenKind::Gt) {
            // After '>', must be '(' to be a generic call
            return tokens_[pos_ + i + 1].kind == TokenKind::LParen;
        }
        return false;
    }
}

std::vector<TypePtr> Parser::parse_type_arg_list() {
    std::vector<TypePtr> args;
    advance(); // consume '<'
    do {
        args.push_back(parse_type());
    } while (match(TokenKind::Comma) && !check(TokenKind::Gt));
    expect(TokenKind::Gt, "expected '>' to close type argument list");
    return args;
}

// ===========================================================================
// Expressions — recursive descent with explicit precedence levels
// ===========================================================================

ExprPtr Parser::parse_expr() {
    return parse_assign();
}

// Assignment / delegate:  target = expr  |  target += expr  |  target -= expr
ExprPtr Parser::parse_assign() {
    ExprPtr left = parse_nil_coalesce();

    if (check(TokenKind::Assign)       || check(TokenKind::PlusAssign)  ||
        check(TokenKind::MinusAssign)  || check(TokenKind::StarAssign)  ||
        check(TokenKind::SlashAssign)  || check(TokenKind::PercentAssign)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();
        ExprPtr right = parse_assign(); // right-associative
        auto node = std::make_unique<AssignExpr>();
        node->loc    = loc;
        node->op     = op;
        node->target = std::move(left);
        node->value  = std::move(right);
        return node;
    }
    return left;
}

// a ?? b  — right-associative, lower than ||
ExprPtr Parser::parse_nil_coalesce() {
    ExprPtr left = parse_or();
    if (check(TokenKind::QuestionQuestion)) {
        SourceLoc loc = cur_loc();
        advance();
        auto node  = std::make_unique<BinaryExpr>();
        node->loc  = loc;
        node->op   = TokenKind::QuestionQuestion;
        node->left = std::move(left);
        node->right = parse_nil_coalesce(); // right-associative
        return node;
    }
    return left;
}

ExprPtr Parser::parse_or() {
    ExprPtr left = parse_and();
    while (check(TokenKind::Or)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();
        auto node  = std::make_unique<BinaryExpr>();
        node->loc  = loc;
        node->op   = op;
        node->left = std::move(left);
        node->right = parse_and();
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_and() {
    ExprPtr left = parse_equality();
    while (check(TokenKind::And)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();
        auto node  = std::make_unique<BinaryExpr>();
        node->loc  = loc;
        node->op   = op;
        node->left = std::move(left);
        node->right = parse_equality();
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_equality() {
    ExprPtr left = parse_comparison();
    while (check(TokenKind::Eq) || check(TokenKind::NotEq) || check(TokenKind::KwIs)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();
        auto node   = std::make_unique<BinaryExpr>();
        node->loc   = loc;
        node->op    = op;
        node->left  = std::move(left);
        node->right = parse_comparison();
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_comparison() {
    ExprPtr left = parse_range();
    while (check(TokenKind::Lt) || check(TokenKind::LtEq) ||
           check(TokenKind::Gt) || check(TokenKind::GtEq)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();
        auto node   = std::make_unique<BinaryExpr>();
        node->loc   = loc;
        node->op    = op;
        node->left  = std::move(left);
        node->right = parse_addition();
        left = std::move(node);
    }
    return left;
}

// Range operators: 0..10  /  0..<10  — lower precedence than arithmetic
ExprPtr Parser::parse_range() {
    ExprPtr left = parse_addition();
    if (check(TokenKind::DotDot) || check(TokenKind::DotDotLt)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();
        auto node   = std::make_unique<BinaryExpr>();
        node->loc   = loc;
        node->op    = op;
        node->left  = std::move(left);
        node->right = parse_addition();
        return node;
    }
    return left;
}

ExprPtr Parser::parse_addition() {
    ExprPtr left = parse_multiplication();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();
        auto node   = std::make_unique<BinaryExpr>();
        node->loc   = loc;
        node->op    = op;
        node->left  = std::move(left);
        node->right = parse_multiplication();
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_multiplication() {
    ExprPtr left = parse_power();
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();
        auto node   = std::make_unique<BinaryExpr>();
        node->loc   = loc;
        node->op    = op;
        node->left  = std::move(left);
        node->right = parse_power();
        left = std::move(node);
    }
    return left;
}

// ** is right-associative and tighter than * / %
ExprPtr Parser::parse_power() {
    ExprPtr left = parse_unary();
    if (check(TokenKind::StarStar)) {
        SourceLoc loc = cur_loc();
        advance();
        auto node   = std::make_unique<BinaryExpr>();
        node->loc   = loc;
        node->op    = TokenKind::StarStar;
        node->left  = std::move(left);
        node->right = parse_power(); // right-associative
        return node;
    }
    return left;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenKind::Bang) || check(TokenKind::Minus) || check(TokenKind::Hash)) {
        TokenKind op  = peek().kind;
        SourceLoc loc = cur_loc();
        advance();

        // If what follows can't start an expression, report the error here
        // (at the operator) rather than deep inside parse_primary.
        // This catches patterns like `i--` where the second `-` has no operand.
        const TokenKind next = peek().kind;
        bool can_start = (next == TokenKind::Bang   || next == TokenKind::Minus  ||
                          next == TokenKind::LParen  || next == TokenKind::LBracket ||
                          next == TokenKind::Ident   || next == TokenKind::KwSelf  ||
                          next == TokenKind::KwFn    ||
                          peek().is_literal());
        if (!can_start) {
            // Report at the operator location
            ParseError e;
            e.loc     = loc;
            e.message = std::string("operand expected after '") +
                        (op == TokenKind::Minus ? "-" : "!") + "'";
            errors_.push_back(e);
            auto nil = std::make_unique<LitExpr>();
            nil->loc  = loc;
            nil->kind = LitExpr::Kind::Nil;
            nil->value = "";
            return nil;
        }

        auto node    = std::make_unique<UnaryExpr>();
        node->loc    = loc;
        node->op     = op;
        node->operand = parse_unary();
        return node;
    }
    return parse_postfix();
}

// ---------------------------------------------------------------------------
// Call argument list parser
//
// Handles positional and named args:
//   f(1, 2, z: 3)  →  args=[1,2]  named_args=[{z,3}]
//
// A named arg is detected by: current token is Ident AND next token is ':'.
// Once a named arg is seen, all remaining args must also be named.
// Caller must consume '(' before calling and ')' after.
// ---------------------------------------------------------------------------
void Parser::parse_call_args(CallExpr& call) {
    if (check(TokenKind::RParen)) return; // empty arg list

    bool in_named = false;
    do {
        if (check(TokenKind::RParen)) break;

        // Named arg: Ident ':'  (but NOT Ident '::' which would be scope resolution)
        bool is_named = check(TokenKind::Ident) && peek(1).kind == TokenKind::Colon;
        if (is_named) {
            in_named = true;
            NamedArg na;
            na.name  = advance().lexeme; // consume ident
            advance();                   // consume ':'
            na.value = parse_expr();
            call.named_args.push_back(std::move(na));
        } else {
            if (in_named) {
                errors_.push_back({cur_loc(), "positional arg cannot follow named arg"});
            }
            call.args.push_back(parse_expr());
        }
    } while (match(TokenKind::Comma) && !check(TokenKind::RParen));
}

// Postfix: call, field access, index, generic call, safe/force access
ExprPtr Parser::parse_postfix() {
    ExprPtr expr = parse_primary();

    while (true) {
        SourceLoc loc = cur_loc();

        // Field access: .name  /  ?.name  /  !.name
        // Also handles safe/force subscript: ?.[i]  /  !.[i]
        if (check(TokenKind::Dot) || check(TokenKind::QDot) || check(TokenKind::BangDot)) {
            FieldExpr::Access access =
                check(TokenKind::Dot)     ? FieldExpr::Access::Dot   :
                check(TokenKind::QDot)    ? FieldExpr::Access::Safe  :
                                            FieldExpr::Access::Force;
            advance(); // consume . / ?. / !.

            // Safe/force subscript: ?.[expr]  /  !.[expr]
            if (access != FieldExpr::Access::Dot && check(TokenKind::LBracket)) {
                advance(); // consume [
                auto node    = std::make_unique<IndexExpr>();
                node->loc    = loc;
                node->access = (access == FieldExpr::Access::Safe)
                                   ? IndexExpr::Access::Safe
                                   : IndexExpr::Access::Force;
                node->object = std::move(expr);
                node->index  = parse_expr();
                expect(TokenKind::RBracket, "expected ']'");
                expr = std::move(node);
                continue;
            }

            std::string field = expect(TokenKind::Ident, "expected field name after '.'").lexeme;

            // Check if this is actually a method call: .name(   or .name<T>(
            // We'll build the FieldExpr first, then wrap in CallExpr if needed.
            auto field_node = std::make_unique<FieldExpr>();
            field_node->loc    = loc;
            field_node->access = access;
            field_node->object = std::move(expr);
            field_node->field  = field;
            expr = std::move(field_node);

            // If followed by '(' or '<T>(', it's a method call — fall through
            // to the call parsing below on the next loop iteration.
            continue;
        }

        // Generic call or regular call
        if (check(TokenKind::Lt) && is_generic_call_lookahead()) {
            auto call = std::make_unique<CallExpr>();
            call->loc      = loc;
            call->callee   = std::move(expr);
            call->type_args = parse_type_arg_list();
            expect(TokenKind::LParen, "expected '(' after generic type arguments");
            parse_call_args(*call);
            expect(TokenKind::RParen, "expected ')' to close call");
            expr = std::move(call);
            continue;
        }

        if (check(TokenKind::LParen)) {
            auto call    = std::make_unique<CallExpr>();
            call->loc    = loc;
            call->callee = std::move(expr);
            advance(); // consume '('
            parse_call_args(*call);
            expect(TokenKind::RParen, "expected ')' to close call");
            expr = std::move(call);
            continue;
        }

        // Index: [expr]
        if (check(TokenKind::LBracket)) {
            advance();
            auto node    = std::make_unique<IndexExpr>();
            node->loc    = loc;
            node->object = std::move(expr);
            node->index  = parse_expr();
            expect(TokenKind::RBracket, "expected ']'");
            expr = std::move(node);
            continue;
        }

        break;
    }
    return expr;
}

// Primary: literal, ident, self, (expr), lambda
ExprPtr Parser::parse_primary() {
    SourceLoc loc = cur_loc();

    // if as expression: if cond { then } else { else }
    if (check(TokenKind::KwIf)) {
        advance(); // consume 'if'
        auto node = std::make_unique<IfExpr>();
        node->loc  = loc;
        node->cond = parse_expr();
        node->then_block = parse_block();
        expect(TokenKind::KwElse, "expected 'else' in if-expression");
        node->else_block = parse_block();
        return node;
    }

    // Lambda: fn(params) { body }
    if (check(TokenKind::KwFn)) return parse_lambda();

    // self
    if (check(TokenKind::KwSelf)) {
        advance();
        auto node = std::make_unique<SelfExpr>();
        node->loc = loc;
        return node;
    }

    // Table/dict literal: {key: val, "str": val}
    // Disambiguate from a block: look ahead for Ident/String followed by ':'
    // Also accept empty {} as an empty table when used as expression.
    if (check(TokenKind::LBrace)) {
        // Peek ahead to decide: table if next is "}" (empty), or Ident/String + ":"
        bool is_table = false;
        if (peek(1).kind == TokenKind::RBrace) {
            is_table = true;  // empty table literal {}
        } else if ((peek(1).kind == TokenKind::Ident || peek(1).kind == TokenKind::LitString)
                   && peek(2).kind == TokenKind::Colon) {
            is_table = true;
        }
        if (is_table) {
            advance(); // consume '{'
            auto node = std::make_unique<TableExpr>();
            node->loc = loc;
            while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                TableExpr::Field field;
                if (check(TokenKind::LitString)) {
                    field.key = advance().lexeme; // raw string value
                } else {
                    field.key = expect(TokenKind::Ident, "expected field name").lexeme;
                }
                expect(TokenKind::Colon, "expected ':' after field name");
                field.value = parse_expr();
                node->fields.push_back(std::move(field));
                if (!match(TokenKind::Comma)) break;
            }
            expect(TokenKind::RBrace, "expected '}' to close table literal");
            return node;
        }
    }

    // Array literal: [a, b, c]
    if (check(TokenKind::LBracket)) {
        advance(); // consume '['
        auto node = std::make_unique<ArrayExpr>();
        node->loc = loc;
        while (!check(TokenKind::RBracket) && !check(TokenKind::Eof)) {
            node->elements.push_back(parse_expr());
            if (!match(TokenKind::Comma)) break;
        }
        expect(TokenKind::RBracket, "expected ']' to close array literal");
        return node;
    }

    // Grouped expression: (expr)
    if (check(TokenKind::LParen)) {
        advance();
        auto inner = parse_expr();
        expect(TokenKind::RParen, "expected ')' to close grouped expression");
        auto node   = std::make_unique<GroupExpr>();
        node->loc   = loc;
        node->inner = std::move(inner);
        return node;
    }

    // Boolean / nil literals
    if (check(TokenKind::LitTrue) || check(TokenKind::LitFalse) || check(TokenKind::LitNil)) {
        auto node = std::make_unique<LitExpr>();
        node->loc = loc;
        node->kind = peek().kind == TokenKind::LitTrue  ? LitExpr::Kind::True  :
                     peek().kind == TokenKind::LitFalse ? LitExpr::Kind::False :
                                                          LitExpr::Kind::Nil;
        node->value = advance().lexeme;
        return node;
    }

    // Integer literal
    if (check(TokenKind::LitInt)) {
        auto node   = std::make_unique<LitExpr>();
        node->loc   = loc;
        node->kind  = LitExpr::Kind::Int;
        node->value = advance().lexeme;
        return node;
    }

    // Float literal
    if (check(TokenKind::LitFloat)) {
        auto node   = std::make_unique<LitExpr>();
        node->loc   = loc;
        node->kind  = LitExpr::Kind::Float;
        node->value = advance().lexeme;
        return node;
    }

    // String literal (plain or interpolated)
    if (check(TokenKind::LitString)) {
        std::string text = peek().lexeme;
        advance();
        // Peek: if next is InterpStart, this is an interpolated string
        if (check(TokenKind::InterpStart)) {
            return finish_string_interp(std::move(text), loc);
        }
        // Plain string
        auto node   = std::make_unique<LitExpr>();
        node->loc   = loc;
        node->kind  = LitExpr::Kind::String;
        node->value = std::move(text);
        return node;
    }

    // Table literal: {}
    if (check(TokenKind::LBrace)) {
        advance(); // {
        expect(TokenKind::RBrace, "expected '}'");
        // Represent as a zero-arg call to a synthetic "__newtable__" ident so the
        // compiler can emit NewTable without a new AST node.
        // Simpler: use a dedicated TableExpr — but we don't have one yet.
        // For now return a LitExpr with a special sentinel value.
        auto node   = std::make_unique<LitExpr>();
        node->loc   = loc;
        node->kind  = LitExpr::Kind::Nil;
        node->value = "__table__";
        return node;
    }

    // Identifier
    if (check(TokenKind::Ident)) {
        auto node   = std::make_unique<IdentExpr>();
        node->loc   = loc;
        node->name  = advance().lexeme;
        return node;
    }

    // Error fallback
    record_error("expected expression");
    // Emit a nil literal so parsing can continue
    auto node   = std::make_unique<LitExpr>();
    node->loc   = loc;
    node->kind  = LitExpr::Kind::Nil;
    node->value = "";
    // Advance past the bad token to avoid infinite loop.
    if (!check(TokenKind::Eof)) advance();
    return node;
}

// ===========================================================================
// Lambda:  fn(params) -> RetType { body }
// ===========================================================================
ExprPtr Parser::parse_lambda() {
    auto node = std::make_unique<LambdaExpr>();
    node->loc = cur_loc();
    expect(TokenKind::KwFn, "expected 'fn'");
    expect(TokenKind::LParen, "expected '('");
    if (!check(TokenKind::RParen)) node->params = parse_param_list();
    expect(TokenKind::RParen, "expected ')'");
    if (match(TokenKind::Arrow)) node->return_type = parse_type();
    node->body = parse_block();
    return node;
}

// ===========================================================================
// String interpolation
//   first_text is the text of the LitString already consumed.
//   Token stream at entry:  InterpStart expr* InterpEnd LitString ...
// ===========================================================================
ExprPtr Parser::finish_string_interp(std::string first_text, SourceLoc loc) {
    auto node = std::make_unique<StringInterpExpr>();
    node->loc = loc;

    // First segment
    auto seg = std::make_unique<LitExpr>();
    seg->loc   = loc;
    seg->kind  = LitExpr::Kind::String;
    seg->value = std::move(first_text);
    node->parts.push_back(std::move(seg));

    while (check(TokenKind::InterpStart)) {
        advance(); // consume InterpStart '{'
        node->parts.push_back(parse_expr());
        expect(TokenKind::InterpEnd, "expected '}' to close string interpolation");
        // Next must be a LitString (possibly empty)
        SourceLoc sloc = cur_loc();
        std::string rest;
        if (check(TokenKind::LitString)) {
            rest = peek().lexeme;
            advance();
        }
        auto seg2  = std::make_unique<LitExpr>();
        seg2->loc   = sloc;
        seg2->kind  = LitExpr::Kind::String;
        seg2->value = std::move(rest);
        node->parts.push_back(std::move(seg2));
    }
    return node;
}

} // namespace zscript
