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
// Variadic functions
// ---------------------------------------------------------------------------

TEST_CASE("variadic function receives args as array", "[functions][variadic]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn sum(...args) {
            var total = 0
            for v in args { total = total + v }
            return total
        }
        let r = sum(1, 2, 3, 4, 5)
    )"));
    CHECK(c.g("r").as_int() == 15);
}

TEST_CASE("variadic with zero extra args", "[functions][variadic]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn count(...args) { return #args }
        let r = count()
    )"));
    CHECK(c.g("r").as_int() == 0);
}

TEST_CASE("variadic with fixed params and rest", "[functions][variadic]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn first_and_rest(first, ...rest) {
            return first + #rest
        }
        let r = first_and_rest(10, 1, 2, 3)
    )"));
    CHECK(c.g("r").as_int() == 13);  // 10 + 3 (length of rest)
}

TEST_CASE("variadic function called with single arg", "[functions][variadic]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn sum(...args) {
            var total = 0
            for v in args { total = total + v }
            return total
        }
        let r = sum(42)
    )"));
    CHECK(c.g("r").as_int() == 42);
}

// ---------------------------------------------------------------------------
// Multiple return values
// ---------------------------------------------------------------------------

TEST_CASE("function returns two values", "[functions][multi_return]") {
    Ctx c;
    // multi-return destructuring produces locals; assign to vars (globals) to inspect
    REQUIRE(c.run(R"(
        fn min_max(a, b) {
            if a < b { return a, b }
            return b, a
        }
        let lo, hi = min_max(5, 3)
        var r_lo = lo
        var r_hi = hi
    )"));
    CHECK(c.g("r_lo").as_int() == 3);
    CHECK(c.g("r_hi").as_int() == 5);
}

TEST_CASE("function returns three values", "[functions][multi_return]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn rgb() { return 255, 128, 0 }
        let rv, gv, bv = rgb()
        var out_r = rv
        var out_g = gv
        var out_b = bv
    )"));
    CHECK(c.g("out_r").as_int() == 255);
    CHECK(c.g("out_g").as_int() == 128);
    CHECK(c.g("out_b").as_int() == 0);
}

TEST_CASE("multiple return values at top level", "[functions][multi_return]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn swap(a, b) { return b, a }
        let xv, yv = swap(1, 2)
        var out_x = xv
        var out_y = yv
    )"));
    CHECK(c.g("out_x").as_int() == 2);
    CHECK(c.g("out_y").as_int() == 1);
}

TEST_CASE("caller requests fewer values than returned", "[functions][multi_return]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn pair() { return 10, 20 }
        let a = pair()
    )"));
    CHECK(c.g("a").as_int() == 10);
}
