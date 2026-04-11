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
// Array literals
// ---------------------------------------------------------------------------

TEST_CASE("array literal stores elements by index", "[collections][array]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [10, 20, 30]
        let a = arr[0]
        let b = arr[1]
        let cc = arr[2]
    )"));
    CHECK(c.g("a").as_int()  == 10);
    CHECK(c.g("b").as_int()  == 20);
    CHECK(c.g("cc").as_int() == 30);
}

TEST_CASE("array length operator #", "[collections][array]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [1, 2, 3, 4, 5]
        let n = #arr
    )"));
    CHECK(c.g("n").as_int() == 5);
}

TEST_CASE("empty array has length 0", "[collections][array]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = []
        let n = #arr
    )"));
    CHECK(c.g("n").as_int() == 0);
}

TEST_CASE("array index write and read", "[collections][array]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var arr = [0, 0, 0]
        arr[1] = 99
        let val = arr[1]
    )"));
    CHECK(c.g("val").as_int() == 99);
}

TEST_CASE("for-in iterates array values in order", "[collections][array]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [3, 1, 4, 1, 5]
        var sum = 0
        for v in arr { sum = sum + v }
    )"));
    CHECK(c.g("sum").as_int() == 14);
}

// ---------------------------------------------------------------------------
// Table / dict literals
// ---------------------------------------------------------------------------

TEST_CASE("table literal field access", "[collections][table]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let t = {name: "Alice", age: 30}
        let n = t.name
        let a = t.age
    )"));
    CHECK(c.g("n").as_string() == "Alice");
    CHECK(c.g("a").as_int()    == 30);
}

TEST_CASE("table field write and read", "[collections][table]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var t = {x: 0}
        t.x = 42
        let val = t.x
    )"));
    CHECK(c.g("val").as_int() == 42);
}

TEST_CASE("table string key access via index", "[collections][table]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let t = {score: 100}
        let v = t["score"]
    )"));
    CHECK(c.g("v").as_int() == 100);
}

TEST_CASE("for k, v in table iterates key-value pairs", "[collections][table]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let t = {a: 1, b: 2, c: 3}
        var key_count = 0
        var val_sum = 0
        for k, v in t {
            key_count = key_count + 1
            val_sum = val_sum + v
        }
    )"));
    CHECK(c.g("key_count").as_int() == 3);
    CHECK(c.g("val_sum").as_int()   == 6);
}

TEST_CASE("string length operator #", "[collections][string]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let s = "hello"
        let n = #s
    )"));
    CHECK(c.g("n").as_int() == 5);
}

TEST_CASE("empty string has length 0", "[collections][string]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let s = ""
        let n = #s
    )"));
    CHECK(c.g("n").as_int() == 0);
}

TEST_CASE("nested table access", "[collections][table]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let t = {inner: {value: 42}}
        let v = t.inner.value
    )"));
    CHECK(c.g("v").as_int() == 42);
}

TEST_CASE("array of tables", "[collections][array][table]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let people = [
            {name: "Alice", age: 30},
            {name: "Bob",   age: 25},
        ]
        let n0 = people[0].name
        let a1 = people[1].age
    )"));
    CHECK(c.g("n0").as_string() == "Alice");
    CHECK(c.g("a1").as_int()    == 25);
}
