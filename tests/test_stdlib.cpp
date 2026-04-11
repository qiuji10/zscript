#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
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
// Type cast functions: int(), float(), str()
// ---------------------------------------------------------------------------

TEST_CASE("int() truncates float", "[stdlib][casts]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = int(3.9) )"));
    CHECK(c.g("r").as_int() == 3);
}

TEST_CASE("int() parses string", "[stdlib][casts]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = int("42") )"));
    CHECK(c.g("r").as_int() == 42);
}

TEST_CASE("float() converts int", "[stdlib][casts]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = float(5) )"));
    CHECK(c.g("r").as_float() == Approx(5.0));
}

TEST_CASE("float() parses string", "[stdlib][casts]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = float("2.5") )"));
    CHECK(c.g("r").as_float() == Approx(2.5));
}

TEST_CASE("str() converts int", "[stdlib][casts]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = str(123) )"));
    CHECK(c.g("r").as_string() == "123");
}

TEST_CASE("str() converts bool", "[stdlib][casts]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = str(true) )"));
    CHECK(c.g("r").as_string() == "true");
}

// ---------------------------------------------------------------------------
// Math stdlib
// ---------------------------------------------------------------------------

TEST_CASE("math.abs on negative", "[stdlib][math]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = math.abs(-5) )"));
    CHECK(c.g("r").as_int() == 5);
}

TEST_CASE("math.sqrt", "[stdlib][math]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = math.sqrt(16.0) )"));
    CHECK(c.g("r").as_float() == Approx(4.0));
}

TEST_CASE("math.floor", "[stdlib][math]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = math.floor(3.9) )"));
    CHECK(c.g("r").as_int() == 3);
}

TEST_CASE("math.ceil", "[stdlib][math]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = math.ceil(3.1) )"));
    CHECK(c.g("r").as_int() == 4);
}

TEST_CASE("math.min and math.max", "[stdlib][math]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let mn = math.min(3, 7)
        let mx = math.max(3, 7)
    )"));
    CHECK(c.g("mn").as_float() == Approx(3.0));
    CHECK(c.g("mx").as_float() == Approx(7.0));
}

TEST_CASE("math.div integer division", "[stdlib][math]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let a = math.div(10, 3)
        let b = math.div(9,  3)
        let d = math.div(7,  2)
    )"));
    CHECK(c.g("a").as_int() == 3);
    CHECK(c.g("b").as_int() == 3);
    CHECK(c.g("d").as_int() == 3);
}

TEST_CASE("math.pow", "[stdlib][math]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = math.pow(2.0, 8.0) )"));
    CHECK(c.g("r").as_float() == Approx(256.0));
}

// ---------------------------------------------------------------------------
// String stdlib (via string.X and dot method syntax)
// ---------------------------------------------------------------------------

TEST_CASE("string.len", "[stdlib][string]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = string.len("hello") )"));
    CHECK(c.g("r").as_int() == 5);
}

TEST_CASE("string.upper via method syntax", "[stdlib][string]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = "hello".upper() )"));
    CHECK(c.g("r").as_string() == "HELLO");
}

TEST_CASE("string.lower via method syntax", "[stdlib][string]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = "HELLO".lower() )"));
    CHECK(c.g("r").as_string() == "hello");
}

TEST_CASE("string.sub extracts substring", "[stdlib][string]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = string.sub("hello world", 6) )"));
    CHECK(c.g("r").as_string() == "world");
}

TEST_CASE("string.contains", "[stdlib][string]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let yes = string.contains("hello world", "world")
        let no  = string.contains("hello world", "xyz")
    )"));
    CHECK(c.g("yes").as_bool() == true);
    CHECK(c.g("no").as_bool()  == false);
}

TEST_CASE("string.replace", "[stdlib][string]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = string.replace("foo bar foo", "foo", "baz") )"));
    CHECK(c.g("r").as_string() == "baz bar baz");
}

TEST_CASE("string.trim", "[stdlib][string]") {
    Ctx c;
    REQUIRE(c.run(R"( let r = string.trim("  hello  ") )"));
    CHECK(c.g("r").as_string() == "hello");
}

// ---------------------------------------------------------------------------
// split / join (global convenience functions)
// ---------------------------------------------------------------------------

TEST_CASE("split divides by delimiter", "[stdlib][split_join]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let parts = split("a,b,c", ",")
        let n  = #parts
        let p0 = parts[0]
        let p2 = parts[2]
    )"));
    CHECK(c.g("n").as_int()   == 3);
    CHECK(c.g("p0").as_string() == "a");
    CHECK(c.g("p2").as_string() == "c");
}

TEST_CASE("join assembles with separator", "[stdlib][split_join]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let parts = split("x-y-z", "-")
        let r = join(parts, ".")
    )"));
    CHECK(c.g("r").as_string() == "x.y.z");
}

TEST_CASE("split with empty delimiter splits chars", "[stdlib][split_join]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let parts = split("abc", "")
        let n = #parts
    )"));
    CHECK(c.g("n").as_int() == 3);
}

// ---------------------------------------------------------------------------
// range()
// ---------------------------------------------------------------------------

TEST_CASE("range(n) produces n elements starting at 0", "[stdlib][range]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = range(5)
        let n = #r
        let first = r[0]
        let last  = r[4]
    )"));
    CHECK(c.g("n").as_int()     == 5);
    CHECK(c.g("first").as_int() == 0);
    CHECK(c.g("last").as_int()  == 4);
}

TEST_CASE("range(start, stop) is exclusive of stop", "[stdlib][range]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = range(2, 6)
        let n = #r
        let first = r[0]
        let last  = r[3]
    )"));
    CHECK(c.g("n").as_int()     == 4);
    CHECK(c.g("first").as_int() == 2);
    CHECK(c.g("last").as_int()  == 5);
}

TEST_CASE("range with step", "[stdlib][range]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let r = range(0, 10, 2)
        let n = #r
        let v = r[3]
    )"));
    CHECK(c.g("n").as_int() == 5);  // 0,2,4,6,8
    CHECK(c.g("v").as_int() == 6);
}

TEST_CASE("for-in over range() iterates values", "[stdlib][range]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var sum = 0
        for v in range(5) { sum = sum + v }
    )"));
    CHECK(c.g("sum").as_int() == 10);  // 0+1+2+3+4
}
