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
#include "codegen.h"

#include "ast.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace atom {

struct Emitter {
    std::ostringstream out;
    int indent = 0;
    bool enabled = true;

    void line(const std::string &text) {
        if (!enabled)
            return;
        for (int i = 0; i < indent; ++i)
            out << "  ";
        out << text << "\n";
    }

    void open(const std::string &text) {
        line(text);
        indent++;
    }

    void close(const std::string &text) {
        indent--;
        line(text);
    }
};

struct StringData {
    std::string value;
    int offset = 0;
};

bool type_equals(const TypePtr &a, const TypePtr &b, bool strict_virtual) {
    if (!a || !b)
        return false;
    if (a->kind != b->kind)
        return false;
    if (strict_virtual && a->is_virtual != b->is_virtual)
        return false;
    if (a->kind == Type::Kind::Array) {
        if (!a->elem || !b->elem)
            return false;
        return type_equals(a->elem, b->elem, strict_virtual);
    }
    if (a->kind == Type::Kind::Class)
        return a->name == b->name;
    return true;
}

TypePtr unify_numeric(const TypePtr &a, const TypePtr &b) {
    if (a->kind == Type::Kind::Real || b->kind == Type::Kind::Real) {
        return Type::make(Type::Kind::Real);
    }
    return Type::make(Type::Kind::Int);
}

struct Scope {
    std::unordered_map<std::string, TypePtr> vars;
};

struct TypeChecker {
    Program &program;
    SemanticContext &ctx;
    std::string current_class;
    std::string current_function;
    std::vector<Scope> scopes;

    TypeChecker(Program &p, SemanticContext &c) : program(p), ctx(c) {}

    void push_scope() { scopes.push_back({}); }
    void pop_scope() { scopes.pop_back(); }

    void define(const std::string &name, const TypePtr &type) {
        if (scopes.empty())
            push_scope();
        scopes.back().vars[name] = type;
    }

    TypePtr lookup(const std::string &name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->vars.find(name);
            if (found != it->vars.end())
                return found->second;
        }
        if (!current_class.empty()) {
            auto &cls = ctx.classes[current_class];
            for (auto &field : cls.def->fields) {
                if (field.name == name)
                    return field.type;
            }
        }
        return Type::make_unknown();
    }

    TypePtr check_expr(Expr *expr) {
        if (auto lit = dynamic_cast<IntLiteral *>(expr)) {
            lit->type = Type::make(Type::Kind::Int);
            return lit->type;
        }
        if (auto lit = dynamic_cast<RealLiteral *>(expr)) {
            lit->type = Type::make(Type::Kind::Real);
            return lit->type;
        }
        if (auto lit = dynamic_cast<StringLiteral *>(expr)) {
            lit->type = Type::make(Type::Kind::String);
            return lit->type;
        }
        if (auto lit = dynamic_cast<CharLiteral *>(expr)) {
            lit->type = Type::make(Type::Kind::Char);
            return lit->type;
        }
        if (auto lit = dynamic_cast<BoolLiteral *>(expr)) {
            lit->type = Type::make(Type::Kind::Bool);
            return lit->type;
        }
        if (dynamic_cast<NullLiteral *>(expr)) {
            expr->type = Type::make(Type::Kind::Null);
            return expr->type;
        }
        if (auto ident = dynamic_cast<Identifier *>(expr)) {
            if (ident->name == "inner") {
                ident->type = Type::make(Type::Kind::Void);
                return ident->type;
            }
            TypePtr type = lookup(ident->name);
            if (type->kind == Type::Kind::Unknown) {
                if (ctx.functions.count(ident->name))
                    type = ctx.functions[ident->name].def->return_type;
                if (ctx.classes.count(ident->name))
                    type = Type::make_class(ident->name);
            }
            ident->type = type;
            return type;
        }
        if (dynamic_cast<ThisExpr *>(expr)) {
            expr->type = Type::make_class(current_class);
            return expr->type;
        }
        if (auto unary = dynamic_cast<UnaryExpr *>(expr)) {
            auto t = check_expr(unary->expr.get());
            if (unary->op == "-") {
                unary->type = (t->kind == Type::Kind::Real) ? Type::make(Type::Kind::Real) : Type::make(Type::Kind::Int);
            } else {
                unary->type = Type::make(Type::Kind::Bool);
            }
            return unary->type;
        }
        if (auto bin = dynamic_cast<BinaryExpr *>(expr)) {
            auto lt = check_expr(bin->left.get());
            auto rt = check_expr(bin->right.get());
            if (bin->op == "+" && (lt->kind == Type::Kind::String || rt->kind == Type::Kind::String)) {
                bin->type = Type::make(Type::Kind::String);
            } else if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == "<=" || bin->op == ">" || bin->op == ">=") {
                bin->type = Type::make(Type::Kind::Bool);
            } else if (bin->op == "&&" || bin->op == "||") {
                bin->type = Type::make(Type::Kind::Bool);
            } else if (bin->op == "%") {
                bin->type = Type::make(Type::Kind::Int);
            } else {
                bin->type = unify_numeric(lt, rt);
            }
            return bin->type;
        }
        if (auto mem = dynamic_cast<MemberExpr *>(expr)) {
            auto objt = check_expr(mem->object.get());
            if (objt->kind == Type::Kind::Class) {
                auto &cls = ctx.classes[objt->name];
                for (auto &field : cls.def->fields) {
                    if (field.name == mem->member) {
                        mem->type = field.type;
                        return mem->type;
                    }
                }
                mem->type = Type::make_unknown();
            } else {
                mem->type = Type::make_unknown();
            }
            return mem->type;
        }
        if (auto idx = dynamic_cast<IndexExpr *>(expr)) {
            auto at = check_expr(idx->array.get());
            check_expr(idx->index.get());
            if (at->kind == Type::Kind::Array)
                idx->type = at->elem ? at->elem : Type::make_unknown();
            else
                idx->type = Type::make_unknown();
            return idx->type;
        }
        if (auto arr = dynamic_cast<ArrayLiteral *>(expr)) {
            TypePtr elem = nullptr;
            for (auto &el : arr->elements) {
                TypePtr t = check_expr(el.get());
                if (!elem)
                    elem = t;
                else if (elem->kind != t->kind)
                    elem = Type::make_unknown();
            }
            if (!elem)
                elem = Type::make_unknown();
            arr->type = Type::make_array(elem);
            return arr->type;
        }
        if (auto call = dynamic_cast<CallExpr *>(expr)) {
            if (auto callee_ident = dynamic_cast<Identifier *>(call->callee.get())) {
                if (ctx.functions.count(callee_ident->name)) {
                    auto fn = ctx.functions[callee_ident->name].def;
                    for (auto &arg : call->args)
                        check_expr(arg.get());
                    call->type = fn->return_type;
                    return call->type;
                }
                if (ctx.classes.count(callee_ident->name)) {
                    for (auto &arg : call->args)
                        check_expr(arg.get());
                    call->type = Type::make_class(callee_ident->name);
                    return call->type;
                }
            }
            if (auto callee_mem = dynamic_cast<MemberExpr *>(call->callee.get())) {
                auto objt = check_expr(callee_mem->object.get());
                for (auto &arg : call->args)
                    check_expr(arg.get());
                if (callee_mem->member == "new" && objt->kind == Type::Kind::Class) {
                    call->type = Type::make_class(objt->name);
                    return call->type;
                }
                if (callee_mem->member == "print" || callee_mem->member == "println") {
                    call->type = Type::make(Type::Kind::Void);
                    return call->type;
                }
                if (objt->kind == Type::Kind::Array) {
                    if (callee_mem->member == "size" || callee_mem->member == "count")
                        call->type = Type::make(Type::Kind::Int);
                    else if (callee_mem->member == "push" || callee_mem->member == "set")
                        call->type = Type::make(Type::Kind::Void);
                    else if (callee_mem->member == "pop" || callee_mem->member == "get")
                        call->type = objt->elem ? objt->elem : Type::make_unknown();
                    else if (callee_mem->member == "getString")
                        call->type = Type::make(Type::Kind::String);
                    else if (callee_mem->member == "getInt")
                        call->type = Type::make(Type::Kind::Int);
                    else
                        call->type = Type::make_unknown();
                    return call->type;
                }
                if (objt->kind == Type::Kind::String) {
                    if (callee_mem->member == "length")
                        call->type = Type::make(Type::Kind::Int);
                    else
                        call->type = Type::make(Type::Kind::Void);
                    return call->type;
                }
                if (objt->kind == Type::Kind::Class) {
                    auto &layout = ctx.layouts[objt->name];
                    auto it = layout.methods.find(callee_mem->member);
                    if (it != layout.methods.end()) {
                        call->type = it->second.def->return_type;
                        return call->type;
                    }
                }
            }
            call->type = Type::make_unknown();
            return call->type;
        }
        expr->type = Type::make_unknown();
        return expr->type;
    }

    void check_stmt(Statement *stmt) {
        if (auto var = dynamic_cast<VarDeclStmt *>(stmt)) {
            TypePtr declared = var->type;
            TypePtr init_type = Type::make_unknown();
            if (var->init)
                init_type = check_expr(var->init.get());
            if (declared->kind == Type::Kind::Unknown)
                declared = init_type;
            if (declared->kind == Type::Kind::Array && declared->elem && declared->elem->kind == Type::Kind::Unknown && init_type->kind == Type::Kind::Array && init_type->elem) {
                declared->elem = init_type->elem;
            }
            var->type = declared;
            define(var->name, declared);
            return;
        }
        if (auto asg = dynamic_cast<AssignStmt *>(stmt)) {
            check_expr(asg->target.get());
            check_expr(asg->value.get());
            return;
        }
        if (auto expr = dynamic_cast<ExprStmt *>(stmt)) {
            check_expr(expr->expr.get());
            return;
        }
        if (auto ret = dynamic_cast<ReturnStmt *>(stmt)) {
            if (ret->value)
                check_expr(ret->value.get());
            return;
        }
        if (auto iff = dynamic_cast<IfStmt *>(stmt)) {
            check_expr(iff->cond.get());
            push_scope();
            for (auto &s : iff->then_body)
                check_stmt(s.get());
            pop_scope();
            push_scope();
            for (auto &s : iff->else_body)
                check_stmt(s.get());
            pop_scope();
            return;
        }
        if (auto wh = dynamic_cast<WhileStmt *>(stmt)) {
            check_expr(wh->cond.get());
            push_scope();
            for (auto &s : wh->body)
                check_stmt(s.get());
            pop_scope();
            return;
        }
    }

    void check_function(FunctionDef &fn) {
        current_function = fn.name;
        current_class.clear();
        push_scope();
        for (auto &param : fn.params)
            define(param.name, param.type);
        for (auto &stmt : fn.body)
            check_stmt(stmt.get());
        pop_scope();
    }

    void check_method(const std::string &class_name, MethodDef &method) {
        current_class = class_name;
        current_function = method.name;
        push_scope();
        define("this", Type::make_class(class_name));
        for (auto &param : method.params)
            define(param.name, param.type);
        for (auto &stmt : method.body)
            check_stmt(stmt.get());
        pop_scope();
    }
};

struct Codegen {
    Program &program;
    SemanticContext &ctx;
    Emitter emit;

    int label_id = 0;

    struct Local {
        TypePtr type;
        std::string name;
    };

    struct FunctionContext {
        std::string name;
        std::string class_name;
        TypePtr return_type;
        std::unordered_map<std::string, Local> locals;
        std::unordered_map<std::string, Local> field_alias;
        std::vector<Local> extra_locals;
        std::unordered_map<std::string, std::string> const_strings;
        std::vector<TypePtr> local_types;
        int param_count = 0;
        int local_count = 0;
        std::string temp_i32 = "$t0";
        std::string temp_i64 = "$t1";
        std::string temp_i32_alt = "$t2";
        std::string temp_f64 = "$t3";
    };

    std::unordered_map<std::string, StringData> string_pool;
    std::vector<StringData *> string_list;
    int string_offset = 0;
    std::unordered_map<MethodDef *, int> method_func_index;
    std::unordered_map<MethodDef *, int> method_type_index;
    std::unordered_map<FunctionDef *, int> function_type_index;
    std::vector<std::string> function_table;
    std::vector<std::string> type_defs;
    std::unordered_map<std::string, int> signature_to_type;
    std::unordered_set<FunctionDef *> reachable_functions;
    std::unordered_set<MethodDef *> reachable_methods;
    std::unordered_set<std::string> reachable_classes;
    std::unordered_map<MethodDef *, std::string> method_owner;
    std::vector<std::pair<std::string, std::string>> virtual_call_sites;
    std::unordered_set<std::string> runtime_used;

    int vtable_base = 0;
    int inner_base = 0;
    int scratch_base = 0;
    int heap_base = 0;
    int max_slots = 0;
    std::unordered_map<std::string, std::vector<std::string>> class_children;
    std::unordered_map<std::string, std::unordered_set<std::string>> overridden_in_descendants;

    Codegen(Program &p, SemanticContext &c) : program(p), ctx(c) {}

    bool is_type_identifier(const std::string &name) const {
        if (name == "Int" || name == "Real" || name == "Bool" || name == "Char" || name == "Void" || name == "String" || name == "Array") {
            return true;
        }
        return ctx.classes.count(name) > 0;
    }

    std::optional<std::string> eval_string_literal(Expr *expr, FunctionContext &fctx) {
        if (auto lit = dynamic_cast<StringLiteral *>(expr))
            return lit->value;
        if (auto ident = dynamic_cast<Identifier *>(expr)) {
            auto it = fctx.const_strings.find(ident->name);
            if (it != fctx.const_strings.end())
                return it->second;
        }
        if (auto bin = dynamic_cast<BinaryExpr *>(expr)) {
            if (bin->op != "+")
                return std::nullopt;
            auto left = eval_string_literal(bin->left.get(), fctx);
            if (!left)
                return std::nullopt;
            auto right = eval_string_literal(bin->right.get(), fctx);
            if (!right)
                return std::nullopt;
            return *left + *right;
        }
        return std::nullopt;
    }

    bool is_trivial_getter(const MethodDef &method, const std::string &class_name, std::string &field_name) const {
        if (!method.params.empty())
            return false;
        if (method.body.size() != 1)
            return false;
        auto ret = dynamic_cast<ReturnStmt *>(method.body[0].get());
        if (!ret || !ret->value)
            return false;
        if (auto ident = dynamic_cast<Identifier *>(ret->value.get())) {
            if (ctx.layouts.at(class_name).field_offsets.count(ident->name)) {
                field_name = ident->name;
                return true;
            }
        }
        if (auto mem = dynamic_cast<MemberExpr *>(ret->value.get())) {
            bool is_this = dynamic_cast<ThisExpr *>(mem->object.get()) != nullptr;
            if (!is_this) {
                if (auto obj_ident = dynamic_cast<Identifier *>(mem->object.get())) {
                    is_this = obj_ident->name == "this";
                }
            }
            if (is_this && ctx.layouts.at(class_name).field_offsets.count(mem->member)) {
                field_name = mem->member;
                return true;
            }
        }
        return false;
    }

