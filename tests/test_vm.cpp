#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "chunk.h"
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

// ---------------------------------------------------------------------------
// Helper: compile + run, return VM for inspection
// ---------------------------------------------------------------------------
struct Ctx {
    VM vm;
    std::unique_ptr<Chunk> chunk;
    bool ok = false;

    Ctx() { vm.open_stdlib(); }

    bool run(const std::string& src, EngineMode mode = EngineMode::None) {
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        if (lexer.has_errors()) {
            std::cerr << "  lex error\n";
            return false;
        }
        Parser parser(std::move(tokens));
        Program prog = parser.parse();
        if (parser.has_errors()) {
            for (auto& e : parser.errors())
                std::cerr << "  parse error: " << e.message << "\n";
            return false;
        }
        Compiler compiler(mode);
        chunk = compiler.compile(prog, "<test>");
        if (compiler.has_errors()) {
            for (auto& e : compiler.errors())
                std::cerr << "  compile error: " << e.message << "\n";
            return false;
        }
        ok = vm.execute(*chunk);
        if (!ok) {
            std::cerr << "  runtime error: " << vm.last_error().message << "\n";
        }
        return ok;
    }

    Value global(const std::string& name) { return vm.get_global(name); }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_global_var() {
    Ctx c;
    REQUIRE(c.run("var x = 42"), "runs");
    EXPECT(c.global("x").is_int(), "x is int");
    EXPECT(c.global("x").as_int() == 42, "x == 42");
}

void test_global_let() {
    Ctx c;
    REQUIRE(c.run("let msg = \"hello\""), "runs");
    EXPECT(c.global("msg").is_string(), "msg is string");
    EXPECT(c.global("msg").as_string() == "hello", "msg == hello");
}

void test_arithmetic() {
    Ctx c;
    c.run("var r = 2 + 3 * 4");
    EXPECT(c.global("r").as_int() == 14, "2+3*4=14");
}

void test_float_arith() {
    Ctx c;
    c.run("var r = 1.5 + 0.5");
    EXPECT(c.global("r").is_float(), "float result");
    EXPECT(c.global("r").to_float() == 2.0, "1.5+0.5=2.0");
}

void test_string_concat() {
    Ctx c;
    c.run("var r = \"hello\" + \" world\"");
    EXPECT(c.global("r").as_string() == "hello world", "concat");
}

void test_comparison() {
    Ctx c;
    c.run("var a = 1 < 2  var b = 2 < 1  var eq = 3 == 3");
    EXPECT(c.global("a").as_bool() == true,  "1<2");
    EXPECT(c.global("b").as_bool() == false, "2<1");
    EXPECT(c.global("eq").as_bool() == true, "3==3");
}

void test_logic() {
    Ctx c;
    c.run("var a = true && false  var b = true || false  var n = !true");
    EXPECT(c.global("a").as_bool() == false, "and");
    EXPECT(c.global("b").as_bool() == true,  "or");
    EXPECT(c.global("n").as_bool() == false, "not");
}

void test_if_true() {
    Ctx c;
    c.run("var x = 0  if true { x = 1 }");
    EXPECT(c.global("x").as_int() == 1, "if true executes");
}

void test_if_false() {
    Ctx c;
    c.run("var x = 0  if false { x = 1 } else { x = 2 }");
    EXPECT(c.global("x").as_int() == 2, "if false takes else");
}

void test_if_else_if() {
    Ctx c;
    c.run("var x = 5  var r = 0  if x < 3 { r = 1 } else if x < 7 { r = 2 } else { r = 3 }");
    EXPECT(c.global("r").as_int() == 2, "else if branch");
}

void test_while() {
    Ctx c;
    c.run("var i = 0  var s = 0  while i < 5 { s = s + i  i = i + 1 }");
    EXPECT(c.global("s").as_int() == 10, "while sum 0..4");
}

void test_for_range_exclusive() {
    Ctx c;
    c.run("var sum = 0  for let i in 0..<5 { sum = sum + i }");
    EXPECT(c.global("sum").as_int() == 10, "for 0..<5 sum=10");
}

void test_for_range_inclusive() {
    Ctx c;
    c.run("var sum = 0  for let i in 1..5 { sum = sum + i }");
    EXPECT(c.global("sum").as_int() == 15, "for 1..5 sum=15");
}

void test_function_call() {
    Ctx c;
    c.run("fn add(a: Int, b: Int) -> Int { return a + b }  var r = add(3, 4)");
    EXPECT(c.global("r").as_int() == 7, "fn add(3,4)=7");
}

void test_recursive_function() {
    Ctx c;
    c.run(R"(
        fn fact(n: Int) -> Int {
            if n <= 1 { return 1 }
            return n * fact(n - 1)
        }
        var r = fact(5)
    )");
    EXPECT(c.global("r").as_int() == 120, "fact(5)=120");
}

void test_nested_function_call() {
    Ctx c;
    c.run(R"(
        fn square(n: Int) -> Int { return n * n }
        fn sum_squares(a: Int, b: Int) -> Int { return square(a) + square(b) }
        var r = sum_squares(3, 4)
    )");
    EXPECT(c.global("r").as_int() == 25, "3^2+4^2=25");
}

void test_let_immutability_error() {
    // Compiler should emit an error for reassigning a let
    Lexer lexer("fn f() -> nil { let x = 1  x = 2 }");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    Program prog = parser.parse();
    Compiler compiler;
    auto chunk = compiler.compile(prog, "<test>");
    EXPECT(compiler.has_errors(), "let reassign should produce compile error");
}

void test_native_stdlib() {
    Ctx c;
    // max and min
    c.run("var a = max(3, 7)  var b = min(3, 7)");
    EXPECT(c.global("a").as_int() == 7, "max(3,7)=7");
    EXPECT(c.global("b").as_int() == 3, "min(3,7)=3");
}

void test_boolean_literals() {
    Ctx c;
    c.run("var t = true  var f = false  var n = nil");
    EXPECT(c.global("t").as_bool() == true,  "true");
    EXPECT(c.global("f").as_bool() == false, "false");
    EXPECT(c.global("n").is_nil(),            "nil");
}

void test_unary_neg() {
    Ctx c;
    c.run("var x = -42  var y = -3.14");
    EXPECT(c.global("x").as_int() == -42,    "-42");
    EXPECT(c.global("y").to_float() == -3.14, "-3.14");
}

void test_lambda() {
    Ctx c;
    c.run(R"(
        var add = fn(a: Int, b: Int) -> Int { return a + b }
        var r = add(10, 20)
    )");
    EXPECT(c.global("r").as_int() == 30, "lambda(10,20)=30");
}

void test_table_field() {
    Ctx c;
    c.run(R"(
        var t = {}
    )");
    // Table creation — just check it doesn't crash and is a table
    EXPECT(c.global("t").is_table(), "t is table");
}

void test_multiple_returns() {
    Ctx c;
    c.run(R"(
        fn pick(flag: Int) -> Int {
            if flag == 1 { return 100 }
            if flag == 2 { return 200 }
            return 0
        }
        var a = pick(1)
        var b = pick(2)
        var c2 = pick(3)
    )");
    EXPECT(c.global("a").as_int() == 100, "pick(1)=100");
    EXPECT(c.global("b").as_int() == 200, "pick(2)=200");
    EXPECT(c.global("c2").as_int() == 0,  "pick(3)=0");
}

void test_engine_block_stripped() {
    // In Unity mode, @unreal block should be skipped
    Ctx c;
    c.run(R"(
        var x = 0
        @unity   { x = 1 }
        @unreal  { x = 2 }
    )", EngineMode::Unity);
    EXPECT(c.global("x").as_int() == 1, "@unity block runs, @unreal skipped");
}

void test_engine_block_none() {
    // In None mode, both blocks run
    Ctx c;
    c.run(R"(
        var x = 0
        @unity   { x = x + 1 }
        @unreal  { x = x + 1 }
    )", EngineMode::None);
    EXPECT(c.global("x").as_int() == 2, "both engine blocks run in None mode");
}

void test_serialization() {
    // Compile, serialize, deserialize, re-execute
    Ctx c;
    REQUIRE(c.run("var x = 21 + 21"), "initial run");

    auto bytes = serialize_chunk(*c.chunk);
    EXPECT(!bytes.empty(), "serialized non-empty");

    Chunk chunk2;
    bool ok = deserialize_chunk(bytes.data(), bytes.size(), chunk2);
    EXPECT(ok, "deserialized ok");

    VM vm2;
    vm2.open_stdlib();
    bool exec_ok = vm2.execute(chunk2);
    EXPECT(exec_ok, "re-executed ok");
    EXPECT(vm2.get_global("x").as_int() == 42, "x==42 after round-trip");
}

void test_string_interp_runtime() {
    Ctx c;
    c.run(R"(
        var name = "world"
        var msg = "hello " + name + "!"
        var r = msg
    )");
    EXPECT(c.global("r").as_string() == "hello world!", "concat at runtime");
}

void test_fibonacci() {
    Ctx c;
    c.run(R"(
        fn fib(n: Int) -> Int {
            if n <= 1 { return n }
            return fib(n - 1) + fib(n - 2)
        }
        var r = fib(10)
    )");
    EXPECT(c.global("r").as_int() == 55, "fib(10)=55");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    test_global_var();
    test_global_let();
    test_arithmetic();
    test_float_arith();
    test_string_concat();
    test_comparison();
    test_logic();
    test_if_true();
    test_if_false();
    test_if_else_if();
    test_while();
    test_for_range_exclusive();
    test_for_range_inclusive();
    test_function_call();
    test_recursive_function();
    test_nested_function_call();
    test_let_immutability_error();
    test_native_stdlib();
    test_boolean_literals();
    test_unary_neg();
    test_lambda();
    test_table_field();
    test_multiple_returns();
    test_engine_block_stripped();
    test_engine_block_none();
    test_serialization();
    test_string_interp_runtime();
    test_fibonacci();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail > 0 ? 1 : 0;
}
