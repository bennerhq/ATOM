#include "parser.h"

#include <cctype>
#include <stdexcept>

namespace atom {

Parser::Parser(const std::vector<Token> &tokens) : tokens(tokens) {
    type_names = {"Int", "Real", "Bool", "Char", "Void", "String", "Array"};
}

const Token &Parser::peek() const {
    return tokens[pos];
}

const Token &Parser::previous() const {
    return tokens[pos - 1];
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        pos++;
        return true;
    }
    return false;
}

const Token &Parser::expect(TokenType type, const std::string &message) {
    if (!check(type)) {
        throw std::runtime_error(message);
    }
    pos++;
    return previous();
}

void Parser::consume_newlines() {
    while (match(TokenType::Newline)) {
    }
}

bool Parser::is_type_name(const std::string &name) const {
    if (type_names.count(name)) return true;
    return !name.empty() && std::isupper(static_cast<unsigned char>(name[0]));
}

Program Parser::parse_program() {
    Program program;
    consume_newlines();
    while (!check(TokenType::End)) {
        if (check(TokenType::KwImport)) {
            program.imports.push_back(parse_import());
        } else if (check(TokenType::Identifier)) {
            size_t save = pos;
            Token name = peek();
            pos++;
            bool is_struct = false;
            if (check(TokenType::Colon)) {
                is_struct = true;
            } else if (check(TokenType::Identifier)) {
                pos++;
                if (check(TokenType::Colon)) {
                    is_struct = true;
                }
            } else if (check(TokenType::LParen)) {
                is_struct = false;
            }
            pos = save;

            if (is_struct) {
                program.structs.push_back(parse_struct());
            } else {
                program.functions.push_back(parse_function());
            }
        } else {
            throw std::runtime_error("Unexpected token at top-level");
        }
        consume_newlines();
    }
    return program;
}

ImportStmt Parser::parse_import() {
    expect(TokenType::KwImport, "Expected 'import'");
    Token name = expect(TokenType::Identifier, "Expected import name");
    ImportStmt stmt;
    stmt.name = name.text;
    if (match(TokenType::KwFrom)) {
        Token path = expect(TokenType::String, "Expected import path string");
        stmt.path = path.text;
    }
    if (match(TokenType::KwAs)) {
        Token alias = expect(TokenType::Identifier, "Expected import alias");
        stmt.alias = alias.text;
    }
    expect(TokenType::Newline, "Expected newline after import");
    return stmt;
}

StructDef Parser::parse_struct() {
    StructDef s;
    Token name = expect(TokenType::Identifier, "Expected struct name");
    type_names.insert(name.text);
    s.name = name.text;
    expect(TokenType::Colon, "Expected ':' after struct name");
    if (check(TokenType::Identifier)) {
        size_t save = pos;
        Token parent = expect(TokenType::Identifier, "Expected parent name");
        if (check(TokenType::Colon)) {
            s.parent = parent.text;
            expect(TokenType::Colon, "Expected ':' after struct header");
        } else {
            pos = save;
        }
    }
    expect(TokenType::Newline, "Expected newline after struct header");
    expect(TokenType::Indent, "Expected indent for struct body");

    while (!check(TokenType::Dedent) && !check(TokenType::End)) {
        if (check(TokenType::Newline)) {
            consume_newlines();
            continue;
        }
        if (check(TokenType::Identifier) && pos + 1 < tokens.size() && tokens[pos + 1].type == TokenType::LParen) {
            s.methods.push_back(parse_method());
            continue;
        }
        size_t save = pos;
        parse_type();
        if (check(TokenType::Identifier)) {
            Token ident = expect(TokenType::Identifier, "Expected identifier");
            if (check(TokenType::LParen)) {
                pos = save;
                s.methods.push_back(parse_method());
            } else {
                pos = save;
                s.fields.push_back(parse_field());
            }
        } else {
            throw std::runtime_error("Expected struct member");
        }
    }
    expect(TokenType::Dedent, "Expected dedent after struct body");
    return s;
}

