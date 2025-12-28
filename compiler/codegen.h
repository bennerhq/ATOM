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
