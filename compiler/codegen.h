#ifndef ATOM_CODEGEN_H
#define ATOM_CODEGEN_H

#include "ast.h"

#include <string>

namespace atom {

struct CodegenResult {
    std::string wat;
};

CodegenResult generate_wat(Program &program);

} // namespace atom

#endif