    const std::unordered_set<std::string> &inline_method_allowlist() const {
        static const std::unordered_set<std::string> names = {
            "getX", "getY", "getValue", "getCount", "getSize",
            "getWidth", "getHeight", "getBalance", "getAge",
            "getState", "getHealth", "getScore", "getRows",
            "getCols", "getName", "getTitle", "getDepartment",
            "getStudentId", "getIndent"};
        return names;
    }

    void use_runtime(const std::string &name) {
        if (!runtime_used.insert(name).second)
            return;
        if (name == "print_literal") {
            use_runtime("write_buf");
        } else if (name == "print_string") {
            use_runtime("write_buf");
        } else if (name == "print_int") {
            use_runtime("itoa");
            use_runtime("write_buf");
        } else if (name == "print_bool") {
            use_runtime("print_literal");
        } else if (name == "print_newline") {
            use_runtime("print_literal");
        } else if (name == "print_real") {
            use_runtime("print_int");
        } else if (name == "write_buf") {
            use_runtime("fd_write");
        } else if (name == "string_new") {
            use_runtime("malloc");
            use_runtime("memcpy");
        } else if (name == "string_concat") {
            use_runtime("malloc");
            use_runtime("memcpy");
        } else if (name == "array_new") {
            use_runtime("malloc");
        } else if (name == "array_push_i64") {
            use_runtime("malloc");
            use_runtime("memcpy");
        } else if (name == "array_push_f64") {
            use_runtime("array_push_i64");
        } else if (name == "array_get_ptr") {
            use_runtime("array_get_i64");
        } else if (name == "array_pop_f64") {
            use_runtime("array_pop_i64");
        } else if (name == "array_pop_ptr") {
            use_runtime("array_pop_i64");
        } else if (name == "split_ws") {
            use_runtime("malloc");
            use_runtime("array_new");
            use_runtime("array_set_i64");
            use_runtime("string_new");
        }
    }

    void collect_field_reads(Expr *expr, FunctionContext &fctx, std::unordered_set<std::string> &reads, bool &has_this_calls) {
        if (!expr)
            return;
        if (auto ident = dynamic_cast<Identifier *>(expr)) {
            if (!fctx.locals.count(ident->name) && ctx.layouts[fctx.class_name].field_offsets.count(ident->name)) {
                reads.insert(ident->name);
            }
            return;
        }
        if (auto mem = dynamic_cast<MemberExpr *>(expr)) {
            bool is_this = dynamic_cast<ThisExpr *>(mem->object.get()) != nullptr;
            if (!is_this) {
                if (auto obj_ident = dynamic_cast<Identifier *>(mem->object.get())) {
                    is_this = obj_ident->name == "this";
                }
            }
            if (is_this && ctx.layouts[fctx.class_name].field_offsets.count(mem->member)) {
                reads.insert(mem->member);
                return;
            }
            collect_field_reads(mem->object.get(), fctx, reads, has_this_calls);
            return;
        }
        if (auto idx = dynamic_cast<IndexExpr *>(expr)) {
            collect_field_reads(idx->array.get(), fctx, reads, has_this_calls);
            collect_field_reads(idx->index.get(), fctx, reads, has_this_calls);
            return;
        }
        if (auto call = dynamic_cast<CallExpr *>(expr)) {
            if (auto mem = dynamic_cast<MemberExpr *>(call->callee.get())) {
                Expr *base = mem->object.get();
                while (auto nested = dynamic_cast<MemberExpr *>(base))
                    base = nested->object.get();
                bool is_this = dynamic_cast<ThisExpr *>(base) != nullptr;
                if (!is_this) {
                    if (auto obj_ident = dynamic_cast<Identifier *>(base)) {
                        is_this = obj_ident->name == "this";
                    }
                }
                if (is_this)
                    has_this_calls = true;
            }
            collect_field_reads(call->callee.get(), fctx, reads, has_this_calls);
            for (auto &arg : call->args)
                collect_field_reads(arg.get(), fctx, reads, has_this_calls);
            return;
        }
        if (auto bin = dynamic_cast<BinaryExpr *>(expr)) {
            collect_field_reads(bin->left.get(), fctx, reads, has_this_calls);
            collect_field_reads(bin->right.get(), fctx, reads, has_this_calls);
            return;
        }
        if (auto unary = dynamic_cast<UnaryExpr *>(expr)) {
            collect_field_reads(unary->expr.get(), fctx, reads, has_this_calls);
            return;
        }
        if (auto arr = dynamic_cast<ArrayLiteral *>(expr)) {
            for (auto &el : arr->elements)
                collect_field_reads(el.get(), fctx, reads, has_this_calls);
            return;
        }
    }

    bool collect_field_write_target(Expr *expr, FunctionContext &fctx, std::unordered_set<std::string> &writes) {
        if (auto ident = dynamic_cast<Identifier *>(expr)) {
            if (!fctx.locals.count(ident->name) && ctx.layouts[fctx.class_name].field_offsets.count(ident->name)) {
                writes.insert(ident->name);
                return true;
            }
        }
        if (auto mem = dynamic_cast<MemberExpr *>(expr)) {
            bool is_this = dynamic_cast<ThisExpr *>(mem->object.get()) != nullptr;
            if (!is_this) {
                if (auto obj_ident = dynamic_cast<Identifier *>(mem->object.get())) {
                    is_this = obj_ident->name == "this";
                }
            }
            if (is_this && ctx.layouts[fctx.class_name].field_offsets.count(mem->member)) {
                writes.insert(mem->member);
                return true;
            }
        }
        return false;
    }

    void collect_field_accesses(const std::vector<std::unique_ptr<Statement>> &stmts, FunctionContext &fctx,
                                std::unordered_set<std::string> &reads, std::unordered_set<std::string> &writes,
                                bool &has_this_calls) {
        for (const auto &stmt : stmts) {
            if (auto var = dynamic_cast<VarDeclStmt *>(stmt.get())) {
                if (var->init)
                    collect_field_reads(var->init.get(), fctx, reads, has_this_calls);
                continue;
            }
            if (auto asg = dynamic_cast<AssignStmt *>(stmt.get())) {
                bool direct_field = collect_field_write_target(asg->target.get(), fctx, writes);
                if (!direct_field)
                    collect_field_reads(asg->target.get(), fctx, reads, has_this_calls);
                collect_field_reads(asg->value.get(), fctx, reads, has_this_calls);
                continue;
            }
            if (auto expr = dynamic_cast<ExprStmt *>(stmt.get())) {
                collect_field_reads(expr->expr.get(), fctx, reads, has_this_calls);
                continue;
            }
            if (auto ret = dynamic_cast<ReturnStmt *>(stmt.get())) {
                if (ret->value)
                    collect_field_reads(ret->value.get(), fctx, reads, has_this_calls);
                continue;
            }
            if (auto iff = dynamic_cast<IfStmt *>(stmt.get())) {
                collect_field_reads(iff->cond.get(), fctx, reads, has_this_calls);
                collect_field_accesses(iff->then_body, fctx, reads, writes, has_this_calls);
                collect_field_accesses(iff->else_body, fctx, reads, writes, has_this_calls);
                continue;
            }
            if (auto wh = dynamic_cast<WhileStmt *>(stmt.get())) {
                collect_field_reads(wh->cond.get(), fctx, reads, has_this_calls);
                collect_field_accesses(wh->body, fctx, reads, writes, has_this_calls);
                continue;
            }
        }
    }

    void setup_field_cache(FunctionContext &fctx, const std::vector<std::unique_ptr<Statement>> &body) {
        if (fctx.class_name.empty())
            return;
        std::unordered_set<std::string> reads;
        std::unordered_set<std::string> writes;
        bool has_this_calls = false;
        collect_field_accesses(body, fctx, reads, writes, has_this_calls);
        if (has_this_calls)
            return;
        if (reads.empty())
            return;

        const auto &fields = ctx.classes[fctx.class_name].def->fields;
        for (const auto &field : fields) {
            if (!reads.count(field.name))
                continue;
            if (writes.count(field.name))
                continue;
            Local local{field.type, "$f" + std::to_string(fctx.extra_locals.size())};
            fctx.field_alias[field.name] = local;
            fctx.extra_locals.push_back(local);
        }
    }

    void emit_field_cache_init(FunctionContext &fctx) {
        if (fctx.class_name.empty())
            return;
        for (const auto &kv : fctx.field_alias) {
            const std::string &field = kv.first;
            const Local &local = kv.second;
            int offset = ctx.layouts[fctx.class_name].field_offsets[field];
            emit.line("local.get $p0");
            emit.line("i32.const " + std::to_string(offset));
            emit.line("i32.add");
            emit_load(local.type);
            emit.line("local.set " + local.name);
        }
    }

    std::string next_label(const std::string &base) {
        return base + std::to_string(label_id++);
    }

    int add_type_signature(const std::vector<std::string> &params, const std::string &result) {
        std::ostringstream sig;
        sig << "(";
        for (auto &p : params)
            sig << p << ",";
        sig << ")" << result;
        std::string key = sig.str();
        auto it = signature_to_type.find(key);
        if (it != signature_to_type.end())
            return it->second;

        int index = static_cast<int>(type_defs.size());
        std::ostringstream line;
        line << "(type $t" << index << " (func";
        for (auto &p : params)
            line << " (param " << p << ")";
        if (!result.empty())
            line << " (result " << result << ")";
        line << "))";
        type_defs.push_back(line.str());
        signature_to_type[key] = index;
        return index;
    }

    std::string wasm_type(const TypePtr &type) {
        if (!type)
            return "i32";
        switch (type->kind) {
        case Type::Kind::Int:
        case Type::Kind::Bool:
        case Type::Kind::Char:
            return "i64";
        case Type::Kind::Real:
            return "f64";
        case Type::Kind::Void:
            return "";
        case Type::Kind::String:
        case Type::Kind::Array:
        case Type::Kind::Class:
        case Type::Kind::Null:
        case Type::Kind::Unknown:
        default:
            return "i32";
        }
    }

    int add_string_literal(const std::string &value) {
        auto it = string_pool.find(value);
        if (it != string_pool.end())
            return it->second.offset;
        int offset = string_offset;
        string_pool[value] = StringData{value, offset};
        string_list.push_back(&string_pool[value]);
        string_offset += static_cast<int>(value.size());
        return offset;
    }

    void build_class_layouts() {
        int class_id = 0;
        for (auto &kv : ctx.classes)
            ctx.layouts[kv.first].class_id = class_id++;
        for (auto &kv : ctx.classes)
            build_class_layout(kv.first);
    }

    void build_method_owner_map() {
        method_owner.clear();
        for (auto &kv : ctx.classes) {
            for (auto &method : kv.second.def->methods)
                method_owner[&method] = kv.first;
        }
    }

    void build_inheritance_info() {
        class_children.clear();
        overridden_in_descendants.clear();

        for (auto &kv : ctx.classes) {
            const auto &parent = kv.second.def->parent;
            if (!parent.empty())
                class_children[parent].push_back(kv.first);
        }

        for (auto &kv : ctx.layouts) {
            const std::string &base = kv.first;
            const auto &base_methods = kv.second.methods;
            if (base_methods.empty())
                continue;

            std::vector<std::string> stack = class_children[base];
            std::vector<std::string> descendants;
            while (!stack.empty()) {
                std::string cur = stack.back();
                stack.pop_back();
                descendants.push_back(cur);
                auto it = class_children.find(cur);
                if (it != class_children.end()) {
                    for (const auto &child : it->second)
                        stack.push_back(child);
                }
            }

            for (const auto &desc : descendants) {
                const auto &desc_layout = ctx.layouts[desc];
                for (const auto &method_kv : base_methods) {
                    const std::string &method_name = method_kv.first;
                    const std::string &def_owner = method_kv.second.owner;
                    auto it = desc_layout.methods.find(method_name);
                    if (it != desc_layout.methods.end() && it->second.owner != def_owner) {
                        overridden_in_descendants[base].insert(method_name);
                    }
                }
            }
        }
    }

    bool method_overridden_in_descendants(const std::string &class_name, const std::string &method_name) const {
        auto it = overridden_in_descendants.find(class_name);
        if (it == overridden_in_descendants.end())
            return false;
        return it->second.count(method_name) > 0;
    }

    bool method_overridden_in_ancestors(const std::string &class_name, const std::string &method_name) const {
        auto it = ctx.classes.find(class_name);
        if (it == ctx.classes.end())
            return false;
        std::string parent = it->second.def->parent;
        while (!parent.empty()) {
            auto layout_it = ctx.layouts.find(parent);
            if (layout_it != ctx.layouts.end() && layout_it->second.methods.count(method_name))
                return true;
            auto class_it = ctx.classes.find(parent);
            if (class_it == ctx.classes.end())
                break;
            parent = class_it->second.def->parent;
        }
        return false;
    }

    bool is_descendant(const std::string &child, const std::string &base) const {
        if (child == base)
            return true;
        auto it = ctx.classes.find(child);
        if (it == ctx.classes.end())
            return false;
        std::string parent = it->second.def->parent;
        while (!parent.empty()) {
            if (parent == base)
                return true;
            auto pit = ctx.classes.find(parent);
            if (pit == ctx.classes.end())
                break;
            parent = pit->second.def->parent;
        }
        return false;
    }

    void mark_class(const std::string &name) {
        if (!reachable_classes.insert(name).second)
            return;
        for (const auto &site : virtual_call_sites) {
            if (!is_descendant(name, site.first))
                continue;
            auto layout_it = ctx.layouts.find(name);
            if (layout_it == ctx.layouts.end())
                continue;
            auto it = layout_it->second.methods.find(site.second);
            if (it != layout_it->second.methods.end())
                mark_method(it->second.def);
        }
    }

    void add_virtual_call_site(const std::string &static_class, const std::string &method_name) {
        for (const auto &site : virtual_call_sites) {
            if (site.first == static_class && site.second == method_name)
                return;
        }
        virtual_call_sites.emplace_back(static_class, method_name);
        for (const auto &cls : reachable_classes) {
            if (!is_descendant(cls, static_class))
                continue;
            auto layout_it = ctx.layouts.find(cls);
            if (layout_it == ctx.layouts.end())
                continue;
            auto it = layout_it->second.methods.find(method_name);
            if (it != layout_it->second.methods.end())
                mark_method(it->second.def);
        }
    }