FunctionDef Parser::parse_function() {
    FunctionDef fn;
    TypePtr type = Type::make(Type::Kind::Void);
    if (check(TokenType::Identifier) && pos + 1 < tokens.size() && tokens[pos + 1].type != TokenType::LParen) {
        type = parse_type();
    }
    fn.return_type = (type->kind == Type::Kind::Unknown) ? Type::make(Type::Kind::Void) : type;
    Token name = expect(TokenType::Identifier, "Expected function name");
    fn.name = name.text;
    expect(TokenType::LParen, "Expected '('");
    if (!check(TokenType::RParen)) {
        fn.params.push_back(parse_param());
        while (match(TokenType::Comma)) {
            fn.params.push_back(parse_param());
        }
    }
    expect(TokenType::RParen, "Expected ')'");
    if (match(TokenType::Assign)) {
        fn.is_inline = true;
        auto expr = parse_expression();
        auto ret = std::make_unique<ReturnStmt>();
        ret->value = std::move(expr);
        fn.body.push_back(std::move(ret));
        expect(TokenType::Newline, "Expected newline after inline function");
        return fn;
    }
    expect(TokenType::Colon, "Expected ':' after function header");
    expect(TokenType::Newline, "Expected newline after function header");
    expect(TokenType::Indent, "Expected indent for function body");
    fn.body = parse_block();
    expect(TokenType::Dedent, "Expected dedent after function body");
    return fn;
}

FieldDecl Parser::parse_field() {
    FieldDecl field;
    field.type = parse_type();
    Token name = expect(TokenType::Identifier, "Expected field name");
    field.name = name.text;
    if (match(TokenType::Assign)) {
        field.init = parse_expression();
    }
    expect(TokenType::Newline, "Expected newline after field");
    return field;
}

MethodDef Parser::parse_method() {
    MethodDef method;
    TypePtr type = Type::make(Type::Kind::Void);
    if (check(TokenType::Identifier) && pos + 1 < tokens.size() && tokens[pos + 1].type != TokenType::LParen) {
        type = parse_type();
    }
    method.return_type = (type->kind == Type::Kind::Unknown) ? Type::make(Type::Kind::Void) : type;
    Token name = expect(TokenType::Identifier, "Expected method name");
    method.name = name.text;
    if (method.name == "init") {
        method.is_constructor = true;
    }
    expect(TokenType::LParen, "Expected '('");
    if (!check(TokenType::RParen)) {
        method.params.push_back(parse_param());
        while (match(TokenType::Comma)) {
            method.params.push_back(parse_param());
        }
    }
    expect(TokenType::RParen, "Expected ')'");
    expect(TokenType::Colon, "Expected ':' after method header");
    expect(TokenType::Newline, "Expected newline after method header");
    expect(TokenType::Indent, "Expected indent for method body");
    method.body = parse_block();
    expect(TokenType::Dedent, "Expected dedent after method body");
    return method;
}

TypePtr Parser::parse_type() {
    if (check(TokenType::Identifier)) {
        Token tok = expect(TokenType::Identifier, "Expected type");
        std::string name = tok.text;
        bool is_virtual = false;
        if (name.rfind("virtual", 0) == 0 && name.size() > 7) {
            std::string base = name.substr(7);
            if (base == "Int" || base == "Real" || base == "Bool" || base == "Char" || base == "Void" || base == "String") {
                name = base;
                is_virtual = true;
            }
        }
        TypePtr type;
        if (name == "Int") type = Type::make(Type::Kind::Int);
        else if (name == "Real") type = Type::make(Type::Kind::Real);
        else if (name == "Bool") type = Type::make(Type::Kind::Bool);
        else if (name == "Char") type = Type::make(Type::Kind::Char);
        else if (name == "Void") type = Type::make(Type::Kind::Void);
        else if (name == "String") type = Type::make(Type::Kind::String);
        else if (name == "Array") {
            if (match(TokenType::LBracket)) {
                TypePtr elem = parse_type();
                expect(TokenType::RBracket, "Expected ']'");
                type = Type::make_array(elem);
            } else {
                type = Type::make_array(Type::make_unknown());
            }
        } else {
            type = Type::make_class(name);
        }
        type->is_virtual = is_virtual;

        while (match(TokenType::LBracket)) {
            expect(TokenType::RBracket, "Expected ']'");
            type = Type::make_array(type);
        }
        return type;
    }

    return Type::make_unknown();
}

Param Parser::parse_param() {
    Param param;
    param.type = parse_type();
    Token name = expect(TokenType::Identifier, "Expected parameter name");
    param.name = name.text;
    return param;
}

std::vector<std::unique_ptr<Statement>> Parser::parse_block() {
    std::vector<std::unique_ptr<Statement>> stmts;
    while (!check(TokenType::Dedent) && !check(TokenType::End)) {
        if (check(TokenType::Newline)) {
            consume_newlines();
            continue;
        }
        stmts.push_back(parse_statement());
    }
    return stmts;
}

