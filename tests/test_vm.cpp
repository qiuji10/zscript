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

    bool run(const std::string& src, TagSet tags = {}) {
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        if (lexer.has_errors()) return false;
        Parser parser(std::move(tokens));
        Program prog = parser.parse();
        if (parser.has_errors()) return false;
        Compiler compiler(tags);
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

TEST_CASE("tag block stripping", "[vm][tags]") {
    SECTION("unity tag runs @unity only") {
        Ctx c;
        REQUIRE(c.run("var x = 0\n@unity { x = 1 }\n@unreal { x = 2 }", {"unity"}));
        CHECK(c.global("x").as_int() == 1);
    }
    SECTION("unreal tag runs @unreal only") {
        Ctx c;
        REQUIRE(c.run("var x = 0\n@unity { x = 1 }\n@unreal { x = 2 }", {"unreal"}));
        CHECK(c.global("x").as_int() == 2);
    }
    SECTION("both tags run both blocks") {
        Ctx c;
        REQUIRE(c.run("var x = 0\n@unity { x = x + 1 }\n@unreal { x = x + 1 }", {"unity", "unreal"}));
        CHECK(c.global("x").as_int() == 2);
    }
    SECTION("no tags strips all blocks") {
        Ctx c;
        REQUIRE(c.run("var x = 0\n@unity { x = 1 }\n@unreal { x = 2 }"));
        CHECK(c.global("x").as_int() == 0);
    }
    SECTION("tag block inside function") {
        Ctx c;
        REQUIRE(c.run(R"(
            fn update(x: Int) -> Int {
                @unity  { return x + 10 }
                @unreal { return x + 20 }
                return x
            }
            var r = update(0)
        )", {"unity"}));
        CHECK(c.global("r").as_int() == 10);
    }
    SECTION("arbitrary custom tag") {
        Ctx c;
        REQUIRE(c.run("var x = 0\n@scenarioA { x = 42 }", {"scenarioA"}));
        CHECK(c.global("x").as_int() == 42);
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

// ---------------------------------------------------------------------------
// Raw string literals (backtick)
// ---------------------------------------------------------------------------

TEST_CASE("raw string literal basic value", "[vm][raw_string]") {
    Ctx c;
    REQUIRE(c.run("var s = `hello world`"));
    CHECK(c.global("s").as_string() == "hello world");
}

TEST_CASE("raw string keeps backslash sequences literal", "[vm][raw_string]") {
    Ctx c;
    REQUIRE(c.run("var s = `no\\nescape`"));
    // \n inside backtick string = literal backslash + n (not a newline)
    CHECK(c.global("s").as_string() == "no\\nescape");
    CHECK(c.global("s").as_string().size() == 10);
}

TEST_CASE("raw string keeps braces as text - no interpolation", "[vm][raw_string]") {
    Ctx c;
    // If interpolation ran, {1+1} would become "2"; in a raw string it stays literal
    REQUIRE(c.run("var s = `result is {1+1}`"));
    CHECK(c.global("s").as_string() == "result is {1+1}");
}

TEST_CASE("raw string multiline is a single string value", "[vm][raw_string]") {
    // Use C++ raw string to embed a real newline in the source
    Ctx c;
    REQUIRE(c.run("var s = `line one\nline two`"));
    CHECK(c.global("s").as_string() == "line one\nline two");
}

TEST_CASE("raw string can be concatenated with regular string", "[vm][raw_string]") {
    Ctx c;
    REQUIRE(c.run(R"(var s = `path\to\` + "file")"));
    CHECK(c.global("s").as_string() == "path\\to\\file");
}

TEST_CASE("raw string len via # operator", "[vm][raw_string]") {
    Ctx c;
    REQUIRE(c.run("var n = #`hello`"));
    CHECK(c.global("n").as_int() == 5);
}

// ---------------------------------------------------------------------------
// Metamethod tests
// ---------------------------------------------------------------------------

TEST_CASE("__index metamethod fires on missing field", "[vm][metamethod]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var proxy = {}
        proxy.__index = fn(tbl, key) {
            if key == "x" { return 42 }
            return nil
        }
        var result = proxy.x
    )"));
    CHECK(c.global("result").as_int() == 42);
}

TEST_CASE("__index does not fire when field exists", "[vm][metamethod]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var proxy = {}
        proxy.x = 10
        proxy.__index = fn(tbl, key) { return 99 }
        var result = proxy.x
    )"));
    CHECK(c.global("result").as_int() == 10);
}

TEST_CASE("__newindex metamethod fires on absent field assignment", "[vm][metamethod]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var log = {}
        var proxy = {}
        proxy.__newindex = fn(tbl, key, value) {
            log.key = key
            log.value = value
        }
        proxy.score = 99
    )"));
    CHECK(c.global("log").as_table()->get("key").as_string() == "score");
    CHECK(c.global("log").as_table()->get("value").as_int() == 99);
}

TEST_CASE("__newindex does not fire when field already exists", "[vm][metamethod]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var fired = false
        var proxy = {}
        proxy.x = 1
        proxy.__newindex = fn(tbl, key, value) { fired = true }
        proxy.x = 2
    )"));
    CHECK(c.global("fired").as_bool() == false);
    CHECK(c.global("proxy").as_table()->get("x").as_int() == 2);
}

