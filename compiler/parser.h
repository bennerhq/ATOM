#ifndef ATOM_PARSER_H
#define ATOM_PARSER_H

#include "ast.h"
#include "lexer.h"

#include <unordered_set>
#include <vector>

namespace atom {

class Parser {
public:
    explicit Parser(const std::vector<Token> &tokens);
    Program parse_program();

private:
    const std::vector<Token> &tokens;
    size_t pos = 0;
    std::unordered_set<std::string> type_names;

    const Token &peek() const;
    const Token &previous() const;
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token &expect(TokenType type, const std::string &message);

    void consume_newlines();
    bool is_type_name(const std::string &name) const;

    ImportStmt parse_import();
    StructDef parse_struct();
    FunctionDef parse_function();
    FieldDecl parse_field();
    MethodDef parse_method();

    TypePtr parse_type();
    Param parse_param();

    std::vector<std::unique_ptr<Statement>> parse_block();
    std::unique_ptr<Statement> parse_statement();
    std::unique_ptr<Statement> parse_if();
    std::unique_ptr<Statement> parse_while();
    std::unique_ptr<Statement> parse_return();
    std::unique_ptr<Statement> parse_var_decl();

    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_logical_or();
    std::unique_ptr<Expr> parse_logical_and();
    std::unique_ptr<Expr> parse_equality();
    std::unique_ptr<Expr> parse_comparison();
    std::unique_ptr<Expr> parse_additive();
    std::unique_ptr<Expr> parse_multiplicative();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> parse_primary();
};

} // namespace atom

#endif
