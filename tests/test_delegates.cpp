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
        Lexer  l(src); auto tokens = l.tokenize();
        Parser p(std::move(tokens)); Program prog = p.parse();
        if (p.has_errors()) return false;
        Compiler c; auto chunk = c.compile(prog, "<test>");
        if (c.has_errors()) return false;
        return vm.execute(*chunk);
    }
    Value g(const std::string& n) { return vm.get_global(n); }
};

// ---------------------------------------------------------------------------
// Basic += creates a delegate and calls it
// ---------------------------------------------------------------------------

TEST_CASE("single handler fires on call", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Button {
            fn init() { self.on_click = nil }
        }
        let btn = Button()
        var fired = 0
        btn.on_click += fn() { fired = fired + 1 }
        btn.on_click()
    )"));
    CHECK(c.g("fired").as_int() == 1);
}

TEST_CASE("two handlers both fire in order", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Btn {
            fn init() { self.ev = nil }
        }
        let b = Btn()
        var log = ""
        b.ev += fn() { log = log + "A" }
        b.ev += fn() { log = log + "B" }
        b.ev()
    )"));
    CHECK(c.g("log").as_string() == "AB");
}

TEST_CASE("three handlers fire in registration order", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        var s = ""
        e.ev += fn() { s = s + "1" }
        e.ev += fn() { s = s + "2" }
        e.ev += fn() { s = s + "3" }
        e.ev()
    )"));
    CHECK(c.g("s").as_string() == "123");
}

// ---------------------------------------------------------------------------
// -= removes the correct handler
// ---------------------------------------------------------------------------

TEST_CASE("remove handler via -=", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        var s = ""
        let h1 = fn() { s = s + "A" }
        let h2 = fn() { s = s + "B" }
        e.ev += h1
        e.ev += h2
        e.ev -= h1
        e.ev()
    )"));
    CHECK(c.g("s").as_string() == "B");
}

TEST_CASE("remove unknown handler is a no-op", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        var fired = 0
        let h1 = fn() { fired = fired + 1 }
        let h2 = fn() { fired = fired + 10 }
        e.ev += h1
        e.ev -= h2    // h2 was never added
        e.ev()
    )"));
    CHECK(c.g("fired").as_int() == 1);
}

TEST_CASE("remove all handlers leaves empty delegate, call is safe", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        var fired = 0
        let h = fn() { fired = fired + 1 }
        e.ev += h
        e.ev -= h
        e.ev()      // fires nothing — no crash
    )"));
    CHECK(c.g("fired").as_int() == 0);
}

// ---------------------------------------------------------------------------
// Handlers receive arguments
// ---------------------------------------------------------------------------

TEST_CASE("handler receives arguments", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        var result = 0
        e.ev += fn(x, y) { result = x + y }
        e.ev(10, 32)
    )"));
    CHECK(c.g("result").as_int() == 42);
}

TEST_CASE("multiple handlers all receive the same args", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        var total = 0
        e.ev += fn(x) { total = total + x }
        e.ev += fn(x) { total = total + x * 2 }
        e.ev(5)
    )"));
    CHECK(c.g("total").as_int() == 15);  // 5 + 10
}

// ---------------------------------------------------------------------------
// type_of a delegate
// ---------------------------------------------------------------------------

TEST_CASE("type_of delegate is 'delegate'", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        e.ev += fn() {}
        var t = type(e.ev)
    )"));
    CHECK(c.g("t").as_string() == "delegate");
}

// ---------------------------------------------------------------------------
// += on a field that already holds a plain function promotes it
// ---------------------------------------------------------------------------

TEST_CASE("+= promotes bare function field to delegate", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        var s = ""
        let h1 = fn() { s = s + "X" }
        let h2 = fn() { s = s + "Y" }
        e.ev += h1     // nil -> delegate[h1]
        e.ev += h2     // delegate[h1] -> delegate[h1, h2]
        e.ev()
    )"));
    CHECK(c.g("s").as_string() == "XY");
}

// ---------------------------------------------------------------------------
// Arithmetic += / -= still work on numeric fields
// ---------------------------------------------------------------------------

TEST_CASE("+= on numeric field is arithmetic", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Counter {
            fn init() { self.count = 0 }
            fn inc()   { self.count += 1 }
        }
        let c = Counter()
        c.inc()
        c.inc()
        c.inc()
        var r = c.count
    )"));
    CHECK(c.g("r").as_int() == 3);
}

TEST_CASE("compound minus-assign on numeric field is arithmetic", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Box {
            fn init() { self.hp = 100 }
            fn hit(d) { self.hp -= d }
        }
        let b = Box()
        b.hit(30)
        b.hit(20)
        var r = b.hp
    )"));
    CHECK(c.g("r").as_int() == 50);
}

// ---------------------------------------------------------------------------
// Closures as handlers capture their environment
// ---------------------------------------------------------------------------

TEST_CASE("handler closure captures outer variable", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class E { fn init() { self.ev = nil } }
        let e = E()
        var prefix = "hello"
        var out = ""
        e.ev += fn() { out = prefix + " world" }
        e.ev()
    )"));
    CHECK(c.g("out").as_string() == "hello world");
}

// ---------------------------------------------------------------------------
// Multiple separate delegate fields on one object
// ---------------------------------------------------------------------------

TEST_CASE("separate delegate fields are independent", "[delegate]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Widget {
            fn init() { self.on_press = nil  self.on_release = nil }
        }
        let w = Widget()
        var log = ""
        w.on_press   += fn() { log = log + "P" }
        w.on_release += fn() { log = log + "R" }
        w.on_press()
        w.on_release()
        w.on_press()
    )"));
    CHECK(c.g("log").as_string() == "PRP");
}
