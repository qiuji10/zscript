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

// ---------------------------------------------------------------------------
// Array destructuring
// ---------------------------------------------------------------------------

TEST_CASE("array destructuring at top level", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let [a, b, cc] = [10, 20, 30]
    )"));
    CHECK(c.g("a").as_int()  == 10);
    CHECK(c.g("b").as_int()  == 20);
    CHECK(c.g("cc").as_int() == 30);
}

TEST_CASE("array destructuring inside function", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn test() {
            let [x, y] = [100, 200]
            return x + y
        }
        let r = test()
    )"));
    CHECK(c.g("r").as_int() == 300);
}

TEST_CASE("array destructuring rest element", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let [head, ...tail] = [1, 2, 3, 4, 5]
        var h = head
        var n = #tail
        var last = tail[3]
    )"));
    CHECK(c.g("h").as_int()    == 1);
    CHECK(c.g("n").as_int()    == 4);
    CHECK(c.g("last").as_int() == 5);
}

TEST_CASE("array destructuring with function return", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn coords() { return [3, 7] }
        let [x, y] = coords()
        var rx = x
        var ry = y
    )"));
    CHECK(c.g("rx").as_int() == 3);
    CHECK(c.g("ry").as_int() == 7);
}

TEST_CASE("array destructuring partial (fewer bindings than elements)", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let [first, second] = [10, 20, 30, 40]
        var a = first
        var b = second
    )"));
    CHECK(c.g("a").as_int() == 10);
    CHECK(c.g("b").as_int() == 20);
}

// ---------------------------------------------------------------------------
// Table destructuring
// ---------------------------------------------------------------------------

TEST_CASE("table destructuring at top level", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let {x, y} = {x: 5, y: 10}
    )"));
    CHECK(c.g("x").as_int() == 5);
    CHECK(c.g("y").as_int() == 10);
}

TEST_CASE("table destructuring with rename", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let {name: n, age: a} = {name: "Alice", age: 30}
    )"));
    CHECK(c.g("n").as_string() == "Alice");
    CHECK(c.g("a").as_int()    == 30);
}

TEST_CASE("table destructuring inside function", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn get_point() { return {x: 3, y: 4} }
        fn dist() {
            let {x, y} = get_point()
            return x * x + y * y
        }
        let r = dist()
    )"));
    CHECK(c.g("r").as_int() == 25);
}

TEST_CASE("table destructuring from class instance", "[functions][destructure]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Vec {
            fn init(x, y) { self.x = x  self.y = y }
        }
        let v = Vec(6, 8)
        let {x, y} = v
        var rx = x
        var ry = y
    )"));
    CHECK(c.g("rx").as_int() == 6);
    CHECK(c.g("ry").as_int() == 8);
}