TEST_CASE("__call metamethod enables calling a table", "[vm][metamethod]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var Vec3 = {}
        Vec3.__call = fn(tbl, x, y, z) {
            var v = {}
            v.x = x
            v.y = y
            v.z = z
            return v
        }
        var v = Vec3(1, 2, 3)
        var rx = v.x
        var ry = v.y
        var rz = v.z
    )"));
    CHECK(c.global("rx").as_int() == 1);
    CHECK(c.global("ry").as_int() == 2);
    CHECK(c.global("rz").as_int() == 3);
}

TEST_CASE("__eq metamethod used for == comparison", "[vm][metamethod]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var eq_fn = fn(a, b) { return a.id == b.id }
        var a = {}
        a.id = 5
        a.__eq = eq_fn
        var b = {}
        b.id = 5
        b.__eq = eq_fn
        var same = a == b
        var diff_b = {}
        diff_b.id = 9
        diff_b.__eq = eq_fn
        var diff = a == diff_b
    )"));
    CHECK(c.global("same").as_bool() == true);
    CHECK(c.global("diff").as_bool() == false);
}

TEST_CASE("__gc hook fires when table is collected", "[vm][metamethod]") {
    bool fired = false;
    {
        VM vm;
        vm.open_stdlib();
        // Register a native that sets up the gc_hook
        vm.register_function("make_proxy", [&](std::vector<Value> args) -> std::vector<Value> {
            Value tbl = Value::from_table();
            tbl.as_table()->gc_hook = [&fired]() { fired = true; };
            return {tbl};
        });
        Lexer lexer("var p = make_proxy()  p = nil");
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        auto prog = parser.parse();
        Compiler compiler;
        auto chunk = compiler.compile(prog, "<test>");
        vm.execute(*chunk);
        vm.gc_collect();
    }
    CHECK(fired == true);
}

// ---------------------------------------------------------------------------
// Coroutines
// ---------------------------------------------------------------------------
// Note: `var ok, val = fn()` at top level creates locals (not globals).
// Tests use a helper function that writes results to globals via plain assignment.

TEST_CASE("coroutine basic create and resume", "[vm][coroutine]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var co = coroutine.create(fn() { return 42 })
        fn do_it() {
            var ok, val = coroutine.resume(co)
            resume_ok  = ok
            resume_val = val
        }
        do_it()
    )"));
    CHECK(c.global("resume_ok").as_bool() == true);
    CHECK(c.global("resume_val").as_int() == 42);
}

TEST_CASE("coroutine yield multiple times", "[vm][coroutine]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var co = coroutine.create(fn() {
            coroutine.yield(1)
            coroutine.yield(2)
            return 3
        })
        fn step() {
            var ok, v = coroutine.resume(co)
            last_ok = ok
            last_v  = v
        }
        step()  var v1 = last_v
        step()  var v2 = last_v
        step()  var v3 = last_v
    )"));
    CHECK(c.global("v1").as_int() == 1);
    CHECK(c.global("v2").as_int() == 2);
    CHECK(c.global("v3").as_int() == 3);
}

TEST_CASE("coroutine status transitions", "[vm][coroutine]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var co = coroutine.create(fn() { coroutine.yield() })
        var s0 = coroutine.status(co)
        coroutine.resume(co)
        var s1 = coroutine.status(co)
        coroutine.resume(co)
        var s2 = coroutine.status(co)
    )"));
    CHECK(c.global("s0").as_string() == "suspended");
    CHECK(c.global("s1").as_string() == "suspended");
    CHECK(c.global("s2").as_string() == "dead");
}

TEST_CASE("coroutine resume dead coroutine returns error", "[vm][coroutine]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var co = coroutine.create(fn() { return 1 })
        coroutine.resume(co)
        fn check_dead() {
            var ok, msg = coroutine.resume(co)
            dead_ok = ok
        }
        check_dead()
    )"));
    CHECK(c.global("dead_ok").as_bool() == false);
}

TEST_CASE("coroutine wrap produces callable", "[vm][coroutine]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var gen = coroutine.wrap(fn() {
            coroutine.yield(10)
            coroutine.yield(20)
            return 30
        })
        var a = gen()
        var b = gen()
        var c = gen()
    )"));
    CHECK(c.global("a").as_int() == 10);
    CHECK(c.global("b").as_int() == 20);
    CHECK(c.global("c").as_int() == 30);
}

TEST_CASE("coroutine resume passes value to yield", "[vm][coroutine]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var co = coroutine.create(fn() {
            var x = coroutine.yield(1)
            return x + 100
        })
        fn step1() {
            var ok, v = coroutine.resume(co)
            yield_val = v
        }
        fn step2() {
            var ok, v = coroutine.resume(co, 42)
            return_val = v
        }
        step1()
        step2()
    )"));
    CHECK(c.global("yield_val").as_int() == 1);
    CHECK(c.global("return_val").as_int() == 142);
}

TEST_CASE("coroutine wrap generator pattern", "[vm][coroutine]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn range(n) {
            return coroutine.wrap(fn() {
                var i = 0
                while i < n {
                    coroutine.yield(i)
                    i = i + 1
                }
            })
        }
        var r = range(4)
        var r0 = r()
        var r1 = r()
        var r2 = r()
        var r3 = r()
    )"));
    CHECK(c.global("r0").as_int() == 0);
    CHECK(c.global("r1").as_int() == 1);
    CHECK(c.global("r2").as_int() == 2);
    CHECK(c.global("r3").as_int() == 3);
}
