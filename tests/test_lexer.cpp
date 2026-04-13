#include <catch2/catch_test_macros.hpp>
#include "lexer.h"

using namespace zscript;

static std::vector<Token> lex(const std::string& src) {
    Lexer l(src);
    return l.tokenize();
}
static TokenKind kind(const std::vector<Token>& ts, size_t i) {
    return ts[i].kind;
}

// ---------------------------------------------------------------------------
// Keywords
// ---------------------------------------------------------------------------

TEST_CASE("keywords are lexed correctly", "[lexer][keywords]") {
    auto ts = lex("let var fn return if else while for in class trait impl self mut import");
    REQUIRE(kind(ts, 0)  == TokenKind::KwLet);
    REQUIRE(kind(ts, 1)  == TokenKind::KwVar);
    REQUIRE(kind(ts, 2)  == TokenKind::KwFn);
    REQUIRE(kind(ts, 3)  == TokenKind::KwReturn);
    REQUIRE(kind(ts, 4)  == TokenKind::KwIf);
    REQUIRE(kind(ts, 5)  == TokenKind::KwElse);
    REQUIRE(kind(ts, 6)  == TokenKind::KwWhile);
    REQUIRE(kind(ts, 7)  == TokenKind::KwFor);
    REQUIRE(kind(ts, 8)  == TokenKind::KwIn);
    REQUIRE(kind(ts, 9)  == TokenKind::KwClass);
    REQUIRE(kind(ts, 10) == TokenKind::KwTrait);
    REQUIRE(kind(ts, 11) == TokenKind::KwImpl);
    REQUIRE(kind(ts, 12) == TokenKind::KwSelf);
    REQUIRE(kind(ts, 13) == TokenKind::KwMut);
    REQUIRE(kind(ts, 14) == TokenKind::KwImport);
}

TEST_CASE("keyword prefix is not a keyword", "[lexer][keywords]") {
    auto ts = lex("letting variable");
    CHECK(kind(ts, 0) == TokenKind::Ident);
    CHECK(ts[0].lexeme == "letting");
    CHECK(kind(ts, 1) == TokenKind::Ident);
}

