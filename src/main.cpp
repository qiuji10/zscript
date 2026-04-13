#include "ast_dump.h"
#include "chunk.h"
#include "compiler.h"
#include "dap.h"
#include "lexer.h"
#include "lsp.h"
#include "parser.h"
#include "vm.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace zscript;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void print_errors(const std::string& path,
                         const std::vector<LexError>& errs) {
    for (auto& e : errs)
        std::cerr << path << ":" << e.loc.line << ":" << e.loc.column
                  << ": error: " << e.message << "\n";
}

static void print_errors(const std::string& path,
                         const std::vector<ParseError>& errs) {
    for (auto& e : errs)
        std::cerr << path << ":" << e.loc.line << ":" << e.loc.column
                  << ": error: " << e.message << "\n";
}

static void print_errors(const std::string& path,
                         const std::vector<CompileError>& errs) {
    for (auto& e : errs) {
        const char* label = (e.severity == CompileError::Severity::Warning)
                            ? "warning" : "error";
        std::cerr << path << ":" << e.loc.line << ":" << e.loc.column
                  << ": " << label << ": " << e.message << "\n";
    }
}

// Parse --tag=<name> flags (repeatable) from an argv array.
// Rejects names that are not valid identifiers and prints an error.
static TagSet parse_tags(int argc, char* argv[]) {
    TagSet tags;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--tag=", 0) != 0) continue;
        std::string name = arg.substr(6);
        if (!is_valid_tag(name)) {
            std::cerr << "zsc: invalid tag name '" << name
                      << "' (must match [A-Za-z_][A-Za-z0-9_]*)\n";
            std::exit(1);
        }
        tags.insert(name);
    }
    return tags;
}

// Compile a .zs source file → Chunk. Returns nullptr on error.
static std::unique_ptr<Chunk> compile_source(const std::string& path,
                                              const TagSet& tags,
                                              bool& had_error) {
    std::string src = read_file(path);
    if (src.empty() && !std::ifstream(path)) {
        std::cerr << "zsc: cannot open '" << path << "'\n";
        had_error = true;
        return nullptr;
    }

    Lexer lexer(src, path);
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        print_errors(path, lexer.errors());
        had_error = true;
        return nullptr;
    }

    Parser parser(std::move(tokens), path);
    Program prog = parser.parse();
    if (parser.has_errors()) {
        print_errors(path, parser.errors());
        had_error = true;
        return nullptr;
    }

    Compiler compiler(tags);
    auto chunk = compiler.compile(prog, path);
    if (!compiler.errors().empty())
        print_errors(path, compiler.errors());
    if (compiler.has_errors()) {
        had_error = true;
        return nullptr;
    }

    return chunk;
}

// ---------------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------------

static int cmd_run(int argc, char* argv[]) {
    // zsc run [--tag=<name>]... <file.zs>
    TagSet tags = parse_tags(argc, argv);
    std::string file;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] != '-') file = arg;
    }

    if (file.empty()) {
        std::cerr << "usage: zsc run [--tag=<name>]... <file.zs>\n";
        return 1;
    }

    bool had_error = false;
    auto chunk = compile_source(file, tags, had_error);
    if (had_error) return 1;

    VM vm;
    for (auto& t : tags) vm.add_tag(t);
    vm.open_stdlib();
    vm.open_io();
    vm.open_os();

    // Add the script's directory to the module search path for imports.
    auto sep = file.find_last_of("/\\");
    vm.loader().add_search_path(sep != std::string::npos ? file.substr(0, sep) : ".");

    if (!vm.execute(*chunk)) {
        std::cerr << "zsc: runtime error: " << vm.last_error().message << "\n";
        if (!vm.last_error().trace.empty())
            std::cerr << vm.last_error().trace;
        return 1;
    }
    return 0;
}

static int cmd_compile(int argc, char* argv[]) {
    // zsc compile [--tag=<name>]... <file.zs> [-o <file.zbc>]
    TagSet tags = parse_tags(argc, argv);
    std::string input, output;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-o") && i + 1 < argc) output = argv[++i];
        else if (arg[0] != '-') input = arg;
    }

    if (input.empty()) {
        std::cerr << "usage: zsc compile [--tag=<name>]... <file.zs> [-o <file.zbc>]\n";
        return 1;
    }
    if (output.empty()) {
        output = input;
        if (output.size() > 3 && output.substr(output.size() - 3) == ".zs")
            output = output.substr(0, output.size() - 3) + ".zbc";
        else
            output += ".zbc";
    }

    bool had_error = false;
    auto chunk = compile_source(input, tags, had_error);
    if (had_error) return 1;

    std::ofstream out(output, std::ios::binary);
    if (!out) {
        std::cerr << "zsc: cannot write '" << output << "'\n";
        return 1;
    }
    auto bytes = serialize_chunk(*chunk);
    out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    std::cout << "compiled: " << output << " (" << bytes.size() << " bytes)\n";
    return 0;
}

