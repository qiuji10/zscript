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
// Type parameter as a local — accessible inside the function body
// ---------------------------------------------------------------------------

TEST_CASE("type param T is accessible as a local inside generic fn", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn identity<T>(x) {
            return x
        }
        var r = identity<int>(42)
    )"));
    CHECK(c.g("r").as_int() == 42);
}

TEST_CASE("type param T holds the class table when a class is passed", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Enemy {}
        fn get_type<T>() {
            return T
        }
        let cls = get_type<Enemy>()
        var r = (cls == Enemy)
    )"));
    CHECK(c.g("r").as_bool() == true);
}

TEST_CASE("primitive type arg string for int", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn type_name<T>() { return T }
        var r = type_name<int>()
    )"));
    CHECK(c.g("r").as_string() == "int");
}

TEST_CASE("primitive type arg string for string", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn type_name<T>() { return T }
        var r = type_name<string>()
    )"));
    CHECK(c.g("r").as_string() == "string");
}

TEST_CASE("primitive type arg string for float", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn type_name<T>() { return T }
        var r = type_name<float>()
    )"));
    CHECK(c.g("r").as_string() == "float");
}

// ---------------------------------------------------------------------------
// Two type parameters
// ---------------------------------------------------------------------------

TEST_CASE("two type params both accessible", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn pair_types<K, V>() {
            return K + ":" + V
        }
        var r = pair_types<string, int>()
    )"));
    CHECK(c.g("r").as_string() == "string:int");
}

// ---------------------------------------------------------------------------
// Type param used with 'is' for runtime type checking
// ---------------------------------------------------------------------------

TEST_CASE("type param used with is operator to filter array", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Dog {}
        class Cat {}

        fn filter<T>(arr) {
            var out = []
            for item in arr {
                if item is T { out.push(item) }
            }
            return out
        }

        let animals = [Dog(), Cat(), Dog(), Cat(), Cat()]
        let dogs = filter<Dog>(animals)
        let cats = filter<Cat>(animals)
        var nd = #dogs
        var nc = #cats
    )"));
    CHECK(c.g("nd").as_int() == 2);
    CHECK(c.g("nc").as_int() == 3);
}

// ---------------------------------------------------------------------------
// Type param used to instantiate — T()
// ---------------------------------------------------------------------------

TEST_CASE("type param used to create instance T()", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Bullet {
            fn init() { self.speed = 100 }
        }
        fn spawn<T>() {
            return T()
        }
        let b = spawn<Bullet>()
        var r = b.speed
    )"));
    CHECK(c.g("r").as_int() == 100);
}

// ---------------------------------------------------------------------------
// Generic function called without type args — type params receive nil
// ---------------------------------------------------------------------------

TEST_CASE("generic fn called without type args receives nil for T", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn maybe_typed<T>(x) {
            if T == nil { return x * 2 }
            return x
        }
        var r = maybe_typed(5)   // no type arg — T is nil
    )"));
    CHECK(c.g("r").as_int() == 10);
}

// ---------------------------------------------------------------------------
// Generic function with default regular params + type param
// ---------------------------------------------------------------------------

TEST_CASE("generic fn with type param and default regular param", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn repeat<T>(x, n = 3) {
            var s = ""
            for i in 0..<n { s = s + tostring(x) }
            return s
        }
        var r = repeat<string>("ab")
    )"));
    CHECK(c.g("r").as_string() == "ababab");
}

// ---------------------------------------------------------------------------
// Method call with type arg — type arg passed as leading arg to native
// ---------------------------------------------------------------------------

TEST_CASE("method call with type arg passes it as first arg", "[generics]") {
    Ctx c;
    // Register a native that returns its first argument as a string
    c.vm.register_function("get_first_arg_type",
        [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {Value::from_string("none")};
            return {Value::from_string(args[0].to_string())};
        });
    REQUIRE(c.run(R"(
        class Repo {
            fn find<T>() {
                return get_first_arg_type(T)
            }
        }
        let repo = Repo()
        var r = repo.find<int>()
    )"));
    // T = "int" (primitive type string), passed to get_first_arg_type
    CHECK(c.g("r").as_string() == "int");
}

// ---------------------------------------------------------------------------
// Existing (non-generic) code unaffected
// ---------------------------------------------------------------------------

TEST_CASE("non-generic function unaffected by generic machinery", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn add(a, b) { return a + b }
        var r = add(10, 32)
    )"));
    CHECK(c.g("r").as_int() == 42);
}

TEST_CASE("class methods without type params unaffected", "[generics]") {
    Ctx c;
    REQUIRE(c.run(R"(
        class Vec {
            fn init(x, y) { self.x = x  self.y = y }
            fn len_sq() { return self.x * self.x + self.y * self.y }
        }
        let v = Vec(3, 4)
        var r = v.len_sq()
    )"));
    CHECK(c.g("r").as_int() == 25);
}
