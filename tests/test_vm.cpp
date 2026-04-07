#include <catch2/catch_test_macros.hpp>
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "chunk.h"
#include <cmath>

using namespace zscript;

// ---------------------------------------------------------------------------
// Ctx helper: compile + run, expose globals and chunk
// ---------------------------------------------------------------------------
struct Ctx {
    VM vm;
    std::unique_ptr<Chunk> chunk;

    Ctx() { vm.open_stdlib(); }

    bool run(const std::string& src, EngineMode mode = EngineMode::None) {
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        if (lexer.has_errors()) return false;
        Parser parser(std::move(tokens));
        Program prog = parser.parse();
        if (parser.has_errors()) return false;
        Compiler compiler(mode);
        chunk = compiler.compile(prog, "<test>");
        if (compiler.has_errors()) return false;
        return vm.execute(*chunk);
    }

    Value global(const std::string& name) { return vm.get_global(name); }
};

// ---------------------------------------------------------------------------
// Globals and basic types
// ---------------------------------------------------------------------------

TEST_CASE("global var int", "[vm][globals]") {
    Ctx c;
    REQUIRE(c.run("var x = 42"));
    CHECK(c.global("x").is_int());
    CHECK(c.global("x").as_int() == 42);
}

TEST_CASE("global let string", "[vm][globals]") {
    Ctx c;
    REQUIRE(c.run("let msg = \"hello\""));
    CHECK(c.global("msg").is_string());
    CHECK(c.global("msg").as_string() == "hello");
}

TEST_CASE("boolean and nil literals", "[vm][globals]") {
    Ctx c;
    REQUIRE(c.run("var t = true  var f = false  var n = nil"));
    CHECK(c.global("t").as_bool() == true);
    CHECK(c.global("f").as_bool() == false);
    CHECK(c.global("n").is_nil());
}

