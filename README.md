# ATOM Compiler

This repository contains a C++17 compiler for the ATOM programming language. The compiler emits WebAssembly Text (WAT) and targets WASI. The project includes the language front-end (lexer, parser, type checking), code generation, and a comprehensive test/benchmark suite.

## AI generated

**All code in this repository has been generated, refactored, corrected, or documented using OpenAI's coding Large Language Model (LLM) tool codex.**

## Build and Test

```bash
make clean
make all
./test_functions.sh
./test_benchmarks.sh
```

### Tooling

- **Compiler:** C++17 (g++ or clang++)
- **WASM toolchain:** `wat2wasm` (from WABT)
- **Runtime:** `wasmtime`

## Usage

```bash
./atomc path/to/program.atom -o output.wat
wat2wasm output.wat -o output.wasm
wasmtime output.wasm
```

For quick iteration on a single file with expected output comparison:

```bash
./debug.sh path/to/program.atom [args...]
```

## Language Summary

### Syntax

- Indentation-based blocks, Python-style.
- Block headers end with a colon (`:`).
- Conditions for `if` and `while` use parentheses.
- Comments: `#` or `//` to end of line.

### Types

- `Int`: 64-bit signed integer
- `Real`: 64-bit floating point
- `Bool`: `true` / `false`
- `Char`: single byte character
- `String`: UTF-8 byte string
- `Array[T]` or `T[]`: array type
- `Void`: no return value
- `null`: null literal for reference types

### Variables and Functions

All variables are declared with an explicit type.

```atom
Int add(Int a, Int b):
    return a + b

Int main():
    Int result = add(2, 3)
    "result=%i".println(result)
    return 0
```

Inline function bodies are supported:

```atom
Int add(Int a, Int b) = a + b
```

### Structures, Inheritance, and Methods

Structures define fields and methods. Single inheritance is supported. Method calls are dynamically dispatched.

```atom
Animal:
    String name

    init(String name):
        this.name = name

    Void speak():
        "...".println()

Dog: Animal:
    Int age

    init(String name, Int age):
        this.name = name
        this.age = age

    Void speak():
        "Woof".println()
```

### Inner Extension Points (Beta-style)

`inner` is a statement used inside a base-class method to call the next override in the class hierarchy (the most-derived implementation relative to the current class).

```atom
Base:
    Void log(String msg):
        "[base]".print()
        inner

Child: Base:
    Void log(String msg):
        msg.println()
```

### Arrays

Arrays are heap-allocated and store 64-bit slots. Reference values are stored as pointers.

Supported methods:

- `size()` / `count()` -> `Int`
- `push(value)`
- `pop()`
- `get(i)`
- `set(i, value)`
- `getInt(i)`
- `getString(i)`

Out-of-bounds access returns:
- `-1` for numeric arrays (`Int`, `Real`)
- `null` for reference arrays

### Strings

- Double-quoted literals with escapes: `\n`, `\t`, `\r`, `\"`, `\\`
- Concatenation with `+`
- `length()` returns byte length
- `print()` / `println()` output the string

Formatted printing supports:
- `%i` (Int)
- `%r` (Real)
- `%s` (String)
- `%b` (Bool)

Unsupported format specifiers are printed literally.

### Imports

Imports are parsed and used to include other source files:

```atom
import Math from "./math.atom"
import Utils as U from "./utils.atom"
```

Notes:
- Only `from "path"` triggers a file import.
- Paths without `/` are resolved relative to the importing file.
- `as` aliases are parsed but not currently used during compilation.

### Entry Point and Arguments

The compiler looks for `Int main(...)` and exports `_start` for WASI.

Two supported signatures:

```atom
Int main():
    return 0

Int main(String[] args):
    "argc=%i".println(args.count())
    return 0
```

`args` are populated from:
- **stdin tokens** when stdin is not a TTY (whitespace split), otherwise
- **WASI argv** (excluding argv[0]).

## Implementation Notes

- **Memory layout (class instances):**
  - offset `0`: class id (`i32`)
  - offset `8`: reference count (`i64`, not used for reclamation)
  - offset `16+`: fields (8-byte aligned)
- **Strings:**
  - offset `16`: length (`i64`)
  - offset `24`: data pointer (`i32`)
- **Arrays:**
  - offset `16`: length (`i64`)
  - offset `24`: capacity (`i64`)
  - offset `32`: data pointer (`i32`)
