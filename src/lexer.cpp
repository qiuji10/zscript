#include "lexer.h"
#include <cassert>
#include <unordered_map>

namespace zscript {

// ---------------------------------------------------------------------------
// Keyword table
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, TokenKind> keywords = {
    {"let",    TokenKind::KwLet},
    {"var",    TokenKind::KwVar},
    {"fn",     TokenKind::KwFn},
    {"return", TokenKind::KwReturn},
    {"if",     TokenKind::KwIf},
    {"else",   TokenKind::KwElse},
    {"while",  TokenKind::KwWhile},
    {"for",    TokenKind::KwFor},
    {"in",     TokenKind::KwIn},
    {"class",  TokenKind::KwClass},
    {"trait",  TokenKind::KwTrait},
    {"impl",   TokenKind::KwImpl},
    {"self",   TokenKind::KwSelf},
    {"mut",      TokenKind::KwMut},
    {"import",   TokenKind::KwImport},
    {"break",    TokenKind::KwBreak},
    {"continue", TokenKind::KwContinue},
    {"match",    TokenKind::KwMatch},
    {"not",      TokenKind::Bang},   // 'not' is an alias for '!'
    {"true",   TokenKind::LitTrue},
    {"false",  TokenKind::LitFalse},
    {"nil",    TokenKind::LitNil},
    {"and",    TokenKind::And},
    {"or",     TokenKind::Or},
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Lexer::Lexer(std::string source, std::string filename)
    : source_(std::move(source)), filename_(std::move(filename)) {}

// ---------------------------------------------------------------------------
// Source navigation helpers
// ---------------------------------------------------------------------------
bool Lexer::at_end() const {
    return pos_ >= source_.size();
}

char Lexer::peek(int ahead) const {
    size_t p = pos_ + ahead;
    return (p < source_.size()) ? source_[p] : '\0';
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else           { ++col_; }
    return c;
}

bool Lexer::match(char expected) {
    if (at_end() || source_[pos_] != expected) return false;
    advance();
    return true;
}

// ---------------------------------------------------------------------------
// Whitespace and comment skipping
// ---------------------------------------------------------------------------
void Lexer::skip_whitespace_and_comments() {
    while (!at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            // line comment
            while (!at_end() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            // block comment
            advance(); advance(); // consume /*
            while (!at_end()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance(); advance();
                    break;
                }
                advance();
            }
        } else {
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Token construction helpers
// ---------------------------------------------------------------------------
Token Lexer::make(TokenKind k, std::string lexeme) {
    Token t;
    t.kind   = k;
    t.lexeme = std::move(lexeme);
    t.loc    = { tok_line_, tok_col_, tok_offset_ };
    return t;
}

Token Lexer::error_tok(const std::string& msg) {
    errors_.push_back({ {tok_line_, tok_col_, tok_offset_}, msg });
    return make(TokenKind::Error, msg);
}

// ---------------------------------------------------------------------------
// Number literal
// ---------------------------------------------------------------------------
Token Lexer::scan_number() {
    std::string buf;
    bool is_float = false;

    while (!at_end() && (std::isdigit(peek()) || peek() == '_')) {
        char c = advance();
        if (c != '_') buf += c;  // allow _ separators: 1_000_000
    }

    if (peek() == '.' && peek(1) != '.' && std::isdigit(peek(1))) {
        is_float = true;
        buf += advance(); // '.'
        while (!at_end() && (std::isdigit(peek()) || peek() == '_')) {
            char c = advance();
            if (c != '_') buf += c;
        }
    }

    // Optional exponent
    if (!is_float && (peek() == 'e' || peek() == 'E')) {
        is_float = true;
        buf += advance();
        if (peek() == '+' || peek() == '-') buf += advance();
        while (!at_end() && std::isdigit(peek())) buf += advance();
    }

    return make(is_float ? TokenKind::LitFloat : TokenKind::LitInt, buf);
}

// ---------------------------------------------------------------------------
// String literal (with {expr} interpolation)
//
// Strategy:
//   "hello {name}!"
//    ^               → LitString("hello ")
//                     → InterpStart
//                         → Ident("name")     ← from scan_interp_body
//                     → InterpEnd
//                     → LitString("!")
//                     → (caller sees Eof or next segment)
//
// We return the first LitString token; any extra tokens for the interpolated
// segments are pushed onto pending_ and will be drained by tokenize().
// ---------------------------------------------------------------------------
Token Lexer::scan_string() {
    // opening quote already consumed by scan_next
    std::string buf;

    while (!at_end() && peek() != '"') {
        char c = peek();

        if (c == '\\') {
            advance(); // consume backslash
            if (at_end()) break;
            char esc = advance();
            switch (esc) {
                case 'n':  buf += '\n'; break;
                case 't':  buf += '\t'; break;
                case 'r':  buf += '\r'; break;
                case '"':  buf += '"';  break;
                case '\\': buf += '\\'; break;
                case '{':  buf += '{';  break; // escaped brace
                default:
                    errors_.push_back({{line_, col_, (uint32_t)pos_},
                                       std::string("unknown escape: \\") + esc});
                    buf += esc;
            }
        } else if (c == '{') {
            // Start of interpolated expression.
            // Emit the string-so-far as a LitString, then push the interp tokens.
            Token str_tok = make(TokenKind::LitString, buf);
            advance(); // consume '{'

            // Push InterpStart
            pending_.push_back(make(TokenKind::InterpStart, "{"));

            // Tokenize the expression inside { } and push onto pending_
            scan_interp_body();

            // After interp, continue scanning the rest of the string.
            // We do this recursively by continuing this loop — any further
            // text or interpolations end up in pending_ as well.
            // But first we must resume building the next string segment.
            // We collect it directly here and push a LitString + Eof at end.
            std::string rest;
            while (!at_end() && peek() != '"') {
                char cc = peek();
                if (cc == '{') {
                    Token seg = make(TokenKind::LitString, rest);
                    pending_.push_back(seg);
                    rest.clear();
                    advance(); // consume '{'
                    pending_.push_back(make(TokenKind::InterpStart, "{"));
                    scan_interp_body();
                } else if (cc == '\\') {
                    advance();
                    if (at_end()) break;
                    char esc = advance();
                    switch (esc) {
                        case 'n':  rest += '\n'; break;
                        case 't':  rest += '\t'; break;
                        case 'r':  rest += '\r'; break;
                        case '"':  rest += '"';  break;
                        case '\\': rest += '\\'; break;
                        case '{':  rest += '{';  break;
                        default:   rest += esc;  break;
                    }
                } else {
                    rest += advance();
                }
            }
            if (!at_end()) advance(); // closing "
            // Push final segment (may be empty)
            pending_.push_back(make(TokenKind::LitString, rest));
            return str_tok;
        } else {
            buf += advance();
        }
    }

    if (at_end()) {
        return error_tok("unterminated string literal");
    }
    advance(); // closing "
    return make(TokenKind::LitString, buf);
}

// ---------------------------------------------------------------------------
// Scan the body of a string interpolation { expr } and push tokens to pending_.
// Stops at the matching closing '}'.
// ---------------------------------------------------------------------------
void Lexer::scan_interp_body() {
    int depth = 1; // we already consumed the opening '{'
    while (!at_end() && depth > 0) {
        skip_whitespace_and_comments();
        if (at_end()) break;

        if (peek() == '}') {
            --depth;
            advance();
            if (depth == 0) {
                pending_.push_back(make(TokenKind::InterpEnd, "}"));
                return;
            }
            pending_.push_back(make(TokenKind::RBrace, "}"));
            continue;
        }
        if (peek() == '{') {
            ++depth;
            advance();
            pending_.push_back(make(TokenKind::LBrace, "{"));
            continue;
        }

        // Save loc for this token
        tok_line_   = line_;
        tok_col_    = col_;
        tok_offset_ = (uint32_t)pos_;
        Token t = scan_next();
        pending_.push_back(t);
    }
}

// ---------------------------------------------------------------------------
// Identifier or keyword
// ---------------------------------------------------------------------------
Token Lexer::scan_ident_or_kw() {
    std::string buf;
    while (!at_end() && (std::isalnum(peek()) || peek() == '_')) {
        buf += advance();
    }
    auto it = keywords.find(buf);
    if (it != keywords.end()) return make(it->second, buf);
    return make(TokenKind::Ident, buf);
}

// ---------------------------------------------------------------------------
// Core scanner — one token from current position
// ---------------------------------------------------------------------------
Token Lexer::scan_next() {
    skip_whitespace_and_comments();

    tok_line_   = line_;
    tok_col_    = col_;
    tok_offset_ = (uint32_t)pos_;

    if (at_end()) return make(TokenKind::Eof);

    char c = advance();

    switch (c) {
        // --- single-char delimiters ---
        case '(': return make(TokenKind::LParen,    "(");
        case ')': return make(TokenKind::RParen,    ")");
        case '[': return make(TokenKind::LBracket,  "[");
        case ']': return make(TokenKind::RBracket,  "]");
        case '{': return make(TokenKind::LBrace,    "{");
        case '}': return make(TokenKind::RBrace,    "}");
        case ',': return make(TokenKind::Comma,     ",");
        case ';': return make(TokenKind::Semicolon, ";");
        case '@': return make(TokenKind::At,        "@");
        case '%': return make(TokenKind::Percent,   "%");
        case '*': return make(TokenKind::Star,      "*");
        case '/': return make(TokenKind::Slash,     "/");

        // --- colon ---
        case ':': return make(TokenKind::Colon, ":");

        // --- dot / range ---
        case '.':
            if (peek() == '.' && peek(1) == '<') {
                advance(); advance();
                return make(TokenKind::DotDotLt, "..<");
            }
            if (peek() == '.') {
                advance();
                return make(TokenKind::DotDot, "..");
            }
            return make(TokenKind::Dot, ".");

        // --- arrow / minus ---
        case '-':
            if (match('>')) return make(TokenKind::Arrow, "->");
            if (match('=')) return make(TokenKind::MinusAssign, "-=");
            return make(TokenKind::Minus, "-");

        // --- fat arrow / assign / eq ---
        case '=':
            if (match('>')) return make(TokenKind::FatArrow, "=>");
            if (match('=')) return make(TokenKind::Eq, "==");
            return make(TokenKind::Assign, "=");

        // --- plus ---
        case '+':
            if (match('=')) return make(TokenKind::PlusAssign, "+=");
            return make(TokenKind::Plus, "+");

        // --- bang / not-eq / force-unwrap ---
        case '!':
            if (match('=')) return make(TokenKind::NotEq, "!=");
            if (match('.')) return make(TokenKind::BangDot, "!.");
            return make(TokenKind::Bang, "!");

        // --- hash / length operator ---
        case '#':
            return make(TokenKind::Hash, "#");

        // --- question / safe-call ---
        case '?':
            if (match('.')) return make(TokenKind::QDot, "?.");
            return make(TokenKind::Question, "?");

        // --- comparison ---
        case '<':
            if (match('=')) return make(TokenKind::LtEq, "<=");
            return make(TokenKind::Lt, "<");
        case '>':
            if (match('=')) return make(TokenKind::GtEq, ">=");
            return make(TokenKind::Gt, ">");

        // --- logical ---
        case '&':
            if (match('&')) return make(TokenKind::And, "&&");
            return error_tok(std::string("unexpected character: ") + c);
        case '|':
            if (match('|')) return make(TokenKind::Or, "||");
            return error_tok(std::string("unexpected character: ") + c);

        // --- string ---
        case '"': return scan_string();

        default:
            if (std::isdigit(c)) {
                --pos_; // put back, scan_number will re-consume
                if (c == '\n') { --line_; col_ = 1; } else { --col_; }
                return scan_number();
            }
            if (std::isalpha(c) || c == '_') {
                --pos_;
                if (c == '\n') { --line_; col_ = 1; } else { --col_; }
                return scan_ident_or_kw();
            }
            return error_tok(std::string("unexpected character: ") + c);
    }
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        // Drain any tokens queued by interpolation processing
        while (!pending_.empty()) {
            tokens.push_back(pending_.front());
            pending_.erase(pending_.begin());
        }

        tok_line_   = line_;
        tok_col_    = col_;
        tok_offset_ = (uint32_t)pos_;

        Token t = scan_next();
        tokens.push_back(t);

        if (t.kind == TokenKind::Eof) break;
    }

    // Drain any remaining pending tokens before Eof
    // (shouldn't happen in normal flow, but be safe)
    return tokens;
}

} // namespace zscript