TEST_CASE("logical operator keywords", "[lexer][keywords]") {
    auto ts = lex("and or");
    CHECK(kind(ts, 0) == TokenKind::And);
    CHECK(kind(ts, 1) == TokenKind::Or);
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

TEST_CASE("basic literals", "[lexer][literals]") {
    auto ts = lex("42 3.14 true false nil");
    CHECK(kind(ts, 0) == TokenKind::LitInt);
    CHECK(ts[0].lexeme == "42");
    CHECK(kind(ts, 1) == TokenKind::LitFloat);
    CHECK(ts[1].lexeme == "3.14");
    CHECK(kind(ts, 2) == TokenKind::LitTrue);
    CHECK(kind(ts, 3) == TokenKind::LitFalse);
    CHECK(kind(ts, 4) == TokenKind::LitNil);
}

TEST_CASE("integer zero", "[lexer][literals]") {
    auto ts = lex("0");
    CHECK(kind(ts, 0) == TokenKind::LitInt);
    CHECK(ts[0].lexeme == "0");
}

TEST_CASE("large integer", "[lexer][literals]") {
    auto ts = lex("9999999999");
    CHECK(kind(ts, 0) == TokenKind::LitInt);
    CHECK(ts[0].lexeme == "9999999999");
}

TEST_CASE("float literals", "[lexer][literals]") {
    // Lexer requires digit after '.': "2." is LitInt + Dot
    auto ts = lex("1.0 0.5");
    CHECK(kind(ts, 0) == TokenKind::LitFloat);
    CHECK(kind(ts, 1) == TokenKind::LitFloat);
}

TEST_CASE("trailing dot is not a float", "[lexer][literals]") {
    auto ts = lex("2.");
    CHECK(kind(ts, 0) == TokenKind::LitInt);
    CHECK(kind(ts, 1) == TokenKind::Dot);
}

TEST_CASE("numeric separators", "[lexer][literals]") {
    auto ts = lex("1_000_000 3.14_15");
    CHECK(kind(ts, 0) == TokenKind::LitInt);
    CHECK(ts[0].lexeme == "1000000");
    CHECK(kind(ts, 1) == TokenKind::LitFloat);
    CHECK(ts[1].lexeme == "3.1415");
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

TEST_CASE("all operators", "[lexer][operators]") {
    auto ts = lex("+ - * / % == != < <= > >= && || ! = += -= -> => ?.  !. ..");
    CHECK(kind(ts, 0)  == TokenKind::Plus);
    CHECK(kind(ts, 1)  == TokenKind::Minus);
    CHECK(kind(ts, 2)  == TokenKind::Star);
    CHECK(kind(ts, 3)  == TokenKind::Slash);
    CHECK(kind(ts, 4)  == TokenKind::Percent);
    CHECK(kind(ts, 5)  == TokenKind::Eq);
    CHECK(kind(ts, 6)  == TokenKind::NotEq);
    CHECK(kind(ts, 7)  == TokenKind::Lt);
    CHECK(kind(ts, 8)  == TokenKind::LtEq);
    CHECK(kind(ts, 9)  == TokenKind::Gt);
    CHECK(kind(ts, 10) == TokenKind::GtEq);
    CHECK(kind(ts, 11) == TokenKind::And);
    CHECK(kind(ts, 12) == TokenKind::Or);
    CHECK(kind(ts, 13) == TokenKind::Bang);
    CHECK(kind(ts, 14) == TokenKind::Assign);
    CHECK(kind(ts, 15) == TokenKind::PlusAssign);
    CHECK(kind(ts, 16) == TokenKind::MinusAssign);
    CHECK(kind(ts, 17) == TokenKind::Arrow);
    CHECK(kind(ts, 18) == TokenKind::FatArrow);
    CHECK(kind(ts, 19) == TokenKind::QDot);
    CHECK(kind(ts, 20) == TokenKind::BangDot);
    CHECK(kind(ts, 21) == TokenKind::DotDot);
}

TEST_CASE("exclusive range operator ..<", "[lexer][operators]") {
    auto ts = lex("0..<10");
    CHECK(kind(ts, 0) == TokenKind::LitInt);
    CHECK(kind(ts, 1) == TokenKind::DotDotLt);
    CHECK(kind(ts, 2) == TokenKind::LitInt);
}

TEST_CASE("inclusive range operator ..", "[lexer][operators]") {
    auto ts = lex("1..5");
    CHECK(kind(ts, 0) == TokenKind::LitInt);
    CHECK(kind(ts, 1) == TokenKind::DotDot);
    CHECK(kind(ts, 2) == TokenKind::LitInt);
}

TEST_CASE("punctuation tokens", "[lexer][operators]") {
    auto ts = lex("{ } ( ) [ ] : , .");
    CHECK(kind(ts, 0) == TokenKind::LBrace);
    CHECK(kind(ts, 1) == TokenKind::RBrace);
    CHECK(kind(ts, 2) == TokenKind::LParen);
    CHECK(kind(ts, 3) == TokenKind::RParen);
    CHECK(kind(ts, 4) == TokenKind::LBracket);
    CHECK(kind(ts, 5) == TokenKind::RBracket);
    CHECK(kind(ts, 6) == TokenKind::Colon);
    CHECK(kind(ts, 7) == TokenKind::Comma);
    CHECK(kind(ts, 8) == TokenKind::Dot);
}

TEST_CASE("arrow token vs minus+gt", "[lexer][operators]") {
    // "->" is Arrow; "- >" is Minus then Gt
    auto ts = lex("a->b");
    CHECK(kind(ts, 0) == TokenKind::Ident);
    CHECK(kind(ts, 1) == TokenKind::Arrow);
    CHECK(kind(ts, 2) == TokenKind::Ident);

    auto ts2 = lex("a - > b");
    CHECK(kind(ts2, 1) == TokenKind::Minus);
    CHECK(kind(ts2, 2) == TokenKind::Gt);
}

// ---------------------------------------------------------------------------
// Null safety
// ---------------------------------------------------------------------------

TEST_CASE("null safety operators", "[lexer][null-safety]") {
    auto ts = lex("b?.Destroy()");
    CHECK(kind(ts, 0) == TokenKind::Ident);
    CHECK(kind(ts, 1) == TokenKind::QDot);
    CHECK(kind(ts, 2) == TokenKind::Ident);

    auto ts2 = lex("c!.Destroy()");
    CHECK(kind(ts2, 0) == TokenKind::Ident);
    CHECK(kind(ts2, 1) == TokenKind::BangDot);
}

TEST_CASE("question mark in type position", "[lexer][null-safety]") {
    auto ts = lex("Actor?");
    CHECK(kind(ts, 0) == TokenKind::Ident);
    CHECK(kind(ts, 1) == TokenKind::Question);
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

TEST_CASE("plain string literal", "[lexer][strings]") {
    auto ts = lex("\"hello world\"");
    CHECK(kind(ts, 0) == TokenKind::LitString);
    CHECK(ts[0].lexeme == "hello world");
}

TEST_CASE("empty string", "[lexer][strings]") {
    auto ts = lex("\"\"");
    CHECK(kind(ts, 0) == TokenKind::LitString);
    CHECK(ts[0].lexeme.empty());
}

TEST_CASE("string escape sequences", "[lexer][strings]") {
    SECTION("newline") {
        auto ts = lex("\"line1\\nline2\"");
        CHECK(ts[0].lexeme == "line1\nline2");
    }
    SECTION("tab") {
        auto ts = lex("\"col1\\tcol2\"");
        CHECK(ts[0].lexeme == "col1\tcol2");
    }
    SECTION("quote") {
        auto ts = lex("\"say \\\"hi\\\"\"");
        CHECK(ts[0].lexeme == "say \"hi\"");
    }
    SECTION("backslash") {
        auto ts = lex("\"a\\\\b\"");
        CHECK(ts[0].lexeme == "a\\b");
    }
}

TEST_CASE("simple string interpolation", "[lexer][strings][interp]") {
    // "hello {name}" → LitString InterpStart Ident InterpEnd LitString
    auto ts = lex("\"hello {name}\"");
    REQUIRE(ts.size() >= 5);
    CHECK(kind(ts, 0) == TokenKind::LitString);
    CHECK(ts[0].lexeme == "hello ");
    CHECK(kind(ts, 1) == TokenKind::InterpStart);
    CHECK(kind(ts, 2) == TokenKind::Ident);
    CHECK(ts[2].lexeme == "name");
    CHECK(kind(ts, 3) == TokenKind::InterpEnd);
    CHECK(kind(ts, 4) == TokenKind::LitString);
    CHECK(ts[4].lexeme == "");
}

TEST_CASE("interpolation with expression", "[lexer][strings][interp]") {
    // "x={a+b}!" → LitString InterpStart Ident Plus Ident InterpEnd LitString
    auto ts = lex("\"x={a+b}!\"");
    CHECK(kind(ts, 0) == TokenKind::LitString);
    CHECK(kind(ts, 1) == TokenKind::InterpStart);
    CHECK(kind(ts, 2) == TokenKind::Ident);
    CHECK(kind(ts, 3) == TokenKind::Plus);
    CHECK(kind(ts, 4) == TokenKind::Ident);
    CHECK(kind(ts, 5) == TokenKind::InterpEnd);
    CHECK(kind(ts, 6) == TokenKind::LitString);
    CHECK(ts[6].lexeme == "!");
}

TEST_CASE("multiple interpolations", "[lexer][strings][interp]") {
    auto ts = lex("\"{a} and {b}\"");
    CHECK(ts[0].lexeme.empty());
    CHECK(kind(ts, 1) == TokenKind::InterpStart);
    CHECK(kind(ts, 2) == TokenKind::Ident);
    CHECK(kind(ts, 3) == TokenKind::InterpEnd);
    CHECK(kind(ts, 4) == TokenKind::LitString);
    CHECK(ts[4].lexeme == " and ");
    CHECK(kind(ts, 5) == TokenKind::InterpStart);
    CHECK(kind(ts, 6) == TokenKind::Ident);
    CHECK(kind(ts, 7) == TokenKind::InterpEnd);
}

TEST_CASE("interpolation with method call", "[lexer][strings][interp]") {
    auto ts = lex("\"{x.toStr()}\"");
    CHECK(kind(ts, 1) == TokenKind::InterpStart);
    CHECK(kind(ts, 2) == TokenKind::Ident);
    CHECK(kind(ts, 3) == TokenKind::Dot);
    CHECK(kind(ts, 4) == TokenKind::Ident);
    CHECK(kind(ts, 5) == TokenKind::LParen);
    CHECK(kind(ts, 6) == TokenKind::RParen);
    CHECK(kind(ts, 7) == TokenKind::InterpEnd);
}

TEST_CASE("interpolation with field access", "[lexer][strings][interp]") {
    auto ts = lex("\"pos={pos.x}\"");
    CHECK(ts[0].lexeme == "pos=");
    CHECK(kind(ts, 1) == TokenKind::InterpStart);
    CHECK(kind(ts, 3) == TokenKind::Dot);
    CHECK(kind(ts, 4) == TokenKind::Ident);
    CHECK(ts[4].lexeme == "x");
    CHECK(kind(ts, 5) == TokenKind::InterpEnd);
}

TEST_CASE("double-slash inside string is not a comment", "[lexer][strings]") {
    auto ts = lex("\"not // a comment\"");
    CHECK(kind(ts, 0) == TokenKind::LitString);
    CHECK(ts[0].lexeme == "not // a comment");
}

// ---------------------------------------------------------------------------
// Annotations
// ---------------------------------------------------------------------------

TEST_CASE("simple annotation", "[lexer][annotations]") {
    auto ts = lex("@unity");
    CHECK(kind(ts, 0) == TokenKind::At);
    CHECK(kind(ts, 1) == TokenKind::Ident);
    CHECK(ts[1].lexeme == "unity");
}

TEST_CASE("dotted annotation", "[lexer][annotations]") {
    auto ts = lex("@unreal.uclass");
    CHECK(kind(ts, 0) == TokenKind::At);
    CHECK(kind(ts, 1) == TokenKind::Ident);
    CHECK(kind(ts, 2) == TokenKind::Dot);
    CHECK(kind(ts, 3) == TokenKind::Ident);
    CHECK(ts[3].lexeme == "uclass");
}

TEST_CASE("annotation with args", "[lexer][annotations]") {
    auto ts = lex("@unreal.uproperty(EditAnywhere)");
    CHECK(kind(ts, 0) == TokenKind::At);
    CHECK(kind(ts, 4) == TokenKind::LParen);
    CHECK(kind(ts, 5) == TokenKind::Ident);
    CHECK(ts[5].lexeme == "EditAnywhere");
    CHECK(kind(ts, 6) == TokenKind::RParen);
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------

TEST_CASE("line comment skipped", "[lexer][comments]") {
    auto ts = lex("let x = 1 // comment\nvar y = 2");
    CHECK(kind(ts, 0) == TokenKind::KwLet);
    CHECK(kind(ts, 4) == TokenKind::KwVar);
}

TEST_CASE("block comment skipped", "[lexer][comments]") {
    auto ts = lex("let /* block */ x = 1");
    CHECK(kind(ts, 0) == TokenKind::KwLet);
    CHECK(kind(ts, 1) == TokenKind::Ident);
    CHECK(ts[1].lexeme == "x");
}

TEST_CASE("multiline block comment", "[lexer][comments]") {
    auto ts = lex("let /*\nblock\ncomment\n*/ x = 1");
    CHECK(kind(ts, 0) == TokenKind::KwLet);
    CHECK(kind(ts, 1) == TokenKind::Ident);
    CHECK(ts[1].lexeme == "x");
}

// ---------------------------------------------------------------------------
// Source locations
// ---------------------------------------------------------------------------

TEST_CASE("source locations", "[lexer][locations]") {
    SECTION("basic line tracking") {
        auto ts = lex("let\nx");
        CHECK(ts[0].loc.line == 1);
        CHECK(ts[0].loc.column == 1);
        CHECK(ts[1].loc.line == 2);
        CHECK(ts[1].loc.column == 1);
    }
    SECTION("column tracking") {
        auto ts = lex("let x = 42");
        CHECK(ts[0].loc.column == 1);
        CHECK(ts[1].loc.column == 5);
        CHECK(ts[2].loc.column == 7);
        CHECK(ts[3].loc.column == 9);
    }
    SECTION("line after comment") {
        auto ts = lex("// comment\nlet x");
        CHECK(ts[0].loc.line == 2);
    }
}

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

TEST_CASE("identifiers", "[lexer][idents]") {
    SECTION("basic") {
        auto ts = lex("foo Bar _baz x123");
        CHECK(kind(ts, 0) == TokenKind::Ident);
        CHECK(ts[0].lexeme == "foo");
        CHECK(kind(ts, 1) == TokenKind::Ident);
        CHECK(ts[1].lexeme == "Bar");
        CHECK(kind(ts, 2) == TokenKind::Ident);
        CHECK(ts[2].lexeme == "_baz");
        CHECK(kind(ts, 3) == TokenKind::Ident);
    }
    SECTION("underscore prefix") {
        auto ts = lex("_private __double");
        CHECK(kind(ts, 0) == TokenKind::Ident);
        CHECK(ts[0].lexeme == "_private");
        CHECK(kind(ts, 1) == TokenKind::Ident);
        CHECK(ts[1].lexeme == "__double");
    }
    SECTION("PascalCase") {
        auto ts = lex("PlayerActor BeginPlay Vec3");
        CHECK(kind(ts, 0) == TokenKind::Ident);
        CHECK(kind(ts, 1) == TokenKind::Ident);
        CHECK(kind(ts, 2) == TokenKind::Ident);
    }
}

// ---------------------------------------------------------------------------
// EOF
// ---------------------------------------------------------------------------

TEST_CASE("EOF handling", "[lexer][eof]") {
    SECTION("empty input") {
        auto ts = lex("");
        CHECK(ts.size() == 1);
        CHECK(kind(ts, 0) == TokenKind::Eof);
    }
    SECTION("eof at end of tokens") {
        auto ts = lex("let x");
        CHECK(ts.back().kind == TokenKind::Eof);
    }
    SECTION("whitespace only") {
        auto ts = lex("   \n\t  \n  ");
        CHECK(ts.size() == 1);
        CHECK(kind(ts, 0) == TokenKind::Eof);
    }
}

// ---------------------------------------------------------------------------
// Integration / token sequences
// ---------------------------------------------------------------------------

TEST_CASE("class declaration token sequence", "[lexer][integration]") {
    auto ts = lex("class PlayerActor : Actor {");
    CHECK(kind(ts, 0) == TokenKind::KwClass);
    CHECK(kind(ts, 1) == TokenKind::Ident);
    CHECK(ts[1].lexeme == "PlayerActor");
    CHECK(kind(ts, 2) == TokenKind::Colon);
    CHECK(kind(ts, 3) == TokenKind::Ident);
    CHECK(ts[3].lexeme == "Actor");
    CHECK(kind(ts, 4) == TokenKind::LBrace);
}

TEST_CASE("fn declaration token sequence", "[lexer][integration]") {
    auto ts = lex("fn tick(dt: Float) -> nil { }");
    CHECK(kind(ts, 0) == TokenKind::KwFn);
    CHECK(kind(ts, 1) == TokenKind::Ident);
    CHECK(kind(ts, 2) == TokenKind::LParen);
    CHECK(kind(ts, 3) == TokenKind::Ident);
    CHECK(kind(ts, 4) == TokenKind::Colon);
    CHECK(kind(ts, 5) == TokenKind::Ident);
    CHECK(kind(ts, 6) == TokenKind::RParen);
    CHECK(kind(ts, 7) == TokenKind::Arrow);
    CHECK(kind(ts, 8) == TokenKind::LitNil);
}

TEST_CASE("let declaration token sequence", "[lexer][integration]") {
    auto ts = lex("let speed: Float = 600");
    CHECK(kind(ts, 0) == TokenKind::KwLet);
    CHECK(kind(ts, 1) == TokenKind::Ident);
    CHECK(ts[1].lexeme == "speed");
    CHECK(kind(ts, 2) == TokenKind::Colon);
    CHECK(kind(ts, 3) == TokenKind::Ident);
    CHECK(ts[3].lexeme == "Float");
    CHECK(kind(ts, 4) == TokenKind::Assign);
    CHECK(kind(ts, 5) == TokenKind::LitInt);
}

TEST_CASE("generic call token sequence", "[lexer][integration]") {
    auto ts = lex("GetComponent<Rigidbody>()");
    CHECK(kind(ts, 0) == TokenKind::Ident);
    CHECK(kind(ts, 1) == TokenKind::Lt);
    CHECK(kind(ts, 2) == TokenKind::Ident);
    CHECK(ts[2].lexeme == "Rigidbody");
    CHECK(kind(ts, 3) == TokenKind::Gt);
    CHECK(kind(ts, 4) == TokenKind::LParen);
    CHECK(kind(ts, 5) == TokenKind::RParen);
}

TEST_CASE("engine block token sequence", "[lexer][integration]") {
    // @unity { x = 1 } @unreal { x = 2 }
    // indices: 0  1   2 3 4 5 6 7  8   ...
    auto ts = lex("@unity { x = 1 } @unreal { x = 2 }");
    CHECK(kind(ts, 0) == TokenKind::At);
    CHECK(ts[1].lexeme == "unity");
    CHECK(kind(ts, 2) == TokenKind::LBrace);
    // RBrace at 6, second @ at 7
    CHECK(kind(ts, 6) == TokenKind::RBrace);
    CHECK(kind(ts, 7) == TokenKind::At);
    CHECK(ts[8].lexeme == "unreal");
}

TEST_CASE("for range exclusive token sequence", "[lexer][integration]") {
    auto ts = lex("for let i in 0..<10 { }");
    CHECK(kind(ts, 0) == TokenKind::KwFor);
    CHECK(kind(ts, 1) == TokenKind::KwLet);
    CHECK(kind(ts, 2) == TokenKind::Ident);
    CHECK(kind(ts, 3) == TokenKind::KwIn);
    CHECK(kind(ts, 4) == TokenKind::LitInt);
    CHECK(kind(ts, 5) == TokenKind::DotDotLt);
    CHECK(kind(ts, 6) == TokenKind::LitInt);
}

TEST_CASE("delegate += token sequence", "[lexer][integration]") {
    auto ts = lex("actor.OnHit += handler");
    CHECK(kind(ts, 0) == TokenKind::Ident);
    CHECK(kind(ts, 1) == TokenKind::Dot);
    CHECK(kind(ts, 2) == TokenKind::Ident);
    CHECK(kind(ts, 3) == TokenKind::PlusAssign);
    CHECK(kind(ts, 4) == TokenKind::Ident);
}

TEST_CASE("delegate -= token sequence", "[lexer][integration]") {
    auto ts = lex("actor.OnHit -= handler");
    CHECK(kind(ts, 3) == TokenKind::MinusAssign);
}

TEST_CASE("safe call ?. token sequence", "[lexer][integration]") {
    auto ts = lex("target?.GetActorLocation()");
    CHECK(kind(ts, 0) == TokenKind::Ident);
    CHECK(kind(ts, 1) == TokenKind::QDot);
    CHECK(kind(ts, 2) == TokenKind::Ident);
    CHECK(kind(ts, 3) == TokenKind::LParen);
    CHECK(kind(ts, 4) == TokenKind::RParen);
}

TEST_CASE("force unwrap !. token sequence", "[lexer][integration]") {
    auto ts = lex("target!.Destroy()");
    CHECK(kind(ts, 0) == TokenKind::Ident);
    CHECK(kind(ts, 1) == TokenKind::BangDot);
    CHECK(kind(ts, 2) == TokenKind::Ident);
}

TEST_CASE("impl for token sequence", "[lexer][integration]") {
    auto ts = lex("impl Movable for Transform { }");
    CHECK(kind(ts, 0) == TokenKind::KwImpl);
    CHECK(kind(ts, 1) == TokenKind::Ident);
    CHECK(ts[1].lexeme == "Movable");
    CHECK(kind(ts, 2) == TokenKind::KwFor);
    CHECK(kind(ts, 3) == TokenKind::Ident);
    CHECK(ts[3].lexeme == "Transform");
}

// ---------------------------------------------------------------------------
// Raw string literals (backtick)
// ---------------------------------------------------------------------------

TEST_CASE("backtick raw string emits LitString", "[lexer][raw_string]") {
    auto ts = lex("`hello world`");
    REQUIRE(ts.size() >= 2);
    CHECK(kind(ts, 0) == TokenKind::LitString);
    CHECK(ts[0].lexeme == "hello world");
}

TEST_CASE("backtick raw string keeps backslash escape sequences literal", "[lexer][raw_string]") {
    auto ts = lex("`no\\nescape`");
    REQUIRE(kind(ts, 0) == TokenKind::LitString);
    // The lexeme should contain a literal backslash followed by 'n', not a newline
    CHECK(ts[0].lexeme == "no\\nescape");
}

TEST_CASE("backtick raw string keeps braces literal - no interpolation", "[lexer][raw_string]") {
    auto ts = lex("`value is {x + 1}`");
    REQUIRE(kind(ts, 0) == TokenKind::LitString);
    CHECK(ts[0].lexeme == "value is {x + 1}");
    // Only one string token + Eof — no InterpStart/InterpEnd
    CHECK(kind(ts, 1) == TokenKind::Eof);
}

TEST_CASE("backtick raw string preserves embedded newlines", "[lexer][raw_string]") {
    auto ts = lex("`line one\nline two`");
    REQUIRE(kind(ts, 0) == TokenKind::LitString);
    CHECK(ts[0].lexeme == "line one\nline two");
}

TEST_CASE("unterminated backtick raw string produces error token", "[lexer][raw_string]") {
    Lexer l("`oops");
    auto ts = l.tokenize();
    CHECK(l.has_errors());
}
