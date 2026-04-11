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

// ---------------------------------------------------------------------------
// is type check operator
// ---------------------------------------------------------------------------

TEST_CASE("is operator: direct class match", "[classes][is]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Cat {}
        let cat = Cat()
        var result = cat is Cat
    )"));
    CHECK(c.g("result").as_bool() == true);
}

TEST_CASE("is operator: wrong class returns false", "[classes][is]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Dog {}
        class Cat {}
        let dog = Dog()
        var result = dog is Cat
    )"));
    CHECK(c.g("result").as_bool() == false);
}

TEST_CASE("is operator: instance is also instance of parent", "[classes][is]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Animal {}
        class Dog : Animal {}
        let d = Dog()
        var is_dog    = d is Dog
        var is_animal = d is Animal
    )"));
    CHECK(c.g("is_dog").as_bool()    == true);
    CHECK(c.g("is_animal").as_bool() == true);
}

TEST_CASE("is operator: multi-level inheritance chain", "[classes][is]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class A {}
        class B : A {}
        class C : B {}
        let obj = C()
        var is_c = obj is C
        var is_b = obj is B
        var is_a = obj is A
    )"));
    CHECK(c.g("is_c").as_bool() == true);
    CHECK(c.g("is_b").as_bool() == true);
    CHECK(c.g("is_a").as_bool() == true);
}

TEST_CASE("is operator: sibling class returns false", "[classes][is]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Base {}
        class Left : Base {}
        class Right : Base {}
        let l = Left()
        var l_is_left  = l is Left
        var l_is_right = l is Right
        var l_is_base  = l is Base
    )"));
    CHECK(c.g("l_is_left").as_bool()  == true);
    CHECK(c.g("l_is_right").as_bool() == false);
    CHECK(c.g("l_is_base").as_bool()  == true);
}

// ---------------------------------------------------------------------------
// Default parameter values
// ---------------------------------------------------------------------------

TEST_CASE("default param used when arg is omitted", "[classes][defaults]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn greet(name, greeting = "hello") {
            return greeting + " " + name
        }
        var r1 = greet("world")
        var r2 = greet("world", "hi")
    )"));
    CHECK(c.g("r1").as_string() == "hello world");
    CHECK(c.g("r2").as_string() == "hi world");
}

// ---------------------------------------------------------------------------
// Static methods and fields
// ---------------------------------------------------------------------------

TEST_CASE("static method callable on class without instance", "[classes][static]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class MathHelper {
            static fn square(x) { return x * x }
            static fn cube(x)   { return x * x * x }
        }
        var s = MathHelper.square(5)
        var cu = MathHelper.cube(3)
    )"));
    CHECK(c.g("s").as_int()  == 25);
    CHECK(c.g("cu").as_int() == 27);
}

TEST_CASE("static field shared across class", "[classes][static]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Counter {
            static var count = 0
            fn init() { Counter.count = Counter.count + 1 }
        }
        let a = Counter()
        let b = Counter()
        let c2 = Counter()
        var n = Counter.count
    )"));
    CHECK(c.g("n").as_int() == 3);
}

TEST_CASE("static field not copied to instances", "[classes][static]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Foo {
            static var x = 42
            fn init() {}
        }
        let f = Foo()
        var on_class    = Foo.x
        var on_instance = f.x
    )"));
    CHECK(c.g("on_class").as_int() == 42);
    CHECK(c.g("on_instance").is_nil() == true);
}

TEST_CASE("static factory method creates instances", "[classes][static]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Point {
            fn init(x, y) { self.x = x  self.y = y }
            static fn origin() { return Point(0, 0) }
            static fn unit_x() { return Point(1, 0) }
        }
        let o = Point.origin()
        let u = Point.unit_x()
        var ox = o.x
        var uy = u.y
    )"));
    CHECK(c.g("ox").as_int() == 0);
    CHECK(c.g("uy").as_int() == 0);
}

TEST_CASE("static and instance methods coexist", "[classes][static]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Dog {
            static var species = "Canis lupus"
            fn init(name) { self.name = name }
            fn bark()     { return self.name + " says woof" }
            static fn info() { return Dog.species }
        }
        let d = Dog("Rex")
        var sound   = d.bark()
        var species = Dog.info()
        var inst_species = d.species
    )"));
    CHECK(c.g("sound").as_string()        == "Rex says woof");
    CHECK(c.g("species").as_string()      == "Canis lupus");
    CHECK(c.g("inst_species").is_nil()    == true);
}

