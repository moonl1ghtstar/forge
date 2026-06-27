# Forge Compiler

Forge is a compiler for the Helix language. It translates `.hlx` source code into x86-64 Windows assembly and can continue through NASM and `ld` to produce a native `.exe`.

## What Helix Is

Helix is a small, JavaScript-style language built for command-line programs. It supports:

- `function` declarations
- top-level statements
- variables: `var`, `let`, `const`, `global`
- control flow: `if`, `else`, `while`, `until`, `do`, `for`, `switch`
- loops control: `break()`, `pass()`
- module imports
- extern function declarations for FFI-style calls

If a file has top-level statements but no explicit `main()` function, Forge wraps those statements into an implicit `main()`. If the file has functions only, Forge now generates an empty `main()` so the program still links as an executable.

## Project Layout

- `src/` - compiler source
- `docs/` - language grammar and notes
- `tests/` - sample Helix programs
- `module/` - built-in and system modules

## Build Pipeline

Forge follows this pipeline:

` .hlx -> Forge -> .asm -> nasm -f win64 -> ld -> .exe `

Current toolchain targets Windows x64.

## Build Requirements

- Windows
- MSYS2 / MinGW toolchain
- `gcc`
- `nasm`
- `ld`

## Build

From the project root:

```powershell
gcc -Isrc -Isrc\ast -Isrc\lexer -Isrc\parser -Isrc\sema -Isrc\codegen `
  src\main.c src\lexer\lexer.c src\parser\parser.c src\ast\ast.c `
  src\sema\sema.c src\codegen\codegen.c -o forge.exe
```

## Usage

You need to add the forge.exe path to the system environment variables.

Compile to assembly only:

```powershell
forge tests\main.hlx -asm
```

Compile and link into an executable:

```powershell
forge tests\main.hlx
```

Run after build:

```powershell
forge tests\main.hlx -run
```

## Example

```hlx
import console

function main() {
    console.print("Hello, world!");
    return 0;
}
```

## Language Notes

- String and integer expressions are supported in the current backend.
- Built-in console helpers are available through the `console` module.
- Imported modules are resolved from the project/module directories.
- Current function call ABI supports up to 4 integer arguments.

## Error UX

Forge prints compiler errors with line context and clearer hints for parse, semantic, and codegen failures.

## Roadmap

### C compiler support

Forge should grow into a multi-language compiler front-end, not a one-off language tool. The C path should be added as a separate frontend with its own parser, semantic rules, and diagnostics, while reusing shared compiler infrastructure where possible.

Recommended direction:

- add a shared intermediate representation
- keep language-specific parsers separate
- reuse diagnostic formatting, module loading, codegen, and linker flow
- implement a C frontend that lowers C AST into the shared IR
- add C-specific type checking, declarations, and control-flow validation

### Go compiler support

Go needs a different frontend strategy from C because of its stricter package model, type system, and runtime expectations. Do not force Go into the Helix model. Treat it as another frontend that targets the same compiler pipeline.

Recommended direction:

- design package-aware source loading
- model Go types, methods, interfaces, and visibility separately
- add a Go frontend that lowers Go AST into the shared IR
- keep runtime, calling convention, and memory model decisions explicit
- plan for Go-specific standard library/runtime support before feature expansion

### Long-term architecture

- one compiler core
- many language frontends
- one shared IR
- multiple backends if needed
- consistent diagnostics across every language

This keeps Helix stable while leaving room for C and Go support without rewriting the compiler from scratch.
