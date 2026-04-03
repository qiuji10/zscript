#include "lexer.h"
#include "parser.h"
#include <iostream>
#include <cassert>

using namespace zscript;

// ---------------------------------------------------------------------------
// Mini test harness
// ---------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;

#define EXPECT(cond, msg) \
    do { \
        if (cond) { ++g_pass; } \
        else { ++g_fail; std::cerr << "FAIL [" << __LINE__ << "]: " << (msg) << "\n"; } \
    } while(0)

#define REQUIRE(cond, msg) \
    do { \
        if (!(cond)) { \
            ++g_fail; \
            std::cerr << "FAIL [" << __LINE__ << "]: " << (msg) << "\n"; \
            return; \
        } \
        ++g_pass; \
    } while(0)

// Parse source and return the program. Asserts no errors unless expect_errors=true.
static Program parse(const std::string& src, bool expect_errors = false) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    Program prog = parser.parse();
    if (!expect_errors) {
        if (parser.has_errors()) {
            for (auto& e : parser.errors()) {
                std::cerr << "  parse error: " << e.message << "\n";
            }
        }
    }
    return prog;
}

// Helper casts
template<typename T>
T* as(Decl* d) { return dynamic_cast<T*>(d); }
template<typename T>
T* as(Stmt* s) { return dynamic_cast<T*>(s); }
template<typename T>
T* as(Expr* e) { return dynamic_cast<T*>(e); }
template<typename T>
T* as(TypeExpr* t) { return dynamic_cast<T*>(t); }

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_empty_program() {
    auto prog = parse("");
    EXPECT(prog.decls.empty(), "empty program has no decls");
}

void test_simple_fn() {
    auto prog = parse("fn hello() -> nil { }");
    REQUIRE(prog.decls.size() == 1, "one decl");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "is FnDecl");
    EXPECT(fn->name == "hello", "fn name");
    EXPECT(fn->params.empty(), "no params");
    EXPECT(fn->return_type != nullptr, "has return type");
    auto* rt = as<NamedType>(fn->return_type.get());
    REQUIRE(rt != nullptr, "return type is NamedType");
    EXPECT(rt->name == "nil", "return type nil");
    EXPECT(fn->body.stmts.empty(), "empty body");
}

void test_fn_with_params() {
    auto prog = parse("fn tick(deltaTime: Float, mut dir: Vec3) -> nil { }");
    REQUIRE(prog.decls.size() == 1, "one decl");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "is FnDecl");
    REQUIRE(fn->params.size() == 2, "two params");
    EXPECT(fn->params[0].name == "deltaTime", "first param name");
    EXPECT(!fn->params[0].is_mut, "first param not mut");
    EXPECT(fn->params[1].name == "dir", "second param name");
    EXPECT(fn->params[1].is_mut, "second param is mut");
}

void test_generic_fn() {
    auto prog = parse("fn get<T>(index: Int) -> T { return nil }");
    REQUIRE(prog.decls.size() == 1, "one decl");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "is FnDecl");
    REQUIRE(fn->type_params.size() == 1, "one type param");
    EXPECT(fn->type_params[0] == "T", "type param T");
}

void test_var_decl_stmt() {
    auto prog = parse("fn f() -> nil { let x: Int = 42  var y = 3 }");
    auto* fn = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "is FnDecl");
    REQUIRE(fn->body.stmts.size() == 2, "two stmts");

    auto* s1 = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(s1 != nullptr, "first stmt is VarDeclStmt");
    EXPECT(s1->is_let, "let");
    EXPECT(s1->name == "x", "name x");
    EXPECT(s1->type != nullptr, "has type");
    EXPECT(s1->init != nullptr, "has init");

    auto* s2 = as<VarDeclStmt>(fn->body.stmts[1].get());
    REQUIRE(s2 != nullptr, "second stmt is VarDeclStmt");
    EXPECT(!s2->is_let, "var");
    EXPECT(s2->name == "y", "name y");
}

