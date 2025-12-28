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
#include "ast.h"

#include <sstream>

namespace atom {

TypePtr Type::make(Kind kind, const std::string &name) {
    auto t = std::make_shared<Type>();
    t->kind = kind;
    t->name = name;
    return t;
}

TypePtr Type::make_array(const TypePtr &elem) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::Array;
    t->elem = elem;
    return t;
}

TypePtr Type::make_class(const std::string &name) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::Class;
    t->name = name;
    return t;
}

TypePtr Type::make_unknown() {
    auto t = std::make_shared<Type>();
    t->kind = Kind::Unknown;
    return t;
}

bool Type::is_ref() const {
    return kind == Kind::String || kind == Kind::Array || kind == Kind::Class;
}

std::string Type::str() const {
    std::ostringstream oss;
    if (is_virtual) oss << "virtual";
    switch (kind) {
        case Kind::Int: oss << "Int"; break;
        case Kind::Real: oss << "Real"; break;
        case Kind::Bool: oss << "Bool"; break;
        case Kind::Char: oss << "Char"; break;
        case Kind::Void: oss << "Void"; break;
        case Kind::String: oss << "String"; break;
        case Kind::Array: oss << "Array"; if (elem) oss << "[" << elem->str() << "]"; break;
        case Kind::Class: oss << name; break;
        case Kind::Null: oss << "null"; break;
        case Kind::Unknown: oss << "Unknown"; break;
    }
    return oss.str();
}

} // namespace atom
