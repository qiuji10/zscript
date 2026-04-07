#include <catch2/catch_test_macros.hpp>
#include "lexer.h"
#include "parser.h"

using namespace zscript;

static Program parse(const std::string& src) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    return parser.parse();
}

static Program parse_with_errors(const std::string& src) {
    return parse(src); // same, errors not fatal
}

template<typename T> T* as(Decl* d) { return dynamic_cast<T*>(d); }
template<typename T> T* as(Stmt* s) { return dynamic_cast<T*>(s); }
template<typename T> T* as(Expr* e) { return dynamic_cast<T*>(e); }
template<typename T> T* as(TypeExpr* t) { return dynamic_cast<T*>(t); }

// ---------------------------------------------------------------------------
// Basic declarations
// ---------------------------------------------------------------------------

TEST_CASE("empty program", "[parser][decls]") {
    auto prog = parse("");
    CHECK(prog.decls.empty());
}

TEST_CASE("simple fn decl", "[parser][decls]") {
    auto prog = parse("fn hello() -> nil { }");
    REQUIRE(prog.decls.size() == 1);
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    CHECK(fn->name == "hello");
    CHECK(fn->params.empty());
    REQUIRE(fn->return_type != nullptr);
    auto* rt = as<NamedType>(fn->return_type.get());
    REQUIRE(rt != nullptr);
    CHECK(rt->name == "nil");
    CHECK(fn->body.stmts.empty());
}

