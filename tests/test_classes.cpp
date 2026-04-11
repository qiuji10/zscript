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
// Basic class and methods
// ---------------------------------------------------------------------------

TEST_CASE("class instantiation via init", "[classes]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Point {
            fn init(x, y) { self.x = x  self.y = y }
        }
        let p = Point(3, 4)
        let px = p.x
        let py = p.y
    )"));
    CHECK(c.g("px").as_int() == 3);
    CHECK(c.g("py").as_int() == 4);
}

TEST_CASE("class method access via self", "[classes]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Counter {
            fn init(start) { self.val = start }
            fn inc() { self.val = self.val + 1 }
            fn get() { return self.val }
        }
        let ctr = Counter(0)
        ctr.inc()
        ctr.inc()
        ctr.inc()
        let v = ctr.get()
    )"));
    CHECK(c.g("v").as_int() == 3);
}

TEST_CASE("class method returns computed value", "[classes]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Point {
            fn init(x, y) { self.x = x  self.y = y }
            fn magnitude_sq() { return self.x * self.x + self.y * self.y }
        }
        let p = Point(3, 4)
        let m = p.magnitude_sq()
    )"));
    CHECK(c.g("m").as_int() == 25);
}

TEST_CASE("multiple class instances are independent", "[classes]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Box {
            fn init(v) { self.val = v }
            fn set(v) { self.val = v }
            fn get() { return self.val }
        }
        let a = Box(1)
        let b = Box(2)
        a.set(10)
        let av = a.get()
        let bv = b.get()
    )"));
    CHECK(c.g("av").as_int() == 10);
    CHECK(c.g("bv").as_int() == 2);
}

// ---------------------------------------------------------------------------
// Inheritance
// ---------------------------------------------------------------------------

TEST_CASE("child class inherits parent methods", "[classes][inheritance]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Animal {
            fn init(name) { self.name = name }
            fn describe() { return "I am " + self.name }
        }
        class Dog : Animal {
            fn speak() { return self.name + " barks" }
        }
        let d = Dog("Rex")
        let desc  = d.describe()
        let sound = d.speak()
    )"));
    CHECK(c.g("desc").as_string()  == "I am Rex");
    CHECK(c.g("sound").as_string() == "Rex barks");
}

TEST_CASE("child method overrides parent method", "[classes][inheritance]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Shape {
            fn area() { return 0 }
        }
        class Square : Shape {
            fn init(side) { self.side = side }
            fn area() { return self.side * self.side }
        }
        let s = Square(5)
        let a = s.area()
    )"));
    CHECK(c.g("a").as_int() == 25);
}

TEST_CASE("super.init called from child init", "[classes][inheritance][super]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Vehicle {
            fn init(brand) { self.brand = brand }
            fn info() { return self.brand }
        }
        class Car : Vehicle {
            fn init(brand, model) {
                super.init(brand)
                self.model = model
            }
            fn full_name() { return self.brand + " " + self.model }
        }
        let car = Car("Toyota", "Corolla")
        let brand = car.info()
        let name  = car.full_name()
    )"));
    CHECK(c.g("brand").as_string() == "Toyota");
    CHECK(c.g("name").as_string()  == "Toyota Corolla");
}

TEST_CASE("super.method extends parent behavior", "[classes][inheritance][super]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Base {
            fn greet() { return "Hello" }
        }
        class Child : Base {
            fn greet() { return super.greet() + ", World" }
        }
        let obj = Child()
        let msg = obj.greet()
    )"));
    CHECK(c.g("msg").as_string() == "Hello, World");
}

TEST_CASE("multi-level inheritance chain", "[classes][inheritance]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class A {
            fn init(x) { self.x = x }
            fn get_x() { return self.x }
        }
        class B : A {
            fn double_x() { return self.x * 2 }
        }
        class C : B {
            fn triple_x() { return self.x * 3 }
        }
        let obj = C(7)
        let gx  = obj.get_x()
        let dx  = obj.double_x()
        let tx  = obj.triple_x()
    )"));
    CHECK(c.g("gx").as_int() == 7);
    CHECK(c.g("dx").as_int() == 14);
    CHECK(c.g("tx").as_int() == 21);
}

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

TEST_CASE("enum auto-increments from zero", "[classes][enum]") {
    Ctx c;
    REQUIRE(c.run(R"(
        enum Dir { North, South, East, West }
        let n = Dir.North
        let s = Dir.South
        let e = Dir.East
        let w = Dir.West
    )"));
    CHECK(c.g("n").as_int() == 0);
    CHECK(c.g("s").as_int() == 1);
    CHECK(c.g("e").as_int() == 2);
    CHECK(c.g("w").as_int() == 3);
}

TEST_CASE("enum with explicit values", "[classes][enum]") {
    Ctx c;
    REQUIRE(c.run(R"(
        enum Status { Ok = 200, NotFound = 404, Error = 500 }
        let ok  = Status.Ok
        let nf  = Status.NotFound
        let err = Status.Error
    )"));
    CHECK(c.g("ok").as_int()  == 200);
    CHECK(c.g("nf").as_int()  == 404);
    CHECK(c.g("err").as_int() == 500);
}

TEST_CASE("enum auto-increments after explicit value", "[classes][enum]") {
    Ctx c;
    REQUIRE(c.run(R"(
        enum Priority { Low = 10, Medium, High }
        let lo = Priority.Low
        let me = Priority.Medium
        let hi = Priority.High
    )"));
    CHECK(c.g("lo").as_int() == 10);
    CHECK(c.g("me").as_int() == 11);
    CHECK(c.g("hi").as_int() == 12);
}

TEST_CASE("enum values usable in comparisons", "[classes][enum]") {
    Ctx c;
    REQUIRE(c.run(R"(
        enum Color { Red, Green, Blue }
        let c2 = Color.Green
        let is_green = c2 == Color.Green
        let is_red   = c2 == Color.Red
    )"));
    CHECK(c.g("is_green").as_bool() == true);
    CHECK(c.g("is_red").as_bool()   == false);
}

// ---------------------------------------------------------------------------
// Method syntax on values (string methods, table methods)
// ---------------------------------------------------------------------------

TEST_CASE("string method upper via dot syntax", "[classes][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let s = "hello"
        let u = s.upper()
    )"));
    CHECK(c.g("u").as_string() == "HELLO");
}

TEST_CASE("string method lower via dot syntax", "[classes][methods]") {
    Ctx c;
    REQUIRE(c.run(R"(
        let s = "WORLD"
        let l = s.lower()
    )"));
    CHECK(c.g("l").as_string() == "world");
}
