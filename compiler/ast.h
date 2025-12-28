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
#ifndef ATOM_AST_H
#define ATOM_AST_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace atom {

struct Type;

using TypePtr = std::shared_ptr<Type>;

struct Type {
    enum class Kind { Int, Real, Bool, Char, Void, String, Array, Class, Null, Unknown };

    Kind kind;
    std::string name;
    TypePtr elem;
    bool is_virtual = false;

    static TypePtr make(Kind kind, const std::string &name = "");
    static TypePtr make_array(const TypePtr &elem);
    static TypePtr make_class(const std::string &name);
    static TypePtr make_unknown();

    bool is_ref() const;
    std::string str() const;
};

struct Expr {
    virtual ~Expr() = default;
    TypePtr type;
};

struct IntLiteral : Expr {
    long long value;
};

struct RealLiteral : Expr {
    double value;
};

struct StringLiteral : Expr {
    std::string value;
};

struct CharLiteral : Expr {
    char value;
};

struct BoolLiteral : Expr {
    bool value;
};

struct NullLiteral : Expr {
};

struct Identifier : Expr {
    std::string name;
};

struct ThisExpr : Expr {
};

struct UnaryExpr : Expr {
    std::string op;
    std::unique_ptr<Expr> expr;
};

struct BinaryExpr : Expr {
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct CallExpr : Expr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
};

struct MemberExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string member;
};

struct IndexExpr : Expr {
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
};

struct ArrayLiteral : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
};

struct Statement {
    virtual ~Statement() = default;
};

struct VarDeclStmt : Statement {
    TypePtr type;
    std::string name;
    std::unique_ptr<Expr> init;
};

struct AssignStmt : Statement {
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
};

struct IfStmt : Statement {
    std::unique_ptr<Expr> cond;
    std::vector<std::unique_ptr<Statement>> then_body;
    std::vector<std::unique_ptr<Statement>> else_body;
};

struct WhileStmt : Statement {
    std::unique_ptr<Expr> cond;
    std::vector<std::unique_ptr<Statement>> body;
};

struct ReturnStmt : Statement {
    std::unique_ptr<Expr> value;
};

struct ExprStmt : Statement {
    std::unique_ptr<Expr> expr;
};

struct InnerStmt : Statement {
};

struct Param {
    TypePtr type;
    std::string name;
};

struct FunctionDef {
    TypePtr return_type;
    std::string name;
    std::vector<Param> params;
    std::vector<std::unique_ptr<Statement>> body;
    bool is_inline = false;
};

struct FieldDecl {
    TypePtr type;
    std::string name;
    std::unique_ptr<Expr> init;
};

struct MethodDef {
    TypePtr return_type;
    std::string name;
    std::vector<Param> params;
    std::vector<std::unique_ptr<Statement>> body;
    bool is_constructor = false;
};

struct StructDef {
    std::string name;
    std::string parent;
    std::vector<FieldDecl> fields;
    std::vector<MethodDef> methods;
};

struct ImportStmt {
    std::string name;
    std::string alias;
    std::string path;
};

struct Program {
    std::vector<ImportStmt> imports;
    std::vector<StructDef> structs;
    std::vector<FunctionDef> functions;
};

struct ClassInfo {
    StructDef *def = nullptr;
};

struct MethodInfo {
    MethodDef *def = nullptr;
    std::string owner;
    int slot = -1;
    int func_index = -1;
    bool overrides = false;
};

struct ClassLayout {
    int class_id = -1;
    std::unordered_map<std::string, int> field_offsets;
    int size = 16;
    std::unordered_map<std::string, MethodInfo> methods;
};

struct FunctionInfo {
    FunctionDef *def = nullptr;
};

struct SemanticContext {
    std::unordered_map<std::string, ClassInfo> classes;
    std::unordered_map<std::string, ClassLayout> layouts;
    std::unordered_map<std::string, FunctionInfo> functions;
};

} // namespace atom

#endif
