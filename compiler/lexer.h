#ifndef ATOM_LEXER_H
#define ATOM_LEXER_H

#include <string>
#include <vector>

namespace atom {

enum class TokenType {
    End,
    Newline,
    Indent,
    Dedent,
    Identifier,
    Integer,
    Real,
    String,
    Char,

    // Keywords
    KwImport,
    KwAs,
    KwFrom,
    KwIf,
    KwElse,
    KwWhile,
    KwReturn,
    KwTrue,
    KwFalse,
    KwNull,
    KwThis,
    KwInner,

    // Symbols
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Dot,
    Colon,
    Assign,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    EqEq,
    NotEq,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    AndAnd,
    OrOr,
    Bang
};

struct Token {
    TokenType type;
    std::string text;
    int line;
    int col;
};

class Lexer {
public:
    explicit Lexer(const std::string &source);
    std::vector<Token> tokenize();

private:
    const std::string &src;
    size_t pos = 0;
    int line = 1;
    int col = 1;
    std::vector<int> indent_stack;
    bool at_line_start = true;

    char peek() const;
    char get();
    bool eof() const;

    void skip_whitespace();
    void skip_comment();

    Token make(TokenType type, const std::string &text, int line, int col) const;
    Token lex_number();
    Token lex_identifier_or_keyword();
    Token lex_string();
    Token lex_char();
};

} // namespace atom

#endif
