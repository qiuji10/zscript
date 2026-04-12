#pragma once
#include "token.h"
#include <string>
#include <vector>

namespace zscript {

struct LexError {
    SourceLoc loc;
    std::string message;
};

class Lexer {
public:
    // Tokenizes the full source. Errors are collected into errors(); lexing
    // always completes (error tokens are emitted inline and skipped).
    explicit Lexer(std::string source, std::string filename = "<input>");

    // Run the lexer and return all tokens (including final Eof).
    std::vector<Token> tokenize();

    const std::vector<LexError>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }

private:
    // ----- source navigation -----
    char peek(int ahead = 0) const;
    char advance();
    bool match(char expected);
    bool at_end() const;

    void skip_whitespace_and_comments();

    // ----- token builders -----
    Token make(TokenKind k, std::string lexeme = "");
    Token error_tok(const std::string& msg);

    // ----- scanners -----
    Token scan_next();
    Token scan_number();
    Token scan_string();       // handles interpolation — may push extra tokens
    Token scan_raw_string();   // backtick string — no escapes, no interpolation, multiline
    Token scan_ident_or_kw();

    // String interpolation helper: tokenizes the content between { } inside a
    // string literal and appends those tokens to pending_, then returns InterpEnd.
    void scan_interp_body();

    // ----- state -----
    std::string  source_;
    std::string  filename_;
    size_t       pos_    = 0;
    uint32_t     line_   = 1;
    uint32_t     col_    = 1;
    uint32_t     tok_line_  = 1;
    uint32_t     tok_col_   = 1;
    uint32_t     tok_offset_ = 0;

    // Tokens produced during string interpolation that need to be emitted
    // before the next real scan_next() call.
    std::vector<Token> pending_;

    std::vector<LexError> errors_;
};

} // namespace zscript