std::unique_ptr<Statement> Parser::parse_statement() {
    if (check(TokenType::KwIf)) {
        return parse_if();
    }
    if (check(TokenType::KwWhile)) {
        return parse_while();
    }
    if (check(TokenType::KwReturn)) {
        return parse_return();
    }
    if (check(TokenType::KwInner)) {
        expect(TokenType::KwInner, "Expected inner");
        expect(TokenType::Newline, "Expected newline after inner");
        return std::make_unique<InnerStmt>();
    }

    if (check(TokenType::Identifier) && is_type_name(peek().text)) {
        return parse_var_decl();
    }

    auto expr = parse_expression();
    if (match(TokenType::Assign)) {
        auto assign = std::make_unique<AssignStmt>();
        assign->target = std::move(expr);
        assign->value = parse_expression();
        expect(TokenType::Newline, "Expected newline after assignment");
        return assign;
    }
    auto stmt = std::make_unique<ExprStmt>();
    stmt->expr = std::move(expr);
    expect(TokenType::Newline, "Expected newline after expression");
    return stmt;
}

std::unique_ptr<Statement> Parser::parse_if() {
    expect(TokenType::KwIf, "Expected if");
    expect(TokenType::LParen, "Expected '('");
    auto cond = parse_expression();
    expect(TokenType::RParen, "Expected ')'");
    expect(TokenType::Colon, "Expected ':'");
    expect(TokenType::Newline, "Expected newline after if");
    expect(TokenType::Indent, "Expected indent for if body");
    auto stmt = std::make_unique<IfStmt>();
    stmt->cond = std::move(cond);
    stmt->then_body = parse_block();
    expect(TokenType::Dedent, "Expected dedent after if body");

    if (match(TokenType::KwElse)) {
        expect(TokenType::Colon, "Expected ':' after else");
        expect(TokenType::Newline, "Expected newline after else");
        expect(TokenType::Indent, "Expected indent for else body");
        stmt->else_body = parse_block();
        expect(TokenType::Dedent, "Expected dedent after else body");
    }
    return stmt;
}

std::unique_ptr<Statement> Parser::parse_while() {
    expect(TokenType::KwWhile, "Expected while");
    expect(TokenType::LParen, "Expected '('");
    auto cond = parse_expression();
    expect(TokenType::RParen, "Expected ')'");
    expect(TokenType::Colon, "Expected ':'");
    expect(TokenType::Newline, "Expected newline after while");
    expect(TokenType::Indent, "Expected indent for while body");
    auto stmt = std::make_unique<WhileStmt>();
    stmt->cond = std::move(cond);
    stmt->body = parse_block();
    expect(TokenType::Dedent, "Expected dedent after while body");
    return stmt;
}

std::unique_ptr<Statement> Parser::parse_return() {
    expect(TokenType::KwReturn, "Expected return");
    auto stmt = std::make_unique<ReturnStmt>();
    if (!check(TokenType::Newline)) {
        stmt->value = parse_expression();
    }
    expect(TokenType::Newline, "Expected newline after return");
    return stmt;
}

std::unique_ptr<Statement> Parser::parse_var_decl() {
    auto stmt = std::make_unique<VarDeclStmt>();
    stmt->type = parse_type();
    Token name = expect(TokenType::Identifier, "Expected variable name");
    stmt->name = name.text;
    if (match(TokenType::Assign)) {
        stmt->init = parse_expression();
    }
    expect(TokenType::Newline, "Expected newline after variable declaration");
    return stmt;
}

std::unique_ptr<Expr> Parser::parse_expression() {
    return parse_logical_or();
}

