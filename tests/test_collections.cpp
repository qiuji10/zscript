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

// ---------------------------------------------------------------------------
// Array methods — pure (no callbacks)
// ---------------------------------------------------------------------------

TEST_CASE("push appends to array", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var arr = [1, 2, 3]
        arr.push(4)
        arr.push(5)
        var n = #arr
        var last = arr[4]
    )"));
    CHECK(c.g("n").as_int()    == 5);
    CHECK(c.g("last").as_int() == 5);
}

TEST_CASE("pop removes and returns last element", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var arr = [10, 20, 30]
        var v = arr.pop()
        var n = #arr
    )"));
    CHECK(c.g("v").as_int() == 30);
    CHECK(c.g("n").as_int() == 2);
}

TEST_CASE("contains finds value in array", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [1, 2, 3, 4, 5]
        var has3 = arr.contains(3)
        var has9 = arr.contains(9)
    )"));
    CHECK(c.g("has3").as_bool() == true);
    CHECK(c.g("has9").as_bool() == false);
}

TEST_CASE("index_of returns index or -1", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [10, 20, 30, 40]
        var i1 = arr.index_of(30)
        var i2 = arr.index_of(99)
    )"));
    CHECK(c.g("i1").as_int() == 2);
    CHECK(c.g("i2").as_int() == -1);
}

TEST_CASE("slice returns sub-array", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [10, 20, 30, 40, 50]
        let s1 = arr.slice(1, 4)
        let s2 = arr.slice(3)
        var n1 = #s1
        var n2 = #s2
        var v0 = s1[0]
        var v1 = s2[0]
    )"));
    CHECK(c.g("n1").as_int() == 3);
    CHECK(c.g("v0").as_int() == 20);
    CHECK(c.g("n2").as_int() == 2);
    CHECK(c.g("v1").as_int() == 40);
}

TEST_CASE("reverse reverses array in-place", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var arr = [1, 2, 3, 4, 5]
        arr.reverse()
        var a = arr[0]
        var b = arr[4]
    )"));
    CHECK(c.g("a").as_int() == 5);
    CHECK(c.g("b").as_int() == 1);
}

TEST_CASE("concat joins two arrays", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let a = [1, 2, 3]
        let b = [4, 5, 6]
        let c2 = a.concat(b)
        var n = #c2
        var first = c2[0]
        var last = c2[5]
    )"));
    CHECK(c.g("n").as_int()     == 6);
    CHECK(c.g("first").as_int() == 1);
    CHECK(c.g("last").as_int()  == 6);
}

TEST_CASE("sort sorts array in-place", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var arr = [5, 2, 8, 1, 9, 3]
        arr.sort()
        var first = arr[0]
        var last  = arr[5]
    )"));
    CHECK(c.g("first").as_int() == 1);
    CHECK(c.g("last").as_int()  == 9);
}

TEST_CASE("first and last return endpoints", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [10, 20, 30]
        var f = arr.first()
        var l = arr.last()
    )"));
    CHECK(c.g("f").as_int() == 10);
    CHECK(c.g("l").as_int() == 30);
}

// ---------------------------------------------------------------------------
// Array methods — higher-order (callbacks)
// ---------------------------------------------------------------------------

TEST_CASE("map transforms each element", "[collections][methods][hof]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [1, 2, 3, 4, 5]
        let doubled = arr.map(fn(x) { return x * 2 })
        var a = doubled[0]
        var b = doubled[4]
        var n = #doubled
    )"));
    CHECK(c.g("n").as_int() == 5);
    CHECK(c.g("a").as_int() == 2);
    CHECK(c.g("b").as_int() == 10);
}

TEST_CASE("filter keeps truthy elements", "[collections][methods][hof]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [1, 2, 3, 4, 5, 6]
        let evens = arr.filter(fn(x) { return x % 2 == 0 })
        var n = #evens
        var first = evens[0]
        var last  = evens[2]
    )"));
    CHECK(c.g("n").as_int()     == 3);
    CHECK(c.g("first").as_int() == 2);
    CHECK(c.g("last").as_int()  == 6);
}

TEST_CASE("reduce folds array to single value", "[collections][methods][hof]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [1, 2, 3, 4, 5]
        var sum = arr.reduce(fn(acc, x) { return acc + x }, 0)
    )"));
    CHECK(c.g("sum").as_int() == 15);
}

TEST_CASE("each iterates with side effects", "[collections][methods][hof]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [1, 2, 3, 4, 5]
        var sum = 0
        arr.each(fn(x) { sum = sum + x })
    )"));
    CHECK(c.g("sum").as_int() == 15);
}

TEST_CASE("any returns true when predicate matches", "[collections][methods][hof]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [1, 3, 5, 6, 7]
        var has_even = arr.any(fn(x) { return x % 2 == 0 })
        var has_neg  = arr.any(fn(x) { return x < 0 })
    )"));
    CHECK(c.g("has_even").as_bool() == true);
    CHECK(c.g("has_neg").as_bool()  == false);
}

TEST_CASE("all returns true when all elements match", "[collections][methods][hof]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let pos = [1, 2, 3, 4, 5]
        let mix = [1, 2, -3, 4, 5]
        var all_pos = pos.all(fn(x) { return x > 0 })
        var all_mix = mix.all(fn(x) { return x > 0 })
    )"));
    CHECK(c.g("all_pos").as_bool() == true);
    CHECK(c.g("all_mix").as_bool() == false);
}

TEST_CASE("flat flattens one level of nesting", "[collections][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let nested = [[1, 2], [3, 4], [5]]
        let flat = nested.flat()
        var n = #flat
        var first = flat[0]
        var last  = flat[4]
    )"));
    CHECK(c.g("n").as_int()     == 5);
    CHECK(c.g("first").as_int() == 1);
    CHECK(c.g("last").as_int()  == 5);
}

TEST_CASE("chained map and filter", "[collections][methods][hof]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        let result = arr.filter(fn(x) { return x % 2 == 0 }).map(fn(x) { return x * x })
        var n = #result
        var first = result[0]
        var last  = result[4]
    )"));
    CHECK(c.g("n").as_int()     == 5);
    CHECK(c.g("first").as_int() == 4);   // 2*2
    CHECK(c.g("last").as_int()  == 100); // 10*10
}
