# Atom Project

**All code in this repository is written in Visual Studio Code using Large Language Models (LLMs) for code generation, refactoring, and documentation.**

## Project Overview

This project is an experimental compiler, written in C++, for the Atom programming language. Atom is described in detail in this document. The compiler compiles ATOM code to WebAssembly (WASM) and WASI. Atom is a minimal, imperative language designed for clarity, performance, and extensibility. The codebase includes a full parser, AST, code generator, and a suite of benchmarks and tests.

## Fast Start

```bash
make clean
make all
./test_functions.sh
./test_benchmarks.sh
```

## Directory Structure and Contents

- **compiler/** — Contains all C++17 source code for the Atom compiler:
    - `lexer.cpp`, `lexer.h`: Tokenizer for Atom source code. Handles indentation state.
    - `parser.cpp`, `parser.h`: Parses tokens into an abstract syntax tree (AST) using Atom's BNF grammar.
    - `ast.cpp`, `ast.h`: Defines AST node structures, symbol tables, and semantic analysis logic.
    - `codegen.cpp`, `codegen.h`: Emits WebAssembly (WASM) and WebAssembly Text (WAT) code from the AST. Handles memory layout and ARC.
    - Other files: Utilities, error handling, and support code for compilation.

- **build/** — Stores generated output files:
    - `.wasm` files: Compiled WebAssembly binaries for Atom programs.
    - `.wat` files: Human-readable WebAssembly Text format for inspection and debugging.

- **testing/benchmarks/** — Contains benchmark programs for performance and correctness testing:
    - `.atom` files: Atom language benchmark sources.
    - `.c` files: Equivalent C benchmarks for comparison.

- **testing/stdin/** — Contains input files for automated tests and benchmarks:
    - `.in` files: Provide standard input data for Atom and C programs during test execution.

- **testing/stdout/** — Stores stdout output from running benchmarks and tests:
    - `.out` files: Output from running Atom and C benchmarks, named to match the source file.

- `test_benchmarks.sh`, `test_functions.sh` — Shell scripts for automated testing.
- `Makefile` — Build automation.
- `debug.sh` — Compiles the compiler, runs the atomc compiler, and executes code to compare expected output.

## Example: Atom Code

```atom
# Regression: nested constructor inside init should work

Point:
        Int x
        Int y

        init(Int x, Int y):
                this.x = x
                this.y = y

Container:
        Point p

        init(Int x, Int y):
                this.p = Point.new(x, y)

        Int px():
                return this.p.x

        Int py():
                return this.p.y

Int main():
        Container c = Container.new(7, 9)
        "px=%i".println(c.px())
        "py=%i".println(c.py())
        return 0
```

## Development Notes

- All code is written and maintained in Visual Studio Code, leveraging LLMs for code generation, review, and documentation.
- The Atom language and compiler are experimental and subject to change.
- Contributions, bug reports, and suggestions are welcome.

---

# ATOM Programming Language Specification

## Overview

ATOM is a pure object-oriented programming language where everything is an object. It features static typing with type inference, single inheritance, message passing, and compiles directly to WebAssembly.

## Core Concepts

- **Pure OOP:** Every value (including primitives) is an object.
- **Single Inheritance:** Structures inherit from parent structures with function overriding.
- **Static Typing:** Mandatory type annotations with inference support.
- **Reference Counting:** Automatic memory management via ARC (Automatic Reference Counting).
- **Modules:** Import external modules using object-oriented import syntax.
- **Null Safety:** Explicit null handling.
- **Inner Extension Points:** `inner` keyword allows parent methods to defer execution to subclasses (inspired by Beta).

## Type System

**Primitive Types:**
- `Int` - 64-bit integer
- `Char` - Single character (single UTF-8 codepoint)
- `Void` - Represents absence of a return value
- `Real` - 64-bit floating point
- `Bool` - Boolean (true/false)
- `Array[T]` - Generic array type containing elements of type T (aliased as `T[]`)
- `String` - UTF-8 strings (double-quoted literals)

## Syntax

### Imports

```atom
import Math
import Graphics as G
```

### Structures and Inheritance

```atom
Animal:
        String name
        
        init(String name):
                this.name = name
        
        Void speak():
                "Animal speaks".println()

Dog: Animal:
        Int age
        
        init(String name, Int age):
                this.name = name
                this.age = age
        
        Void speak():
                "Woof!".println()
```

### Inner Extension Points (Beta)

```atom
Logger:
        Void log(String message):
                "Log: ".print()
                message.println()

DetailedLogger: Logger:
        Void log(String message):
                inner  # Executes parent (Logger) logic here? No, strictly extends logic.
                # In Beta, 'inner' in the PARENT calls the CHILD. 
                # For Atom, we treat 'inner' as a specific extension point mechanism.
```

### Variables and Fields

```atom
Int x = 42
String msg = "hello"
Array[Int] numbers = [1, 2, 3] # Array literal
```

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

## Example Programs

### Standalone Functions

```atom
Int add(Int a, Int b) = a + b

Int main():
        Int result = add(2, 3)
        return 0
```

### Inheritance and Functions

```atom
Shape:
        String color
        
        init(String color):
                this.color = color
        
        Real getArea():
                return 0.0

Circle: Shape:
        Real radius
        
        init(String color, Real radius):
                this.color = color
                this.radius = radius
        
        Real getArea():
                return radius * radius * 3.14159

Int main(String[] args):
        Circle c = Circle.new("Red", 5.0)
        "Circle area: %.2f".println(c.getArea())
```

### Control Flow and Arrays

```atom
Int main(String[] args):
        Array[Int] arr = []
        Int size = 5
        Int j = 0
        while (j < size):
                arr.push((j + 1) * 10)
                j = j + 1
        return 0
```

## Compiler Implementation Checklist

The following system constraints must be followed when generating the C++ compiler code:

### 1. Lexer & Parser Logic

- **Indentation Handling:** The Lexer must maintain a state stack to track indentation levels. Emit INDENT tokens when whitespace increases and DEDENT tokens when it decreases.
- **Array Syntax:** The Parser must normalize `String[]` (Java-style) and `Array[String]` (Generic-style) into the same internal Type representation (e.g., `Type::Array(Type::String)`).
- **Syntactic Sugar:**
    - Convert `Int add(a,b) = a+b` into a standard function node with a ReturnStatement containing the expression.
    - Treat `init` as a standard method in the AST, but flag it with `is_constructor = true`.

### 2. Semantic Analysis

- **Symbol Tables:** Implement a scoped symbol table. Scopes are: Global → Class → Method → Block.
- **Type Inference:**
    - For `var x = 5`, infer type `Int`.
    - For `var obj = Container.new()`, infer type `Container`.
- **The `this` Pointer:** In any non-static method, implicitly add `this` as the first argument with type `Pointer<CurrentClass>`.

### 3. Memory Management (ARC Strategy)

- **Object Header:** Every heap object must have a header containing:
    - V-Table Pointer (for dynamic dispatch).
    - Reference Count (64-bit integer).
- **Retain/Release Injection:**
    - Assignment (`a = b`): Emit `decref(a)`, then `a = b`, then `incref(b)`.
    - Scope Exit: At the end of every block (DEDENT), emit `decref()` for every local variable declared in that scope.
    - Return Values: If returning an object, `incref` it before the stack frame is destroyed so it survives to the caller.

### 4. Code Generation (WASM)

- **WASM Linear Memory Layout:**
    - Offset 0: V-Table Index.
    - Offset 8: Reference Count.
    - Offset 16+: Instance fields (aligned to 8 bytes).
- **V-Tables (Virtual Method Dispatch):**
    - Generate a table section in WASM.
    - Assign every method a unique index in the table.
    - Method calls `obj.method()` must load the V-Table index from `obj`, look up the function index, and use `call_indirect`.
- **Static Methods (`.new`):**
    - `ClassName.new(...)` should allocate memory (malloc size of struct), call `init(...)`, and return the pointer.
- **String Literals:**
    - Store string literals in the WASM data section.
- **Entry Point:**
    - The compiler must locate `Int main(...)`.
    - Export a function named `_start` (for WASI) that calls `main`.

## Supporting language features
 - Imports: Paths resolve relative to the importing file. the format is: import <structure> from "<file path>"
  - inner semantics is Beta-style
  - Arrays: for out‑of‑bounds access return -1 or null
  - Built‑ins:
      - Array: size(), count(), push(x), pop(), get(i), set(i, v)
      - String: length(), print(), println(), +, ==, !=
      - String[] args: count(), getString(i), getInt(i)
  - Formatting: should print/println support: %i %r %s %b
  