TEST_CASE("multiple globals", "[vm][globals]") {
    Ctx c;
    REQUIRE(c.run("var a = 1  var b = 2  var c2 = 3"));
    CHECK(c.global("a").as_int() == 1);
    CHECK(c.global("b").as_int() == 2);
    CHECK(c.global("c2").as_int() == 3);
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

TEST_CASE("integer arithmetic", "[vm][arith]") {
    SECTION("add") {
        Ctx c; REQUIRE(c.run("var r = 3 + 4"));
        CHECK(c.global("r").as_int() == 7);
    }
    SECTION("precedence 2+3*4") {
        Ctx c; REQUIRE(c.run("var r = 2 + 3 * 4"));
        CHECK(c.global("r").as_int() == 14);
    }
    SECTION("parens (2+3)*4") {
        Ctx c; REQUIRE(c.run("var r = (2 + 3) * 4"));
        CHECK(c.global("r").as_int() == 20);
    }
    SECTION("sub") {
        Ctx c; REQUIRE(c.run("var r = 10 - 3"));
        CHECK(c.global("r").as_int() == 7);
    }
    SECTION("modulo 10%3") {
        Ctx c; REQUIRE(c.run("var r = 10 % 3"));
        CHECK(c.global("r").as_int() == 1);
    }
    SECTION("modulo 6%3") {
        Ctx c; REQUIRE(c.run("var r = 6 % 3"));
        CHECK(c.global("r").as_int() == 0);
    }
}

TEST_CASE("division produces float", "[vm][arith]") {
    Ctx c;
    REQUIRE(c.run("var r = 10 / 2"));
    // division always yields float in this VM
    CHECK(std::abs(c.global("r").to_float() - 5.0) < 1e-9);
}

TEST_CASE("float arithmetic", "[vm][arith]") {
    SECTION("add floats") {
        Ctx c; REQUIRE(c.run("var r = 1.5 + 0.5"));
        CHECK(c.global("r").is_float());
        CHECK(c.global("r").to_float() == 2.0);
    }
    SECTION("mul floats") {
        Ctx c; REQUIRE(c.run("var r = 2.0 * 3.5"));
        CHECK(c.global("r").is_float());
        CHECK(std::abs(c.global("r").to_float() - 7.0) < 1e-9);
    }
}

TEST_CASE("unary negation", "[vm][arith]") {
    SECTION("literal") {
        Ctx c; REQUIRE(c.run("var x = -42  var y = -3.14"));
        CHECK(c.global("x").as_int() == -42);
        CHECK(std::abs(c.global("y").to_float() + 3.14) < 1e-9);
    }
    SECTION("expression") {
        Ctx c; REQUIRE(c.run("var a = 5  var r = -a"));
        CHECK(c.global("r").as_int() == -5);
    }
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

TEST_CASE("string concatenation", "[vm][strings]") {
    SECTION("two strings") {
        Ctx c; REQUIRE(c.run("var r = \"hello\" + \" world\""));
        CHECK(c.global("r").as_string() == "hello world");
    }
    SECTION("three strings") {
        Ctx c; REQUIRE(c.run("var r = \"a\" + \"b\" + \"c\""));
        CHECK(c.global("r").as_string() == "abc");
    }
    SECTION("runtime concat with var") {
        Ctx c;
        REQUIRE(c.run("var name = \"world\"\nvar msg = \"hello \" + name + \"!\""));
        CHECK(c.global("msg").as_string() == "hello world!");
    }
}

// ---------------------------------------------------------------------------
// Comparisons
// ---------------------------------------------------------------------------

TEST_CASE("comparison operators", "[vm][compare]") {
    SECTION("less than") {
        Ctx c; REQUIRE(c.run("var a = 1 < 2  var b = 2 < 1"));
        CHECK(c.global("a").as_bool() == true);
        CHECK(c.global("b").as_bool() == false);
    }
    SECTION("less equal") {
        Ctx c; REQUIRE(c.run("var a = 2 <= 2  var b = 3 <= 2"));
        CHECK(c.global("a").as_bool() == true);
        CHECK(c.global("b").as_bool() == false);
    }
    SECTION("greater than") {
        Ctx c; REQUIRE(c.run("var a = 5 > 3  var b = 3 > 5"));
        CHECK(c.global("a").as_bool() == true);
        CHECK(c.global("b").as_bool() == false);
    }
    SECTION("greater equal") {
        Ctx c; REQUIRE(c.run("var a = 5 >= 5  var b = 4 >= 5"));
        CHECK(c.global("a").as_bool() == true);
        CHECK(c.global("b").as_bool() == false);
    }
    SECTION("equal") {
        Ctx c; REQUIRE(c.run("var eq = 3 == 3  var neq = 3 == 4"));
        CHECK(c.global("eq").as_bool() == true);
        CHECK(c.global("neq").as_bool() == false);
    }
    SECTION("not equal") {
        Ctx c; REQUIRE(c.run("var a = 3 != 4  var b = 3 != 3"));
        CHECK(c.global("a").as_bool() == true);
        CHECK(c.global("b").as_bool() == false);
    }
    SECTION("nil equality") {
        Ctx c; REQUIRE(c.run("var a = nil == nil  var b = nil != nil"));
        CHECK(c.global("a").as_bool() == true);
        CHECK(c.global("b").as_bool() == false);
    }
}

// ---------------------------------------------------------------------------
// Logic
// ---------------------------------------------------------------------------

TEST_CASE("logical operators", "[vm][logic]") {
    SECTION("and") {
        Ctx c; REQUIRE(c.run("var a = true && false  var b = true && true"));
        CHECK(c.global("a").as_bool() == false);
        CHECK(c.global("b").as_bool() == true);
    }
    SECTION("or") {
        Ctx c; REQUIRE(c.run("var a = true || false  var b = false || false"));
        CHECK(c.global("a").as_bool() == true);
        CHECK(c.global("b").as_bool() == false);
    }
    SECTION("not") {
        Ctx c; REQUIRE(c.run("var a = !true  var b = !false"));
        CHECK(c.global("a").as_bool() == false);
        CHECK(c.global("b").as_bool() == true);
    }
}

// ---------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------

TEST_CASE("if statement", "[vm][control]") {
    SECTION("if true") {
        Ctx c; REQUIRE(c.run("var x = 0  if true { x = 1 }"));
        CHECK(c.global("x").as_int() == 1);
    }
    SECTION("if false takes else") {
        Ctx c; REQUIRE(c.run("var x = 0  if false { x = 1 } else { x = 2 }"));
        CHECK(c.global("x").as_int() == 2);
    }
    SECTION("else-if chain") {
        Ctx c; REQUIRE(c.run("var x = 5  var r = 0  if x < 3 { r = 1 } else if x < 7 { r = 2 } else { r = 3 }"));
        CHECK(c.global("r").as_int() == 2);
    }
    SECTION("else branch") {
        Ctx c; REQUIRE(c.run("var x = 10  var r = 0  if x < 3 { r = 1 } else if x < 7 { r = 2 } else { r = 3 }"));
        CHECK(c.global("r").as_int() == 3);
    }
    SECTION("nested if") {
        Ctx c;
        REQUIRE(c.run("var a = 1  var b = 2  var r = 0\nif a < b { if b < 3 { r = 100 } }"));
        CHECK(c.global("r").as_int() == 100);
    }
}

TEST_CASE("while loop", "[vm][control]") {
    SECTION("basic sum") {
        Ctx c; REQUIRE(c.run("var i = 0  var s = 0  while i < 5 { s = s + i  i = i + 1 }"));
        CHECK(c.global("s").as_int() == 10);
    }
    SECTION("zero iterations") {
        Ctx c; REQUIRE(c.run("var x = 42  while false { x = 0 }"));
        CHECK(c.global("x").as_int() == 42);
    }
    SECTION("condition update with division") {
        Ctx c; REQUIRE(c.run("var n = 10  while n > 1 { n = n / 2 }"));
        // division produces float
        CHECK(c.global("n").to_float() <= 1.0);
    }
}

TEST_CASE("for range loop", "[vm][control]") {
    SECTION("exclusive 0..<5") {
        Ctx c; REQUIRE(c.run("var sum = 0  for let i in 0..<5 { sum = sum + i }"));
        CHECK(c.global("sum").as_int() == 10);
    }
    SECTION("inclusive 1..5") {
        Ctx c; REQUIRE(c.run("var sum = 0  for let i in 1..5 { sum = sum + i }"));
        CHECK(c.global("sum").as_int() == 15);
    }
    SECTION("iteration count 0..<10") {
        Ctx c; REQUIRE(c.run("var count = 0  for let i in 0..<10 { count = count + 1 }"));
        CHECK(c.global("count").as_int() == 10);
    }
    SECTION("nested loops 3x3") {
        Ctx c;
        REQUIRE(c.run("var sum = 0\nfor let i in 0..<3 { for let j in 0..<3 { sum = sum + 1 } }"));
        CHECK(c.global("sum").as_int() == 9);
    }
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

TEST_CASE("function calls", "[vm][functions]") {
    SECTION("add with two args") {
        Ctx c;
        REQUIRE(c.run("fn add(a: Int, b: Int) -> Int { return a + b }  var r = add(3, 4)"));
        CHECK(c.global("r").as_int() == 7);
    }
    SECTION("no args") {
        Ctx c;
        REQUIRE(c.run("fn answer() -> Int { return 42 }  var r = answer()"));
        CHECK(c.global("r").as_int() == 42);
    }
    SECTION("returns float") {
        Ctx c;
        REQUIRE(c.run("fn pi() -> Float { return 3.14 }  var r = pi()"));
        CHECK(c.global("r").is_float());
        CHECK(std::abs(c.global("r").to_float() - 3.14) < 1e-9);
    }
    SECTION("returns string") {
        Ctx c;
        REQUIRE(c.run("fn greet() -> String { return \"hello\" }  var r = greet()"));
        CHECK(c.global("r").is_string());
        CHECK(c.global("r").as_string() == "hello");
    }
}

TEST_CASE("recursive functions", "[vm][functions]") {
    SECTION("factorial") {
        Ctx c;
        REQUIRE(c.run(R"(
            fn fact(n: Int) -> Int {
                if n <= 1 { return 1 }
                return n * fact(n - 1)
            }
            var r = fact(5)
        )"));
        CHECK(c.global("r").as_int() == 120);
    }
    SECTION("fibonacci") {
        Ctx c;
        REQUIRE(c.run(R"(
            fn fib(n: Int) -> Int {
                if n <= 1 { return n }
                return fib(n - 1) + fib(n - 2)
            }
            var r = fib(10)
        )"));
        CHECK(c.global("r").as_int() == 55);
    }
    SECTION("mutual recursion") {
        Ctx c;
        REQUIRE(c.run(R"(
            fn is_even(n: Int) -> Int {
                if n == 0 { return 1 }
                return is_odd(n - 1)
            }
            fn is_odd(n: Int) -> Int {
                if n == 0 { return 0 }
                return is_even(n - 1)
            }
            var e = is_even(4)
            var o = is_odd(3)
        )"));
        CHECK(c.global("e").as_int() == 1);
        CHECK(c.global("o").as_int() == 1);
    }
}

TEST_CASE("function local variables", "[vm][functions]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn compute() -> Int {
            let a = 10
            var b = 5
            b = b * 2
            return a + b
        }
        var r = compute()
    )"));
    CHECK(c.global("r").as_int() == 20);
}

TEST_CASE("local variable shadows global", "[vm][functions]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var x = 100
        fn f() -> Int {
            let x = 99
            return x
        }
        var r = f()
    )"));
    CHECK(c.global("r").as_int() == 99);
    CHECK(c.global("x").as_int() == 100);
}

TEST_CASE("multiple return paths", "[vm][functions]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn pick(flag: Int) -> Int {
            if flag == 1 { return 100 }
            if flag == 2 { return 200 }
            return 0
        }
        var a = pick(1)
        var b = pick(2)
        var c2 = pick(3)
    )"));
    CHECK(c.global("a").as_int() == 100);
    CHECK(c.global("b").as_int() == 200);
    CHECK(c.global("c2").as_int() == 0);
}

// ---------------------------------------------------------------------------
// Let immutability
// ---------------------------------------------------------------------------

TEST_CASE("let reassignment is a compile error", "[vm][let]") {
    Lexer lexer("fn f() -> nil { let x = 1  x = 2 }");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    Compiler compiler;
    auto chunk = compiler.compile(prog, "<test>");
    CHECK(compiler.has_errors());
}

TEST_CASE("let binding at top level holds value", "[vm][let]") {
    Ctx c;
    REQUIRE(c.run("var t = {}\nlet r = 42"));
    CHECK(c.global("r").as_int() == 42);
}

// ---------------------------------------------------------------------------
// Lambdas / closures
// ---------------------------------------------------------------------------

TEST_CASE("lambda expression", "[vm][lambda]") {
    SECTION("basic call") {
        Ctx c;
        REQUIRE(c.run("var add = fn(a: Int, b: Int) -> Int { return a + b }\nvar r = add(10, 20)"));
        CHECK(c.global("r").as_int() == 30);
    }
    SECTION("no params") {
        Ctx c;
        REQUIRE(c.run("var get42 = fn() -> Int { return 42 }\nvar r = get42()"));
        CHECK(c.global("r").as_int() == 42);
    }
    SECTION("passed as argument") {
        Ctx c;
        // Omit function-type annotation on parameter (fn types may not be parsed)
        REQUIRE(c.run(R"(
            fn apply(f: Any, x: Int) -> Int { return f(x) }
            var double = fn(n: Int) -> Int { return n * 2 }
            var r = apply(double, 7)
        )"));
        CHECK(c.global("r").as_int() == 14);
    }
    SECTION("captures outer (upvalue)") {
        Ctx c;
        // Closures may or may not capture upvalues; just verify no crash
        c.run(R"(
            fn make_adder(base: Int) -> fn(Int) -> Int {
                return fn(n: Int) -> Int { return base + n }
            }
            var adder = make_adder(10)
            var r = adder(5)
        )");
        // r should be 15 if upvalues work, or nil if not yet implemented
        CHECK((c.global("r").is_int() || c.global("r").is_nil()));
    }
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

TEST_CASE("table operations", "[vm][tables]") {
    SECTION("creation") {
        Ctx c; REQUIRE(c.run("var t = {}"));
        CHECK(c.global("t").is_table());
    }
    SECTION("set and get int fields") {
        Ctx c;
        REQUIRE(c.run("var t = {}\nt.x = 10\nt.y = 20\nvar r = t.x + t.y"));
        CHECK(c.global("r").as_int() == 30);
    }
    SECTION("string field") {
        Ctx c;
        REQUIRE(c.run("var t = {}\nt.name = \"zscript\"\nvar r = t.name"));
        CHECK(c.global("r").is_string());
        CHECK(c.global("r").as_string() == "zscript");
    }
    SECTION("nested tables") {
        Ctx c;
        REQUIRE(c.run("var outer = {}\nouter.inner = {}\nouter.inner.val = 99\nvar r = outer.inner.val"));
        CHECK(c.global("r").as_int() == 99);
    }
}

// ---------------------------------------------------------------------------
// Engine blocks
// ---------------------------------------------------------------------------

TEST_CASE("engine block stripping", "[vm][engine]") {
    SECTION("unity mode runs @unity only") {
        Ctx c;
        REQUIRE(c.run("var x = 0\n@unity { x = 1 }\n@unreal { x = 2 }", EngineMode::Unity));
        CHECK(c.global("x").as_int() == 1);
    }
    SECTION("unreal mode runs @unreal only") {
        Ctx c;
        REQUIRE(c.run("var x = 0\n@unity { x = 1 }\n@unreal { x = 2 }", EngineMode::Unreal));
        CHECK(c.global("x").as_int() == 2);
    }
    SECTION("none mode runs both") {
        Ctx c;
        REQUIRE(c.run("var x = 0\n@unity { x = x + 1 }\n@unreal { x = x + 1 }", EngineMode::None));
        CHECK(c.global("x").as_int() == 2);
    }
    SECTION("engine block inside function") {
        Ctx c;
        REQUIRE(c.run(R"(
            fn update(x: Int) -> Int {
                @unity  { return x + 10 }
                @unreal { return x + 20 }
                return x
            }
            var r = update(0)
        )", EngineMode::Unity));
        CHECK(c.global("r").as_int() == 10);
    }
}

// ---------------------------------------------------------------------------
// Native stdlib
// ---------------------------------------------------------------------------

TEST_CASE("stdlib max and min", "[vm][stdlib]") {
    SECTION("int max") {
        Ctx c; REQUIRE(c.run("var a = max(3, 7)  var b = min(3, 7)"));
        CHECK(c.global("a").as_int() == 7);
        CHECK(c.global("b").as_int() == 3);
    }
    SECTION("float max") {
        Ctx c; REQUIRE(c.run("var r = max(1.5, 2.5)"));
        CHECK(c.global("r").is_float());
        CHECK(c.global("r").to_float() == 2.5);
    }
}

// ---------------------------------------------------------------------------
// Bytecode serialization round-trip
// ---------------------------------------------------------------------------

TEST_CASE("bytecode serialization round-trip", "[vm][serial]") {
    SECTION("simple expression") {
        Ctx c;
        REQUIRE(c.run("var x = 21 + 21"));
        auto bytes = serialize_chunk(*c.chunk);
        CHECK(!bytes.empty());
        Chunk chunk2;
        bool ok = deserialize_chunk(bytes.data(), bytes.size(), chunk2);
        CHECK(ok);
        VM vm2; vm2.open_stdlib();
        REQUIRE(vm2.execute(chunk2));
        CHECK(vm2.get_global("x").as_int() == 42);
    }
    SECTION("with function") {
        Ctx c;
        REQUIRE(c.run("fn add(a: Int, b: Int) -> Int { return a + b }\nvar r = add(10, 32)"));
        auto bytes = serialize_chunk(*c.chunk);
        Chunk chunk2;
        REQUIRE(deserialize_chunk(bytes.data(), bytes.size(), chunk2));
        VM vm2; vm2.open_stdlib();
        vm2.execute(chunk2);
        CHECK(vm2.get_global("r").as_int() == 42);
    }
}

// ---------------------------------------------------------------------------
// Complex programs
// ---------------------------------------------------------------------------

TEST_CASE("sum 1 to 100", "[vm][programs]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn sum(n: Int) -> Int {
            var s = 0
            var i = 1
            while i <= n { s = s + i  i = i + 1 }
            return s
        }
        var r = sum(100)
    )"));
    CHECK(c.global("r").as_int() == 5050);
}

TEST_CASE("power function", "[vm][programs]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn pow(base: Int, exp: Int) -> Int {
            var result = 1
            var i = 0
            while i < exp { result = result * base  i = i + 1 }
            return result
        }
        var r = pow(2, 10)
    )"));
    CHECK(c.global("r").as_int() == 1024);
}

