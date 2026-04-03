#include "lexer.h"
#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: zsc <file.zs>\n";
        return 1;
    }

    std::ifstream f(argv[1]);
    if (!f) {
        std::cerr << "error: cannot open '" << argv[1] << "'\n";
        return 1;
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    zscript::Lexer lexer(ss.str(), argv[1]);
    auto tokens = lexer.tokenize();

    for (auto& t : tokens) {
        std::cout << t.loc.line << ":" << t.loc.column
                  << "\t" << zscript::token_kind_name(t.kind)
                  << "\t\"" << t.lexeme << "\"\n";
    }

    if (lexer.has_errors()) {
        for (auto& e : lexer.errors()) {
            std::cerr << argv[1] << ":" << e.loc.line << ":" << e.loc.column
                      << ": error: " << e.message << "\n";
        }
        return 1;
    }
    return 0;
}