- **Dynamic dispatch:** method calls use a vtable table stored in linear memory and `call_indirect`.
- **Inner dispatch:** `inner` uses a separate table to resolve the next override at runtime.
- **Reference counting helpers** exist (`incref`/`decref`), but the compiler does not currently emit retain/release or free memory.

## Directory Structure

- `compiler/` — C++17 compiler sources
- `build/` — build artifacts (`.o`)
- `testing/functions/` — functional tests (`.atom`)
- `testing/benchmarks/` — benchmarks (`.atom` and `.c`)
- `testing/stdin/` — stdin fixtures (`.in`)
- `testing/stdout/` — expected outputs (`.out`)
- `test_functions.sh`, `test_benchmarks.sh` — test runners
- `debug.sh` — compile+run helper for a single file

## Formal Grammar (BNF)

```bnf
<program>          ::= <import_stmt>* (<structure_def> | <function_def>)*

<import_stmt>      ::= "import" IDENTIFIER ("as" IDENTIFIER)? NEWLINE

<structure_def>    ::= IDENTIFIER (":" IDENTIFIER)? ":" NEWLINE INDENT <structure_member>* DEDENT

<structure_member> ::= <var_decl> | <method_def>

# Function definition (standalone)
# Supports block body or inline assignment: Int add(a,b) = a+b
<function_def>     ::= <type>? IDENTIFIER "(" <param_list>? ")" (":" NEWLINE INDENT <statement>* DEDENT | "=" <expression> NEWLINE)

<var_decl>         ::= <type> IDENTIFIER ("=" <expression>)? NEWLINE

# Method definition (inside structures)
# Note: 'init' is matched here as an IDENTIFIER but treated semantically as a constructor
<method_def>       ::= <type>? IDENTIFIER "(" <param_list>? ")" ":" NEWLINE INDENT <statement>* DEDENT

<param_list>       ::= <type> IDENTIFIER ("," <type> IDENTIFIER)*

# Supports both Array[Int] and Int[] styles
<type>             ::= <base_type> ("[" "]")*
<base_type>        ::= "Int" | "Real" | "Bool" | "String" | "Char" | "Void" 
                                         | "Array" ("[" <type> "]")? 
                                         | IDENTIFIER

<statement>        ::= <var_decl>
                                         | <if_stmt>
                                         | <while_stmt>
                                         | <return_stmt>
                                         | <expr_stmt>
                                         | <inner_stmt>

<inner_stmt>       ::= "inner" NEWLINE

<if_stmt>          ::= "if" "(" <expression> ")" ":" NEWLINE INDENT <statement>* DEDENT ("else" ":" NEWLINE INDENT <statement>* DEDENT)?

<while_stmt>       ::= "while" "(" <expression> ")" ":" NEWLINE INDENT <statement>* DEDENT

<return_stmt>      ::= "return" <expression>? NEWLINE

<expr_stmt>        ::= <expression> NEWLINE

<expression>       ::= <logical_or>

<logical_or>       ::= <logical_and> (("||") <logical_and>)*

<logical_and>      ::= <equality> (("&&") <equality>)*

<equality>         ::= <comparison> (("==" | "!=") <comparison>)*

<comparison>       ::= <additive> (("<" | ">" | "<=" | ">=") <additive>)*

<additive>         ::= <multiplicative> (("+" | "-") <multiplicative>)*

<multiplicative>   ::= <unary> (("*" | "/") <unary>)*

<unary>            ::= ("!")? <postfix>

# Postfix handles method calls (foo()), property access (foo.bar), and array access (foo[i])
<postfix>          ::= <primary> (<call_access>)*

<call_access>      ::= "(" <arg_list>? ")" 
                                         | "." IDENTIFIER 
                                         | "[" <expression> "]"

<arg_list>         ::= <expression> ("," <expression>)*

<primary>          ::= IDENTIFIER
                                         | INTEGER
                                         | REAL
                                         | STRING
                                         | CHAR
                                         | "true" | "false"
                                         | "null"
                                         | "this"
                                         | "inner"
                                         | "(" <expression> ")"
                                         | "[" <arg_list>? "]"   # Array Literal: [] or [1, 2]

INTEGER            ::= [0-9]+
REAL               ::= [0-9]+\.[0-9]+
STRING             ::= "\"(\\.|[^\"\\\\])*\""
CHAR               ::= "'(\\.|[^'\\\\])'"
IDENTIFIER         ::= [a-zA-Z_][a-zA-Z0-9_]*
COMMENT            ::= "#" (~NEWLINE)* | "//" (~NEWLINE)*
```
