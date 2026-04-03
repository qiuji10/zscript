#include "lexer.h"
#include <iostream>
#include <cassert>
#include <vector>

using namespace zscript;

// ---------------------------------------------------------------------------
// Mini test harness
// ---------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;

#define EXPECT(cond, msg) \
    do { \
        if (cond) { ++g_pass; } \
        else { ++g_fail; std::cerr << "FAIL [" << __LINE__ << "]: " << msg << "\n"; } \
    } while(0)

static std::vector<Token> lex(const std::string& src) {
    Lexer l(src);
    return l.tokenize();
}

static TokenKind kind(const std::vector<Token>& ts, size_t i) {
    return ts[i].kind;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_keywords() {
    auto ts = lex("let var fn return if else while for in class trait impl self mut import");
    EXPECT(kind(ts,0)  == TokenKind::KwLet,    "let");
    EXPECT(kind(ts,1)  == TokenKind::KwVar,    "var");
    EXPECT(kind(ts,2)  == TokenKind::KwFn,     "fn");
    EXPECT(kind(ts,3)  == TokenKind::KwReturn, "return");
    EXPECT(kind(ts,4)  == TokenKind::KwIf,     "if");
    EXPECT(kind(ts,5)  == TokenKind::KwElse,   "else");
    EXPECT(kind(ts,6)  == TokenKind::KwWhile,  "while");
    EXPECT(kind(ts,7)  == TokenKind::KwFor,    "for");
    EXPECT(kind(ts,8)  == TokenKind::KwIn,     "in");
    EXPECT(kind(ts,9)  == TokenKind::KwClass,  "class");
    EXPECT(kind(ts,10) == TokenKind::KwTrait,  "trait");
    EXPECT(kind(ts,11) == TokenKind::KwImpl,   "impl");
    EXPECT(kind(ts,12) == TokenKind::KwSelf,   "self");
    EXPECT(kind(ts,13) == TokenKind::KwMut,    "mut");
    EXPECT(kind(ts,14) == TokenKind::KwImport, "import");
}

void test_literals() {
    auto ts = lex("42 3.14 true false nil");
    EXPECT(kind(ts,0) == TokenKind::LitInt,   "int");
    EXPECT(ts[0].lexeme == "42",              "int value");
    EXPECT(kind(ts,1) == TokenKind::LitFloat, "float");
    EXPECT(ts[1].lexeme == "3.14",            "float value");
    EXPECT(kind(ts,2) == TokenKind::LitTrue,  "true");
    EXPECT(kind(ts,3) == TokenKind::LitFalse, "false");
    EXPECT(kind(ts,4) == TokenKind::LitNil,   "nil");
}

void test_number_separators() {
    auto ts = lex("1_000_000 3.14_15");
    EXPECT(kind(ts,0) == TokenKind::LitInt,   "sep int");
    EXPECT(ts[0].lexeme == "1000000",          "sep int value");
    EXPECT(kind(ts,1) == TokenKind::LitFloat, "sep float");
    EXPECT(ts[1].lexeme == "3.1415",           "sep float value");
}

void test_operators() {
    auto ts = lex("+ - * / % == != < <= > >= && || ! = += -= -> => ?.  !. ..");
    EXPECT(kind(ts,0)  == TokenKind::Plus,        "+");
    EXPECT(kind(ts,1)  == TokenKind::Minus,       "-");
    EXPECT(kind(ts,2)  == TokenKind::Star,        "*");
    EXPECT(kind(ts,3)  == TokenKind::Slash,       "/");
    EXPECT(kind(ts,4)  == TokenKind::Percent,     "%");
    EXPECT(kind(ts,5)  == TokenKind::Eq,          "==");
    EXPECT(kind(ts,6)  == TokenKind::NotEq,       "!=");
    EXPECT(kind(ts,7)  == TokenKind::Lt,          "<");
    EXPECT(kind(ts,8)  == TokenKind::LtEq,        "<=");
    EXPECT(kind(ts,9)  == TokenKind::Gt,          ">");
    EXPECT(kind(ts,10) == TokenKind::GtEq,        ">=");
    EXPECT(kind(ts,11) == TokenKind::And,         "&&");
    EXPECT(kind(ts,12) == TokenKind::Or,          "||");
    EXPECT(kind(ts,13) == TokenKind::Bang,        "!");
    EXPECT(kind(ts,14) == TokenKind::Assign,      "=");
    EXPECT(kind(ts,15) == TokenKind::PlusAssign,  "+=");
    EXPECT(kind(ts,16) == TokenKind::MinusAssign, "-=");
    EXPECT(kind(ts,17) == TokenKind::Arrow,       "->");
    EXPECT(kind(ts,18) == TokenKind::FatArrow,    "=>");
    EXPECT(kind(ts,19) == TokenKind::QDot,        "?.");
    EXPECT(kind(ts,20) == TokenKind::BangDot,     "!.");
    EXPECT(kind(ts,21) == TokenKind::DotDot,      "..");
}

void test_range_operator() {
    auto ts = lex("0..<10");
    EXPECT(kind(ts,0) == TokenKind::LitInt,   "range start");
    EXPECT(kind(ts,1) == TokenKind::DotDotLt, "..<");
    EXPECT(kind(ts,2) == TokenKind::LitInt,   "range end");
}

void test_null_safety() {
    auto ts = lex("actor? b?.Destroy() c!.Destroy()");
    EXPECT(kind(ts,0) == TokenKind::Ident,    "actor");
    EXPECT(kind(ts,1) == TokenKind::Question, "?");
    EXPECT(kind(ts,2) == TokenKind::Ident,    "b");
    EXPECT(kind(ts,3) == TokenKind::QDot,     "?.");
    EXPECT(kind(ts,4) == TokenKind::Ident,    "Destroy");
    EXPECT(kind(ts,8) == TokenKind::BangDot,  "!.");
}

void test_plain_string() {
    auto ts = lex("\"hello world\"");
    EXPECT(kind(ts,0) == TokenKind::LitString, "string kind");
    EXPECT(ts[0].lexeme == "hello world",       "string value");
}

void test_string_escape() {
    auto ts = lex("\"line1\\nline2\"");
    EXPECT(kind(ts,0) == TokenKind::LitString, "escape kind");
    EXPECT(ts[0].lexeme == "line1\nline2",      "escape value");
}

void test_string_interp_simple() {
    // "hello {name}" →
    //   LitString("hello "), InterpStart, Ident("name"), InterpEnd, LitString("")
    auto ts = lex("\"hello {name}\"");
    EXPECT(kind(ts,0) == TokenKind::LitString,   "prefix");
    EXPECT(ts[0].lexeme == "hello ",              "prefix value");
    EXPECT(kind(ts,1) == TokenKind::InterpStart,  "interp start");
    EXPECT(kind(ts,2) == TokenKind::Ident,        "interp ident");
    EXPECT(ts[2].lexeme == "name",                "interp ident value");
    EXPECT(kind(ts,3) == TokenKind::InterpEnd,    "interp end");
    EXPECT(kind(ts,4) == TokenKind::LitString,    "suffix");
    EXPECT(ts[4].lexeme == "",                    "suffix empty");
}

void test_string_interp_expr() {
    // "x={a+b}!" → LitString("x="), InterpStart, Ident, Plus, Ident, InterpEnd, LitString("!")
    auto ts = lex("\"x={a+b}!\"");
    EXPECT(kind(ts,0) == TokenKind::LitString,   "x=");
    EXPECT(kind(ts,1) == TokenKind::InterpStart, "start");
    EXPECT(kind(ts,2) == TokenKind::Ident,       "a");
    EXPECT(kind(ts,3) == TokenKind::Plus,        "+");
    EXPECT(kind(ts,4) == TokenKind::Ident,       "b");
    EXPECT(kind(ts,5) == TokenKind::InterpEnd,   "end");
    EXPECT(kind(ts,6) == TokenKind::LitString,   "!");
    EXPECT(ts[6].lexeme == "!",                  "! value");
}

void test_annotation() {
    // @unreal.uclass → At Ident Dot Ident
    auto ts = lex("@unreal.uclass");
    EXPECT(kind(ts,0) == TokenKind::At,    "@");
    EXPECT(kind(ts,1) == TokenKind::Ident, "unreal");
    EXPECT(kind(ts,2) == TokenKind::Dot,   ".");
    EXPECT(kind(ts,3) == TokenKind::Ident, "uclass");
}

void test_line_comment() {
    auto ts = lex("let x = 1 // this is a comment\nvar y = 2");
    EXPECT(kind(ts,0) == TokenKind::KwLet, "let");
    EXPECT(kind(ts,4) == TokenKind::KwVar, "var after comment");
}

void test_block_comment() {
    auto ts = lex("let /* block */ x = 1");
    EXPECT(kind(ts,0) == TokenKind::KwLet,  "let");
    EXPECT(kind(ts,1) == TokenKind::Ident,  "x");
    EXPECT(ts[1].lexeme == "x",             "x value");
}

void test_source_location() {
    auto ts = lex("let\nx");
    EXPECT(ts[0].loc.line == 1, "let line 1");
    EXPECT(ts[0].loc.column == 1, "let col 1");
    EXPECT(ts[1].loc.line == 2, "x line 2");
    EXPECT(ts[1].loc.column == 1, "x col 1");
}

void test_eof() {
    auto ts = lex("");
    EXPECT(ts.size() == 1,               "only eof");
    EXPECT(kind(ts,0) == TokenKind::Eof, "eof kind");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    test_keywords();
    test_literals();
    test_number_separators();
    test_operators();
    test_range_operator();
    test_null_safety();
    test_plain_string();
    test_string_escape();
    test_string_interp_simple();
    test_string_interp_expr();
    test_annotation();
    test_line_comment();
    test_block_comment();
    test_source_location();
    test_eof();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail > 0 ? 1 : 0;
}
