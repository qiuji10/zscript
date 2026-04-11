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
// Closures capture enclosing locals
// ---------------------------------------------------------------------------

TEST_CASE("closure captures enclosing variable", "[closures]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn make_adder(n) {
            return fn(x) { return x + n }
        }
        let add5 = make_adder(5)
        let result = add5(3)
    )"));
    CHECK(c.g("result").as_int() == 8);
}

TEST_CASE("closure captures mutable variable and sees updates", "[closures]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn make_counter() {
            var count = 0
            return fn() {
                count = count + 1
                return count
            }
        }
        let counter = make_counter()
        let a = counter()
        let b = counter()
        let c2 = counter()
    )"));
    CHECK(c.g("a").as_int() == 1);
    CHECK(c.g("b").as_int() == 2);
    CHECK(c.g("c2").as_int() == 3);
}

TEST_CASE("multiple closures share the same upvalue", "[closures]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn make_pair() {
            var x = 0
            let inc = fn() { x = x + 1 }
            let get = fn() { return x }
            return {inc: inc, get: get}
        }
        let p = make_pair()
        p.inc()
        p.inc()
        p.inc()
        let val = p.get()
    )"));
    CHECK(c.g("val").as_int() == 3);
}

TEST_CASE("lambda expression assigned to variable", "[closures]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let square = fn(x) { return x * x }
        let r = square(7)
    )"));
    CHECK(c.g("r").as_int() == 49);
}

TEST_CASE("closure returned from nested function", "[closures]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn outer(a) {
            fn inner(b) {
                return a + b
            }
            return inner
        }
        let f = outer(10)
        let r = f(3)
    )"));
    CHECK(c.g("r").as_int() == 13);
}

TEST_CASE("closure captures loop variable by value at creation time", "[closures]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn make_adder(n) { return fn(x) { return x + n } }
        let add2 = make_adder(2)
        let add4 = make_adder(4)
        let r1 = add2(10)
        let r2 = add4(10)
    )"));
    CHECK(c.g("r1").as_int() == 12);
    CHECK(c.g("r2").as_int() == 14);
}

TEST_CASE("immediately invoked lambda", "[closures]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = fn(x, y) { return x * y }(6, 7)
    )"));
    CHECK(c.g("r").as_int() == 42);
}