TEST_CASE("multiple default params", "[classes][defaults]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn make(x = 1, y = 2, z = 3) {
            return x + y + z
        }
        var a = make()
        var b = make(10)
        var c2 = make(10, 20)
        var d = make(10, 20, 30)
    )"));
    CHECK(c.g("a").as_int() == 6);
    CHECK(c.g("b").as_int() == 15);
    CHECK(c.g("c2").as_int() == 33);
    CHECK(c.g("d").as_int() == 60);
}

// ---------------------------------------------------------------------------
// Traits
// ---------------------------------------------------------------------------

TEST_CASE("abstract trait: is operator returns true after impl", "[classes][traits]") {
    Ctx c;
    REQUIRE(c.run(R"(
        trait Walkable { fn walk() }
        class Dog {
            fn init(name) { self.name = name }
        }
        impl Walkable for Dog {
            fn walk() { return self.name + " walks" }
        }
        let d = Dog("Rex")
        var result = d is Walkable
    )"));
    CHECK(c.g("result").as_bool() == true);
}

TEST_CASE("is operator returns false for unimplemented trait", "[classes][traits]") {
    Ctx c;
    REQUIRE(c.run(R"(
        trait Flyable { fn fly() }
        class Dog {
            fn init(name) { self.name = name }
        }
        let d = Dog("Rex")
        var result = d is Flyable
    )"));
    CHECK(c.g("result").as_bool() == false);
}

TEST_CASE("impl injects method onto class", "[classes][traits]") {
    Ctx c;
    REQUIRE(c.run(R"(
        trait Greetable { fn greet() }
        class Person {
            fn init(name) { self.name = name }
        }
        impl Greetable for Person {
            fn greet() { return "Hello, " + self.name }
        }
        let p = Person("Alice")
        var msg = p.greet()
    )"));
    CHECK(c.g("msg").as_string() == "Hello, Alice");
}

TEST_CASE("trait with default method is used when impl does not override", "[classes][traits]") {
    Ctx c;
    REQUIRE(c.run(R"(
        trait Describable {
            fn describe() { return "I am an object" }
        }
        class Box {
            fn init(w) { self.w = w }
        }
        impl Describable for Box {}
        let b = Box(5)
        var desc = b.describe()
        var is_d = b is Describable
    )"));
    CHECK(c.g("desc").as_string() == "I am an object");
    CHECK(c.g("is_d").as_bool()   == true);
}

TEST_CASE("impl overrides trait default method", "[classes][traits]") {
    Ctx c;
    REQUIRE(c.run(R"(
        trait Named {
            fn name() { return "unknown" }
        }
        class Cat {
            fn init(n) { self.n = n }
        }
        impl Named for Cat {
            fn name() { return self.n }
        }
        let c2 = Cat("Whiskers")
        var nm = c2.name()
    )"));
    CHECK(c.g("nm").as_string() == "Whiskers");
}

TEST_CASE("child class inherits trait marker from parent", "[classes][traits]") {
    Ctx c;
    REQUIRE(c.run(R"(
        trait Runnable { fn run() }
        class Animal {
            fn init(name) { self.name = name }
        }
        impl Runnable for Animal {
            fn run() { return self.name + " runs" }
        }
        class Horse : Animal {
            fn init(name) { super.init(name) }
        }
        let h = Horse("Spirit")
        var is_r  = h is Runnable
        var speed = h.run()
    )"));
    CHECK(c.g("is_r").as_bool()   == true);
    CHECK(c.g("speed").as_string() == "Spirit runs");
}

TEST_CASE("class can implement multiple traits", "[classes][traits]") {
    Ctx c;
    REQUIRE(c.run(R"(
        trait Swimmer { fn swim() }
        trait Diver   { fn dive() }
        class Duck {
            fn init(name) { self.name = name }
        }
        impl Swimmer for Duck {
            fn swim() { return self.name + " swims" }
        }
        impl Diver for Duck {
            fn dive() { return self.name + " dives" }
        }
        let d = Duck("Donald")
        var sw = d.swim()
        var dv = d.dive()
        var is_sw = d is Swimmer
        var is_dv = d is Diver
    )"));
    CHECK(c.g("sw").as_string()  == "Donald swims");
    CHECK(c.g("dv").as_string()  == "Donald dives");
    CHECK(c.g("is_sw").as_bool() == true);
    CHECK(c.g("is_dv").as_bool() == true);
}
