// -----------------------------------------------------------------------------
// SPDX-License-Identifier: OBL-1.0
// Open Source Beer License (with Extra Bubbles)
//
// Licensor: Atom Compiler Contributors
// Human LLM Controller: jens@bennerhq.com
//
// If we meet some day and you think this code is worth it, you can buy
// the authors a beer (or two). If you see benner, make it a cold one.
//
// If you pour beer on your computer, the compiler will not run faster.
// If you pour beer on the authors, results may vary.
// -----------------------------------------------------------------------------
#include "lexer.h"

#include <cctype>
#include <stdexcept>

namespace atom {

Lexer::Lexer(const std::string &source) : src(source) {
    indent_stack.push_back(0);
}

char Lexer::peek() const {
    if (pos >= src.size()) {
        return '\0';
    }
    return src[pos];
}

char Lexer::get() {
    if (pos >= src.size()) {
        return '\0';
    }
    char c = src[pos++];
    if (c == '\n') {
        line++;
        col = 1;
        at_line_start = true;
    } else {
        col++;
    }
    return c;
}

bool Lexer::eof() const {
    return pos >= src.size();
}

Token Lexer::make(TokenType type, const std::string &text, int l, int c) const {
    return Token{type, text, l, c};
}

void Lexer::skip_whitespace() {
    while (!eof()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            get();
        } else {
            break;
        }
    }
}

void Lexer::skip_comment() {
    while (!eof() && peek() != '\n') {
        get();
    }
}

Token Lexer::lex_number() {
    int start_line = line;
    int start_col = col;
    std::string text;
    bool is_real = false;

    while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
        text.push_back(get());
    }
    if (!eof() && peek() == '.') {
        is_real = true;
        text.push_back(get());
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text.push_back(get());
        }
    }

    return make(is_real ? TokenType::Real : TokenType::Integer, text, start_line, start_col);
}

Token Lexer::lex_identifier_or_keyword() {
    int start_line = line;
    int start_col = col;
    std::string text;
    while (!eof()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            text.push_back(get());
        } else {
            break;
        }
    }

    if (text == "import") return make(TokenType::KwImport, text, start_line, start_col);
    if (text == "as") return make(TokenType::KwAs, text, start_line, start_col);
    if (text == "from") return make(TokenType::KwFrom, text, start_line, start_col);
    if (text == "if") return make(TokenType::KwIf, text, start_line, start_col);
    if (text == "else") return make(TokenType::KwElse, text, start_line, start_col);
    if (text == "while") return make(TokenType::KwWhile, text, start_line, start_col);
    if (text == "return") return make(TokenType::KwReturn, text, start_line, start_col);
    if (text == "true") return make(TokenType::KwTrue, text, start_line, start_col);
    if (text == "false") return make(TokenType::KwFalse, text, start_line, start_col);
    if (text == "null") return make(TokenType::KwNull, text, start_line, start_col);
    if (text == "this") return make(TokenType::KwThis, text, start_line, start_col);
    if (text == "inner") return make(TokenType::KwInner, text, start_line, start_col);

    return make(TokenType::Identifier, text, start_line, start_col);
}

Token Lexer::lex_string() {
    int start_line = line;
    int start_col = col;
    std::string text;
    get();
    while (!eof()) {
        char c = get();
        if (c == '\\') {
            if (eof()) break;
            char esc = get();
            switch (esc) {
                case 'n': text.push_back('\n'); break;
                case 't': text.push_back('\t'); break;
                case 'r': text.push_back('\r'); break;
                case '"': text.push_back('"'); break;
                case '\\': text.push_back('\\'); break;
                default: text.push_back(esc); break;
            }
        } else if (c == '"') {
            break;
        } else {
            text.push_back(c);
        }
    }
    return make(TokenType::String, text, start_line, start_col);
}

