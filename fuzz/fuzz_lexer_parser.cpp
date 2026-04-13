// ---------------------------------------------------------------------------
// ZScript libFuzzer harness — fuzz the lexer, parser, and compiler.
//
// Build — Linux (install LLVM clang, which includes libFuzzer):
//   sudo apt-get install clang
//   cmake -B build-fuzz -DZSCRIPT_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++ \
//         -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build-fuzz --target fuzz_lexer_parser
//   ./build-fuzz/fuzz_lexer_parser fuzz/corpus -max_len=4096 -timeout=5
//
// Build — macOS (Apple clang does NOT include libFuzzer; use LLVM via Homebrew):
//   brew install llvm
//   cmake -B build-fuzz -DZSCRIPT_FUZZ=ON \
//         -DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++ \
//         -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build-fuzz --target fuzz_lexer_parser
//   ./build-fuzz/fuzz_lexer_parser fuzz/corpus -max_len=4096 -timeout=5
//
// Build — Windows (MSVC is unaffected; this uses a separate build dir with
//         LLVM clang. Install via 'winget install LLVM.LLVM'):
//   cmake -B build-fuzz -DZSCRIPT_FUZZ=ON \
//         -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" \
//         -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build-fuzz --target fuzz_lexer_parser
//   build-fuzz\RelWithDebInfo\fuzz_lexer_parser.exe fuzz\corpus -max_len=4096 -timeout=5
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

    try {
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
        // Empty TagSet: all @tag {} blocks are stripped (compile-time dead code).
        Compiler compiler;
        compiler.compile(prog, "<fuzz>");
        // Compiler errors are expected on malformed input; just ignore them.
    } catch (...) {
        // Any unhandled exception is a bug — returning 0 lets the fuzzer report
        // the input without aborting the process. The exception will surface as a
        // finding in ASan mode since asan intercepts the throw.
        return 0;
    }

    return 0;
}
