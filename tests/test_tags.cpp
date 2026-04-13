#include <catch2/catch_test_macros.hpp>
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "chunk.h"

using namespace zscript;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
struct TagCtx {
    VM vm;
    std::unique_ptr<Chunk> chunk;

    TagCtx() { vm.open_stdlib(); }

    bool run(const std::string& src, TagSet tags = {}) {
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        if (lexer.has_errors()) return false;
        Parser parser(std::move(tokens));
        Program prog = parser.parse();
        if (parser.has_errors()) return false;
        Compiler compiler(tags);
        chunk = compiler.compile(prog, "<test>");
        if (compiler.has_errors()) return false;
        for (auto& t : tags) vm.add_tag(t);
        return vm.execute(*chunk);
    }

    Value global(const std::string& name) { return vm.get_global(name); }
};

// ---------------------------------------------------------------------------
// Tag block stripping
// ---------------------------------------------------------------------------

TEST_CASE("tag block: inactive tag is stripped", "[tags]") {
    TagCtx c;
    REQUIRE(c.run("var x = 0\n@windows { x = 1 }"));
    CHECK(c.global("x").as_int() == 0);  // stripped — tag not active
}

TEST_CASE("tag block: active tag is included", "[tags]") {
    TagCtx c;
    REQUIRE(c.run("var x = 0\n@windows { x = 1 }", {"windows"}));
    CHECK(c.global("x").as_int() == 1);
}

TEST_CASE("tag block: only matching tag runs", "[tags]") {
    TagCtx c;
    REQUIRE(c.run("var x = 0\n@windows { x = 1 }\n@macos { x = 2 }", {"macos"}));
    CHECK(c.global("x").as_int() == 2);
}

TEST_CASE("tag block: multiple tags, multiple blocks", "[tags]") {
    TagCtx c;
    REQUIRE(c.run("var x = 0\n@vulkan { x = x + 1 }\n@d3d12 { x = x + 2 }",
                  {"vulkan", "d3d12"}));
    CHECK(c.global("x").as_int() == 3);
}

TEST_CASE("tag block: custom scenario tags", "[tags]") {
    TagCtx c;
    REQUIRE(c.run("var mode = \"none\"\n@scenarioA { mode = \"A\" }\n@scenarioB { mode = \"B\" }",
                  {"scenarioB"}));
    CHECK(c.global("mode").as_string() == "B");
}

TEST_CASE("tag block: empty tag set strips all blocks", "[tags]") {
    TagCtx c;
    REQUIRE(c.run("var x = 99\n@unity { x = 1 }\n@unreal { x = 2 }\n@windows { x = 3 }"));
    CHECK(c.global("x").as_int() == 99);
}

TEST_CASE("tag block inside function", "[tags]") {
    TagCtx c;
    REQUIRE(c.run(R"(
        fn render() -> Int {
            @vulkan  { return 1 }
            @d3d12   { return 2 }
            return 0
        }
        var r = render()
    )", {"d3d12"}));
    CHECK(c.global("r").as_int() == 2);
}

// ---------------------------------------------------------------------------
// VM host API
// ---------------------------------------------------------------------------

TEST_CASE("vm add_tag / has_tag / remove_tag", "[tags][vm]") {
    VM vm;
    CHECK(!vm.has_tag("windows"));
    vm.add_tag("windows");
    CHECK(vm.has_tag("windows"));
    vm.remove_tag("windows");
    CHECK(!vm.has_tag("windows"));
}

TEST_CASE("vm active_tags returns all added tags", "[tags][vm]") {
    VM vm;
    vm.add_tag("vulkan");
    vm.add_tag("windows");
    auto& tags = vm.active_tags();
    CHECK(tags.count("vulkan") == 1);
    CHECK(tags.count("windows") == 1);
    CHECK(tags.count("d3d12") == 0);
}

// ---------------------------------------------------------------------------
// Annotations on classes
// ---------------------------------------------------------------------------

TEST_CASE("class annotation stored in VM registry", "[tags][annotations]") {
    TagCtx c;
    REQUIRE(c.run(R"(
        @unity.component
        class Player {
            var hp = 100
        }
    )"));
    auto& anns = c.vm.get_annotations("Player");
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].ns   == "unity");
    CHECK(anns[0].name == "component");
}

TEST_CASE("class with no annotations returns empty list", "[tags][annotations]") {
    TagCtx c;
    REQUIRE(c.run("class Foo { var x = 1 }"));
    CHECK(c.vm.get_annotations("Foo").empty());
    // __annotations__ must NOT exist as a script-accessible field
    Value cls = c.global("Foo");
    REQUIRE(cls.is_table());
    CHECK(cls.as_table()->get("__annotations__").is_nil());
}

TEST_CASE("class with multiple annotations", "[tags][annotations]") {
    TagCtx c;
    REQUIRE(c.run(R"(
        @unreal.uclass
        @unreal.blueprintable
        class Actor { }
    )"));
    auto& anns = c.vm.get_annotations("Actor");
    REQUIRE(anns.size() == 2);
    CHECK(anns[0].ns == "unreal"); CHECK(anns[0].name == "uclass");
    CHECK(anns[1].ns == "unreal"); CHECK(anns[1].name == "blueprintable");
}

TEST_CASE("script cannot tamper with annotation registry", "[tags][annotations]") {
    TagCtx c;
    // Script tries to forge annotations via globals — should have no effect on registry
    REQUIRE(c.run(R"(
        @unity.component
        class Player { }
        // attempt to overwrite the class table field
        Player.__annotations__ = nil
    )"));
    // VM registry is unaffected
    auto& anns = c.vm.get_annotations("Player");
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].ns == "unity");
}

TEST_CASE("all_annotations returns full registry", "[tags][annotations]") {
    TagCtx c;
    REQUIRE(c.run(R"(
        @unity.component  class A { }
        @unreal.uclass    class B { }
    )"));
    auto& all = c.vm.all_annotations();
    CHECK(all.count("A") == 1);
    CHECK(all.count("B") == 1);
    CHECK(all.at("A")[0].name == "component");
    CHECK(all.at("B")[0].name == "uclass");
}