Token Lexer::lex_char() {
    int start_line = line;
    int start_col = col;
    std::string text;
    get();
    char c = get();
    if (c == '\\') {
        char esc = get();
        switch (esc) {
            case 'n': text.push_back('\n'); break;
            case 't': text.push_back('\t'); break;
            case 'r': text.push_back('\r'); break;
            case '\'': text.push_back('\''); break;
            case '\\': text.push_back('\\'); break;
            case '"': text.push_back('"'); break;
            default: text.push_back(esc); break;
        }
    } else {
        text.push_back(c);
    }
    if (peek() == '\'') {
        get();
    }
    return make(TokenType::Char, text, start_line, start_col);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!eof()) {
        if (at_line_start) {
            int start_line = line;
            int start_col = col;
            int indent = 0;
            size_t save_pos = pos;
            while (!eof()) {
                char c = peek();
                if (c == ' ') {
                    indent += 1;
                    get();
                } else if (c == '\t') {
                    indent += 4;
                    get();
                } else {
                    break;
                }
            }

            if (peek() == '\n') {
                get();
                tokens.push_back(make(TokenType::Newline, "", start_line, start_col));
                continue;
            }

            int current = indent_stack.back();
            if (indent > current) {
                indent_stack.push_back(indent);
                tokens.push_back(make(TokenType::Indent, "", start_line, start_col));
            } else if (indent < current) {
                while (!indent_stack.empty() && indent < indent_stack.back()) {
                    indent_stack.pop_back();
                    tokens.push_back(make(TokenType::Dedent, "", start_line, start_col));
                }
                if (indent_stack.empty() || indent_stack.back() != indent) {
                    throw std::runtime_error("Invalid indentation");
                }
            }

            at_line_start = false;
            if (pos != save_pos) {
                start_col = col;
            }
        }

        char c = peek();
        if (c == '\0') break;
        if (c == '\n') {
            get();
            tokens.push_back(make(TokenType::Newline, "", line - 1, 1));
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            skip_whitespace();
            continue;
        }
        if (c == '#') {
            skip_comment();
            continue;
        }
        if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/') {
            skip_comment();
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(lex_number());
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(lex_identifier_or_keyword());
            continue;
        }
        if (c == '"') {
            tokens.push_back(lex_string());
            continue;
        }
        if (c == '\'') {
            tokens.push_back(lex_char());
            continue;
        }

        int start_line = line;
        int start_col = col;
        switch (c) {
            case '(':
                get(); tokens.push_back(make(TokenType::LParen, "(", start_line, start_col)); break;
            case ')':
                get(); tokens.push_back(make(TokenType::RParen, ")", start_line, start_col)); break;
            case '[':
                get(); tokens.push_back(make(TokenType::LBracket, "[", start_line, start_col)); break;
            case ']':
                get(); tokens.push_back(make(TokenType::RBracket, "]", start_line, start_col)); break;
            case ',':
                get(); tokens.push_back(make(TokenType::Comma, ",", start_line, start_col)); break;
            case '.':
                get(); tokens.push_back(make(TokenType::Dot, ".", start_line, start_col)); break;
            case ':':
                get(); tokens.push_back(make(TokenType::Colon, ":", start_line, start_col)); break;
            case '+':
                get(); tokens.push_back(make(TokenType::Plus, "+", start_line, start_col)); break;
            case '-':
                get(); tokens.push_back(make(TokenType::Minus, "-", start_line, start_col)); break;
            case '*':
                get(); tokens.push_back(make(TokenType::Star, "*", start_line, start_col)); break;
            case '%':
                get(); tokens.push_back(make(TokenType::Percent, "%", start_line, start_col)); break;
            case '!':
                get();
                if (peek() == '=') {
                    get();
                    tokens.push_back(make(TokenType::NotEq, "!=", start_line, start_col));
                } else {
                    tokens.push_back(make(TokenType::Bang, "!", start_line, start_col));
                }
                break;
            case '=':
                get();
                if (peek() == '=') {
                    get();
                    tokens.push_back(make(TokenType::EqEq, "==", start_line, start_col));
                } else {
                    tokens.push_back(make(TokenType::Assign, "=", start_line, start_col));
                }
                break;
            case '<':
                get();
                if (peek() == '=') {
                    get();
                    tokens.push_back(make(TokenType::LessEq, "<=", start_line, start_col));
                } else {
                    tokens.push_back(make(TokenType::Less, "<", start_line, start_col));
                }
                break;
            case '>':
                get();
                if (peek() == '=') {
                    get();
                    tokens.push_back(make(TokenType::GreaterEq, ">=", start_line, start_col));
                } else {
                    tokens.push_back(make(TokenType::Greater, ">", start_line, start_col));
                }
                break;
            case '&':
                get();
                if (peek() == '&') {
                    get();
                    tokens.push_back(make(TokenType::AndAnd, "&&", start_line, start_col));
                } else {
                    throw std::runtime_error("Unexpected '&'");
                }
                break;
            case '|':
                get();
                if (peek() == '|') {
                    get();
                    tokens.push_back(make(TokenType::OrOr, "||", start_line, start_col));
                } else {
                    throw std::runtime_error("Unexpected '|'");
                }
                break;
            case '/':
                get(); tokens.push_back(make(TokenType::Slash, "/", start_line, start_col)); break;
            default:
                throw std::runtime_error("Unexpected character");
        }
    }

    while (indent_stack.size() > 1) {
        indent_stack.pop_back();
        tokens.push_back(make(TokenType::Dedent, "", line, col));
    }
    tokens.push_back(make(TokenType::End, "", line, col));
    return tokens;
}

} // namespace atom
