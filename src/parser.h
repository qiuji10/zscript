#pragma once
#include "ast.h"
#include "token.h"
#include <string>
#include <vector>

namespace zscript {

struct ParseError {
    SourceLoc   loc;
    std::string message;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, std::string filename = "<input>");

    Program parse();

    const std::vector<ParseError>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }

private:
    // ---- token navigation ----
    const Token& peek(int ahead = 0) const;
    const Token& advance();
    bool check(TokenKind k) const;
    bool check_ident(const char* name) const;
    bool match(TokenKind k);
    const Token& expect(TokenKind k, const char* msg);

    // ---- error handling / recovery ----
    ParseError make_error(const char* msg);
    void       record_error(const char* msg);
    // Skip tokens until a statement/declaration boundary is found.
    void       synchronize();

    // ---- source location helpers ----
    SourceLoc cur_loc() const;

    // ---- top-level ----
    std::vector<Annotation> parse_annotations();
    DeclPtr parse_decl(std::vector<Annotation> annots);

    // ---- declarations ----
    DeclPtr parse_fn_decl(std::vector<Annotation> annots);
    DeclPtr parse_class_decl(std::vector<Annotation> annots);
    DeclPtr parse_trait_decl(std::vector<Annotation> annots);
    DeclPtr parse_impl_decl(std::vector<Annotation> annots);
    DeclPtr parse_field_or_var_decl(std::vector<Annotation> annots, bool allow_init);
    DeclPtr parse_prop_decl(std::vector<Annotation> annots);
    DeclPtr parse_import_decl(std::vector<Annotation> annots);
    DeclPtr parse_enum_decl(std::vector<Annotation> annots);

    // Shared helpers
    std::vector<Param>       parse_param_list();
    std::vector<std::string> parse_type_params();   // <T, U>
    Block                    parse_block();

    // ---- statements ----
    StmtPtr parse_stmt();
    StmtPtr parse_var_decl_stmt();
    StmtPtr parse_destructure_stmt(bool is_let, bool is_global);
    StmtPtr parse_return_stmt();
    StmtPtr parse_if_stmt();
    StmtPtr parse_while_stmt();
    StmtPtr parse_for_stmt();
    StmtPtr parse_engine_block_stmt();
    StmtPtr parse_match_stmt();
    StmtPtr parse_throw_stmt();
    StmtPtr parse_try_catch_stmt();

    // ---- expressions ----
    ExprPtr parse_expr();
    ExprPtr parse_assign();
    ExprPtr parse_nil_coalesce();
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_equality();
    ExprPtr parse_comparison();
    ExprPtr parse_range();
    ExprPtr parse_addition();
    ExprPtr parse_multiplication();
    ExprPtr parse_power();
    ExprPtr parse_unary();
    ExprPtr parse_postfix();
    ExprPtr parse_primary();
    ExprPtr parse_lambda();
    ExprPtr finish_string_interp(std::string first_text, SourceLoc loc);

    // ---- types ----
    TypePtr parse_type();
    // Disambiguates expr<Type>(args) from expr < val
    bool is_generic_call_lookahead() const;
    std::vector<TypePtr> parse_type_arg_list(); // consumes < types >

    // ---- state ----
    std::vector<Token>      tokens_;
    size_t                  pos_ = 0;
    std::string             filename_;
    std::vector<ParseError> errors_;
};

} // namespace zscript
