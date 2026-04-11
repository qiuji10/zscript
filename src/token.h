#pragma once
#include <string>
#include <cstdint>

namespace zscript {

// ---------------------------------------------------------------------------
// Token kinds
// ---------------------------------------------------------------------------
enum class TokenKind : uint16_t {
    // --- Literals ---
    LitInt,       // 42
    LitFloat,     // 3.14
    LitString,    // "hello {name}"  (raw chars; interpolation segments split by lexer)
    LitTrue,      // true
    LitFalse,     // false
    LitNil,       // nil

    // --- Identifiers ---
    Ident,        // foo, Bar, _x

    // --- Keywords ---
    KwLet,        // let
    KwVar,        // var
    KwFn,         // fn
    KwReturn,     // return
    KwIf,         // if
    KwElse,       // else
    KwWhile,      // while
    KwFor,        // for
    KwIn,         // in
    KwClass,      // class
    KwTrait,      // trait
    KwImpl,       // impl
    KwFor2,       // for (second use: impl Trait for Type) — same token, context resolves
    KwSelf,       // self
    KwMut,        // mut
    KwImport,     // import
    KwBreak,      // break
    KwContinue,   // continue
    KwMatch,      // match
    KwThrow,      // throw
    KwTry,        // try
    KwCatch,      // catch
    KwEnum,       // enum
    KwIs,         // is  (type check: obj is ClassName)

    // --- Punctuation / Delimiters ---
    LBrace,       // {
    RBrace,       // }
    LParen,       // (
    RParen,       // )
    LBracket,     // [
    RBracket,     // ]
    Semicolon,    // ;  (optional, future)
    Colon,        // :
    Comma,        // ,
    Dot,          // .
    DotDot,       // ..
    DotDotDot,    // ...  (varargs)
    DotDotLt,     // ..<  (exclusive range)
    Arrow,        // ->
    FatArrow,     // =>
    At,           // @

    // --- Operators: arithmetic ---
    Plus,         // +
    Minus,        // -
    Star,         // *
    Slash,        // /
    Percent,      // %
    StarStar,     // **  (power)

    // --- Operators: nil-coalescing ---
    QuestionQuestion, // ??

    // --- Operators: comparison ---
    Eq,           // ==
    NotEq,        // !=
    Lt,           // <
    LtEq,         // <=
    Gt,           // >
    GtEq,         // >=

    // --- Operators: logical ---
    And,          // &&
    Or,           // ||
    Bang,         // !
    Hash,         // #  (length operator)

    // --- Operators: assignment ---
    Assign,       // =
    PlusAssign,   // +=
    MinusAssign,  // -=
    StarAssign,   // *=
    SlashAssign,  // /=
    PercentAssign,// %=

    // --- Null safety ---
    Question,     // ?        (nullable type marker)
    QDot,         // ?.       (safe call)
    BangDot,      // !.       (force unwrap)

    // --- String interpolation helpers ---
    InterpStart,  // marks start of interpolated segment inside a string: {
    InterpEnd,    // marks end of interpolated segment: }

    // --- Annotations ---
    // @unreal / @unity / @unreal.uclass etc. are lexed as:
    //   At  Ident  (Dot Ident)*
    // No special token needed — parser handles it.

    // --- Specials ---
    Eof,
    Error,        // lexer error — carries message in lexeme
};

const char* token_kind_name(TokenKind k);

// ---------------------------------------------------------------------------
// Source location
// ---------------------------------------------------------------------------
struct SourceLoc {
    uint32_t line   = 1;
    uint32_t column = 1;
    uint32_t offset = 0;  // byte offset from start of source
};

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------
struct Token {
    TokenKind   kind    = TokenKind::Eof;
    std::string lexeme;   // raw source text (or error message for Error tokens)
    SourceLoc   loc;

    bool is(TokenKind k)  const { return kind == k; }
    bool is_keyword()     const { return kind >= TokenKind::KwLet && kind <= TokenKind::KwIs; }
    bool is_literal()     const { return kind >= TokenKind::LitInt && kind <= TokenKind::LitNil; }
};

} // namespace zscript