static int cmd_disasm(int argc, char* argv[]) {
    // zsc disasm <file.zbc>
    std::string file;
    for (int i = 0; i < argc; ++i) {
        if (argv[i][0] != '-') file = argv[i];
    }
    if (file.empty()) {
        std::cerr << "usage: zsc disasm <file.zbc>\n";
        return 1;
    }

    std::ifstream f(file, std::ios::binary);
    if (!f) {
        std::cerr << "zsc: cannot open '" << file << "'\n";
        return 1;
    }
    std::vector<uint8_t> bytes(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());

    Chunk chunk;
    if (!deserialize_chunk(bytes.data(), bytes.size(), chunk)) {
        std::cerr << "zsc: failed to deserialize '" << file << "'\n";
        return 1;
    }

    disassemble_chunk(chunk);
    return 0;
}

static int cmd_check(int argc, char* argv[]) {
    // zsc check [--tag=<name>]... <file.zs>  — compile only, report errors
    TagSet tags = parse_tags(argc, argv);
    std::string file;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] != '-') file = arg;
    }
    if (file.empty()) {
        std::cerr << "usage: zsc check [--tag=<name>]... <file.zs>\n";
        return 1;
    }
    bool had_error = false;
    compile_source(file, tags, had_error);
    if (!had_error) std::cout << "ok\n";
    return had_error ? 1 : 0;
}

static int cmd_dump(int argc, char* argv[]) {
    // zsc dump [--tag=<name>]... <file.zs>  — parse and print AST
    std::string file;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] != '-') file = arg;
    }
    if (file.empty()) {
        std::cerr << "usage: zsc dump [--tag=<name>]... <file.zs>\n";
        return 1;
    }

    std::string src = read_file(file);
    if (src.empty() && !std::ifstream(file)) {
        std::cerr << "zsc: cannot open '" << file << "'\n";
        return 1;
    }

    Lexer lexer(src, file);
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        print_errors(file, lexer.errors());
        return 1;
    }

    Parser parser(std::move(tokens), file);
    Program prog = parser.parse();
    if (parser.has_errors()) {
        print_errors(file, parser.errors());
        return 1;
    }

    dump_ast(std::cout, prog);
    return 0;
}

static int cmd_lsp(int /*argc*/, char* /*argv*/[]) {
    zscript::LspServer server;
    server.run();
    return 0;
}

static int cmd_dap(int /*argc*/, char* /*argv*/[]) {
    zscript::DapServer server;
    return server.run();
}

static void print_usage() {
    std::cout <<
        "ZScript compiler and runtime\n"
        "\n"
        "Usage:\n"
        "  zsc run     [--tag=<name>]... <file.zs>              compile and run\n"
        "  zsc check   [--tag=<name>]... <file.zs>              compile only, report errors\n"
        "  zsc compile [--tag=<name>]... <file.zs> [-o out.zbc] compile to bytecode\n"
        "  zsc disasm  <file.zbc>                               disassemble bytecode\n"
        "  zsc dump    <file.zs>                                parse and print AST\n"
        "  zsc lsp                                              start LSP server (stdio)\n"
        "  zsc dap                                              start DAP server (stdio)\n"
        "\n"
        "  --tag=<name>  activate a conditional @tag { } block (repeatable)\n"
        "\n"
        "Examples:\n"
        "  zsc run hello.zs\n"
        "  zsc run --tag=unity --tag=windows game.zs\n"
        "  zsc compile --tag=vulkan renderer.zs -o renderer.zbc\n"
        "  zsc disasm game.zbc\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::string cmd = argv[1];

    if (cmd == "run")
        return cmd_run(argc - 2, argv + 2);
    if (cmd == "check")
        return cmd_check(argc - 2, argv + 2);
    if (cmd == "compile")
        return cmd_compile(argc - 2, argv + 2);
    if (cmd == "disasm")
        return cmd_disasm(argc - 2, argv + 2);
    if (cmd == "dump")
        return cmd_dump(argc - 2, argv + 2);
    if (cmd == "lsp")
        return cmd_lsp(argc - 2, argv + 2);
    if (cmd == "dap")
        return cmd_dap(argc - 2, argv + 2);
    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }

    // Shortcut: zsc <file.zs> acts as zsc run <file.zs>
    if (cmd.size() > 3 && cmd.substr(cmd.size() - 3) == ".zs")
        return cmd_run(argc - 1, argv + 1);

    std::cerr << "zsc: unknown command '" << cmd << "'\n";
    print_usage();
    return 1;
}
