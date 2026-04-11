#pragma once
#include "token.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zscript {

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
struct Expr;
struct Stmt;
struct Decl;
struct TypeExpr;

using ExprPtr   = std::unique_ptr<Expr>;
using StmtPtr   = std::unique_ptr<Stmt>;
using DeclPtr   = std::unique_ptr<Decl>;
using TypePtr   = std::unique_ptr<TypeExpr>;
using StmtList  = std::vector<StmtPtr>;

// ---------------------------------------------------------------------------
// Annotation  (@unreal.uclass, @unity.component, etc.)
// ---------------------------------------------------------------------------
struct Annotation {
    std::vector<std::string> path;  // ["unreal", "uclass"]
    SourceLoc loc;
};

// ---------------------------------------------------------------------------
// Type expressions
// ---------------------------------------------------------------------------
struct TypeExpr {
    SourceLoc loc;
    virtual ~TypeExpr() = default;
};

// Float, Int, Vec3, Actor …
struct NamedType : TypeExpr {
    std::string name;
};

// T? — wraps any type
struct NullableType : TypeExpr {
    TypePtr inner;
};

// List<Int>, Map<String, Int> …
struct GenericType : TypeExpr {
    std::string name;
    std::vector<TypePtr> args;
};

// ---------------------------------------------------------------------------
// Function parameter
// ---------------------------------------------------------------------------
struct Param {
    std::string name;
    bool        is_mut = false;
    TypePtr     type;           // may be null if omitted (parse error)
    ExprPtr     default_val;    // optional default
    SourceLoc   loc;
};

// ---------------------------------------------------------------------------
// Block — reusable ordered list of statements with its own source location
// ---------------------------------------------------------------------------
struct Block {
    SourceLoc loc;
    StmtList  stmts;
};

// ---------------------------------------------------------------------------
// Base node types
// ---------------------------------------------------------------------------
struct Expr { SourceLoc loc; virtual ~Expr() = default; };
struct Stmt { SourceLoc loc; virtual ~Stmt() = default; };
struct Decl {
    SourceLoc             loc;
    std::vector<Annotation> annotations;
    virtual ~Decl() = default;
};

// ===========================================================================
// Expressions
// ===========================================================================

// Integer / float / bool / nil literal
struct LitExpr : Expr {
    enum class Kind { Int, Float, String, True, False, Nil } kind;
    std::string value;
};

// "hello {name}!" — parts alternate: LitExpr(String), any expr, LitExpr(String) …
struct StringInterpExpr : Expr {
    std::vector<ExprPtr> parts;
};

struct IdentExpr : Expr {
    std::string name;
};

struct SelfExpr : Expr {};

// Binary infix expression
struct BinaryExpr : Expr {
    TokenKind op;
    ExprPtr   left;
    ExprPtr   right;
};

// Prefix unary expression  (!, -)
struct UnaryExpr : Expr {
    TokenKind op;
    ExprPtr   operand;
};

// Assignment / delegate operators (=, +=, -=)
struct AssignExpr : Expr {
    TokenKind op;
    ExprPtr   target;
    ExprPtr   value;
};

// Field access: a.b  /  a?.b  /  a!.b
struct FieldExpr : Expr {
    enum class Access { Dot, Safe, Force } access;
    ExprPtr     object;
    std::string field;
};

// Function / method call, optionally with generic type args
struct CallExpr : Expr {
    ExprPtr              callee;
    std::vector<TypePtr> type_args;
    std::vector<ExprPtr> args;
};

// Subscript: a[i]
struct IndexExpr : Expr {
    ExprPtr object;
    ExprPtr index;
};

// Inline function: fn(params) -> RetType { body }
struct LambdaExpr : Expr {
    std::vector<Param> params;
    TypePtr            return_type;  // optional
    Block              body;
};

// Parenthesised expression: (expr)
struct GroupExpr : Expr {
    ExprPtr inner;
};

// ===========================================================================
// Statements
// ===========================================================================

struct ExprStmt : Stmt {
    ExprPtr expr;
};

// let / var declaration inside a function body
struct VarDeclStmt : Stmt {
    bool        is_let;
    std::string name;
    TypePtr     type;   // optional
    ExprPtr     init;   // optional
};

struct ReturnStmt : Stmt {
    ExprPtr value;  // null → bare return
};

struct IfStmt : Stmt {
    ExprPtr   cond;
    Block     then_block;
    StmtPtr   else_clause;  // IfStmt (else if) | BlockStmt (else) | null
};

// Wraps a block so it can appear as a Stmt (used for else { })
struct BlockStmt : Stmt {
    Block block;
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    Block   body;
};

// for let/var name in iterable { }
struct ForStmt : Stmt {
    bool        binding_is_let;
    std::string var_name;
    ExprPtr     iterable;
    Block       body;
};

// @unity { } / @unreal { } inside a function body
struct EngineBlock : Stmt {
    std::string engine;  // "unity" | "unreal"
    Block       body;
};

// ===========================================================================
// Top-level / class-member declarations
// ===========================================================================

// Free function or method
struct FnDecl : Decl {
    std::string              name;
    std::vector<std::string> type_params;  // <T, U>
    std::vector<Param>       params;
    TypePtr                  return_type;  // null if omitted (spec violation, captured later)
    Block                    body;
};

// Class field: let / var name: Type = init
struct FieldDecl : Decl {
    bool        is_let;
    std::string name;
    TypePtr     type;
    ExprPtr     init;
};

// Property (in trait / impl): prop name: Type { get => … set(v) => … }
struct PropAccessor {
    std::string kind;  // "get" | "set"
    std::string param; // set(v) → "v"
    ExprPtr     body_expr;  // => expr
    SourceLoc   loc;
};

struct PropDecl : Decl {
    std::string              name;
    TypePtr                  type;       // null in impl (inferred from trait)
    std::vector<PropAccessor> accessors;  // empty = abstract prop in trait
};

struct ClassDecl : Decl {
    std::string              name;
    std::optional<std::string> base;
    std::vector<DeclPtr>     members;  // FieldDecl | FnDecl
};

struct TraitDecl : Decl {
    std::string          name;
    std::vector<DeclPtr> members;  // PropDecl | FnDecl (bodies optional)
};

struct ImplDecl : Decl {
    std::optional<std::string> trait_name;  // null → inherent impl
    std::string                for_type;
    std::vector<DeclPtr>       members;
};

struct ImportDecl : Decl {
    std::string path;    // dotted module name or quoted string path
    std::string alias;   // optional "as name"; defaults to last segment of path
};

// Wraps a statement that appears at top level (script-style)
struct StmtDecl : Decl {
    StmtPtr stmt;
};

// ===========================================================================
// Program root
// ===========================================================================
struct Program {
    std::vector<DeclPtr> decls;
};

} // namespace zscript
