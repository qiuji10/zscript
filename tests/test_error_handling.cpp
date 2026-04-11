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
// throw / try-catch
// ---------------------------------------------------------------------------

TEST_CASE("throw causes runtime error without catch", "[error_handling][throw]") {
    Ctx c;
    bool ok = c.run(R"(
        throw "something went wrong"
    )");
    CHECK_FALSE(ok);
}

TEST_CASE("try-catch catches thrown string", "[error_handling][try_catch]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var caught = ""
        try {
            throw "oops"
        } catch e {
            caught = e
        }
    )"));
    CHECK(c.g("caught").as_string() == "oops");
}

TEST_CASE("code after try-catch continues executing", "[error_handling][try_catch]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var result = 0
        try {
            throw "err"
        } catch e {
            result = 1
        }
        result = result + 10
    )"));
    CHECK(c.g("result").as_int() == 11);
}

TEST_CASE("no throw means catch block is skipped", "[error_handling][try_catch]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var catch_ran = false
        try {
            var x = 1 + 1
        } catch e {
            catch_ran = true
        }
    )"));
    CHECK(c.g("catch_ran").as_bool() == false);
}

TEST_CASE("throw propagates through function call", "[error_handling][throw]") {
    Ctx c;
    REQUIRE(c.run(R"(
        fn risky() {
            throw "deep error"
        }
        var caught = ""
        try {
            risky()
        } catch e {
            caught = e
        }
    )"));
    CHECK(c.g("caught").as_string() == "deep error");
}

TEST_CASE("catch variable holds thrown value", "[error_handling][try_catch]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var msg = ""
        try {
            throw "test message"
        } catch err {
            msg = err
        }
    )"));
    CHECK(c.g("msg").as_string() == "test message");
}

TEST_CASE("nested try-catch inner catches first", "[error_handling][try_catch]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var inner_caught = false
        var outer_caught = false
        try {
            try {
                throw "inner"
            } catch e {
                inner_caught = true
            }
        } catch e {
            outer_caught = true
        }
    )"));
    CHECK(c.g("inner_caught").as_bool() == true);
    CHECK(c.g("outer_caught").as_bool() == false);
}

TEST_CASE("throw in catch block propagates to outer", "[error_handling][try_catch]") {
    Ctx c;
    REQUIRE(c.run(R"(
        var outer_caught = false
        try {
            try {
                throw "first"
            } catch e {
                throw "rethrown"
            }
        } catch e {
            outer_caught = true
        }
    )"));
    CHECK(c.g("outer_caught").as_bool() == true);
}
