#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
using namespace zscript;
namespace fs = std::filesystem;

struct Ctx {
    VM vm;
    Ctx() { vm.open_stdlib(); vm.open_io(); vm.open_os(); }
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
// Helpers
// ---------------------------------------------------------------------------

// Create a temp directory that auto-cleans on destruction.
struct TmpDir {
    fs::path path;
    TmpDir() {
        path = fs::temp_directory_path() / ("zscript_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TmpDir() { fs::remove_all(path); }
    std::string str() const { return path.string(); }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

// ---------------------------------------------------------------------------
// io.write_file / io.read_file
// ---------------------------------------------------------------------------

TEST_CASE("io.write_file returns true on success", "[io]") {
    TmpDir d;
    std::string f = d.file("hello.txt");
    Ctx c;
    bool ok = c.run("var r = io.write_file(\"" + f + "\", \"hello\")");
    REQUIRE(ok);
    CHECK(c.g("r").as_bool() == true);
}

TEST_CASE("io.read_file returns written content", "[io]") {
    TmpDir d;
    std::string f = d.file("data.txt");
    { std::ofstream of(f); of << "ZScript"; }
    Ctx c;
    REQUIRE(c.run("var r = io.read_file(\"" + f + "\")"));
    CHECK(c.g("r").as_string() == "ZScript");
}

TEST_CASE("io.read_file on missing file returns nil", "[io]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = io.read_file("/nonexistent/surely_missing_xyz.txt"))"));
    CHECK(c.g("r").tag == Value::Tag::Nil);
}

// ---------------------------------------------------------------------------
// io.append_file
// ---------------------------------------------------------------------------

TEST_CASE("io.append_file appends to existing file", "[io]") {
    TmpDir d;
    std::string f = d.file("append.txt");
    { std::ofstream of(f); of << "Hello"; }
    Ctx c;
    REQUIRE(c.run("io.append_file(\"" + f + "\", \" World\")"));
    std::ifstream in(f);
    std::string content((std::istreambuf_iterator<char>(in)), {});
    CHECK(content == "Hello World");
}

// ---------------------------------------------------------------------------
// io.lines
// ---------------------------------------------------------------------------

TEST_CASE("io.lines returns array of strings", "[io]") {
    TmpDir d;
    std::string f = d.file("lines.txt");
    { std::ofstream of(f); of << "one\ntwo\nthree"; }
    Ctx c;
    REQUIRE(c.run("var arr = io.lines(\"" + f + "\")"));
    auto* t = c.g("arr").as_table();
    REQUIRE(t != nullptr);
    CHECK(t->get_index(0).as_string() == "one");
    CHECK(t->get_index(1).as_string() == "two");
    CHECK(t->get_index(2).as_string() == "three");
}

TEST_CASE("io.lines on missing file returns nil", "[io]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = io.lines("/nonexistent/xyz.txt"))"));
    CHECK(c.g("r").tag == Value::Tag::Nil);
}

// ---------------------------------------------------------------------------
// io.exists / io.size / io.delete_file
// ---------------------------------------------------------------------------

TEST_CASE("io.exists returns true for existing file", "[io]") {
    TmpDir d;
    std::string f = d.file("ex.txt");
    { std::ofstream of(f); of << "x"; }
    Ctx c;
    REQUIRE(c.run("var r = io.exists(\"" + f + "\")"));
    CHECK(c.g("r").as_bool() == true);
}

TEST_CASE("io.exists returns false for missing file", "[io]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = io.exists("/nonexistent/surely_xyz.txt"))"));
    CHECK(c.g("r").as_bool() == false);
}

TEST_CASE("io.size returns byte count", "[io]") {
    TmpDir d;
    std::string f = d.file("size.txt");
    { std::ofstream of(f); of << "12345"; }
    Ctx c;
    REQUIRE(c.run("var r = io.size(\"" + f + "\")"));
    CHECK(c.g("r").as_int() == 5);
}

TEST_CASE("io.delete_file removes file", "[io]") {
    TmpDir d;
    std::string f = d.file("del.txt");
    { std::ofstream of(f); of << "bye"; }
    Ctx c;
    REQUIRE(c.run("var r = io.delete_file(\"" + f + "\")"));
    CHECK(c.g("r").as_bool() == true);
    CHECK(!fs::exists(f));
}

// ---------------------------------------------------------------------------
// io.rename / io.copy_file
// ---------------------------------------------------------------------------

TEST_CASE("io.rename moves a file", "[io]") {
    TmpDir d;
    std::string src = d.file("src.txt");
    std::string dst = d.file("dst.txt");
    { std::ofstream of(src); of << "data"; }
    Ctx c;
    REQUIRE(c.run("var r = io.rename(\"" + src + "\", \"" + dst + "\")"));
    CHECK(c.g("r").as_bool() == true);
    CHECK(!fs::exists(src));
    CHECK(fs::exists(dst));
}

TEST_CASE("io.copy_file duplicates a file", "[io]") {
    TmpDir d;
    std::string src = d.file("orig.txt");
    std::string dst = d.file("copy.txt");
    { std::ofstream of(src); of << "content"; }
    Ctx c;
    REQUIRE(c.run("var r = io.copy_file(\"" + src + "\", \"" + dst + "\")"));
    CHECK(c.g("r").as_bool() == true);
    CHECK(fs::exists(src));
    CHECK(fs::exists(dst));
    std::ifstream in(dst);
    std::string s((std::istreambuf_iterator<char>(in)), {});
    CHECK(s == "content");
}

