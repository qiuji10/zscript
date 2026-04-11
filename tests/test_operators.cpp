#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include <cmath>
using namespace zscript;
using Catch::Approx;

struct Ctx {
    VM vm;
    Ctx() { vm.open_stdlib(); }
    bool run(const std::string& src) {
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        Program prog = parser.parse();
        if (parser.has_errors()) return false;
        Compiler compiler;
        auto chunk = compiler.compile(prog, "<test>");
        if (compiler.has_errors()) return false;
        return vm.execute(*chunk);
    }
    Value g(const std::string& n) { return vm.get_global(n); }
};

// ---------------------------------------------------------------------------
// Compound assignment operators
// ---------------------------------------------------------------------------

TEST_CASE("plus-assign on local variable", "[operators][compound_assign]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var x = 10
        x += 5
        let r = x
    )"));
    CHECK(c.g("r").as_int() == 15);
}

TEST_CASE("minus-assign on local variable", "[operators][compound_assign]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var x = 10
        x -= 3
        let r = x
    )"));
    CHECK(c.g("r").as_int() == 7);
}

TEST_CASE("star-assign on local variable", "[operators][compound_assign]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var x = 4
        x *= 3
        let r = x
    )"));
    CHECK(c.g("r").as_int() == 12);
}

TEST_CASE("slash-assign on local variable", "[operators][compound_assign]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var x = 10
        x /= 2
        let r = x
    )"));
    CHECK(c.g("r").as_float() == Approx(5.0));
}

TEST_CASE("percent-assign on local variable", "[operators][compound_assign]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var x = 10
        x %= 3
        let r = x
    )"));
    CHECK(c.g("r").as_int() == 1);
}

TEST_CASE("compound assignment on field", "[operators][compound_assign]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var t = {val: 10}
        t.val += 5
        let r = t.val
    )"));
    CHECK(c.g("r").as_int() == 15);
}

TEST_CASE("compound assignment on index", "[operators][compound_assign]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var arr = [1, 2, 3]
        arr[1] *= 10
        let r = arr[1]
    )"));
    CHECK(c.g("r").as_int() == 20);
}

// ---------------------------------------------------------------------------
// Power operator **
// ---------------------------------------------------------------------------

TEST_CASE("integer power", "[operators][power]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = 2 ** 10
    )"));
    CHECK(c.g("r").as_int() == 1024);
}

TEST_CASE("float power", "[operators][power]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = 9.0 ** 0.5
    )"));
    CHECK(c.g("r").as_float() == Approx(3.0));
}

TEST_CASE("power of zero is 1", "[operators][power]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = 42 ** 0
    )"));
    CHECK(c.g("r").as_int() == 1);
}

// ---------------------------------------------------------------------------
// Nil-coalescing ??
// ---------------------------------------------------------------------------

TEST_CASE("nil-coalescing returns right when left is nil", "[operators][nil_coalesce]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let x = nil ?? 42
    )"));
    CHECK(c.g("x").as_int() == 42);
}

TEST_CASE("nil-coalescing returns left when not nil", "[operators][nil_coalesce]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let x = "hello" ?? "default"
    )"));
    CHECK(c.g("x").as_string() == "hello");
}

TEST_CASE("nil-coalescing with zero is not nil", "[operators][nil_coalesce]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let x = 0 ?? 99
    )"));
    CHECK(c.g("x").as_int() == 0);
}

TEST_CASE("chained nil-coalescing", "[operators][nil_coalesce]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let x = nil ?? nil ?? 7
    )"));
    CHECK(c.g("x").as_int() == 7);
}

// ---------------------------------------------------------------------------
// Optional chaining ?.
// ---------------------------------------------------------------------------

TEST_CASE("safe field access on non-nil returns field", "[operators][optional_chain]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let t = {name: "Alice"}
        let r = t?.name
    )"));
    CHECK(c.g("r").as_string() == "Alice");
}

TEST_CASE("safe field access on nil returns nil", "[operators][optional_chain]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let t = nil
        let r = t?.name
    )"));
    CHECK(c.g("r").tag == Value::Tag::Nil);
}

TEST_CASE("safe method call on non-nil invokes method", "[operators][optional_chain]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Greeter {
            fn init(name) { self.name = name }
            fn greet() { return "hi " + self.name }
        }
        let g = Greeter("Bob")
        let r = g?.greet()
    )"));
    CHECK(c.g("r").as_string() == "hi Bob");
}

TEST_CASE("safe method call on nil returns nil", "[operators][optional_chain]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Greeter {
            fn greet() { return "hi" }
        }
        let g = nil
        let r = g?.greet()
    )"));
    CHECK(c.g("r").tag == Value::Tag::Nil);
}

TEST_CASE("optional chain combined with nil-coalescing", "[operators][optional_chain][nil_coalesce]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let user = nil
        let name = user?.name ?? "anonymous"
    )"));
    CHECK(c.g("name").as_string() == "anonymous");
}

// ---------------------------------------------------------------------------
// not keyword (alias for !)
// ---------------------------------------------------------------------------

TEST_CASE("not keyword negates true to false", "[operators][not]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = not true
    )"));
    CHECK(c.g("r").as_bool() == false);
}

TEST_CASE("not keyword negates false to true", "[operators][not]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = not false
    )"));
    CHECK(c.g("r").as_bool() == true);
}

TEST_CASE("not in condition", "[operators][not]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var flag = false
        if not flag { flag = true }
        let r = flag
    )"));
    CHECK(c.g("r").as_bool() == true);
}
