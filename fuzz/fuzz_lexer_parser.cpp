// ---------------------------------------------------------------------------
// ZScript libFuzzer harness — fuzz the lexer, parser, and compiler.
//
// Build (requires clang):
//   cmake -B build-fuzz -DZSCRIPT_FUZZ=ON \
//         -DCMAKE_CXX_COMPILER=clang++ \
//         -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build-fuzz --target fuzz_lexer_parser
//
// Run:
//   ./build-fuzz/fuzz_lexer_parser fuzz/corpus -max_len=4096 -timeout=5
//
// The target feeds arbitrary bytes into Lexer → Parser → Compiler.
// Any crash, ASAN finding, or unhandled exception is a bug.
// ---------------------------------------------------------------------------
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <cstddef>
#include <cstdint>

using namespace zscript;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Clamp to a reasonable size so the fuzzer doesn't waste time on huge inputs.
    if (size > 65536) return -1;   // -1 = discard this input from the corpus

    std::string src(reinterpret_cast<const char*>(data), size);

    // --- Lex ---
    Lexer lexer(src, "<fuzz>");
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) return 0;

    // --- Parse ---
    Parser parser(std::move(tokens), "<fuzz>");
    Program prog = parser.parse();
    if (parser.has_errors()) return 0;

    // --- Compile ---
    // Use None engine so neither @unity nor @unreal blocks are stripped.
    Compiler compiler(EngineMode::None);
    compiler.compile(prog, "<fuzz>");
    // Compiler errors are expected on malformed input; just ignore them.

    return 0;
}