// ---------------------------------------------------------------------------
// os.getcwd / os.mkdir / os.rmdir / os.listdir
// ---------------------------------------------------------------------------

TEST_CASE("os.getcwd returns non-empty string", "[os]") {
    Ctx c;
    REQUIRE(c.run("var r = os.getcwd()"));
    CHECK(!c.g("r").as_string().empty());
}

TEST_CASE("os.mkdir creates directory", "[os]") {
    TmpDir d;
    std::string sub = d.file("a/b/c");
    Ctx c;
    REQUIRE(c.run("var r = os.mkdir(\"" + sub + "\")"));
    CHECK(c.g("r").as_bool() == true);
    CHECK(fs::is_directory(sub));
}

TEST_CASE("os.rmdir removes directory tree", "[os]") {
    TmpDir d;
    std::string sub = d.file("tree");
    fs::create_directories(sub + "/x");
    Ctx c;
    REQUIRE(c.run("var r = os.rmdir(\"" + sub + "\")"));
    CHECK(c.g("r").as_bool() == true);
    CHECK(!fs::exists(sub));
}

TEST_CASE("os.listdir returns filenames", "[os]") {
    TmpDir d;
    { std::ofstream of(d.file("alpha.txt")); of << "a"; }
    { std::ofstream of(d.file("beta.txt"));  of << "b"; }
    Ctx c;
    REQUIRE(c.run("var arr = os.listdir(\"" + d.str() + "\")"));
    auto* t = c.g("arr").as_table();
    REQUIRE(t != nullptr);
    // two entries; sort-order not guaranteed, so just check count
    CHECK((int64_t)t->array.size() == 2);
}

TEST_CASE("os.listdir on missing dir returns nil", "[os]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = os.listdir("/nonexistent/surely_xyz"))"));
    CHECK(c.g("r").tag == Value::Tag::Nil);
}

// ---------------------------------------------------------------------------
// os.getenv
// ---------------------------------------------------------------------------

TEST_CASE("os.getenv returns existing env var", "[os]") {
    Ctx c;
    // PATH is present on all platforms
    REQUIRE(c.run(R"(var r = os.getenv("PATH"))"));
    CHECK(!c.g("r").as_string().empty());
}

TEST_CASE("os.getenv returns nil for missing var", "[os]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = os.getenv("ZSCRIPT_SURELY_UNSET_XYZ_12345"))"));
    CHECK(c.g("r").tag == Value::Tag::Nil);
}

// ---------------------------------------------------------------------------
// os.time / os.clock / os.platform
// ---------------------------------------------------------------------------

TEST_CASE("os.time returns positive integer", "[os]") {
    Ctx c;
    REQUIRE(c.run("var r = os.time()"));
    CHECK(c.g("r").as_int() > 0);
}

TEST_CASE("os.clock returns non-negative float", "[os]") {
    Ctx c;
    REQUIRE(c.run("var r = os.clock()"));
    CHECK(c.g("r").as_float() >= 0.0);
}

TEST_CASE("os.platform returns known string", "[os]") {
    Ctx c;
    REQUIRE(c.run("var r = os.platform()"));
    std::string p = c.g("r").as_string();
    CHECK((p == "windows" || p == "macos" || p == "linux" ||
           p == "android" || p == "unknown"));
}

// ---------------------------------------------------------------------------
// os.path subtable
// ---------------------------------------------------------------------------

TEST_CASE("os.path.basename extracts filename", "[os][path]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = os.path.basename("/foo/bar/baz.txt"))"));
    CHECK(c.g("r").as_string() == "baz.txt");
}

TEST_CASE("os.path.dirname extracts parent", "[os][path]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = os.path.dirname("/foo/bar/baz.txt"))"));
    CHECK(c.g("r").as_string() == "/foo/bar");
}

TEST_CASE("os.path.ext extracts extension", "[os][path]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = os.path.ext("/foo/bar/baz.txt"))"));
    CHECK(c.g("r").as_string() == ".txt");
}

TEST_CASE("os.path.stem extracts stem", "[os][path]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = os.path.stem("/foo/bar/baz.txt"))"));
    CHECK(c.g("r").as_string() == "baz");
}

TEST_CASE("os.path.join concatenates segments", "[os][path]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = os.path.join("/foo", "bar", "baz.txt"))"));
    std::string joined = c.g("r").as_string();
    // Accept both separators
    CHECK(joined.find("bar") != std::string::npos);
    CHECK(joined.find("baz.txt") != std::string::npos);
}

TEST_CASE("os.path.is_dir true for existing directory", "[os][path]") {
    TmpDir d;
    Ctx c;
    REQUIRE(c.run("var r = os.path.is_dir(\"" + d.str() + "\")"));
    CHECK(c.g("r").as_bool() == true);
}

TEST_CASE("os.path.is_file true for regular file", "[os][path]") {
    TmpDir d;
    std::string f = d.file("f.txt");
    { std::ofstream of(f); of << "x"; }
    Ctx c;
    REQUIRE(c.run("var r = os.path.is_file(\"" + f + "\")"));
    CHECK(c.g("r").as_bool() == true);
}

TEST_CASE("os.path.abs returns absolute path", "[os][path]") {
    Ctx c;
    REQUIRE(c.run(R"(var r = os.path.abs("."))"));
    std::string abs = c.g("r").as_string();
    CHECK(!abs.empty());
    // On POSIX absolute paths start with /; on Windows with a drive letter.
#if defined(_WIN32)
    CHECK(abs.size() >= 3);
#else
    CHECK(abs[0] == '/');
#endif
}