    void visit_expr(Expr *expr, const std::string &current_class, const std::string &current_method) {
        if (!expr)
            return;
        if (auto call = dynamic_cast<CallExpr *>(expr)) {
            if (auto callee_ident = dynamic_cast<Identifier *>(call->callee.get())) {
                if (ctx.functions.count(callee_ident->name)) {
                    mark_function(ctx.functions[callee_ident->name].def);
                } else if (ctx.classes.count(callee_ident->name)) {
                    mark_class(callee_ident->name);
                    auto it = ctx.layouts.find(callee_ident->name);
                    if (it != ctx.layouts.end()) {
                        auto mit = it->second.methods.find("init");
                        if (mit != it->second.methods.end() && call->args.size() == mit->second.def->params.size()) {
                            mark_method(mit->second.def);
                        }
                    }
                }
            } else if (auto callee_mem = dynamic_cast<MemberExpr *>(call->callee.get())) {
                if (callee_mem->member == "new") {
                    if (auto ident = dynamic_cast<Identifier *>(callee_mem->object.get())) {
                        if (ctx.classes.count(ident->name)) {
                            mark_class(ident->name);
                            auto it = ctx.layouts.find(ident->name);
                            if (it != ctx.layouts.end()) {
                                auto mit = it->second.methods.find("init");
                                if (mit != it->second.methods.end() && call->args.size() == mit->second.def->params.size()) {
                                    mark_method(mit->second.def);
                                }
                            }
                        }
                    }
                } else if (callee_mem->member != "print" && callee_mem->member != "println") {
                    auto obj_type = callee_mem->object->type;
                    if (obj_type && obj_type->kind == Type::Kind::Class) {
                        add_virtual_call_site(obj_type->name, callee_mem->member);
                    }
                }
            }
            visit_expr(call->callee.get(), current_class, current_method);
            for (auto &arg : call->args)
                visit_expr(arg.get(), current_class, current_method);
            return;
        }
        if (auto mem = dynamic_cast<MemberExpr *>(expr)) {
            visit_expr(mem->object.get(), current_class, current_method);
            return;
        }
        if (auto idx = dynamic_cast<IndexExpr *>(expr)) {
            visit_expr(idx->array.get(), current_class, current_method);
            visit_expr(idx->index.get(), current_class, current_method);
            return;
        }
        if (auto bin = dynamic_cast<BinaryExpr *>(expr)) {
            visit_expr(bin->left.get(), current_class, current_method);
            visit_expr(bin->right.get(), current_class, current_method);
            return;
        }
        if (auto unary = dynamic_cast<UnaryExpr *>(expr)) {
            visit_expr(unary->expr.get(), current_class, current_method);
            return;
        }
        if (auto arr = dynamic_cast<ArrayLiteral *>(expr)) {
            for (auto &el : arr->elements)
                visit_expr(el.get(), current_class, current_method);
            return;
        }
    }

    void visit_statements(const std::vector<std::unique_ptr<Statement>> &stmts, const std::string &current_class, const std::string &current_method) {
        for (const auto &stmt : stmts) {
            if (auto var = dynamic_cast<VarDeclStmt *>(stmt.get())) {
                if (var->init)
                    visit_expr(var->init.get(), current_class, current_method);
                continue;
            }
            if (auto asg = dynamic_cast<AssignStmt *>(stmt.get())) {
                visit_expr(asg->target.get(), current_class, current_method);
                visit_expr(asg->value.get(), current_class, current_method);
                continue;
            }
            if (auto expr = dynamic_cast<ExprStmt *>(stmt.get())) {
                visit_expr(expr->expr.get(), current_class, current_method);
                continue;
            }
            if (auto ret = dynamic_cast<ReturnStmt *>(stmt.get())) {
                if (ret->value)
                    visit_expr(ret->value.get(), current_class, current_method);
                continue;
            }
            if (auto iff = dynamic_cast<IfStmt *>(stmt.get())) {
                visit_expr(iff->cond.get(), current_class, current_method);
                visit_statements(iff->then_body, current_class, current_method);
                visit_statements(iff->else_body, current_class, current_method);
                continue;
            }
            if (auto wh = dynamic_cast<WhileStmt *>(stmt.get())) {
                visit_expr(wh->cond.get(), current_class, current_method);
                visit_statements(wh->body, current_class, current_method);
                continue;
            }
            if (dynamic_cast<InnerStmt *>(stmt.get())) {
                if (!current_class.empty()) {
                    for (const auto &cls : reachable_classes) {
                        if (!is_descendant(cls, current_class) || cls == current_class)
                            continue;
                        auto layout_it = ctx.layouts.find(cls);
                        if (layout_it == ctx.layouts.end())
                            continue;
                        auto it = layout_it->second.methods.find(current_method);
                        if (it != layout_it->second.methods.end())
                            mark_method(it->second.def);
                    }
                }
            }
        }
    }

    void mark_function(FunctionDef *fn) {
        if (!fn)
            return;
        if (!reachable_functions.insert(fn).second)
            return;
        visit_statements(fn->body, "", "");
    }

    void mark_method(MethodDef *method) {
        if (!method)
            return;
        if (!reachable_methods.insert(method).second)
            return;
        auto it = method_owner.find(method);
        std::string owner = (it != method_owner.end()) ? it->second : "";
        visit_statements(method->body, owner, method->name);
    }

    void compute_reachability() {
        reachable_functions.clear();
        reachable_methods.clear();
        reachable_classes.clear();
        virtual_call_sites.clear();
        build_method_owner_map();

        FunctionDef *main_fn = nullptr;
        for (auto &fn : program.functions)
            if (fn.name == "main")
                main_fn = &fn;
        if (main_fn) {
            mark_function(main_fn);
        } else {
            auto it = ctx.layouts.find("Main");
            if (it != ctx.layouts.end()) {
                mark_class("Main");
                auto start_it = it->second.methods.find("start");
                if (start_it != it->second.methods.end())
                    mark_method(start_it->second.def);
                auto init_it = it->second.methods.find("init");
                if (init_it != it->second.methods.end() && init_it->second.def->params.empty())
                    mark_method(init_it->second.def);
            }
        }
    }

    void build_class_layout(const std::string &name) {
        auto &layout = ctx.layouts[name];
        if (!layout.field_offsets.empty() || !layout.methods.empty())
            return;

        auto &cls = ctx.classes[name];
        int offset = 16;
        int slot_count = 0;
        if (!cls.def->parent.empty()) {
            build_class_layout(cls.def->parent);
            auto &parent_layout = ctx.layouts[cls.def->parent];
            layout.field_offsets = parent_layout.field_offsets;
            layout.methods = parent_layout.methods;
            offset = parent_layout.size;
            for (auto &m : parent_layout.methods)
                slot_count = std::max(slot_count, m.second.slot + 1);
        }

        for (auto &field : cls.def->fields) {
            layout.field_offsets[field.name] = offset;
            offset += 8;
        }
        layout.size = offset;

        for (auto &method : cls.def->methods) {
            bool has_parent = layout.methods.count(method.name) > 0;
            bool override_allowed = true;
            if (has_parent) {
                auto &parent_info = layout.methods[method.name];
                if (method.is_constructor) {
                    override_allowed = true;
                } else if (method.name.rfind("nonVirtual", 0) == 0) {
                    override_allowed = false;
                }
                if (!method.is_constructor) {
                    if (!type_equals(parent_info.def->return_type, method.return_type, true))
                        override_allowed = false;
                    if (parent_info.def->params.size() != method.params.size())
                        override_allowed = false;
                    else {
                        for (size_t i = 0; i < method.params.size(); ++i) {
                            if (!type_equals(parent_info.def->params[i].type, method.params[i].type, true)) {
                                override_allowed = false;
                                break;
                            }
                        }
                    }
                }
                if (override_allowed) {
                    MethodInfo info;
                    info.def = &method;
                    info.owner = name;
                    info.slot = parent_info.slot;
                    info.overrides = true;
                    layout.methods[method.name] = info;
                }
            }
            if (!has_parent || (has_parent && !override_allowed)) {
                if (!has_parent) {
                    MethodInfo info;
                    info.def = &method;
                    info.owner = name;
                    info.slot = slot_count++;
                    layout.methods[method.name] = info;
                }
            }
        }
    }

    void build_method_table() {
        std::unordered_set<MethodDef *> seen;
        for (auto &kv : ctx.layouts) {
            for (auto &m : kv.second.methods) {
                MethodDef *def = m.second.def;
                if (!reachable_methods.empty() && !reachable_methods.count(def))
                    continue;
                if (seen.insert(def).second) {
                    std::string fn_name = "$" + m.second.owner + "$" + def->name;
                    method_func_index[def] = static_cast<int>(function_table.size());
                    function_table.push_back(fn_name);

                    std::vector<std::string> params;
                    params.push_back("i32");
                    for (auto &param : def->params)
                        params.push_back(wasm_type(param.type));
                    std::string result = wasm_type(def->return_type);
                    int type_index = add_type_signature(params, result);
                    method_type_index[def] = type_index;
                }
            }
        }
        for (auto &kv : ctx.layouts) {
            for (auto &m : kv.second.methods)
                max_slots = std::max(max_slots, m.second.slot + 1);
        }
    }

    std::string escape_bytes(const std::string &data) {
        std::ostringstream oss;
        for (size_t i = 0; i < data.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(data[i]);
            if (c == '\\' || c == '"') {
                oss << '\\' << c;
            } else if (c == '\n') {
                oss << "\\0a";
            } else if (c < 32 || c > 126) {
                const char *hex = "0123456789abcdef";
                oss << "\\";
                oss << hex[(c >> 4) & 0x0f];
                oss << hex[c & 0x0f];
            } else {
                oss << static_cast<char>(c);
            }
        }
        return oss.str();
    }

    void emit_data_segments() {
        int offset = 0;
        for (auto *str : string_list) {
            str->offset = offset;
            offset += static_cast<int>(str->value.size());
        }
        offset = (offset + 3) & ~3;

        const size_t layout_count = ctx.layouts.size();
        const size_t max_slots_size = static_cast<size_t>(max_slots);
        std::vector<int> vtable(layout_count * max_slots_size, 0);
        for (auto &kv : ctx.layouts) {
            int cid = kv.second.class_id;
            for (auto &m : kv.second.methods) {
                int slot = m.second.slot;
                size_t index = static_cast<size_t>(cid) * max_slots_size + static_cast<size_t>(slot);
                auto it = method_func_index.find(m.second.def);
                if (it != method_func_index.end())
                    vtable[index] = it->second;
                else
                    vtable[index] = 0;
            }
        }

        int vtable_offset = offset;
        offset += static_cast<int>(vtable.size() * 4);

        std::vector<int> inner_table(layout_count * layout_count * max_slots_size, 0);
        for (auto &current : ctx.layouts) {
            int current_id = current.second.class_id;
            for (auto &actual : ctx.layouts) {
                int actual_id = actual.second.class_id;
                std::vector<std::string> chain;
                std::string cur = actual.first;
                while (!cur.empty()) {
                    chain.push_back(cur);
                    cur = ctx.classes[cur].def->parent;
                }
                bool is_ancestor = false;
                for (auto &name : chain)
                    if (name == current.first)
                        is_ancestor = true;
                if (!is_ancestor)
                    continue;
                int current_pos = -1;
                for (size_t i = 0; i < chain.size(); ++i)
                    if (chain[i] == current.first)
                        current_pos = static_cast<int>(i);

                for (int slot = 0; slot < max_slots; ++slot) {
                    size_t current_func_index = static_cast<size_t>(current_id) * max_slots_size + static_cast<size_t>(slot);
                    int current_func = vtable[current_func_index];
                    int next_func = -1;
                    for (size_t i = static_cast<size_t>(current_pos + 1); i < chain.size(); ++i) {
                        int cid = ctx.layouts[chain[i]].class_id;
                        size_t func_index = static_cast<size_t>(cid) * max_slots_size + static_cast<size_t>(slot);
                        int func = vtable[func_index];
                        if (func != current_func) {
                            next_func = func;
                            break;
                        }
                    }
                    size_t idx = (static_cast<size_t>(current_id) * layout_count + static_cast<size_t>(actual_id)) * max_slots_size + static_cast<size_t>(slot);
                    inner_table[idx] = next_func + 1;
                }
            }
        }

        int inner_offset = offset;
        offset += static_cast<int>(inner_table.size() * 4);

        int scratch_offset = (offset + 7) & ~7;
        offset = scratch_offset + 256;
        int heap_offset = (offset + 7) & ~7;

        vtable_base = vtable_offset;
        inner_base = inner_offset;
        scratch_base = scratch_offset;
        heap_base = heap_offset;

        if (!string_list.empty()) {
            std::string blob;
            for (auto *str : string_list)
                blob += str->value;
            emit.line("(data (i32.const 0) \"" + escape_bytes(blob) + "\")");
        }

        if (!vtable.empty()) {
            std::string blob;
            blob.resize(vtable.size() * 4);
            for (size_t i = 0; i < vtable.size(); ++i) {
                int v = vtable[i];
                blob[i * 4 + 0] = static_cast<char>(v & 0xff);
                blob[i * 4 + 1] = static_cast<char>((v >> 8) & 0xff);
                blob[i * 4 + 2] = static_cast<char>((v >> 16) & 0xff);
                blob[i * 4 + 3] = static_cast<char>((v >> 24) & 0xff);
            }
            emit.line("(data (i32.const " + std::to_string(vtable_offset) + ") \"" + escape_bytes(blob) + "\")");
        }

        if (!inner_table.empty()) {
            std::string blob;
            blob.resize(inner_table.size() * 4);
            for (size_t i = 0; i < inner_table.size(); ++i) {
                int v = inner_table[i];
                blob[i * 4 + 0] = static_cast<char>(v & 0xff);
                blob[i * 4 + 1] = static_cast<char>((v >> 8) & 0xff);
                blob[i * 4 + 2] = static_cast<char>((v >> 16) & 0xff);
                blob[i * 4 + 3] = static_cast<char>((v >> 24) & 0xff);
            }
            emit.line("(data (i32.const " + std::to_string(inner_offset) + ") \"" + escape_bytes(blob) + "\")");
        }

        emit.line("(data (i32.const " + std::to_string(scratch_offset) + ") \"" + escape_bytes(std::string(256, '\0')) + "\")");
    }

    void emit_runtime_imports() {
        auto need = [&](const std::string &name)
        { return runtime_used.count(name) > 0; };
        if (need("fd_write"))
            emit.line("(import \"wasi_snapshot_preview1\" \"fd_write\" (func $fd_write (param i32 i32 i32 i32) (result i32)))");
        if (need("fd_read"))
            emit.line("(import \"wasi_snapshot_preview1\" \"fd_read\" (func $fd_read (param i32 i32 i32 i32) (result i32)))");
        if (need("fd_fdstat_get"))
            emit.line("(import \"wasi_snapshot_preview1\" \"fd_fdstat_get\" (func $fd_fdstat_get (param i32 i32) (result i32)))");
        if (need("args_sizes_get"))
            emit.line("(import \"wasi_snapshot_preview1\" \"args_sizes_get\" (func $args_sizes_get (param i32 i32) (result i32)))");
        if (need("args_get"))
            emit.line("(import \"wasi_snapshot_preview1\" \"args_get\" (func $args_get (param i32 i32) (result i32)))");
    }