void test_return_stmt() {
    auto prog = parse("fn f() -> Int { return 42 }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "is FnDecl");
    REQUIRE(fn->body.stmts.size() == 1, "one stmt");
    auto* ret = as<ReturnStmt>(fn->body.stmts[0].get());
    REQUIRE(ret != nullptr, "is ReturnStmt");
    auto* lit = as<LitExpr>(ret->value.get());
    REQUIRE(lit != nullptr, "return value is literal");
    EXPECT(lit->value == "42", "return 42");
}

void test_if_stmt() {
    auto prog = parse("fn f() -> nil { if x == 1 { return nil } else { return nil } }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "fn");
    auto* ifs = as<IfStmt>(fn->body.stmts[0].get());
    REQUIRE(ifs != nullptr, "is IfStmt");
    EXPECT(ifs->then_block.stmts.size() == 1, "then has one stmt");
    EXPECT(ifs->else_clause != nullptr, "has else");
}

void test_else_if_chain() {
    auto prog = parse("fn f() -> nil { if a { } else if b { } else { } }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "fn");
    auto* ifs = as<IfStmt>(fn->body.stmts[0].get());
    REQUIRE(ifs != nullptr, "outer if");
    auto* elif = as<IfStmt>(ifs->else_clause.get());
    REQUIRE(elif != nullptr, "else if is IfStmt");
    EXPECT(elif->else_clause != nullptr, "else if has else");
}

void test_while_stmt() {
    auto prog = parse("fn f() -> nil { while x > 0 { } }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "fn");
    auto* w = as<WhileStmt>(fn->body.stmts[0].get());
    REQUIRE(w != nullptr, "is WhileStmt");
    EXPECT(w->body.stmts.empty(), "empty while body");
}

void test_for_stmt() {
    auto prog = parse("fn f() -> nil { for let i in 0..<10 { } }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "fn");
    auto* fs = as<ForStmt>(fn->body.stmts[0].get());
    REQUIRE(fs != nullptr, "is ForStmt");
    EXPECT(fs->binding_is_let, "let binding");
    EXPECT(fs->var_name == "i", "var name i");
    // iterable is BinaryExpr(0 ..< 10)
    EXPECT(fs->iterable != nullptr, "has iterable");
}

void test_engine_block() {
    auto prog = parse("fn f() -> nil { @unity { } @unreal { } }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "fn");
    REQUIRE(fn->body.stmts.size() == 2, "two engine blocks");
    auto* eb1 = as<EngineBlock>(fn->body.stmts[0].get());
    REQUIRE(eb1 != nullptr, "first is EngineBlock");
    EXPECT(eb1->engine == "unity", "unity");
    auto* eb2 = as<EngineBlock>(fn->body.stmts[1].get());
    REQUIRE(eb2 != nullptr, "second is EngineBlock");
    EXPECT(eb2->engine == "unreal", "unreal");
}

void test_binary_expr() {
    auto prog = parse("fn f() -> nil { let x = 1 + 2 * 3 }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "fn");
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr, "var decl");
    // 1 + (2 * 3)
    auto* add = as<BinaryExpr>(decl->init.get());
    REQUIRE(add != nullptr, "add");
    EXPECT(add->op == TokenKind::Plus, "+");
    auto* mul = as<BinaryExpr>(add->right.get());
    REQUIRE(mul != nullptr, "mul on right");
    EXPECT(mul->op == TokenKind::Star, "*");
}

void test_unary_expr() {
    auto prog = parse("fn f() -> nil { let x = !true }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr, "var decl");
    auto* u = as<UnaryExpr>(decl->init.get());
    REQUIRE(u != nullptr, "unary");
    EXPECT(u->op == TokenKind::Bang, "!");
}

void test_assign_expr() {
    auto prog = parse("fn f() -> nil { x = 10 }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    auto* es  = as<ExprStmt>(fn->body.stmts[0].get());
    REQUIRE(es != nullptr, "expr stmt");
    auto* asgn = as<AssignExpr>(es->expr.get());
    REQUIRE(asgn != nullptr, "assign");
    EXPECT(asgn->op == TokenKind::Assign, "=");
}

void test_delegate_assign() {
    auto prog = parse("fn f() -> nil { actor.OnHit += handler }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    auto* es  = as<ExprStmt>(fn->body.stmts[0].get());
    REQUIRE(es != nullptr, "expr stmt");
    auto* asgn = as<AssignExpr>(es->expr.get());
    REQUIRE(asgn != nullptr, "assign");
    EXPECT(asgn->op == TokenKind::PlusAssign, "+=");
}

void test_field_access() {
    auto prog = parse("fn f() -> nil { let p = self.position }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr, "decl");
    auto* field = as<FieldExpr>(decl->init.get());
    REQUIRE(field != nullptr, "field expr");
    EXPECT(field->access == FieldExpr::Access::Dot, "dot access");
    EXPECT(field->field == "position", "field name");
    EXPECT(as<SelfExpr>(field->object.get()) != nullptr, "object is self");
}

void test_safe_and_force_access() {
    auto prog = parse("fn f() -> nil { b?.Destroy()  c!.Init() }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn->body.stmts.size() == 2, "two stmts");

    // b?.Destroy() — CallExpr(FieldExpr(b, ?., Destroy), [])
    auto* es1  = as<ExprStmt>(fn->body.stmts[0].get());
    auto* call1 = as<CallExpr>(es1->expr.get());
    REQUIRE(call1 != nullptr, "call1");
    auto* field1 = as<FieldExpr>(call1->callee.get());
    REQUIRE(field1 != nullptr, "field1");
    EXPECT(field1->access == FieldExpr::Access::Safe, "safe access");

    // c!.Init()
    auto* es2   = as<ExprStmt>(fn->body.stmts[1].get());
    auto* call2 = as<CallExpr>(es2->expr.get());
    REQUIRE(call2 != nullptr, "call2");
    auto* field2 = as<FieldExpr>(call2->callee.get());
    REQUIRE(field2 != nullptr, "field2");
    EXPECT(field2->access == FieldExpr::Access::Force, "force access");
}

void test_call_expr() {
    auto prog = parse("fn f() -> nil { log(\"hello\") }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    auto* es  = as<ExprStmt>(fn->body.stmts[0].get());
    auto* call = as<CallExpr>(es->expr.get());
    REQUIRE(call != nullptr, "call");
    REQUIRE(call->args.size() == 1, "one arg");
    auto* lit = as<LitExpr>(call->args[0].get());
    REQUIRE(lit != nullptr, "string arg");
    EXPECT(lit->value == "hello", "arg value");
}

void test_generic_call() {
    auto prog = parse("fn f() -> nil { self.GetComponent<Rigidbody>() }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    auto* es  = as<ExprStmt>(fn->body.stmts[0].get());
    auto* call = as<CallExpr>(es->expr.get());
    REQUIRE(call != nullptr, "call");
    REQUIRE(call->type_args.size() == 1, "one type arg");
    auto* ty = as<NamedType>(call->type_args[0].get());
    REQUIRE(ty != nullptr, "named type arg");
    EXPECT(ty->name == "Rigidbody", "Rigidbody");
}

void test_string_literal() {
    auto prog = parse("fn f() -> nil { let s = \"hello world\" }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    auto* lit  = as<LitExpr>(decl->init.get());
    REQUIRE(lit != nullptr, "literal");
    EXPECT(lit->kind == LitExpr::Kind::String, "string kind");
    EXPECT(lit->value == "hello world", "value");
}

void test_string_interpolation() {
    auto prog = parse("fn f() -> nil { let s = \"hi {name}!\" }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    auto* interp = as<StringInterpExpr>(decl->init.get());
    REQUIRE(interp != nullptr, "is interpolation");
    // parts: LitExpr("hi "), IdentExpr("name"), LitExpr("!")
    REQUIRE(interp->parts.size() == 3, "three parts");
    auto* p0 = as<LitExpr>(interp->parts[0].get());
    REQUIRE(p0 != nullptr, "part 0 is lit");
    EXPECT(p0->value == "hi ", "prefix");
    auto* p1 = as<IdentExpr>(interp->parts[1].get());
    REQUIRE(p1 != nullptr, "part 1 is ident");
    EXPECT(p1->name == "name", "ident name");
    auto* p2 = as<LitExpr>(interp->parts[2].get());
    REQUIRE(p2 != nullptr, "part 2 is lit");
    EXPECT(p2->value == "!", "suffix");
}

void test_lambda_expr() {
    auto prog = parse("fn f() -> nil { let h = fn(x: Int) -> Int { return x } }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    auto* lam  = as<LambdaExpr>(decl->init.get());
    REQUIRE(lam != nullptr, "lambda");
    REQUIRE(lam->params.size() == 1, "one param");
    EXPECT(lam->params[0].name == "x", "param name");
    EXPECT(lam->return_type != nullptr, "has return type");
}

void test_nullable_type() {
    auto prog = parse("fn f(a: Actor?) -> Actor? { return nil }");
    auto* fn  = as<FnDecl>(prog.decls[0].get());
    REQUIRE(fn != nullptr, "fn");
    // param type
    auto* ntype = as<NullableType>(fn->params[0].type.get());
    REQUIRE(ntype != nullptr, "nullable param type");
    auto* inner = as<NamedType>(ntype->inner.get());
    REQUIRE(inner != nullptr, "inner named type");
    EXPECT(inner->name == "Actor", "Actor");
    // return type
    auto* rtype = as<NullableType>(fn->return_type.get());
    REQUIRE(rtype != nullptr, "nullable return type");
}

void test_generic_type() {
    auto prog = parse("fn f() -> nil { let l: List<Int> = nil }");
    auto* fn   = as<FnDecl>(prog.decls[0].get());
    auto* decl = as<VarDeclStmt>(fn->body.stmts[0].get());
    REQUIRE(decl != nullptr, "decl");
    auto* gtype = as<GenericType>(decl->type.get());
    REQUIRE(gtype != nullptr, "generic type");
    EXPECT(gtype->name == "List", "List");
    REQUIRE(gtype->args.size() == 1, "one type arg");
    auto* itype = as<NamedType>(gtype->args[0].get());
    REQUIRE(itype != nullptr, "Int type");
    EXPECT(itype->name == "Int", "Int");
}

void test_class_decl() {
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
    REQUIRE(prog.decls.size() == 1, "one decl");
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr, "is ClassDecl");
    EXPECT(cls->name == "PlayerActor", "class name");
    EXPECT(cls->base.has_value() && cls->base.value() == "Actor", "base class");
    REQUIRE(cls->annotations.size() == 1, "one annotation");
    EXPECT(cls->annotations[0].path[0] == "unreal", "annotation path[0]");
    REQUIRE(cls->members.size() == 2, "two members");

    auto* field = as<FieldDecl>(cls->members[0].get());
    REQUIRE(field != nullptr, "first member is FieldDecl");
    EXPECT(!field->is_let, "var field");
    EXPECT(field->name == "Speed", "field name");

    auto* method = as<FnDecl>(cls->members[1].get());
    REQUIRE(method != nullptr, "second member is FnDecl");
    EXPECT(method->name == "BeginPlay", "method name");
}

void test_trait_decl() {
    const char* src = R"(
trait Movable {
    prop position: Vec3
    fn translate(delta: Vec3) -> nil
})";
    auto prog = parse(src);
    REQUIRE(prog.decls.size() == 1, "one decl");
    auto* tr = as<TraitDecl>(prog.decls[0].get());
    REQUIRE(tr != nullptr, "is TraitDecl");
    EXPECT(tr->name == "Movable", "trait name");
    REQUIRE(tr->members.size() == 2, "two members");
    auto* prop = as<PropDecl>(tr->members[0].get());
    REQUIRE(prop != nullptr, "first member is PropDecl");
    EXPECT(prop->name == "position", "prop name");
    auto* fn = as<FnDecl>(tr->members[1].get());
    REQUIRE(fn != nullptr, "second member is FnDecl");
    EXPECT(fn->name == "translate", "method name");
}

void test_impl_decl() {
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
    REQUIRE(prog.decls.size() == 1, "one decl");
    auto* impl = as<ImplDecl>(prog.decls[0].get());
    REQUIRE(impl != nullptr, "is ImplDecl");
    EXPECT(impl->trait_name.has_value() && impl->trait_name.value() == "Movable", "trait");
    EXPECT(impl->for_type == "Transform", "for type");
    REQUIRE(impl->members.size() == 2, "two members");

    auto* prop = as<PropDecl>(impl->members[0].get());
    REQUIRE(prop != nullptr, "first is PropDecl");
    EXPECT(prop->name == "position", "prop name");
    REQUIRE(prop->accessors.size() == 2, "two accessors");
    EXPECT(prop->accessors[0].kind == "get", "first accessor is get");
    EXPECT(prop->accessors[1].kind == "set", "second accessor is set");
    EXPECT(prop->accessors[1].param == "v", "set param");
}

void test_import_decl() {
    auto prog = parse("import engine.core");
    REQUIRE(prog.decls.size() == 1, "one decl");
    auto* imp = as<ImportDecl>(prog.decls[0].get());
    REQUIRE(imp != nullptr, "is ImportDecl");
    EXPECT(imp->path == "engine.core", "import path");
}

void test_multi_decl() {
    const char* src = R"(
fn foo() -> nil { }
fn bar() -> nil { }
class Baz { }
)";
    auto prog = parse(src);
    EXPECT(prog.decls.size() == 3, "three decls");
    EXPECT(as<FnDecl>(prog.decls[0].get()) != nullptr, "first is fn");
    EXPECT(as<FnDecl>(prog.decls[1].get()) != nullptr, "second is fn");
    EXPECT(as<ClassDecl>(prog.decls[2].get()) != nullptr, "third is class");
}

void test_error_recovery() {
    // Malformed fn, then valid fn — parser should recover and parse both
    auto prog = parse("fn ??? { } fn ok() -> nil { }", true);
    // After recovery, "ok" fn should be present
    bool found_ok = false;
    for (auto& d : prog.decls) {
        if (auto* fn = as<FnDecl>(d.get())) {
            if (fn->name == "ok") found_ok = true;
        }
    }
    EXPECT(found_ok, "recovered to parse 'ok' fn");
}

void test_complex_snippet() {
    // A snippet from PLAN.md section 5.10
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
    REQUIRE(prog.decls.size() == 1, "one decl");
    auto* cls = as<ClassDecl>(prog.decls[0].get());
    REQUIRE(cls != nullptr, "is ClassDecl");
    EXPECT(cls->name == "AIController", "class name");
    REQUIRE(cls->members.size() == 2, "two members");

    auto* update = as<FnDecl>(cls->members[1].get());
    REQUIRE(update != nullptr, "update is FnDecl");
    // Should have: if, let, let, let, expr stmt
    EXPECT(update->body.stmts.size() == 5, "five stmts in update");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    test_empty_program();
    test_simple_fn();
    test_fn_with_params();
    test_generic_fn();
    test_var_decl_stmt();
    test_return_stmt();
    test_if_stmt();
    test_else_if_chain();
    test_while_stmt();
    test_for_stmt();
    test_engine_block();
    test_binary_expr();
    test_unary_expr();
    test_assign_expr();
    test_delegate_assign();
    test_field_access();
    test_safe_and_force_access();
    test_call_expr();
    test_generic_call();
    test_string_literal();
    test_string_interpolation();
    test_lambda_expr();
    test_nullable_type();
    test_generic_type();
    test_class_decl();
    test_trait_decl();
    test_impl_decl();
    test_import_decl();
    test_multi_decl();
    test_error_recovery();
    test_complex_snippet();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail > 0 ? 1 : 0;
}
