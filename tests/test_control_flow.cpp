#include <catch2/catch_test_macros.hpp>
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
using namespace zscript;

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
// break / continue
// ---------------------------------------------------------------------------

TEST_CASE("break exits while loop early", "[control_flow][break]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var i = 0
        while i < 10 {
            if i == 5 { break }
            i = i + 1
        }
    )"));
    CHECK(c.g("i").as_int() == 5);
}

TEST_CASE("break exits for-in loop early", "[control_flow][break]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var found = -1
        let arr = [10, 20, 30, 40, 50]
        for v in arr {
            if v == 30 { found = v  break }
        }
    )"));
    CHECK(c.g("found").as_int() == 30);
}

TEST_CASE("continue skips current iteration", "[control_flow][continue]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var sum = 0
        var i = 0
        while i < 6 {
            i = i + 1
            if i == 3 { continue }
            sum = sum + i
        }
    )"));
    // 1+2+4+5+6 = 18  (skip 3)
    CHECK(c.g("sum").as_int() == 18);
}

TEST_CASE("nested break only exits inner loop", "[control_flow][break]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var outer_count = 0
        var inner_sum = 0
        var i = 0
        while i < 3 {
            i = i + 1
            outer_count = outer_count + 1
            var j = 0
            while j < 5 {
                j = j + 1
                if j == 2 { break }
                inner_sum = inner_sum + 1
            }
        }
    )"));
    CHECK(c.g("outer_count").as_int() == 3);
    CHECK(c.g("inner_sum").as_int() == 3);  // 1 per outer iteration
}

// ---------------------------------------------------------------------------
// match statement
// ---------------------------------------------------------------------------

TEST_CASE("match on integer literal arms", "[control_flow][match]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var result = 0
        let x = 2
        match x {
            1 => { result = 10 }
            2 => { result = 20 }
            3 => { result = 30 }
        }
    )"));
    CHECK(c.g("result").as_int() == 20);
}

TEST_CASE("match wildcard arm fires when no literal matches", "[control_flow][match]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var result = 0
        let x = 99
        match x {
            1 => { result = 1 }
            2 => { result = 2 }
            _ => { result = -1 }
        }
    )"));
    CHECK(c.g("result").as_int() == -1);
}

TEST_CASE("match on string arms", "[control_flow][match]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var code = 0
        let s = "ok"
        match s {
            "ok"    => { code = 200 }
            "error" => { code = 500 }
            _       => { code = 0 }
        }
    )"));
    CHECK(c.g("code").as_int() == 200);
}

TEST_CASE("match with no matching arm and no wildcard does nothing", "[control_flow][match]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var result = 42
        let x = 99
        match x {
            1 => { result = 1 }
            2 => { result = 2 }
        }
    )"));
    CHECK(c.g("result").as_int() == 42);  // unchanged
}

// ---------------------------------------------------------------------------
// if-expression
// ---------------------------------------------------------------------------

TEST_CASE("if-expression returns then value", "[control_flow][if_expr]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let x = if true { 1 } else { 2 }
    )"));
    CHECK(c.g("x").as_int() == 1);
}

TEST_CASE("if-expression returns else value", "[control_flow][if_expr]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let x = if false { 1 } else { 2 }
    )"));
    CHECK(c.g("x").as_int() == 2);
}

TEST_CASE("if-expression used inline in expression", "[control_flow][if_expr]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let score = 85
        let grade = if score >= 90 { "A" } else { if score >= 80 { "B" } else { "C" } }
    )"));
    CHECK(c.g("grade").as_string() == "B");
}

TEST_CASE("if-expression result used in arithmetic", "[control_flow][if_expr]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let positive = true
        let val = (if positive { 1 } else { -1 }) * 10
    )"));
    CHECK(c.g("val").as_int() == 10);
}