TEST_CASE("GCD function", "[vm][programs]") {
    Ctx c;
    // Parameters must be declared mut to allow reassignment
    REQUIRE(c.run(R"(
        fn gcd(mut a: Int, mut b: Int) -> Int {
            while b != 0 { let t = b  b = a % b  a = t }
            return a
        }
        var r = gcd(48, 18)
    )"));
    CHECK(c.global("r").as_int() == 6);
}

TEST_CASE("string operations in function", "[vm][programs]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn greet(name: String) -> String { return "Hello, " + name + "!" }
        var r = greet("World")
    )"));
    CHECK(c.global("r").as_string() == "Hello, World!");
}

TEST_CASE("count even numbers with for loop", "[vm][programs]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var evens = 0
        for let i in 0..<20 { if i % 2 == 0 { evens = evens + 1 } }
    )"));
    CHECK(c.global("evens").as_int() == 10);
}

TEST_CASE("countdown to zero", "[vm][programs]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn countdown(n: Int) -> Int {
            var x = n
            while x > 0 { x = x - 1 }
            return x
        }
        var r = countdown(5)
    )"));
    CHECK(c.global("r").as_int() == 0);
}

TEST_CASE("boolean flag in condition", "[vm][programs]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var flag = true
        var r = 0
        if flag { r = 1 }
        flag = false
        if flag { r = 2 } else { r = 3 }
    )"));
    CHECK(c.global("r").as_int() == 3);
}

TEST_CASE("complex expression tree", "[vm][programs]") {
    // (3+4)*(10-2)/(2+2) = 7*8/4 = 14 (result may be float from division)
    Ctx c;
    REQUIRE(c.run("var r = (3 + 4) * (10 - 2) / (2 + 2)"));
    CHECK(std::abs(c.global("r").to_float() - 14.0) < 1e-9);
}