std::unique_ptr<Expr> Parser::parse_logical_or() {
    auto expr = parse_logical_and();
    while (match(TokenType::OrOr)) {
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = "||";
        bin->left = std::move(expr);
        bin->right = parse_logical_and();
        expr = std::move(bin);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_logical_and() {
    auto expr = parse_equality();
    while (match(TokenType::AndAnd)) {
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = "&&";
        bin->left = std::move(expr);
        bin->right = parse_equality();
        expr = std::move(bin);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_equality() {
    auto expr = parse_comparison();
    while (check(TokenType::EqEq) || check(TokenType::NotEq)) {
        auto bin = std::make_unique<BinaryExpr>();
        if (match(TokenType::EqEq)) bin->op = "==";
        else { match(TokenType::NotEq); bin->op = "!="; }
        bin->left = std::move(expr);
        bin->right = parse_comparison();
        expr = std::move(bin);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_comparison() {
    auto expr = parse_additive();
    while (check(TokenType::Less) || check(TokenType::LessEq) || check(TokenType::Greater) || check(TokenType::GreaterEq)) {
        auto bin = std::make_unique<BinaryExpr>();
        if (match(TokenType::Less)) bin->op = "<";
        else if (match(TokenType::LessEq)) bin->op = "<=";
        else if (match(TokenType::Greater)) bin->op = ">";
        else { match(TokenType::GreaterEq); bin->op = ">="; }
        bin->left = std::move(expr);
        bin->right = parse_additive();
        expr = std::move(bin);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_additive() {
    auto expr = parse_multiplicative();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        auto bin = std::make_unique<BinaryExpr>();
        if (match(TokenType::Plus)) bin->op = "+";
        else { match(TokenType::Minus); bin->op = "-"; }
        bin->left = std::move(expr);
        bin->right = parse_multiplicative();
        expr = std::move(bin);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_multiplicative() {
    auto expr = parse_unary();
    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
        auto bin = std::make_unique<BinaryExpr>();
        if (match(TokenType::Star)) bin->op = "*";
        else if (match(TokenType::Slash)) bin->op = "/";
        else { match(TokenType::Percent); bin->op = "%"; }
        bin->left = std::move(expr);
        bin->right = parse_unary();
        expr = std::move(bin);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_unary() {
    if (match(TokenType::Bang)) {
        auto unary = std::make_unique<UnaryExpr>();
        unary->op = "!";
        unary->expr = parse_unary();
        return unary;
    }
    if (match(TokenType::Minus)) {
        auto unary = std::make_unique<UnaryExpr>();
        unary->op = "-";
        unary->expr = parse_unary();
        return unary;
    }
    return parse_postfix();
}

std::unique_ptr<Expr> Parser::parse_postfix() {
    auto expr = parse_primary();
    while (true) {
        if (match(TokenType::LParen)) {
            auto call = std::make_unique<CallExpr>();
            call->callee = std::move(expr);
            if (!check(TokenType::RParen)) {
                call->args.push_back(parse_expression());
                while (match(TokenType::Comma)) {
                    call->args.push_back(parse_expression());
                }
            }
            expect(TokenType::RParen, "Expected ')'");
            expr = std::move(call);
            continue;
        }
        if (match(TokenType::Dot)) {
            Token member = expect(TokenType::Identifier, "Expected member name");
            auto mem = std::make_unique<MemberExpr>();
            mem->object = std::move(expr);
            mem->member = member.text;
            expr = std::move(mem);
            continue;
        }
        if (match(TokenType::LBracket)) {
            auto idx = std::make_unique<IndexExpr>();
            idx->array = std::move(expr);
            idx->index = parse_expression();
            expect(TokenType::RBracket, "Expected ']'");
            expr = std::move(idx);
            continue;
        }
        break;
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    if (match(TokenType::Integer)) {
        auto lit = std::make_unique<IntLiteral>();
        lit->value = std::stoll(previous().text);
        return lit;
    }
    if (match(TokenType::Real)) {
        auto lit = std::make_unique<RealLiteral>();
        lit->value = std::stod(previous().text);
        return lit;
    }
    if (match(TokenType::String)) {
        auto lit = std::make_unique<StringLiteral>();
        lit->value = previous().text;
        return lit;
    }
    if (match(TokenType::Char)) {
        auto lit = std::make_unique<CharLiteral>();
        std::string t = previous().text;
        if (t.size() == 1) lit->value = t[0];
        else lit->value = '\0';
        return lit;
    }
    if (match(TokenType::KwTrue)) {
        auto lit = std::make_unique<BoolLiteral>();
        lit->value = true;
        return lit;
    }
    if (match(TokenType::KwFalse)) {
        auto lit = std::make_unique<BoolLiteral>();
        lit->value = false;
        return lit;
    }
    if (match(TokenType::KwNull)) {
        return std::make_unique<NullLiteral>();
    }
    if (match(TokenType::KwThis)) {
        return std::make_unique<ThisExpr>();
    }
    if (match(TokenType::KwInner)) {
        auto ident = std::make_unique<Identifier>();
        ident->name = "inner";
        return ident;
    }
    if (match(TokenType::Identifier)) {
        auto ident = std::make_unique<Identifier>();
        ident->name = previous().text;
        return ident;
    }
    if (match(TokenType::LParen)) {
        auto expr = parse_expression();
        expect(TokenType::RParen, "Expected ')'");
        return expr;
    }
    if (match(TokenType::LBracket)) {
        auto arr = std::make_unique<ArrayLiteral>();
        if (!check(TokenType::RBracket)) {
            arr->elements.push_back(parse_expression());
            while (match(TokenType::Comma)) {
                arr->elements.push_back(parse_expression());
            }
        }
        expect(TokenType::RBracket, "Expected ']'");
        return arr;
    }

    throw std::runtime_error("Unexpected expression");
}

} // namespace atom