TEST_CASE("fn with params", "[parser][decls]") {
    auto prog = parse("fn tick(deltaTime: Float, mut dir: Vec3) -> nil { }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->params.size() == 2);
    CHECK(fn->params[0].name == "deltaTime");
    CHECK(!fn->params[0].is_mut);
    CHECK(fn->params[1].name == "dir");
    CHECK(fn->params[1].is_mut);
}

TEST_CASE("fn with typed params", "[parser][decls]") {
    auto prog = parse("fn add(a: Int, b: Int) -> Int { return a + b }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->params.size() == 2);
    auto* t0 = as<NamedType>(fn->params[0].type.get());
    REQUIRE(t0 != nullptr);
    CHECK(t0->name == "Int");
    auto* rt = as<NamedType>(fn->return_type.get());
    REQUIRE(rt != nullptr);
    CHECK(rt->name == "Int");
}

TEST_CASE("fn return float", "[parser][decls]") {
    auto prog = parse("fn getSpeed() -> Float { return 0.0 }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* rt = as<NamedType>(fn->return_type.get());
    REQUIRE(rt != nullptr);
    CHECK(rt->name == "Float");
}

TEST_CASE("generic fn single type param", "[parser][decls]") {
    auto prog = parse("fn get<T>(index: Int) -> T { return nil }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->type_params.size() == 1);
    CHECK(fn->type_params[0] == "T");
}

TEST_CASE("generic fn multiple type params", "[parser][decls]") {
    auto prog = parse("fn pair<A, B>(a: A, b: B) -> nil { }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->type_params.size() == 2);
    CHECK(fn->type_params[0] == "A");
    CHECK(fn->type_params[1] == "B");
}

// ---------------------------------------------------------------------------
// Variable declarations
// ---------------------------------------------------------------------------

TEST_CASE("var decl inside function", "[parser][vardecl]") {
    auto prog = parse("fn f() -> nil { let x: Int = 42  var y = 3 }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->body.stmts.size() == 2);

    auto* s1 = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(s1 != nullptr);
    CHECK(s1->is_let);
    CHECK(s1->name == "x");
    CHECK(s1->type != nullptr);
    CHECK(s1->init != nullptr);

    auto* s2 = as<VarDeclStmt>(fn->body.stmts[1].get());
    REQUIRE(s2 != nullptr);
    CHECK(!s2->is_let);
    CHECK(s2->name == "y");
}

TEST_CASE("var decl without init", "[parser][vardecl]") {
    auto prog = parse("fn f() -> nil { var x: Int }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* s = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(s != nullptr);
    CHECK(s->name == "x");
    CHECK(s->init == nullptr);
    CHECK(s->type != nullptr);
}

TEST_CASE("var decl nullable type", "[parser][vardecl]") {
    auto prog = parse("fn f() -> nil { var a: Actor? }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* s = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(s != nullptr);
    auto* nt = as<NullableType>(s->type.get());
    REQUIRE(nt != nullptr);
    auto* inner = as<NamedType>(nt->inner.get());
    REQUIRE(inner != nullptr);
    CHECK(inner->name == "Actor");
}

TEST_CASE("top-level var is FieldDecl", "[parser][vardecl]") {
    // var/let at top level parses as FieldDecl, not StmtDecl
    auto prog = parse("var globalX = 100");
    REQUIRE(prog.decls.size() == 1);
    auto* fd = as<FieldDecl>(prog.decls[0].get());
    REQUIRE(fd != nullptr);
    CHECK(fd->name == "globalX");
    CHECK(!fd->is_let);
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

TEST_CASE("return statement", "[parser][stmts]") {
    auto prog = parse("fn f() -> Int { return 42 }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->body.stmts.size() == 1);
    auto* ret = as<ReturnStmt>(fn->body.stmts[0].get());
    REQUIRE(ret != nullptr);
    auto* lit = as<LitExpr>(ret->value.get());
    REQUIRE(lit != nullptr);
    CHECK(lit->value == "42");
}

TEST_CASE("return nil literal", "[parser][stmts]") {
    auto prog = parse("fn f() -> nil { return nil }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* ret = as<ReturnStmt>(fn->body.stmts[0].get());
    REQUIRE(ret != nullptr);
    auto* lit = as<LitExpr>(ret->value.get());
    REQUIRE(lit != nullptr);
    CHECK(lit->kind == LitExpr::Kind::Nil);
}

TEST_CASE("if statement with else", "[parser][stmts]") {
    auto prog = parse("fn f() -> nil { if x == 1 { return nil } else { return nil } }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* ifs = as<IfStmt>(fn->body.stmts[0].get());
    REQUIRE(ifs != nullptr);
    CHECK(ifs->then_block.stmts.size() == 1);
    CHECK(ifs->else_clause != nullptr);
}

TEST_CASE("if statement without else", "[parser][stmts]") {
    auto prog = parse("fn f() -> nil { if x { } }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* ifs = as<IfStmt>(fn->body.stmts[0].get());
    REQUIRE(ifs != nullptr);
    CHECK(ifs->else_clause == nullptr);
}

TEST_CASE("else-if chain", "[parser][stmts]") {
    auto prog = parse("fn f() -> nil { if a { } else if b { } else { } }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* ifs = as<IfStmt>(fn->body.stmts[0].get());
    REQUIRE(ifs != nullptr);
    auto* elif = as<IfStmt>(ifs->else_clause.get());
    REQUIRE(elif != nullptr);
    CHECK(elif->else_clause != nullptr);
}

TEST_CASE("while statement", "[parser][stmts]") {
    SECTION("empty body") {
        auto prog = parse("fn f() -> nil { while x > 0 { } }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        auto* w = as<WhileStmt>(fn->body.stmts[0].get());
        REQUIRE(w != nullptr);
        CHECK(w->body.stmts.empty());
    }
    SECTION("with body") {
        auto prog = parse("fn f() -> nil { while i < 10 { i = i + 1 } }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        auto* w = as<WhileStmt>(fn->body.stmts[0].get());
        REQUIRE(w != nullptr);
        CHECK(w->body.stmts.size() == 1);
    }
}

TEST_CASE("for statement", "[parser][stmts]") {
    SECTION("let binding exclusive range") {
        auto prog = parse("fn f() -> nil { for let i in 0..<10 { } }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        auto* fs = as<ForStmt>(fn->body.stmts[0].get());
        REQUIRE(fs != nullptr);
        CHECK(fs->binding_is_let);
        CHECK(fs->var_name == "i");
        CHECK(fs->iterable != nullptr);
    }
    SECTION("var binding") {
        auto prog = parse("fn f() -> nil { for var i in 1..5 { } }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        auto* fs = as<ForStmt>(fn->body.stmts[0].get());
        REQUIRE(fs != nullptr);
        CHECK(!fs->binding_is_let);
    }
    SECTION("inclusive range operator") {
        auto prog = parse("fn f() -> nil { for let i in 1..10 { } }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        auto* fs = as<ForStmt>(fn->body.stmts[0].get());
        REQUIRE(fs != nullptr);
        auto* range = as<BinaryExpr>(fs->iterable.get());
        REQUIRE(range != nullptr);
        CHECK(range->op == TokenKind::DotDot);
    }
}

TEST_CASE("engine block statement", "[parser][stmts]") {
    SECTION("empty blocks") {
        auto prog = parse("fn f() -> nil { @unity { } @unreal { } }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        REQUIRE(fn->body.stmts.size() == 2);
        auto* eb1 = as<EngineBlock>(fn->body.stmts[0].get());
        REQUIRE(eb1 != nullptr);
        CHECK(eb1->engine == "unity");
        auto* eb2 = as<EngineBlock>(fn->body.stmts[1].get());
        REQUIRE(eb2 != nullptr);
        CHECK(eb2->engine == "unreal");
    }
    SECTION("with body stmts") {
        auto prog = parse("fn f() -> nil { @unity { x = 1  y = 2 } }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        auto* eb = as<EngineBlock>(fn->body.stmts[0].get());
        REQUIRE(eb != nullptr);
        CHECK(eb->body.stmts.size() == 2);
    }
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

TEST_CASE("binary expr operator precedence", "[parser][expr]") {
    // 1 + 2 * 3  →  1 + (2 * 3)
    auto prog = parse("fn f() -> nil { let x = 1 + 2 * 3 }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr);
    auto* add  = as<BinaryExpr>(decl->init.get());
    REQUIRE(add != nullptr);
    CHECK(add->op == TokenKind::Plus);
    auto* mul = as<BinaryExpr>(add->right.get());
    REQUIRE(mul != nullptr);
    CHECK(mul->op == TokenKind::Star);
}

TEST_CASE("comparison expression", "[parser][expr]") {
    auto prog = parse("fn f() -> nil { let r = a < b }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    auto* cmp  = as<BinaryExpr>(decl->init.get());
    REQUIRE(cmp != nullptr);
    CHECK(cmp->op == TokenKind::Lt);
}

TEST_CASE("logical expression", "[parser][expr]") {
    auto prog = parse("fn f() -> nil { let r = a && b || c }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    auto* orEx = as<BinaryExpr>(decl->init.get());
    REQUIRE(orEx != nullptr);
    CHECK(orEx->op == TokenKind::Or);
}

TEST_CASE("modulo expression", "[parser][expr]") {
    auto prog = parse("fn f() -> nil { let r = 10 % 3 }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    auto* mod  = as<BinaryExpr>(decl->init.get());
    REQUIRE(mod != nullptr);
    CHECK(mod->op == TokenKind::Percent);
}

TEST_CASE("unary not expression", "[parser][expr]") {
    auto prog = parse("fn f() -> nil { let x = !true }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr);
    auto* u = as<UnaryExpr>(decl->init.get());
    REQUIRE(u != nullptr);
    CHECK(u->op == TokenKind::Bang);
}

TEST_CASE("unary neg expression", "[parser][expr]") {
    auto prog = parse("fn f() -> nil { let x = -42 }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr);
    auto* u = as<UnaryExpr>(decl->init.get());
    REQUIRE(u != nullptr);
    CHECK(u->op == TokenKind::Minus);
}

TEST_CASE("assignment expressions", "[parser][expr]") {
    SECTION("simple assign") {
        auto prog = parse("fn f() -> nil { x = 10 }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        auto* es  = as<ExprStmt>(fn->body.stmts[0].get());
        REQUIRE(es != nullptr);
        auto* asgn = as<AssignExpr>(es->expr.get());
        REQUIRE(asgn != nullptr);
        CHECK(asgn->op == TokenKind::Assign);
    }
    SECTION("compound assign") {
        auto prog = parse("fn f() -> nil { x += 5  y -= 3 }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn->body.stmts.size() == 2);
        auto* a1 = as<AssignExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(a1 != nullptr);
        CHECK(a1->op == TokenKind::PlusAssign);
        auto* a2 = as<AssignExpr>(as<ExprStmt>(fn->body.stmts[1].get())->expr.get());
        REQUIRE(a2 != nullptr);
        CHECK(a2->op == TokenKind::MinusAssign);
    }
}

TEST_CASE("delegate assign and remove", "[parser][expr]") {
    SECTION("delegate +=") {
        auto prog = parse("fn f() -> nil { actor.OnHit += handler }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        auto* asgn = as<AssignExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(asgn != nullptr);
        CHECK(asgn->op == TokenKind::PlusAssign);
    }
    SECTION("delegate -=") {
        auto prog = parse("fn f() -> nil { actor.OnHit -= handler }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        auto* asgn = as<AssignExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(asgn != nullptr);
        CHECK(asgn->op == TokenKind::MinusAssign);
    }
}

TEST_CASE("field access expressions", "[parser][expr]") {
    SECTION("dot access") {
        auto prog = parse("fn f() -> nil { let p = self.position }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        REQUIRE(decl != nullptr);
        auto* field = as<FieldExpr>(decl->init.get());
        REQUIRE(field != nullptr);
        CHECK(field->access == FieldExpr::Access::Dot);
        CHECK(field->field == "position");
        CHECK(as<SelfExpr>(field->object.get()) != nullptr);
    }
    SECTION("chained a.b.c") {
        auto prog = parse("fn f() -> nil { let v = a.b.c }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        REQUIRE(decl != nullptr);
        auto* outer = as<FieldExpr>(decl->init.get());
        REQUIRE(outer != nullptr);
        CHECK(outer->field == "c");
        auto* inner = as<FieldExpr>(outer->object.get());
        REQUIRE(inner != nullptr);
        CHECK(inner->field == "b");
    }
    SECTION("safe and force access") {
        auto prog = parse("fn f() -> nil { b?.Destroy()  c!.Init() }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn->body.stmts.size() == 2);

        auto* call1 = as<CallExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(call1 != nullptr);
        auto* f1 = as<FieldExpr>(call1->callee.get());
        REQUIRE(f1 != nullptr);
        CHECK(f1->access == FieldExpr::Access::Safe);

        auto* call2 = as<CallExpr>(as<ExprStmt>(fn->body.stmts[1].get())->expr.get());
        REQUIRE(call2 != nullptr);
        auto* f2 = as<FieldExpr>(call2->callee.get());
        REQUIRE(f2 != nullptr);
        CHECK(f2->access == FieldExpr::Access::Force);
    }
}

TEST_CASE("call expressions", "[parser][expr]") {
    SECTION("single arg") {
        auto prog = parse("fn f() -> nil { log(\"hello\") }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        auto* call = as<CallExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(call != nullptr);
        REQUIRE(call->args.size() == 1);
        auto* lit = as<LitExpr>(call->args[0].get());
        REQUIRE(lit != nullptr);
        CHECK(lit->value == "hello");
    }
    SECTION("multiple args") {
        auto prog = parse("fn f() -> nil { add(1, 2, 3) }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        auto* call = as<CallExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(call != nullptr);
        CHECK(call->args.size() == 3);
    }
    SECTION("no args") {
        auto prog = parse("fn f() -> nil { doThing() }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        auto* call = as<CallExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(call != nullptr);
        CHECK(call->args.empty());
    }
    SECTION("nested calls") {
        auto prog = parse("fn f() -> nil { let r = outer(inner(x)) }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        REQUIRE(decl != nullptr);
        auto* outer_call = as<CallExpr>(decl->init.get());
        REQUIRE(outer_call != nullptr);
        REQUIRE(outer_call->args.size() == 1);
        auto* inner_call = as<CallExpr>(outer_call->args[0].get());
        REQUIRE(inner_call != nullptr);
    }
    SECTION("generic type args") {
        auto prog = parse("fn f() -> nil { self.GetComponent<Rigidbody>() }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        auto* call = as<CallExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(call != nullptr);
        REQUIRE(call->type_args.size() == 1);
        auto* ty = as<NamedType>(call->type_args[0].get());
        REQUIRE(ty != nullptr);
        CHECK(ty->name == "Rigidbody");
    }
}

TEST_CASE("index expression", "[parser][expr]") {
    auto prog = parse("fn f() -> nil { let v = list[0] }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr);
    auto* idx  = as<IndexExpr>(decl->init.get());
    REQUIRE(idx != nullptr);
    auto* zero = as<LitExpr>(idx->index.get());
    REQUIRE(zero != nullptr);
    CHECK(zero->value == "0");
}

TEST_CASE("group expression changes precedence", "[parser][expr]") {
    // (a + b) * c  →  mul, left side is the add
    auto prog = parse("fn f() -> nil { let r = (a + b) * c }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr);
    auto* mul  = as<BinaryExpr>(decl->init.get());
    REQUIRE(mul != nullptr);
    CHECK(mul->op == TokenKind::Star);
    CHECK(mul->left != nullptr);
}

// ---------------------------------------------------------------------------
// Strings / interpolation
// ---------------------------------------------------------------------------

TEST_CASE("string literal", "[parser][strings]") {
    auto prog = parse("fn f() -> nil { let s = \"hello world\" }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    auto* lit  = as<LitExpr>(decl->init.get());
    REQUIRE(lit != nullptr);
    CHECK(lit->kind == LitExpr::Kind::String);
    CHECK(lit->value == "hello world");
}

TEST_CASE("string interpolation", "[parser][strings]") {
    SECTION("ident interpolation") {
        auto prog = parse("fn f() -> nil { let s = \"hi {name}!\" }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        auto* interp = as<StringInterpExpr>(decl->init.get());
        REQUIRE(interp != nullptr);
        REQUIRE(interp->parts.size() == 3);
        auto* p0 = as<LitExpr>(interp->parts[0].get());
        REQUIRE(p0 != nullptr);
        CHECK(p0->value == "hi ");
        auto* p1 = as<IdentExpr>(interp->parts[1].get());
        REQUIRE(p1 != nullptr);
        CHECK(p1->name == "name");
        auto* p2 = as<LitExpr>(interp->parts[2].get());
        REQUIRE(p2 != nullptr);
        CHECK(p2->value == "!");
    }
    SECTION("expression interpolation") {
        auto prog = parse("fn f() -> nil { let s = \"result={a+b}\" }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        auto* interp = as<StringInterpExpr>(decl->init.get());
        REQUIRE(interp != nullptr);
        REQUIRE(interp->parts.size() >= 2);
        auto* p1 = as<BinaryExpr>(interp->parts[1].get());
        REQUIRE(p1 != nullptr);
        CHECK(p1->op == TokenKind::Plus);
    }
}

// ---------------------------------------------------------------------------
// Lambda / closures
// ---------------------------------------------------------------------------

TEST_CASE("lambda expression", "[parser][lambda]") {
    SECTION("with param") {
        auto prog = parse("fn f() -> nil { let h = fn(x: Int) -> Int { return x } }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        auto* lam  = as<LambdaExpr>(decl->init.get());
        REQUIRE(lam != nullptr);
        REQUIRE(lam->params.size() == 1);
        CHECK(lam->params[0].name == "x");
        CHECK(lam->return_type != nullptr);
    }
    SECTION("no params") {
        auto prog = parse("fn f() -> nil { let h = fn() -> nil { } }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        auto* lam  = as<LambdaExpr>(decl->init.get());
        REQUIRE(lam != nullptr);
        CHECK(lam->params.empty());
    }
    SECTION("inline delegate lambda") {
        auto prog = parse(R"(fn f() -> nil {
            actor.OnHit += fn(other: Actor) -> nil { log("hit") }
        })");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        auto* asgn = as<AssignExpr>(as<ExprStmt>(fn->body.stmts[0].get())->expr.get());
        REQUIRE(asgn != nullptr);
        CHECK(asgn->op == TokenKind::PlusAssign);
        auto* lam = as<LambdaExpr>(asgn->value.get());
        REQUIRE(lam != nullptr);
        REQUIRE(lam->params.size() == 1);
        CHECK(lam->params[0].name == "other");
    }
}

// ---------------------------------------------------------------------------
// Type system
// ---------------------------------------------------------------------------

TEST_CASE("nullable type", "[parser][types]") {
    auto prog = parse("fn f(a: Actor?) -> Actor? { return nil }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    auto* ntype = as<NullableType>(fn->params[0].type.get());
    REQUIRE(ntype != nullptr);
    auto* inner = as<NamedType>(ntype->inner.get());
    REQUIRE(inner != nullptr);
    CHECK(inner->name == "Actor");
    auto* rtype = as<NullableType>(fn->return_type.get());
    REQUIRE(rtype != nullptr);
}

TEST_CASE("generic types", "[parser][types]") {
    SECTION("single arg") {
        auto prog = parse("fn f() -> nil { let l: List<Int> = nil }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        REQUIRE(decl != nullptr);
        auto* gtype = as<GenericType>(decl->type.get());
        REQUIRE(gtype != nullptr);
        CHECK(gtype->name == "List");
        REQUIRE(gtype->args.size() == 1);
        auto* itype = as<NamedType>(gtype->args[0].get());
        REQUIRE(itype != nullptr);
        CHECK(itype->name == "Int");
    }
    SECTION("two args") {
        auto prog = parse("fn f() -> nil { let m: Map<String, Int> = nil }");
        auto* fn   = as<FnDecl>(prog.decls[0].get());
        auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
        REQUIRE(decl != nullptr);
        auto* gtype = as<GenericType>(decl->type.get());
        REQUIRE(gtype != nullptr);
        CHECK(gtype->name == "Map");
        REQUIRE(gtype->args.size() == 2);
        auto* k = as<NamedType>(gtype->args[0].get());
        auto* v = as<NamedType>(gtype->args[1].get());
        REQUIRE(k != nullptr);
        REQUIRE(v != nullptr);
        CHECK(k->name == "String");
        CHECK(v->name == "Int");
    }
    SECTION("generic return type") {
        auto prog = parse("fn getList() -> List<Actor> { return nil }");
        auto* fn  = as<FnDecl>(prog.decls[0].get());
        REQUIRE(fn != nullptr);
        auto* gtype = as<GenericType>(fn->return_type.get());
        REQUIRE(gtype != nullptr);
        CHECK(gtype->name == "List");
    }
}

// ---------------------------------------------------------------------------
// Class declarations
// ---------------------------------------------------------------------------

TEST_CASE("simple class decl", "[parser][class]") {
    auto prog = parse("class Foo { }");
    REQUIRE(prog.decls.size() == 1);
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr);
    CHECK(cls->name == "Foo");
    CHECK(!cls->base.has_value());
    CHECK(cls->members.empty());
}

TEST_CASE("class with base class", "[parser][class]") {
    auto prog = parse("class Player : Character { }");
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr);
    CHECK(cls->name == "Player");
    REQUIRE(cls->base.has_value());
    CHECK(cls->base.value() == "Character");
}

TEST_CASE("class with annotation", "[parser][class]") {
    auto prog = parse("@unreal.uclass\nclass PlayerActor : Actor { }");
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr);
    REQUIRE(cls->annotations.size() == 1);
    CHECK(cls->annotations[0].path[0] == "unreal");
    CHECK(cls->annotations[0].path[1] == "uclass");
}

TEST_CASE("class with field and method", "[parser][class]") {
    const char* src = R"(
@unreal.uclass
class PlayerActor : Actor {
    @unreal.uproperty(EditAnywhere)
    var Speed: Float = 600

    fn BeginPlay() {
        log("Player spawned")
    }
})";
    auto prog = parse(src);
    REQUIRE(prog.decls.size() == 1);
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr);
    CHECK(cls->name == "PlayerActor");
    REQUIRE(cls->base.has_value());
    CHECK(cls->base.value() == "Actor");
    REQUIRE(cls->annotations.size() == 1);
    REQUIRE(cls->members.size() == 2);

    auto* field = as<FieldDecl>(cls->members[0].get());
    REQUIRE(field != nullptr);
    CHECK(!field->is_let);
    CHECK(field->name == "Speed");

    auto* method = as<FnDecl>(cls->members[1].get());
    REQUIRE(method != nullptr);
    CHECK(method->name == "BeginPlay");
}

TEST_CASE("class readonly field (let)", "[parser][class]") {
    auto prog = parse("class Foo { let id: Int = 1 }");
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr);
    REQUIRE(cls->members.size() == 1);
    auto* field = as<FieldDecl>(cls->members[0].get());
    REQUIRE(field != nullptr);
    CHECK(field->is_let);
    CHECK(field->name == "id");
}

TEST_CASE("class with multiple methods", "[parser][class]") {
    const char* src = R"(class HealthComponent : ActorComponent {
    var MaxHp: Int = 100
    var CurrentHp: Int = 100
    fn ApplyDamage(amount: Int) -> nil { CurrentHp = CurrentHp - amount }
    fn Heal(amount: Int) -> nil { CurrentHp = CurrentHp + amount }
})";
    auto prog = parse(src);
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr);
    CHECK(cls->members.size() == 4);
}

// ---------------------------------------------------------------------------
// Trait and Impl
// ---------------------------------------------------------------------------

TEST_CASE("trait declaration", "[parser][trait]") {
    const char* src = R"(
trait Movable {
    prop position: Vec3
    fn translate(delta: Vec3) -> nil
})";
    auto prog = parse(src);
    REQUIRE(prog.decls.size() == 1);
    auto* tr = as<TraitDecl>(prog.decls[0].get());
    REQUIRE(tr != nullptr);
    CHECK(tr->name == "Movable");
    REQUIRE(tr->members.size() == 2);
    auto* prop = as<PropDecl>(tr->members[0].get());
    REQUIRE(prop != nullptr);
    CHECK(prop->name == "position");
    auto* fn = as<FnDecl>(tr->members[1].get());
    REQUIRE(fn != nullptr);
    CHECK(fn->name == "translate");
}

TEST_CASE("trait with multiple props and methods", "[parser][trait]") {
    const char* src = R"(
trait Damageable {
    prop health: Int
    prop maxHealth: Int
    fn takeDamage(amount: Int) -> nil
    fn heal(amount: Int) -> nil
})";
    auto prog = parse(src);
    auto* tr = as<TraitDecl>(prog.decls[0].get());
    REQUIRE(tr != nullptr);
    CHECK(tr->members.size() == 4);
}

TEST_CASE("impl declaration", "[parser][impl]") {
    const char* src = R"(
@unity.adapter
impl Movable for Transform {
    prop position {
        get => self.position
        set(v) => self.position = v
    }
    fn translate(delta: Vec3) -> nil {
        self.position += delta
    }
})";
    auto prog = parse(src);
    REQUIRE(prog.decls.size() == 1);
    auto* impl = as<ImplDecl>(prog.decls[0].get());
    REQUIRE(impl != nullptr);
    REQUIRE(impl->trait_name.has_value());
    CHECK(impl->trait_name.value() == "Movable");
    CHECK(impl->for_type == "Transform");
    REQUIRE(impl->members.size() == 2);

    auto* prop = as<PropDecl>(impl->members[0].get());
    REQUIRE(prop != nullptr);
    CHECK(prop->name == "position");
    REQUIRE(prop->accessors.size() == 2);
    CHECK(prop->accessors[0].kind == "get");
    CHECK(prop->accessors[1].kind == "set");
    CHECK(prop->accessors[1].param == "v");
}

TEST_CASE("impl for unreal adapter", "[parser][impl]") {
    const char* src = R"(
@unreal.adapter
impl Movable for Actor {
    prop position {
        get => self.GetActorLocation()
        set(v) => self.SetActorLocation(v)
    }
    fn translate(delta: Vec3) -> nil {
        self.SetActorLocation(self.GetActorLocation() + delta)
    }
})";
    auto prog = parse(src);
    auto* impl = as<ImplDecl>(prog.decls[0].get());
    REQUIRE(impl != nullptr);
    CHECK(impl->for_type == "Actor");
    REQUIRE(impl->annotations.size() == 1);
    CHECK(impl->annotations[0].path[0] == "unreal");
}

TEST_CASE("inherent impl (no trait)", "[parser][impl]") {
    auto prog = parse("impl Player { fn greet() -> nil { } }");
    auto* impl = as<ImplDecl>(prog.decls[0].get());
    REQUIRE(impl != nullptr);
    CHECK(!impl->trait_name.has_value());
    CHECK(impl->for_type == "Player");
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

TEST_CASE("import declaration", "[parser][import]") {
    SECTION("dotted path") {
        auto prog = parse("import engine.core");
        REQUIRE(prog.decls.size() == 1);
        auto* imp = as<ImportDecl>(prog.decls[0].get());
        REQUIRE(imp != nullptr);
        CHECK(imp->path == "engine.core");
    }
    SECTION("simple name") {
        auto prog = parse("import math");
        auto* imp = as<ImportDecl>(prog.decls[0].get());
        REQUIRE(imp != nullptr);
        CHECK(imp->path == "math");
    }
}

// ---------------------------------------------------------------------------
// Multi-declaration / error recovery
// ---------------------------------------------------------------------------

TEST_CASE("multiple top-level declarations", "[parser][multi]") {
    const char* src = "fn foo() -> nil { }\nfn bar() -> nil { }\nclass Baz { }";
    auto prog = parse(src);
    CHECK(prog.decls.size() == 3);
    CHECK(as<FnDecl>(prog.decls[0].get()) != nullptr);
    CHECK(as<FnDecl>(prog.decls[1].get()) != nullptr);
    CHECK(as<ClassDecl>(prog.decls[2].get()) != nullptr);
}

TEST_CASE("error recovery after bad fn", "[parser][errors]") {
    // Missing fn name — should recover and parse 'ok'
    auto prog = parse_with_errors("fn () -> nil { } fn ok() -> nil { }");
    bool found_ok = false;
    for (auto& d : prog.decls)
        if (auto* fn = as<FnDecl>(d.get()))
            if (fn->name == "ok") found_ok = true;
    CHECK(found_ok);
}

TEST_CASE("error recovery after bad class", "[parser][errors]") {
    auto prog = parse_with_errors("class { } fn ok() -> nil { }");
    bool found_ok = false;
    for (auto& d : prog.decls)
        if (auto* fn = as<FnDecl>(d.get()))
            if (fn->name == "ok") found_ok = true;
    CHECK(found_ok);
}

// ---------------------------------------------------------------------------
// Integration: complex PLAN.md snippets
// ---------------------------------------------------------------------------

TEST_CASE("AI controller class", "[parser][integration]") {
    const char* src = R"(
class AIController {
    var target: Actor?
    fn update(dt: Float) -> nil {
        if target == nil { return }
        let pos       = self.GetActorLocation()
        let targetPos = target!.GetActorLocation()
        let dir       = (targetPos - pos)
        self.AddMovementInput(dir)
    }
})";
    auto prog = parse(src);
    REQUIRE(prog.decls.size() == 1);
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr);
    CHECK(cls->name == "AIController");
    REQUIRE(cls->members.size() == 2);
    auto* update = as<FnDecl>(cls->members[1].get());
    REQUIRE(update != nullptr);
    CHECK(update->body.stmts.size() == 5);
}

TEST_CASE("cross-engine function with @unity/@unreal blocks", "[parser][integration]") {
    const char* src = R"(
fn UpdatePlayer(player: Movable) -> nil {
    MoveForward(player, 10, Time.deltaTime)
    @unity { player.GetComponent<Rigidbody>()?.AddForce(Vec3(0, 1, 0)) }
    @unreal { player.AddMovementInput(Vec3(0, 1, 0)) }
})";
    auto prog = parse(src);
    REQUIRE(prog.decls.size() == 1);
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    CHECK(fn->name == "UpdatePlayer");
    CHECK(fn->body.stmts.size() == 3);
    auto* unity  = as<EngineBlock>(fn->body.stmts[1].get());
    REQUIRE(unity != nullptr);
    CHECK(unity->engine == "unity");
    auto* unreal = as<EngineBlock>(fn->body.stmts[2].get());
    REQUIRE(unreal != nullptr);
    CHECK(unreal->engine == "unreal");
}

TEST_CASE("spawn enemies fn with nested for + engine blocks", "[parser][integration]") {
    const char* src = R"(
fn SpawnEnemies() -> nil {
    for let i in 0..<10 {
        let pos = Vec3(i * 100, 0, 0)
        @unreal { World.get().SpawnActor(Enemy, pos) }
        @unity  { instantiate(enemyPrefab, pos) }
    }
})";
    auto prog = parse(src);
    REQUIRE(prog.decls.size() == 1);
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    CHECK(fn->name == "SpawnEnemies");
    REQUIRE(fn->body.stmts.size() == 1);
    auto* fs = as<ForStmt>(fn->body.stmts[0].get());
    REQUIRE(fs != nullptr);
    CHECK(fs->var_name == "i");
    CHECK(fs->body.stmts.size() == 3);
}

TEST_CASE("on_reload hook function", "[parser][integration]") {
    const char* src = "fn on_reload(old: Any) -> Any { return old }";
    auto prog = parse(src);
    REQUIRE(prog.decls.size() == 1);
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr);
    CHECK(fn->name == "on_reload");
    REQUIRE(fn->params.size() == 1);
    CHECK(fn->params[0].name == "old");
}
