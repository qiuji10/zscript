#include "token.h"

namespace zscript {

const char* token_kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::LitInt:       return "LitInt";
        case TokenKind::LitFloat:     return "LitFloat";
        case TokenKind::LitString:    return "LitString";
        case TokenKind::LitTrue:      return "true";
        case TokenKind::LitFalse:     return "false";
        case TokenKind::LitNil:       return "nil";
        case TokenKind::Ident:        return "Ident";
        case TokenKind::KwLet:        return "let";
        case TokenKind::KwVar:        return "var";
        case TokenKind::KwFn:         return "fn";
        case TokenKind::KwReturn:     return "return";
        case TokenKind::KwIf:         return "if";
        case TokenKind::KwElse:       return "else";
        case TokenKind::KwWhile:      return "while";
        case TokenKind::KwFor:        return "for";
        case TokenKind::KwIn:         return "in";
        case TokenKind::KwClass:      return "class";
        case TokenKind::KwTrait:      return "trait";
        case TokenKind::KwImpl:       return "impl";
        case TokenKind::KwFor2:       return "for";
        case TokenKind::KwSelf:       return "self";
        case TokenKind::KwMut:        return "mut";
        case TokenKind::KwImport:     return "import";
        case TokenKind::Hash:         return "#";
        case TokenKind::KwBreak:      return "break";
        case TokenKind::KwContinue:   return "continue";
        case TokenKind::KwMatch:      return "match";
        case TokenKind::LBrace:       return "{";
        case TokenKind::RBrace:       return "}";
        case TokenKind::LParen:       return "(";
        case TokenKind::RParen:       return ")";
        case TokenKind::LBracket:     return "[";
        case TokenKind::RBracket:     return "]";
        case TokenKind::Semicolon:    return ";";
        case TokenKind::Colon:        return ":";
        case TokenKind::Comma:        return ",";
        case TokenKind::Dot:          return ".";
        case TokenKind::DotDot:       return "..";
        case TokenKind::DotDotLt:     return "..<";
        case TokenKind::Arrow:        return "->";
        case TokenKind::FatArrow:     return "=>";
        case TokenKind::At:           return "@";
        case TokenKind::Plus:         return "+";
        case TokenKind::Minus:        return "-";
        case TokenKind::Star:         return "*";
        case TokenKind::Slash:        return "/";
        case TokenKind::Percent:      return "%";
        case TokenKind::Eq:           return "==";
        case TokenKind::NotEq:        return "!=";
        case TokenKind::Lt:           return "<";
        case TokenKind::LtEq:         return "<=";
        case TokenKind::Gt:           return ">";
        case TokenKind::GtEq:         return ">=";
        case TokenKind::And:          return "&&";
        case TokenKind::Or:           return "||";
        case TokenKind::Bang:         return "!";
        case TokenKind::Assign:       return "=";
        case TokenKind::PlusAssign:    return "+=";
        case TokenKind::MinusAssign:   return "-=";
        case TokenKind::StarAssign:    return "*=";
        case TokenKind::SlashAssign:   return "/=";
        case TokenKind::PercentAssign: return "%=";
        case TokenKind::Question:     return "?";
        case TokenKind::QDot:         return "?.";
        case TokenKind::BangDot:      return "!.";
        case TokenKind::InterpStart:  return "interp{";
        case TokenKind::InterpEnd:    return "interp}";
        case TokenKind::Eof:          return "<eof>";
        case TokenKind::Error:        return "<error>";
    }
    return "<unknown>";
}

} // namespace zscript