    void emit_runtime_body() {
        auto need = [&](const std::string &name)
        { return runtime_used.count(name) > 0; };
        emit.line("(memory (export \"memory\") 1)");
        emit.line("(global $heap (mut i32) (i32.const 0))");
        emit.line("(global $vtable_base (mut i32) (i32.const 0))");
        emit.line("(global $inner_base (mut i32) (i32.const 0))");
        emit.line("(global $scratch (mut i32) (i32.const 0))");
        emit.line("(global $num_classes i32 (i32.const " + std::to_string(ctx.layouts.size()) + "))");
        if (need("malloc")) {
            emit.open("(func $malloc (param $size i32) (result i32)");
            emit.line("(local $new_heap i32)");
            emit.line("(local $mem_bytes i32)");
            emit.line("(local $grow_pages i32)");
            emit.line("local.get $size");
            emit.line("i32.const 7");
            emit.line("i32.add");
            emit.line("i32.const -8");
            emit.line("i32.and");
            emit.line("local.set $size");
            emit.line("global.get $heap");
            emit.line("local.get $size");
            emit.line("i32.add");
            emit.line("local.set $new_heap");
            emit.line("memory.size");
            emit.line("i32.const 65536");
            emit.line("i32.mul");
            emit.line("local.set $mem_bytes");
            emit.line("local.get $new_heap");
            emit.line("local.get $mem_bytes");
            emit.line("i32.gt_u");
            emit.open("if");
            emit.line("local.get $new_heap");
            emit.line("i32.const 65535");
            emit.line("i32.add");
            emit.line("i32.const -65536");
            emit.line("i32.and");
            emit.line("local.get $mem_bytes");
            emit.line("i32.sub");
            emit.line("i32.const 65536");
            emit.line("i32.div_u");
            emit.line("local.set $grow_pages");
            emit.line("local.get $grow_pages");
            emit.line("memory.grow");
            emit.line("drop");
            emit.close("end");
            emit.line("global.get $heap");
            emit.line("local.get $size");
            emit.line("i32.add");
            emit.line("global.set $heap");
            emit.line("global.get $heap");
            emit.line("local.get $size");
            emit.line("i32.sub");
            emit.close(")");
        }

        if (need("memcpy")) {
            emit.open("(func $memcpy (param $dst i32) (param $src i32) (param $len i32)");
            emit.line("(local $i i32)");
            emit.open("block $done");
            emit.open("loop $loop");
            emit.line("local.get $i");
            emit.line("local.get $len");
            emit.line("i32.ge_u");
            emit.line("br_if $done");
            emit.line("local.get $dst");
            emit.line("local.get $i");
            emit.line("i32.add");
            emit.line("local.get $src");
            emit.line("local.get $i");
            emit.line("i32.add");
            emit.line("i32.load8_u");
            emit.line("i32.store8");
            emit.line("local.get $i");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $i");
            emit.line("br $loop");
            emit.close("end");
            emit.close("end");
            emit.close(")");
        }

        if (need("strlen")) {
            emit.open("(func $strlen (param $ptr i32) (result i32)");
            emit.line("(local $len i32)");
            emit.open("block $done");
            emit.open("loop $loop");
            emit.line("local.get $ptr");
            emit.line("local.get $len");
            emit.line("i32.add");
            emit.line("i32.load8_u");
            emit.line("i32.eqz");
            emit.line("br_if $done");
            emit.line("local.get $len");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $len");
            emit.line("br $loop");
            emit.close("end");
            emit.close("end");
            emit.line("local.get $len");
            emit.close(")");
        }

        if (need("split_ws")) {
            emit.open("(func $split_ws (param $ptr i32) (param $len i32) (result i32)");
            emit.line("(local $i i32)");
            emit.line("(local $count i32)");
            emit.line("(local $in_token i32)");
            emit.line("(local $start i32)");
            emit.line("(local $arr i32)");
            emit.line("(local $idx i32)");
            emit.line("(local $tok_len i32)");
            emit.line("(local $str i32)");
            emit.open("block $count_done");
            emit.open("loop $count_loop");
            emit.line("local.get $i");
            emit.line("local.get $len");
            emit.line("i32.ge_u");
            emit.line("br_if $count_done");
            emit.line("local.get $ptr");
            emit.line("local.get $i");
            emit.line("i32.add");
            emit.line("i32.load8_u");
            emit.line("i32.const 32");
            emit.line("i32.gt_u");
            emit.open("if");
            emit.line("local.get $in_token");
            emit.line("i32.eqz");
            emit.open("if");
            emit.line("local.get $count");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $count");
            emit.line("i32.const 1");
            emit.line("local.set $in_token");
            emit.close("end");
            emit.close("else");
            emit.line("i32.const 0");
            emit.line("local.set $in_token");
            emit.close("end");
            emit.line("local.get $i");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $i");
            emit.line("br $count_loop");
            emit.close("end");
            emit.close("end");
            emit.line("local.get $count");
            emit.line("i64.extend_i32_u");
            emit.line("call $array_new");
            emit.line("local.set $arr");
            emit.line("i32.const 0");
            emit.line("local.set $i");
            emit.line("i32.const 0");
            emit.line("local.set $idx");
            emit.line("i32.const 0");
            emit.line("local.set $in_token");
            emit.open("block $fill_done");
            emit.open("loop $fill_loop");
            emit.line("local.get $i");
            emit.line("local.get $len");
            emit.line("i32.ge_u");
            emit.line("br_if $fill_done");
            emit.line("local.get $ptr");
            emit.line("local.get $i");
            emit.line("i32.add");
            emit.line("i32.load8_u");
            emit.line("i32.const 32");
            emit.line("i32.gt_u");
            emit.open("if");
            emit.line("local.get $in_token");
            emit.line("i32.eqz");
            emit.open("if");
            emit.line("local.get $i");
            emit.line("local.set $start");
            emit.line("i32.const 1");
            emit.line("local.set $in_token");
            emit.close("end");
            emit.close("else");
            emit.line("local.get $in_token");
            emit.line("i32.eqz");
            emit.open("if");
            // do nothing
            emit.close("else");
            emit.line("local.get $i");
            emit.line("local.get $start");
            emit.line("i32.sub");
            emit.line("local.set $tok_len");
            emit.line("local.get $ptr");
            emit.line("local.get $start");
            emit.line("i32.add");
            emit.line("local.get $tok_len");
            emit.line("call $string_new");
            emit.line("local.set $str");
            emit.line("local.get $arr");
            emit.line("local.get $idx");
            emit.line("i64.extend_i32_u");
            emit.line("local.get $str");
            emit.line("i64.extend_i32_u");
            emit.line("call $array_set_i64");
            emit.line("local.get $idx");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $idx");
            emit.line("i32.const 0");
            emit.line("local.set $in_token");
            emit.close("end");
            emit.close("end");
            emit.line("local.get $i");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $i");
            emit.line("br $fill_loop");
            emit.close("end");
            emit.close("end");
            emit.line("local.get $in_token");
            emit.line("i32.eqz");
            emit.open("if");
            // do nothing
            emit.close("else");
            emit.line("local.get $len");
            emit.line("local.get $start");
            emit.line("i32.sub");
            emit.line("local.set $tok_len");
            emit.line("local.get $ptr");
            emit.line("local.get $start");
            emit.line("i32.add");
            emit.line("local.get $tok_len");
            emit.line("call $string_new");
            emit.line("local.set $str");
            emit.line("local.get $arr");
            emit.line("local.get $idx");
            emit.line("i64.extend_i32_u");
            emit.line("local.get $str");
            emit.line("i64.extend_i32_u");
            emit.line("call $array_set_i64");
            emit.close("end");
            emit.line("local.get $arr");
            emit.close(")");
        }

        if (need("write_buf")) {
            emit.open("(func $write_buf (param $ptr i32) (param $len i32)");
            emit.line("global.get $scratch");
            emit.line("local.get $ptr");
            emit.line("i32.store");
            emit.line("global.get $scratch");
            emit.line("i32.const 4");
            emit.line("i32.add");
            emit.line("local.get $len");
            emit.line("i32.store");
            emit.line("i32.const 1");
            emit.line("global.get $scratch");
            emit.line("i32.const 1");
            emit.line("global.get $scratch");
            emit.line("i32.const 8");
            emit.line("i32.add");
            emit.line("call $fd_write");
            emit.line("drop");
            emit.close(")");
        }

        if (need("incref")) {
            emit.open("(func $incref (param $ptr i32)");
            emit.line("local.get $ptr");
            emit.line("i32.eqz");
            emit.open("if");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 8");
            emit.line("i32.add");
            emit.line("local.get $ptr");
            emit.line("i32.const 8");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i64.const 1");
            emit.line("i64.add");
            emit.line("i64.store");
            emit.close(")");
        }

        if (need("decref")) {
            emit.open("(func $decref (param $ptr i32)");
            emit.line("local.get $ptr");
            emit.line("i32.eqz");
            emit.open("if");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 8");
            emit.line("i32.add");
            emit.line("local.get $ptr");
            emit.line("i32.const 8");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i64.const 1");
            emit.line("i64.sub");
            emit.line("i64.store");
            emit.close(")");
        }

        if (need("print_literal")) {
            emit.open("(func $print_literal (param $ptr i32) (param $len i32)");
            emit.line("local.get $ptr");
            emit.line("local.get $len");
            emit.line("call $write_buf");
            emit.close(")");
        }

        if (need("print_newline")) {
            emit.open("(func $print_newline");
            emit.line("i32.const " + std::to_string(add_string_literal("\n")));
            emit.line("i32.const 1");
            emit.line("call $print_literal");
            emit.close(")");
        }

        if (need("itoa")) {
            emit.open("(func $itoa (param $value i64) (result i32) (result i32)");
            emit.line("(local $ptr i32)");
            emit.line("(local $len i32)");
            emit.line("(local $neg i32)");
            emit.line("global.get $scratch");
            emit.line("i32.const 128");
            emit.line("i32.add");
            emit.line("local.set $ptr");
            emit.line("local.get $value");
            emit.line("i64.const 0");
            emit.line("i64.lt_s");
            emit.open("if");
            emit.line("i32.const 1");
            emit.line("local.set $neg");
            emit.line("local.get $value");
            emit.line("i64.const -1");
            emit.line("i64.mul");
            emit.line("local.set $value");
            emit.close("end");
            emit.line("local.get $value");
            emit.line("i64.eqz");
            emit.open("if");
            emit.line("local.get $ptr");
            emit.line("i32.const 1");
            emit.line("i32.sub");
            emit.line("local.tee $ptr");
            emit.line("i32.const 48");
            emit.line("i32.store8");
            emit.line("i32.const 1");
            emit.line("local.set $len");
            emit.open("else");
            emit.open("loop $loop");
            emit.line("local.get $ptr");
            emit.line("i32.const 1");
            emit.line("i32.sub");
            emit.line("local.tee $ptr");
            emit.line("local.get $value");
            emit.line("i64.const 10");
            emit.line("i64.rem_u");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 48");
            emit.line("i32.add");
            emit.line("i32.store8");
            emit.line("local.get $len");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $len");
            emit.line("local.get $value");
            emit.line("i64.const 10");
            emit.line("i64.div_u");
            emit.line("local.set $value");
            emit.line("local.get $value");
            emit.line("i64.const 0");
            emit.line("i64.gt_u");
            emit.line("br_if $loop");
            emit.close("end");
            emit.close("end");
            emit.line("local.get $neg");
            emit.open("if");
            emit.line("local.get $ptr");
            emit.line("i32.const 1");
            emit.line("i32.sub");
            emit.line("local.tee $ptr");
            emit.line("i32.const 45");
            emit.line("i32.store8");
            emit.line("local.get $len");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $len");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("local.get $len");
            emit.close(")");
        }

        if (need("print_int")) {
            emit.open("(func $print_int (param $value i64)");
            emit.line("local.get $value");
            emit.line("call $itoa");
            emit.line("call $write_buf");
            emit.close(")");
        }

        if (need("print_real")) {
            emit.open("(func $print_real (param $value f64)");
            emit.line("local.get $value");
            emit.line("i64.trunc_f64_s");
            emit.line("call $print_int");
            emit.close(")");
        }

        if (need("print_bool")) {
            emit.open("(func $print_bool (param $value i64)");
            emit.line("local.get $value");
            emit.line("i64.eqz");
            emit.open("if");
            emit.line("i32.const " + std::to_string(add_string_literal("false")));
            emit.line("i32.const 5");
            emit.line("call $print_literal");
            emit.open("else");
            emit.line("i32.const " + std::to_string(add_string_literal("true")));
            emit.line("i32.const 4");
            emit.line("call $print_literal");
            emit.close("end");
            emit.close(")");
        }

        if (need("string_new")) {
            emit.open("(func $string_new (param $src i32) (param $len i32) (result i32)");
            emit.line("(local $ptr i32)");
            emit.line("(local $data i32)");
            emit.line("local.get $len");
            emit.line("call $malloc");
            emit.line("local.set $data");
            emit.line("local.get $data");
            emit.line("local.get $src");
            emit.line("local.get $len");
            emit.line("call $memcpy");
            emit.line("i32.const 32");
            emit.line("call $malloc");
            emit.line("local.set $ptr");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("local.get $len");
            emit.line("i64.extend_i32_u");
            emit.line("i64.store");
            emit.line("local.get $ptr");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("local.get $data");
            emit.line("i32.store");
            emit.line("local.get $ptr");
            emit.close(")");
        }

        if (need("print_string")) {
            emit.open("(func $print_string (param $ptr i32)");
            emit.line("local.get $ptr");
            emit.line("i32.eqz");
            emit.open("if");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i32.wrap_i64");
            emit.line("call $write_buf");
            emit.close(")");
        }

        if (need("string_length")) {
            emit.open("(func $string_length (param $ptr i32) (result i64)");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.close(")");
        }

        if (need("string_to_int")) {
            emit.open("(func $string_to_int (param $ptr i32) (result i64)");
            emit.line("(local $len i32)");
            emit.line("(local $idx i32)");
            emit.line("(local $data i32)");
            emit.line("(local $sign i64)");
            emit.line("(local $acc i64)");
            emit.line("(local $ch i32)");
            emit.line("local.get $ptr");
            emit.line("i32.eqz");
            emit.open("if");
            emit.line("i64.const 0");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i32.wrap_i64");
            emit.line("local.set $len");
            emit.line("local.get $ptr");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.set $data");
            emit.line("i64.const 1");
            emit.line("local.set $sign");
            emit.open("block $done");
            emit.open("loop $loop");
            emit.line("local.get $idx");
            emit.line("local.get $len");
            emit.line("i32.ge_u");
            emit.line("br_if $done");
            emit.line("local.get $data");
            emit.line("local.get $idx");
            emit.line("i32.add");
            emit.line("i32.load8_u");
            emit.line("local.set $ch");
            emit.line("local.get $ch");
            emit.line("i32.const 45");
            emit.line("i32.eq");
            emit.open("if");
            emit.line("local.get $idx");
            emit.line("i32.const 0");
            emit.line("i32.eq");
            emit.open("if");
            emit.line("i64.const -1");
            emit.line("local.set $sign");
            emit.line("local.get $idx");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $idx");
            emit.line("br $loop");
            emit.close("end");
            emit.close("end");
            emit.line("local.get $ch");
            emit.line("i32.const 48");
            emit.line("i32.lt_u");
            emit.line("local.get $ch");
            emit.line("i32.const 57");
            emit.line("i32.gt_u");
            emit.line("i32.or");
            emit.line("br_if $done");
            emit.line("local.get $acc");
            emit.line("i64.const 10");
            emit.line("i64.mul");
            emit.line("local.get $ch");
            emit.line("i32.const 48");
            emit.line("i32.sub");
            emit.line("i64.extend_i32_u");
            emit.line("i64.add");
            emit.line("local.set $acc");
            emit.line("local.get $idx");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $idx");
            emit.line("br $loop");
            emit.close("end");
            emit.close("end");
            emit.line("local.get $acc");
            emit.line("local.get $sign");
            emit.line("i64.mul");
            emit.close(")");
        }

        if (need("string_concat")) {
            emit.open("(func $string_concat (param $a i32) (param $b i32) (result i32)");
            emit.line("(local $len i32)");
            emit.line("(local $data i32)");
            emit.line("(local $ptr i32)");
            emit.line("local.get $a");
            emit.line("i32.eqz");
            emit.open("if");
            emit.line("local.get $b");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $b");
            emit.line("i32.eqz");
            emit.open("if");
            emit.line("local.get $a");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $a");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("local.get $b");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i64.add");
            emit.line("i32.wrap_i64");
            emit.line("local.set $len");
            emit.line("local.get $len");
            emit.line("call $malloc");
            emit.line("local.set $data");
            emit.line("local.get $data");
            emit.line("local.get $a");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $a");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i32.wrap_i64");
            emit.line("call $memcpy");
            emit.line("local.get $data");
            emit.line("local.get $a");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i32.wrap_i64");
            emit.line("i32.add");
            emit.line("local.get $b");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $b");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i32.wrap_i64");
            emit.line("call $memcpy");
            emit.line("i32.const 32");
            emit.line("call $malloc");
            emit.line("local.set $ptr");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("local.get $len");
            emit.line("i64.extend_i32_u");
            emit.line("i64.store");
            emit.line("local.get $ptr");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("local.get $data");
            emit.line("i32.store");
            emit.line("local.get $ptr");
            emit.close(")");
        }

        if (need("string_eq")) {
            emit.open("(func $string_eq (param $a i32) (param $b i32) (result i64)");
            emit.line("(local $len i32)");
            emit.line("(local $i i32)");
            emit.line("local.get $a");
            emit.line("local.get $b");
            emit.line("i32.eq");
            emit.open("if");
            emit.line("i64.const 1");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $a");
            emit.line("i32.eqz");
            emit.line("local.get $b");
            emit.line("i32.eqz");
            emit.line("i32.or");
            emit.open("if");
            emit.line("i64.const 0");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $a");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("local.get $b");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i64.ne");
            emit.open("if");
            emit.line("i64.const 0");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $a");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i32.wrap_i64");
            emit.line("local.set $len");
            emit.open("block $done");
            emit.open("loop $loop");
            emit.line("local.get $i");
            emit.line("local.get $len");
            emit.line("i32.ge_u");
            emit.line("br_if $done");
            emit.line("local.get $a");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $i");
            emit.line("i32.add");
            emit.line("i32.load8_u");
            emit.line("local.get $b");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $i");
            emit.line("i32.add");
            emit.line("i32.load8_u");
            emit.line("i32.ne");
            emit.open("if");
            emit.line("i64.const 0");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $i");
            emit.line("i32.const 1");
            emit.line("i32.add");
            emit.line("local.set $i");
            emit.line("br $loop");
            emit.close("end");
            emit.close("end");
            emit.line("i64.const 1");
            emit.close(")");
        }

        if (need("array_new")) {
            emit.open("(func $array_new (param $len i64) (result i32)");
            emit.line("(local $ptr i32)");
            emit.line("(local $data i32)");
            emit.line("local.get $len");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("call $malloc");
            emit.line("local.set $data");
            emit.line("i32.const 40");
            emit.line("call $malloc");
            emit.line("local.set $ptr");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("local.get $len");
            emit.line("i64.store");
            emit.line("local.get $ptr");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("local.get $len");
            emit.line("i64.store");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("local.get $data");
            emit.line("i32.store");
            emit.line("local.get $ptr");
            emit.close(")");
        }

        if (need("array_size")) {
            emit.open("(func $array_size (param $ptr i32) (result i64)");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.close(")");
        }

        if (need("array_get_i64")) {
            emit.open("(func $array_get_i64 (param $ptr i32) (param $idx i64) (result i64)");
            emit.line("local.get $idx");
            emit.line("i64.const 0");
            emit.line("i64.lt_s");
            emit.open("if");
            emit.line("i64.const -1");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("local.get $idx");
            emit.line("i64.le_s");
            emit.open("if");
            emit.line("i64.const -1");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $idx");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.close(")");
        }

        if (need("array_get_f64")) {
            emit.open("(func $array_get_f64 (param $ptr i32) (param $idx i64) (result f64)");
            emit.line("local.get $idx");
            emit.line("i64.const 0");
            emit.line("i64.lt_s");
            emit.open("if");
            emit.line("f64.const -1");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("local.get $idx");
            emit.line("i64.le_s");
            emit.open("if");
            emit.line("f64.const -1");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $idx");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("i32.add");
            emit.line("f64.load");
            emit.close(")");
        }

        if (need("array_get_ptr")) {
            emit.open("(func $array_get_ptr (param $ptr i32) (param $idx i64) (result i32)");
            emit.line("(local $val i64)");
            emit.line("local.get $ptr");
            emit.line("local.get $idx");
            emit.line("call $array_get_i64");
            emit.line("local.set $val");
            emit.line("local.get $val");
            emit.line("i64.const -1");
            emit.line("i64.eq");
            emit.open("if");
            emit.line("i32.const 0");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $val");
            emit.line("i32.wrap_i64");
            emit.close(")");
        }

        if (need("array_set_i64")) {
            emit.open("(func $array_set_i64 (param $ptr i32) (param $idx i64) (param $val i64)");
            emit.line("local.get $idx");
            emit.line("i64.const 0");
            emit.line("i64.lt_s");
            emit.open("if");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("local.get $idx");
            emit.line("i64.le_s");
            emit.open("if");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $idx");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("i32.add");
            emit.line("local.get $val");
            emit.line("i64.store");
            emit.close(")");
        }

        if (need("array_set_f64")) {
            emit.open("(func $array_set_f64 (param $ptr i32) (param $idx i64) (param $val f64)");
            emit.line("local.get $idx");
            emit.line("i64.const 0");
            emit.line("i64.lt_s");
            emit.open("if");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("local.get $idx");
            emit.line("i64.le_s");
            emit.open("if");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $idx");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("i32.add");
            emit.line("local.get $val");
            emit.line("f64.store");
            emit.close(")");
        }

        if (need("array_push_i64")) {
            emit.open("(func $array_push_i64 (param $ptr i32) (param $val i64)");
            emit.line("(local $len i64)");
            emit.line("(local $cap i64)");
            emit.line("(local $data i32)");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("local.set $len");
            emit.line("local.get $ptr");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("local.set $cap");
            emit.line("local.get $len");
            emit.line("local.get $cap");
            emit.line("i64.eq");
            emit.open("if");
            emit.line("local.get $cap");
            emit.line("i64.eqz");
            emit.open("if");
            emit.line("i64.const 4");
            emit.line("local.set $cap");
            emit.open("else");
            emit.line("local.get $cap");
            emit.line("i64.const 2");
            emit.line("i64.mul");
            emit.line("local.set $cap");
            emit.close("end");
            emit.line("local.get $cap");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("call $malloc");
            emit.line("local.set $data");
            emit.line("local.get $data");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $len");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("call $memcpy");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("local.get $data");
            emit.line("i32.store");
            emit.line("local.get $ptr");
            emit.line("i32.const 24");
            emit.line("i32.add");
            emit.line("local.get $cap");
            emit.line("i64.store");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $len");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("i32.add");
            emit.line("local.get $val");
            emit.line("i64.store");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("local.get $len");
            emit.line("i64.const 1");
            emit.line("i64.add");
            emit.line("i64.store");
            emit.close(")");
        }

        if (need("array_push_f64")) {
            emit.open("(func $array_push_f64 (param $ptr i32) (param $val f64)");
            emit.line("local.get $ptr");
            emit.line("local.get $val");
            emit.line("i64.reinterpret_f64");
            emit.line("call $array_push_i64");
            emit.close(")");
        }

        if (need("array_pop_i64")) {
            emit.open("(func $array_pop_i64 (param $ptr i32) (result i64)");
            emit.line("(local $len i64)");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i64.eqz");
            emit.open("if");
            emit.line("i64.const -1");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.line("i64.const 1");
            emit.line("i64.sub");
            emit.line("local.set $len");
            emit.line("local.get $ptr");
            emit.line("i32.const 16");
            emit.line("i32.add");
            emit.line("local.get $len");
            emit.line("i64.store");
            emit.line("local.get $ptr");
            emit.line("i32.const 32");
            emit.line("i32.add");
            emit.line("i32.load");
            emit.line("local.get $len");
            emit.line("i32.wrap_i64");
            emit.line("i32.const 8");
            emit.line("i32.mul");
            emit.line("i32.add");
            emit.line("i64.load");
            emit.close(")");
        }

        if (need("array_pop_f64")) {
            emit.open("(func $array_pop_f64 (param $ptr i32) (result f64)");
            emit.line("local.get $ptr");
            emit.line("call $array_pop_i64");
            emit.line("f64.reinterpret_i64");
            emit.close(")");
        }

        if (need("array_pop_ptr")) {
            emit.open("(func $array_pop_ptr (param $ptr i32) (result i32)");
            emit.line("(local $val i64)");
            emit.line("local.get $ptr");
            emit.line("call $array_pop_i64");
            emit.line("local.set $val");
            emit.line("local.get $val");
            emit.line("i64.const -1");
            emit.line("i64.eq");
            emit.open("if");
            emit.line("i32.const 0");
            emit.line("return");
            emit.close("end");
            emit.line("local.get $val");
            emit.line("i32.wrap_i64");
            emit.close(")");
        }
    }

    void emit_function(FunctionDef &fn) {
        FunctionContext fctx;
        fctx.name = fn.name;
        fctx.return_type = fn.return_type;

        std::vector<std::string> params;
        for (auto &param : fn.params) {
            params.push_back(wasm_type(param.type));
            fctx.locals[param.name] = {param.type, "$p" + std::to_string(fctx.param_count++)};
        }
        int type_index = add_type_signature(params, wasm_type(fn.return_type));
        function_type_index[&fn] = type_index;

        std::ostringstream header;
        header << "(func $" << fn.name;
        for (size_t i = 0; i < params.size(); ++i) {
            header << " (param " << fctx.locals[fn.params[i].name].name << " " << params[i] << ")";
        }
        if (!wasm_type(fn.return_type).empty())
            header << " (result " << wasm_type(fn.return_type) << ")";
        emit.open(header.str());

        collect_locals(fn.body, fctx);
        emit_local_decls(fctx);
        emit_statements(fn.body, fctx);
        if (fn.return_type->kind == Type::Kind::Void) {
            emit.line("return");
        } else if (fn.return_type->kind == Type::Kind::Real) {
            emit.line("f64.const 0");
            emit.line("return");
        } else if (fn.return_type->is_ref()) {
            emit.line("i32.const 0");
            emit.line("return");
        } else {
            emit.line("i64.const 0");
            emit.line("return");
        }
        emit.close(")");
    }

    void emit_method(const std::string &class_name, MethodDef &method) {
        FunctionContext fctx;
        fctx.name = class_name + "$" + method.name;
        fctx.class_name = class_name;
        fctx.return_type = method.return_type;

        std::ostringstream header;
        header << "(func $" << class_name << "$" << method.name;
        header << " (param $p0 i32)";
        fctx.locals["this"] = {Type::make_class(class_name), "$p0"};
        fctx.param_count++;
        for (size_t i = 0; i < method.params.size(); ++i) {
            std::string pname = "$p" + std::to_string(fctx.param_count);
            header << " (param " << pname << " " << wasm_type(method.params[i].type) << ")";
            fctx.locals[method.params[i].name] = {method.params[i].type, pname};
            fctx.param_count++;
        }
        if (!wasm_type(method.return_type).empty())
            header << " (result " << wasm_type(method.return_type) << ")";
        emit.open(header.str());

        collect_locals(method.body, fctx);
        setup_field_cache(fctx, method.body);
        emit_local_decls(fctx);
        emit_field_cache_init(fctx);
        emit_statements(method.body, fctx);
        if (method.return_type->kind == Type::Kind::Void) {
            emit.line("return");
        } else if (method.return_type->kind == Type::Kind::Real) {
            emit.line("f64.const 0");
            emit.line("return");
        } else if (method.return_type->is_ref()) {
            emit.line("i32.const 0");
            emit.line("return");
        } else {
            emit.line("i64.const 0");
            emit.line("return");
        }
        emit.close(")");
    }

    void collect_locals(const std::vector<std::unique_ptr<Statement>> &stmts, FunctionContext &fctx) {
        for (auto &stmt : stmts) {
            if (auto var = dynamic_cast<VarDeclStmt *>(stmt.get())) {
                if (!fctx.locals.count(var->name)) {
                    std::string name = "$l" + std::to_string(fctx.local_count++);
                    fctx.locals[var->name] = {var->type, name};
                    fctx.local_types.push_back(var->type);
                }
            }
            if (auto iff = dynamic_cast<IfStmt *>(stmt.get())) {
                collect_locals(iff->then_body, fctx);
                collect_locals(iff->else_body, fctx);
            }
            if (auto wh = dynamic_cast<WhileStmt *>(stmt.get()))
                collect_locals(wh->body, fctx);
        }
    }

    void emit_local_decls(FunctionContext &fctx) {
        for (const auto &local : fctx.extra_locals) {
            emit.line("(local " + local.name + " " + wasm_type(local.type) + ")");
        }
        for (size_t i = 0; i < fctx.local_types.size(); ++i) {
            emit.line("(local $l" + std::to_string(i) + " " + wasm_type(fctx.local_types[i]) + ")");
        }
        emit.line("(local " + fctx.temp_i32 + " i32)");
        emit.line("(local " + fctx.temp_i64 + " i64)");
        emit.line("(local " + fctx.temp_i32_alt + " i32)");
        emit.line("(local " + fctx.temp_f64 + " f64)");
    }

    void emit_statements(const std::vector<std::unique_ptr<Statement>> &stmts, FunctionContext &fctx) {
        for (auto &stmt : stmts)
            emit_statement(stmt.get(), fctx);
    }

    void emit_statement(Statement *stmt, FunctionContext &fctx) {
        if (auto var = dynamic_cast<VarDeclStmt *>(stmt)) {
            if (var->init) {
                emit_expr(var->init.get(), fctx, var->type);
                store_local(fctx, var->name);
                if (var->type->kind == Type::Kind::String) {
                    auto lit = eval_string_literal(var->init.get(), fctx);
                    if (lit)
                        fctx.const_strings[var->name] = *lit;
                    else
                        fctx.const_strings.erase(var->name);
                }
            }
            return;
        }
        if (auto asg = dynamic_cast<AssignStmt *>(stmt)) {
            emit_assignment(asg->target.get(), asg->value.get(), fctx);
            return;
        }
        if (auto expr = dynamic_cast<ExprStmt *>(stmt)) {
            emit_expr(expr->expr.get(), fctx, expr->expr->type);
            if (expr->expr->type->kind != Type::Kind::Void)
                emit.line("drop");
            return;
        }
        if (auto ret = dynamic_cast<ReturnStmt *>(stmt)) {
            if (ret->value) {
                emit_expr(ret->value.get(), fctx, fctx.return_type);
            } else if (fctx.return_type && fctx.return_type->kind != Type::Kind::Void) {
                if (fctx.return_type->kind == Type::Kind::Real)
                    emit.line("f64.const 0");
                else if (fctx.return_type->is_ref())
                    emit.line("i32.const 0");
                else
                    emit.line("i64.const 0");
            }
            emit.line("return");
            return;
        }
        if (auto iff = dynamic_cast<IfStmt *>(stmt)) {
            auto before_consts = fctx.const_strings;
            emit_expr(iff->cond.get(), fctx, Type::make(Type::Kind::Bool));
            emit.line("i64.const 0");
            emit.line("i64.ne");
            emit.open("if");
            fctx.const_strings = before_consts;
            emit_statements(iff->then_body, fctx);
            auto then_consts = fctx.const_strings;
            if (!iff->else_body.empty()) {
                emit.open("else");
                fctx.const_strings = before_consts;
                emit_statements(iff->else_body, fctx);
                emit.close("end");
            } else {
                emit.close("end");
            }
            if (!iff->else_body.empty()) {
                std::unordered_map<std::string, std::string> merged;
                for (auto &kv : then_consts) {
                    auto it = fctx.const_strings.find(kv.first);
                    if (it != fctx.const_strings.end() && it->second == kv.second) {
                        merged[kv.first] = kv.second;
                    }
                }
                fctx.const_strings = std::move(merged);
            } else {
                fctx.const_strings = std::move(before_consts);
                for (auto &kv : then_consts) {
                    auto it = fctx.const_strings.find(kv.first);
                    if (it == fctx.const_strings.end() || it->second != kv.second) {
                        fctx.const_strings.erase(kv.first);
                    }
                }
            }
            return;
        }
        if (auto wh = dynamic_cast<WhileStmt *>(stmt)) {
            auto before_consts = fctx.const_strings;
            std::string loop = next_label("loop");
            std::string block = next_label("block");
            emit.open("block $" + block);
            emit.open("loop $" + loop);
            emit_expr(wh->cond.get(), fctx, Type::make(Type::Kind::Bool));
            emit.line("i64.eqz");
            emit.line("br_if $" + block);
            fctx.const_strings = before_consts;
            emit_statements(wh->body, fctx);
            auto body_consts = fctx.const_strings;
            fctx.const_strings = std::move(before_consts);
            for (auto &kv : body_consts) {
                auto it = fctx.const_strings.find(kv.first);
                if (it == fctx.const_strings.end() || it->second != kv.second) {
                    fctx.const_strings.erase(kv.first);
                }
            }
            emit.line("br $" + loop);
            emit.close("end");
            emit.close("end");
            return;
        }
        if (dynamic_cast<InnerStmt *>(stmt)) {
            emit_inner_call(fctx);
        }
    }

    void emit_inner_call(FunctionContext &fctx) {
        if (fctx.class_name.empty())
            return;
        auto &layout = ctx.layouts[fctx.class_name];
        std::string method_name = fctx.name.substr(fctx.class_name.size() + 1);
        auto it = layout.methods.find(method_name);
        if (it == layout.methods.end())
            return;
        int slot = it->second.slot;
        emit.line("local.get $p0");
        emit.line("i32.load");
        emit.line("local.set " + fctx.temp_i32);
        emit.line("i32.const " + std::to_string(layout.class_id));
        emit.line("i32.const " + std::to_string(static_cast<int>(ctx.layouts.size())));
        emit.line("i32.mul");
        emit.line("local.get " + fctx.temp_i32);
        emit.line("i32.add");
        emit.line("i32.const " + std::to_string(max_slots * 4));
        emit.line("i32.mul");
        emit.line("i32.const " + std::to_string(slot * 4));
        emit.line("i32.add");
        emit.line("global.get $inner_base");
        emit.line("i32.add");
        emit.line("i32.load");
        emit.line("local.set " + fctx.temp_i32);
        emit.line("local.get " + fctx.temp_i32);
        emit.open("if");
        emit.line("local.get $p0");
        for (size_t i = 0; i < it->second.def->params.size(); ++i)
            emit.line("local.get $p" + std::to_string(i + 1));
        emit.line("local.get " + fctx.temp_i32);
        emit.line("i32.const 1");
        emit.line("i32.sub");
        emit.line("call_indirect (type $t" + std::to_string(method_type_index[it->second.def]) + ")");
        if (it->second.def->return_type->kind != Type::Kind::Void)
            emit.line("drop");
        emit.close("end");
    }

    void emit_assignment(Expr *target, Expr *value, FunctionContext &fctx) {
        if (auto ident = dynamic_cast<Identifier *>(target)) {
            if (fctx.locals.count(ident->name)) {
                emit_expr(value, fctx, ident->type);
                store_local(fctx, ident->name);
                if (ident->type->kind == Type::Kind::String) {
                    auto lit = eval_string_literal(value, fctx);
                    if (lit)
                        fctx.const_strings[ident->name] = *lit;
                    else
                        fctx.const_strings.erase(ident->name);
                }
            } else if (!fctx.class_name.empty()) {
                emit.line("local.get $p0");
                int offset = ctx.layouts[fctx.class_name].field_offsets[ident->name];
                emit.line("i32.const " + std::to_string(offset));
                emit.line("i32.add");
                emit.line("local.set " + fctx.temp_i32);
                emit.line("local.get " + fctx.temp_i32);
                emit_expr(value, fctx, ident->type);
                emit_store(ident->type);
            }
            return;
        }
        if (auto mem = dynamic_cast<MemberExpr *>(target)) {
            emit_expr(mem->object.get(), fctx, mem->object->type);
            int offset = ctx.layouts[mem->object->type->name].field_offsets[mem->member];
            emit.line("i32.const " + std::to_string(offset));
            emit.line("i32.add");
            emit.line("local.set " + fctx.temp_i32);
            emit.line("local.get " + fctx.temp_i32);
            emit_expr(value, fctx, mem->type);
            emit_store(mem->type);
            return;
        }
        if (auto idx = dynamic_cast<IndexExpr *>(target)) {
            emit_expr(idx->array.get(), fctx, idx->array->type);
            emit_expr(idx->index.get(), fctx, Type::make(Type::Kind::Int));
            emit_expr(value, fctx, idx->type);
            if (idx->type->kind == Type::Kind::Real) {
                use_runtime("array_set_f64");
                emit.line("call $array_set_f64");
            } else {
                if (idx->type->is_ref())
                    emit.line("i64.extend_i32_u");
                use_runtime("array_set_i64");
                emit.line("call $array_set_i64");
            }
            return;
        }
    }

    void emit_expr(Expr *expr, FunctionContext &fctx, const TypePtr &expected) {
        if (auto lit = dynamic_cast<IntLiteral *>(expr)) {
            emit.line("i64.const " + std::to_string(lit->value));
            return;
        }
        if (auto lit = dynamic_cast<RealLiteral *>(expr)) {
            emit.line("f64.const " + std::to_string(lit->value));
            return;
        }
        if (auto lit = dynamic_cast<StringLiteral *>(expr)) {
            int offset = add_string_literal(lit->value);
            use_runtime("string_new");
            emit.line("i32.const " + std::to_string(offset));
            emit.line("i32.const " + std::to_string(lit->value.size()));
            emit.line("call $string_new");
            return;
        }
        if (auto lit = dynamic_cast<BoolLiteral *>(expr)) {
            emit.line(std::string("i64.const ") + (lit->value ? "1" : "0"));
            return;
        }
        if (auto lit = dynamic_cast<CharLiteral *>(expr)) {
            emit.line("i64.const " + std::to_string(static_cast<int>(lit->value)));
            return;
        }
        if (auto unary = dynamic_cast<UnaryExpr *>(expr)) {
            if (auto lit = dynamic_cast<IntLiteral *>(unary->expr.get())) {
                if (unary->op == "-") {
                    emit.line("i64.const " + std::to_string(-lit->value));
                    return;
                }
            }
            if (auto lit = dynamic_cast<RealLiteral *>(unary->expr.get())) {
                if (unary->op == "-") {
                    emit.line("f64.const " + std::to_string(-lit->value));
                    return;
                }
            }
            if (auto lit = dynamic_cast<BoolLiteral *>(unary->expr.get())) {
                if (unary->op == "!") {
                    emit.line(std::string("i64.const ") + (lit->value ? "0" : "1"));
                    return;
                }
            }
        }
        if (dynamic_cast<NullLiteral *>(expr)) {
            emit.line("i32.const 0");
            return;
        }
        if (auto ident = dynamic_cast<Identifier *>(expr)) {
            if (fctx.locals.count(ident->name)) {
                load_local(fctx, ident->name);
            } else if (fctx.field_alias.count(ident->name)) {
                emit.line("local.get " + fctx.field_alias[ident->name].name);
            } else if (!fctx.class_name.empty()) {
                emit.line("local.get $p0");
                int offset = ctx.layouts[fctx.class_name].field_offsets[ident->name];
                emit.line("i32.const " + std::to_string(offset));
                emit.line("i32.add");
                emit_load(ident->type);
            }
            return;
        }
        if (dynamic_cast<ThisExpr *>(expr)) {
            emit.line("local.get $p0");
            return;
        }
        if (auto unary = dynamic_cast<UnaryExpr *>(expr)) {
            emit_expr(unary->expr.get(), fctx, unary->expr->type);
            if (unary->op == "-") {
                if (unary->expr->type->kind == Type::Kind::Real)
                    emit.line("f64.neg");
                else {
                    emit.line("i64.const -1");
                    emit.line("i64.mul");
                }
            } else if (unary->op == "!") {
                emit.line("i64.const 0");
                emit.line("i64.eq");
                emit.line("i64.extend_i32_u");
            }
            return;
        }
        if (auto bin = dynamic_cast<BinaryExpr *>(expr)) {
            if (auto l = dynamic_cast<IntLiteral *>(bin->left.get())) {
                if (auto r = dynamic_cast<IntLiteral *>(bin->right.get())) {
                    if (bin->op == "+") {
                        emit.line("i64.const " + std::to_string(l->value + r->value));
                        return;
                    }
                    if (bin->op == "-") {
                        emit.line("i64.const " + std::to_string(l->value - r->value));
                        return;
                    }
                    if (bin->op == "*") {
                        emit.line("i64.const " + std::to_string(l->value * r->value));
                        return;
                    }
                    if (bin->op == "/" && r->value != 0) {
                        emit.line("i64.const " + std::to_string(l->value / r->value));
                        return;
                    }
                    if (bin->op == "%" && r->value != 0) {
                        emit.line("i64.const " + std::to_string(l->value % r->value));
                        return;
                    }
                    if (bin->op == "==") {
                        emit.line(std::string("i64.const ") + (l->value == r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "!=") {
                        emit.line(std::string("i64.const ") + (l->value != r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "<") {
                        emit.line(std::string("i64.const ") + (l->value < r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "<=") {
                        emit.line(std::string("i64.const ") + (l->value <= r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == ">") {
                        emit.line(std::string("i64.const ") + (l->value > r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == ">=") {
                        emit.line(std::string("i64.const ") + (l->value >= r->value ? "1" : "0"));
                        return;
                    }
                }
            }
            if (auto l = dynamic_cast<RealLiteral *>(bin->left.get())) {
                if (auto r = dynamic_cast<RealLiteral *>(bin->right.get())) {
                    if (bin->op == "+") {
                        emit.line("f64.const " + std::to_string(l->value + r->value));
                        return;
                    }
                    if (bin->op == "-") {
                        emit.line("f64.const " + std::to_string(l->value - r->value));
                        return;
                    }
                    if (bin->op == "*") {
                        emit.line("f64.const " + std::to_string(l->value * r->value));
                        return;
                    }
                    if (bin->op == "/" && r->value != 0.0) {
                        emit.line("f64.const " + std::to_string(l->value / r->value));
                        return;
                    }
                    if (bin->op == "==") {
                        emit.line(std::string("i64.const ") + (l->value == r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "!=") {
                        emit.line(std::string("i64.const ") + (l->value != r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "<") {
                        emit.line(std::string("i64.const ") + (l->value < r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "<=") {
                        emit.line(std::string("i64.const ") + (l->value <= r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == ">") {
                        emit.line(std::string("i64.const ") + (l->value > r->value ? "1" : "0"));
                        return;
                    }
                    if (bin->op == ">=") {
                        emit.line(std::string("i64.const ") + (l->value >= r->value ? "1" : "0"));
                        return;
                    }
                }
            }
            if (auto l = dynamic_cast<BoolLiteral *>(bin->left.get())) {
                if (auto r = dynamic_cast<BoolLiteral *>(bin->right.get())) {
                    if (bin->op == "&&") {
                        emit.line(std::string("i64.const ") + ((l->value && r->value) ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "||") {
                        emit.line(std::string("i64.const ") + ((l->value || r->value) ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "==") {
                        emit.line(std::string("i64.const ") + ((l->value == r->value) ? "1" : "0"));
                        return;
                    }
                    if (bin->op == "!=") {
                        emit.line(std::string("i64.const ") + ((l->value != r->value) ? "1" : "0"));
                        return;
                    }
                }
            }
            if (bin->op == "&&" || bin->op == "||") {
                emit_expr(bin->left.get(), fctx, bin->left->type);
                emit.line("i64.const 0");
                emit.line("i64.ne");
                emit.line("i64.extend_i32_u");
                emit.line("local.set " + fctx.temp_i64);
                emit_expr(bin->right.get(), fctx, bin->right->type);
                emit.line("i64.const 0");
                emit.line("i64.ne");
                emit.line("i64.extend_i32_u");
                emit.line("local.get " + fctx.temp_i64);
                if (bin->op == "&&")
                    emit.line("i64.and");
                else
                    emit.line("i64.or");
                return;
            }
            emit_expr(bin->left.get(), fctx, bin->left->type);
            emit_expr(bin->right.get(), fctx, bin->right->type);
            if (bin->op == "+" && bin->left->type->kind == Type::Kind::String) {
                use_runtime("string_concat");
                emit.line("call $string_concat");
                return;
            }
            if ((bin->op == "==" || bin->op == "!=") &&
                (bin->left->type->is_ref() || bin->right->type->is_ref())) {
                if (bin->left->type->kind == Type::Kind::String || bin->right->type->kind == Type::Kind::String) {
                    use_runtime("string_eq");
                    emit.line("call $string_eq");
                    if (bin->op == "!=") {
                        emit.line("i64.eqz");
                        emit.line("i64.extend_i32_u");
                    }
                } else {
                    if (bin->op == "==")
                        emit.line("i32.eq");
                    else
                        emit.line("i32.ne");
                    emit.line("i64.extend_i32_u");
                }
                return;
            }
            if (bin->left->type->kind == Type::Kind::Real || bin->right->type->kind == Type::Kind::Real) {
                if (bin->op == "+")
                    emit.line("f64.add");
                else if (bin->op == "-")
                    emit.line("f64.sub");
                else if (bin->op == "*")
                    emit.line("f64.mul");
                else if (bin->op == "/")
                    emit.line("f64.div");
                else if (bin->op == "==") {
                    emit.line("f64.eq");
                    emit.line("i64.extend_i32_u");
                } else if (bin->op == "!=") {
                    emit.line("f64.ne");
                    emit.line("i64.extend_i32_u");
                } else if (bin->op == "<") {
                    emit.line("f64.lt");
                    emit.line("i64.extend_i32_u");
                } else if (bin->op == "<=") {
                    emit.line("f64.le");
                    emit.line("i64.extend_i32_u");
                } else if (bin->op == ">") {
                    emit.line("f64.gt");
                    emit.line("i64.extend_i32_u");
                } else if (bin->op == ">=") {
                    emit.line("f64.ge");
                    emit.line("i64.extend_i32_u");
                }
                return;
            }
            if (bin->op == "+")
                emit.line("i64.add");
            else if (bin->op == "-")
                emit.line("i64.sub");
            else if (bin->op == "*")
                emit.line("i64.mul");
            else if (bin->op == "/")
                emit.line("i64.div_s");
            else if (bin->op == "%")
                emit.line("i64.rem_s");
            else if (bin->op == "==") {
                if (bin->left->type->kind == Type::Kind::String) {
                    use_runtime("string_eq");
                    emit.line("call $string_eq");
                } else {
                    emit.line("i64.eq");
                    emit.line("i64.extend_i32_u");
                }
            } else if (bin->op == "!=") {
                if (bin->left->type->kind == Type::Kind::String) {
                    use_runtime("string_eq");
                    emit.line("call $string_eq");
                    emit.line("i64.eqz");
                    emit.line("i64.extend_i32_u");
                } else {
                    emit.line("i64.ne");
                    emit.line("i64.extend_i32_u");
                }
            } else if (bin->op == "<") {
                emit.line("i64.lt_s");
                emit.line("i64.extend_i32_u");
            } else if (bin->op == "<=") {
                emit.line("i64.le_s");
                emit.line("i64.extend_i32_u");
            } else if (bin->op == ">") {
                emit.line("i64.gt_s");
                emit.line("i64.extend_i32_u");
            } else if (bin->op == ">=") {
                emit.line("i64.ge_s");
                emit.line("i64.extend_i32_u");
            }
            return;
        }
        if (auto mem = dynamic_cast<MemberExpr *>(expr)) {
            bool is_this = dynamic_cast<ThisExpr *>(mem->object.get()) != nullptr;
            if (!is_this) {
                if (auto obj_ident = dynamic_cast<Identifier *>(mem->object.get())) {
                    is_this = obj_ident->name == "this";
                }
            }
            if (is_this && fctx.field_alias.count(mem->member)) {
                emit.line("local.get " + fctx.field_alias[mem->member].name);
            } else {
                emit_expr(mem->object.get(), fctx, mem->object->type);
                int offset = ctx.layouts[mem->object->type->name].field_offsets[mem->member];
                emit.line("i32.const " + std::to_string(offset));
                emit.line("i32.add");
                emit_load(mem->type);
            }
            return;
        }
        if (auto idx = dynamic_cast<IndexExpr *>(expr)) {
            if (auto ident = dynamic_cast<Identifier *>(idx->array.get())) {
                if (is_type_identifier(ident->name)) {
                    emit_expr(idx->index.get(), fctx, Type::make(Type::Kind::Int));
                    use_runtime("array_new");
                    emit.line("call $array_new");
                    return;
                }
            }
            emit_expr(idx->array.get(), fctx, idx->array->type);
            emit_expr(idx->index.get(), fctx, Type::make(Type::Kind::Int));
            if (expected && expected->kind == Type::Kind::Real) {
                use_runtime("array_get_f64");
                emit.line("call $array_get_f64");
            } else if (expected && expected->is_ref()) {
                use_runtime("array_get_ptr");
                emit.line("call $array_get_ptr");
            } else {
                use_runtime("array_get_i64");
                emit.line("call $array_get_i64");
            }
            return;
        }
        if (auto arr = dynamic_cast<ArrayLiteral *>(expr)) {
            emit.line("i64.const " + std::to_string(arr->elements.size()));
            use_runtime("array_new");
            emit.line("call $array_new");
            emit.line("local.set " + fctx.temp_i32_alt);
            for (size_t i = 0; i < arr->elements.size(); ++i) {
                emit_expr(arr->elements[i].get(), fctx, arr->elements[i]->type);
                if (arr->elements[i]->type->kind == Type::Kind::Real) {
                    emit.line("local.set " + fctx.temp_f64);
                    emit.line("local.get " + fctx.temp_i32_alt);
                    emit.line("i64.const " + std::to_string(i));
                    emit.line("local.get " + fctx.temp_f64);
                    use_runtime("array_set_f64");
                    emit.line("call $array_set_f64");
                } else {
                    if (arr->elements[i]->type->is_ref())
                        emit.line("i64.extend_i32_u");
                    emit.line("local.set " + fctx.temp_i64);
                    emit.line("local.get " + fctx.temp_i32_alt);
                    emit.line("i64.const " + std::to_string(i));
                    emit.line("local.get " + fctx.temp_i64);
                    use_runtime("array_set_i64");
                    emit.line("call $array_set_i64");
                }
            }
            emit.line("local.get " + fctx.temp_i32_alt);
            return;
        }
        if (auto call = dynamic_cast<CallExpr *>(expr)) {
            if (auto callee_ident = dynamic_cast<Identifier *>(call->callee.get())) {
                if (ctx.classes.count(callee_ident->name)) {
                    emit_new_object(callee_ident->name, call->args, fctx);
                    return;
                }
                if (ctx.functions.count(callee_ident->name)) {
                    for (auto &arg : call->args)
                        emit_expr(arg.get(), fctx, arg->type);
                    emit.line("call $" + callee_ident->name);
                    return;
                }
            }
            if (auto callee_mem = dynamic_cast<MemberExpr *>(call->callee.get())) {
                if (callee_mem->member == "new") {
                    auto ident = dynamic_cast<Identifier *>(callee_mem->object.get());
                    if (ident) {
                        emit_new_object(ident->name, call->args, fctx);
                        return;
                    }
                }
                if (callee_mem->member == "print" || callee_mem->member == "println") {
                    emit_print_call(callee_mem->object.get(), call->args, callee_mem->member == "println", fctx);
                    return;
                }
                auto obj_type = callee_mem->object->type;
                if (obj_type->kind == Type::Kind::String && callee_mem->member == "length") {
                    emit_expr(callee_mem->object.get(), fctx, obj_type);
                    use_runtime("string_length");
                    emit.line("call $string_length");
                    return;
                }
                if (obj_type->kind == Type::Kind::Array) {
                    emit_expr(callee_mem->object.get(), fctx, obj_type);
                    if (callee_mem->member == "size" || callee_mem->member == "count") {
                        use_runtime("array_size");
                        emit.line("call $array_size");
                        return;
                    }
                    if (callee_mem->member == "push") {
                        emit_expr(call->args[0].get(), fctx, call->args[0]->type);
                        if (call->args[0]->type->kind == Type::Kind::Real) {
                            use_runtime("array_push_f64");
                            emit.line("call $array_push_f64");
                        } else {
                            if (call->args[0]->type->is_ref())
                                emit.line("i64.extend_i32_u");
                            use_runtime("array_push_i64");
                            emit.line("call $array_push_i64");
                        }
                        return;
                    }
                    if (callee_mem->member == "pop") {
                        if (call->type->kind == Type::Kind::Real) {
                            use_runtime("array_pop_f64");
                            emit.line("call $array_pop_f64");
                        } else if (call->type->is_ref()) {
                            use_runtime("array_pop_ptr");
                            emit.line("call $array_pop_ptr");
                        } else {
                            use_runtime("array_pop_i64");
                            emit.line("call $array_pop_i64");
                        }
                        return;
                    }
                    if (callee_mem->member == "get") {
                        emit_expr(call->args[0].get(), fctx, Type::make(Type::Kind::Int));
                        if (call->type->kind == Type::Kind::Real) {
                            use_runtime("array_get_f64");
                            emit.line("call $array_get_f64");
                        } else if (call->type->is_ref()) {
                            use_runtime("array_get_ptr");
                            emit.line("call $array_get_ptr");
                        } else {
                            use_runtime("array_get_i64");
                            emit.line("call $array_get_i64");
                        }
                        return;
                    }
                    if (callee_mem->member == "set") {
                        emit_expr(call->args[0].get(), fctx, Type::make(Type::Kind::Int));
                        emit_expr(call->args[1].get(), fctx, call->args[1]->type);
                        if (call->args[1]->type->kind == Type::Kind::Real) {
                            use_runtime("array_set_f64");
                            emit.line("call $array_set_f64");
                        } else {
                            if (call->args[1]->type->is_ref())
                                emit.line("i64.extend_i32_u");
                            use_runtime("array_set_i64");
                            emit.line("call $array_set_i64");
                        }
                        return;
                    }
                    if (callee_mem->member == "getString") {
                        emit_expr(call->args[0].get(), fctx, Type::make(Type::Kind::Int));
                        use_runtime("array_get_ptr");
                        emit.line("call $array_get_ptr");
                        return;
                    }
                    if (callee_mem->member == "getInt") {
                        emit_expr(call->args[0].get(), fctx, Type::make(Type::Kind::Int));
                        if (obj_type->elem && obj_type->elem->kind == Type::Kind::String) {
                            use_runtime("array_get_ptr");
                            use_runtime("string_to_int");
                            emit.line("call $array_get_ptr");
                            emit.line("call $string_to_int");
                        } else {
                            use_runtime("array_get_i64");
                            emit.line("call $array_get_i64");
                        }
                        return;
                    }
                }
                if (obj_type->kind == Type::Kind::Class) {
                    emit_expr(callee_mem->object.get(), fctx, obj_type);
                    emit.line("local.set " + fctx.temp_i32);
                    auto &layout = ctx.layouts[obj_type->name];
                    auto it = layout.methods.find(callee_mem->member);
                    if (it != layout.methods.end()) {
                        std::string field_name;
                        bool no_overrides = !method_overridden_in_descendants(obj_type->name, callee_mem->member);
                        const auto &allowlist = inline_method_allowlist();
                        if (no_overrides &&
                            allowlist.count(callee_mem->member) &&
                            is_trivial_getter(*it->second.def, obj_type->name, field_name)) {
                            for (auto &arg : call->args) {
                                emit_expr(arg.get(), fctx, arg->type);
                                emit.line("drop");
                            }
                            emit.line("local.get " + fctx.temp_i32);
                            int offset = ctx.layouts[obj_type->name].field_offsets[field_name];
                            emit.line("i32.const " + std::to_string(offset));
                            emit.line("i32.add");
                            emit_load(it->second.def->return_type);
                            return;
                        }
                        emit.line("local.get " + fctx.temp_i32);
                        for (auto &arg : call->args)
                            emit_expr(arg.get(), fctx, arg->type);
                        if (no_overrides) {
                            emit.line("call $" + it->second.owner + "$" + callee_mem->member);
                        } else {
                            emit_virtual_call(it->second, fctx);
                        }
                        return;
                    }
                }
            }
        }
    }

    void emit_new_object(const std::string &class_name, const std::vector<std::unique_ptr<Expr>> &args, FunctionContext &fctx) {
        auto &layout = ctx.layouts[class_name];
        emit.line("i32.const " + std::to_string(layout.size));
        use_runtime("malloc");
        emit.line("call $malloc");
        emit.line("local.set " + fctx.temp_i32);
        emit.line("local.get " + fctx.temp_i32);
        emit.line("i32.const " + std::to_string(layout.class_id));
        emit.line("i32.store");
        emit.line("local.get " + fctx.temp_i32);
        emit.line("i32.const 8");
        emit.line("i32.add");
        emit.line("i64.const 1");
        emit.line("i64.store");
        auto it = layout.methods.find("init");
        if (it != layout.methods.end() && args.size() == it->second.def->params.size()) {
            emit.line("local.get " + fctx.temp_i32);
            for (auto &arg : args)
                emit_expr(arg.get(), fctx, arg->type);
            emit.line("call $" + it->second.owner + "$init");
            if (it->second.def->return_type->kind != Type::Kind::Void)
                emit.line("drop");
        }
        emit.line("local.get " + fctx.temp_i32);
    }

    void emit_virtual_call(const MethodInfo &info, FunctionContext &fctx) {
        emit.line("local.get " + fctx.temp_i32);
        emit.line("i32.load");
        emit.line("i32.const " + std::to_string(max_slots * 4));
        emit.line("i32.mul");
        emit.line("i32.const " + std::to_string(info.slot * 4));
        emit.line("i32.add");
        emit.line("global.get $vtable_base");
        emit.line("i32.add");
        emit.line("i32.load");
        emit.line("call_indirect (type $t" + std::to_string(method_type_index.at(info.def)) + ")");
    }

    void emit_print_call(Expr *target, const std::vector<std::unique_ptr<Expr>> &args, bool newline, FunctionContext &fctx) {
        auto literal = eval_string_literal(target, fctx);
        if (literal) {
            if (args.empty()) {
                int offset = add_string_literal(*literal);
                use_runtime("print_literal");
                emit.line("i32.const " + std::to_string(offset));
                emit.line("i32.const " + std::to_string(literal->size()));
                emit.line("call $print_literal");
                if (newline) {
                    use_runtime("print_newline");
                    emit.line("call $print_newline");
                }
                return;
            }
            parse_and_emit_format(*literal, args, newline, fctx);
            return;
        }
        emit_expr(target, fctx, target->type);
        if (target->type->kind == Type::Kind::String) {
            use_runtime("print_string");
            emit.line("call $print_string");
        } else if (target->type->kind == Type::Kind::Real) {
            use_runtime("print_real");
            emit.line("call $print_real");
        } else if (target->type->kind == Type::Kind::Bool) {
            use_runtime("print_bool");
            emit.line("call $print_bool");
        } else if (target->type->kind == Type::Kind::Int || target->type->kind == Type::Kind::Char) {
            use_runtime("print_int");
            emit.line("call $print_int");
        } else if (target->type->is_ref()) {
            emit.line("i64.extend_i32_u");
            use_runtime("print_int");
            emit.line("call $print_int");
        } else {
            use_runtime("print_int");
            emit.line("call $print_int");
        }
        if (newline) {
            use_runtime("print_newline");
            emit.line("call $print_newline");
        }
    }

    void parse_and_emit_format(const std::string &fmt, const std::vector<std::unique_ptr<Expr>> &args, bool newline, FunctionContext &fctx) {
        size_t arg_index = 0;
        std::string segment;
        auto flush_segment = [&]() {
            if (segment.empty())
                return;
            int offset = add_string_literal(segment);
            use_runtime("print_literal");
            emit.line("i32.const " + std::to_string(offset));
            emit.line("i32.const " + std::to_string(segment.size()));
            emit.line("call $print_literal");
            segment.clear();
        };

        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] == '%' && i + 1 < fmt.size()) {
                char spec = fmt[i + 1];
                bool supported = (spec == 'i' || spec == 'r' || spec == 's' || spec == 'b');
                if (supported && arg_index < args.size()) {
                    flush_segment();
                    auto &arg = args[arg_index++];
                    emit_expr(arg.get(), fctx, arg->type);
                    if (spec == 'i') {
                        if (arg->type->is_ref())
                            emit.line("i64.extend_i32_u");
                        use_runtime("print_int");
                        emit.line("call $print_int");
                    } else if (spec == 'r') {
                        use_runtime("print_real");
                        emit.line("call $print_real");
                    } else if (spec == 's') {
                        use_runtime("print_string");
                        emit.line("call $print_string");
                    } else if (spec == 'b') {
                        use_runtime("print_bool");
                        emit.line("call $print_bool");
                    }
                } else {
                    segment.push_back('%');
                    segment.push_back(spec);
                }
                i++;
            } else {
                segment.push_back(fmt[i]);
            }
        }
        flush_segment();
        if (newline) {
            use_runtime("print_newline");
            emit.line("call $print_newline");
        }
    }

    void load_local(FunctionContext &fctx, const std::string &name) {
        emit.line("local.get " + fctx.locals[name].name);
    }

    void store_local(FunctionContext &fctx, const std::string &name) {
        emit.line("local.set " + fctx.locals[name].name);
    }

    void emit_load(const TypePtr &type) {
        if (type->kind == Type::Kind::Real)
            emit.line("f64.load");
        else if (type->kind == Type::Kind::String || type->kind == Type::Kind::Array || type->kind == Type::Kind::Class)
            emit.line("i32.load");
        else
            emit.line("i64.load");
    }

    void emit_store(const TypePtr &type) {
        if (type->kind == Type::Kind::Real)
            emit.line("f64.store");
        else if (type->kind == Type::Kind::String || type->kind == Type::Kind::Array || type->kind == Type::Kind::Class)
            emit.line("i32.store");
        else
            emit.line("i64.store");
    }

    void emit_module() {
        runtime_used.clear();
        emit.enabled = false;
        emit.indent = 0;
        for (auto &cls : ctx.classes) {
            for (auto &method : cls.second.def->methods) {
                if (!reachable_methods.empty() && !reachable_methods.count(&method))
                    continue;
                emit_method(cls.first, method);
            }
        }
        for (auto &fn : program.functions) {
            if (!reachable_functions.empty() && !reachable_functions.count(&fn))
                continue;
            emit_function(fn);
        }
        note_start_runtime_usage();

        emit.enabled = true;
        emit.indent = 0;
        emit.out.str("");
        emit.out.clear();

        emit.open("(module");
        for (auto &type : type_defs)
            emit.line(type);
        emit_runtime_imports();
        if (!function_table.empty()) {
            emit.line("(table " + std::to_string(function_table.size()) + " funcref)");
            std::string elem = "(elem (i32.const 0)";
            for (auto &fn : function_table)
                elem += " " + fn;
            elem += ")";
            emit.line(elem);
        }
        for (auto &cls : ctx.classes) {
            for (auto &method : cls.second.def->methods) {
                if (!reachable_methods.empty() && !reachable_methods.count(&method))
                    continue;
                emit_method(cls.first, method);
            }
        }
        for (auto &fn : program.functions) {
            if (!reachable_functions.empty() && !reachable_functions.count(&fn))
                continue;
            emit_function(fn);
        }
        emit_runtime_body();
        emit_data_segments();
        emit_start();
        emit.close(")");
    }

    void note_start_runtime_usage() {
        FunctionDef *main_fn = nullptr;
        for (auto &fn : program.functions)
            if (fn.name == "main")
                main_fn = &fn;
        bool use_main = main_fn != nullptr;
        bool use_class_start = false;
        if (!use_main) {
            auto it = ctx.layouts.find("Main");
            if (it != ctx.layouts.end()) {
                auto mit = it->second.methods.find("start");
                if (mit != it->second.methods.end()) {
                    use_class_start = true;
                }
            }
        }
        if (!use_main && !use_class_start)
            return;

        bool need_args = false;
        if (use_main) {
            for (auto &param : main_fn->params) {
                if (param.type->kind == Type::Kind::Array && param.type->elem && param.type->elem->kind == Type::Kind::String) {
                    need_args = true;
                    break;
                }
            }
        }
        if (!need_args)
            return;

        use_runtime("fd_fdstat_get");
        use_runtime("fd_read");
        use_runtime("args_sizes_get");
        use_runtime("args_get");
        use_runtime("malloc");
        use_runtime("array_new");
        use_runtime("array_set_i64");
        use_runtime("string_new");
        use_runtime("strlen");
        use_runtime("split_ws");
    }

    void emit_start() {
        FunctionDef *main_fn = nullptr;
        for (auto &fn : program.functions)
            if (fn.name == "main")
                main_fn = &fn;
        bool use_main = main_fn != nullptr;
        bool use_class_start = false;
        MethodInfo *start_method = nullptr;
        MethodInfo *init_method = nullptr;
        ClassLayout *start_layout = nullptr;
        if (!use_main) {
            auto it = ctx.layouts.find("Main");
            if (it != ctx.layouts.end()) {
                auto mit = it->second.methods.find("start");
                if (mit != it->second.methods.end()) {
                    use_class_start = true;
                    start_method = &mit->second;
                    start_layout = &it->second;
                    auto init_it = it->second.methods.find("init");
                    if (init_it != it->second.methods.end() && init_it->second.def->params.empty()) {
                        init_method = &init_it->second;
                    }
                }
            }
        }
        if (!use_main && !use_class_start)
            return;
        bool need_args = false;
        if (use_main) {
            for (auto &param : main_fn->params) {
                if (param.type->kind == Type::Kind::Array && param.type->elem && param.type->elem->kind == Type::Kind::String) {
                    need_args = true;
                    break;
                }
            }
        }
        emit.open("(func $_start");
        if (use_class_start || need_args) {
            emit.line("(local $t0 i32)");
            emit.line("(local $t1 i32)");
            emit.line("(local $t2 i32)");
            emit.line("(local $t3 i32)");
            emit.line("(local $t4 i32)");
            emit.line("(local $t5 i32)");
            emit.line("(local $t6 i32)");
            emit.line("(local $t7 i32)");
            emit.line("(local $t8 i32)");
            emit.line("(local $t9 i32)");
            emit.line("(local $t10 i32)");
        }
        emit.line("i32.const " + std::to_string(heap_base));
        emit.line("global.set $heap");
        emit.line("i32.const " + std::to_string(vtable_base));
        emit.line("global.set $vtable_base");
        emit.line("i32.const " + std::to_string(inner_base));
        emit.line("global.set $inner_base");
        emit.line("i32.const " + std::to_string(scratch_base));
        emit.line("global.set $scratch");
        if (use_main) {
            if (need_args) {
                auto emit_args_from_argv = [&]() {
                    emit.out
                        << "          local.get $t0\n"
                        << "          local.get $t0\n"
                        << "          i32.const 4\n"
                        << "          i32.add\n"
                        << "          call $args_sizes_get\n"
                        << "          drop\n"
                        << "          local.get $t0\n"
                        << "          i32.load\n"
                        << "          local.set $t1\n"
                        << "          local.get $t0\n"
                        << "          i32.const 4\n"
                        << "          i32.add\n"
                        << "          i32.load\n"
                        << "          local.set $t2\n"
                        << "          local.get $t1\n"
                        << "          i32.const 1\n"
                        << "          i32.le_u\n"
                        << "          if\n"
                        << "            i64.const 0\n"
                        << "            call $array_new\n"
                        << "            local.set $t6\n"
                        << "          else\n"
                        << "            local.get $t1\n"
                        << "            i32.const 4\n"
                        << "            i32.mul\n"
                        << "            local.set $t3\n"
                        << "            local.get $t3\n"
                        << "            local.get $t2\n"
                        << "            i32.add\n"
                        << "            call $malloc\n"
                        << "            local.set $t4\n"
                        << "            local.get $t4\n"
                        << "            local.set $t5\n"
                        << "            local.get $t4\n"
                        << "            local.get $t3\n"
                        << "            i32.add\n"
                        << "            local.set $t8\n"
                        << "            local.get $t5\n"
                        << "            local.get $t8\n"
                        << "            call $args_get\n"
                        << "            drop\n"
                        << "            local.get $t1\n"
                        << "            i32.const 1\n"
                        << "            i32.sub\n"
                        << "            local.set $t7\n"
                        << "            local.get $t7\n"
                        << "            i64.extend_i32_u\n"
                        << "            call $array_new\n"
                        << "            local.set $t6\n"
                        << "            i32.const 1\n"
                        << "            local.set $t8\n"
                        << "            i32.const 0\n"
                        << "            local.set $t9\n"
                        << "            block $args_done\n"
                        << "              loop $args_loop\n"
                        << "                local.get $t8\n"
                        << "                local.get $t1\n"
                        << "                i32.ge_u\n"
                        << "                br_if $args_done\n"
                        << "                local.get $t5\n"
                        << "                local.get $t8\n"
                        << "                i32.const 4\n"
                        << "                i32.mul\n"
                        << "                i32.add\n"
                        << "                i32.load\n"
                        << "                local.set $t2\n"
                        << "                local.get $t2\n"
                        << "                call $strlen\n"
                        << "                local.set $t10\n"
                        << "                local.get $t2\n"
                        << "                local.get $t10\n"
                        << "                call $string_new\n"
                        << "                local.set $t3\n"
                        << "                local.get $t6\n"
                        << "                local.get $t9\n"
                        << "                i64.extend_i32_u\n"
                        << "                local.get $t3\n"
                        << "                i64.extend_i32_u\n"
                        << "                call $array_set_i64\n"
                        << "                local.get $t8\n"
                        << "                i32.const 1\n"
                        << "                i32.add\n"
                        << "                local.set $t8\n"
                        << "                local.get $t9\n"
                        << "                i32.const 1\n"
                        << "                i32.add\n"
                        << "                local.set $t9\n"
                        << "                br $args_loop\n"
                        << "              end\n"
                        << "            end\n"
                        << "          end\n";
                };

                emit.line("i32.const 0");
                emit.line("local.set $t6");
                emit.line("global.get $scratch");
                emit.line("local.set $t0");
                emit.line("i32.const 0");
                emit.line("local.get $t0");
                emit.line("call $fd_fdstat_get");
                emit.line("drop");
                emit.line("local.get $t0");
                emit.line("i32.load8_u");
                emit.line("i32.const 2");
                emit.line("i32.eq");
                emit.open("if");
                emit_args_from_argv();
                emit.open("else");
                emit.line("i32.const 8192");
                emit.line("call $malloc");
                emit.line("local.set $t4");
                emit.line("local.get $t0");
                emit.line("local.get $t4");
                emit.line("i32.store");
                emit.line("local.get $t0");
                emit.line("i32.const 4");
                emit.line("i32.add");
                emit.line("i32.const 8192");
                emit.line("i32.store");
                emit.line("i32.const 0");
                emit.line("local.get $t0");
                emit.line("i32.const 1");
                emit.line("local.get $t0");
                emit.line("i32.const 8");
                emit.line("i32.add");
                emit.line("call $fd_read");
                emit.line("drop");
                emit.line("local.get $t0");
                emit.line("i32.const 8");
                emit.line("i32.add");
                emit.line("i32.load");
                emit.line("local.set $t1");
                emit.line("local.get $t1");
                emit.line("i32.eqz");
                emit.open("if");
                emit_args_from_argv();
                emit.open("else");
                emit.line("local.get $t4");
                emit.line("local.get $t1");
                emit.line("call $split_ws");
                emit.line("local.set $t6");
                emit.close("end");
                emit.close("end");
            }
            for (auto &param : main_fn->params) {
                if (param.type->kind == Type::Kind::Array && param.type->elem && param.type->elem->kind == Type::Kind::String) {
                    emit.line("local.get $t6");
                } else if (param.type->kind == Type::Kind::Array) {
                    emit.line("i64.const 0");
                    emit.line("call $array_new");
                } else if (param.type->kind == Type::Kind::Real) {
                    emit.line("f64.const 0");
                } else if (param.type->is_ref()) {
                    emit.line("i32.const 0");
                } else {
                    emit.line("i64.const 0");
                }
            }
            emit.line("call $main");
            if (main_fn->return_type->kind != Type::Kind::Void)
                emit.line("drop");
        } else if (use_class_start) {
            emit.line("i32.const " + std::to_string(start_layout->size));
            emit.line("call $malloc");
            emit.line("local.set $t0");
            emit.line("local.get $t0");
            emit.line("i32.const " + std::to_string(start_layout->class_id));
            emit.line("i32.store");
            emit.line("local.get $t0");
            emit.line("i32.const 8");
            emit.line("i32.add");
            emit.line("i64.const 1");
            emit.line("i64.store");
            if (init_method) {
                emit.line("local.get $t0");
                emit.line("call $" + init_method->owner + "$init");
                if (init_method->def->return_type->kind != Type::Kind::Void)
                    emit.line("drop");
            }
            emit.line("local.get $t0");
            emit.line("call $" + start_method->owner + "$start");
            if (start_method->def->return_type->kind != Type::Kind::Void)
                emit.line("drop");
        }
        emit.close(")");
        emit.line("(export \"_start\" (func $_start))");
    }
};

CodegenResult generate_wat(Program &program) {
    SemanticContext ctx;
    for (auto &s : program.structs) {
        ClassInfo info;
        info.def = &s;
        ctx.classes[s.name] = info;
    }
    for (auto &fn : program.functions) {
        ctx.functions[fn.name] = {&fn};
    }

    Codegen codegen(program, ctx);
    codegen.build_class_layouts();
    codegen.build_inheritance_info();
    TypeChecker checker(program, ctx);
    for (auto &fn : program.functions)
        checker.check_function(fn);
    for (auto &cls : program.structs) {
        for (auto &method : cls.methods)
            checker.check_method(cls.name, method);
    }
    codegen.compute_reachability();
    codegen.build_method_table();
    codegen.emit_module();

    CodegenResult result;
    result.wat = codegen.emit.out.str();
    return result;
}
} // namespace atom
