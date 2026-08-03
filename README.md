# Anvil Compiler

Anvil is a multi-language compiler front-end targeting Windows x64.
It compiles `.hlx` (Helix), `.c` (C subset) and `.asm` (Asm) source files into
x86-64 Windows assembly and drives `ld` to produce native `.exe` binaries.

---

## Language Support

### Helix (`.hlx`)

A JavaScript-style systems programming language designed for building command-line applications and native programs.

Supported features:

- `function` declarations
- Top-level statements (automatically wrapped into `main`)
- Variables:
  - `var`
  - `let`
  - `const`
  - `global`
- Data types:
  - `int`
  - `long`
  - `float`
  - `bool`
  - `str`
  - Struct types
- Control flow:
  - `if`
  - `else`
  - `while`
  - `until`
  - `do`
  - `for`
  - `switch`
- Loop control:
  - `break()`
  - `pass()`
- Module imports:
  - `import <module>`
  - `import <module> { func1, func2 }`
- External function declarations:
  - `extern` (FFI support)
- Struct definitions and field access
- Native x86-64 code generation through Anvil backend

---

### C subset (`.c`)

A minimal C frontend that shares the same compiler backend infrastructure as Helix.

Supported features:

- C-style syntax parsing
- `int` typed variables and parameters
- Function declarations and calls
- Functions with up to 4 parameters
- Control flow:
  - `if`
  - `while`
  - `for`
  - `break`
  - `continue`
- Return statements
- Arithmetic expressions
- Comparison expressions
- Shared:
  - AST
  - Semantic analysis
  - IR
  - Code generation

---

### Assembly (`.asm`)

Anvil includes a built-in x86-64 assembler.

Supported features:

- NASM-style assembly syntax subset
- x86-64 instruction encoding
- Register and memory operand parsing
- Labels and symbols
- Relocation handling
- COFF `.obj` generation

The same assembler engine is used for:

```
Compiler generated ASM
        ↓
Anvil Assembler

User written ASM
        ↓
Anvil Assembler
```

---

## Project Layout

```
anvil/                        Root
  build.bat                   Builds anv.exe with gcc, then copies module/ → anvil/bin/lib/
  docs/

  module/                     Built-in module sources
    helix/
      console/                console module (print, input, clear, color)
        src/                  Per-function .hlx source files

  anvil/
    main.c                    Entry point, CLI, pipeline dispatcher

    assembler/                Built-in x86-64 assembler
      docs/                   Assembler documentation
      src/
        lexer/                ASM lexer
        parser/               ASM parser and operand analysis
        x86-encode/           x86-64 instruction encoder
        coff-writer/          COFF object file generator
        anv-asm/            Assembler frontend and integration
      test/                   Assembler test programs

    helix/
      src/
        lexer/                Helix lexer
        parser/               Helix parser (recursive descent)
        sema/                 Semantic analysis (shared by C frontend)
        ir/                   IR data model, builder, and optimizer
        codegen/              x86-64 codegen (Windows x64 ABI)
        ast/                  AST node types
        errors/               Structured error reporting (colored, aligned)
      test/                   Manual test programs (.hlx)
      docs/                   Grammar reference

    c/
      src/
        lexer/                C lexer
        parser/               C parser
        frontend/             C frontend (parse → sema → codegen)
      test/                   Manual test programs (.c)
      docs/                   C frontend documentation

    bin/
      bin/                    anv.exe, fg.exe, 4g.exe
      lib/                    Module files (copied here by build.bat)
        console/
          src/
        helix/
          console/
            src/
      share/
        examples/             Example projects
        templates/            Project templates

    idea/                     Experimental ideas and prototypes

  samples/                    Sample Anvil programs

  temp/                       Temporary build files

  tools/
    assets/                   Tool resources
    cpp/                      C++ utilities and experiments
      build/                  C++ build output
      include/                C++ headers
      src/                    C++ source files
      tests/                  C++ tests
    scripts/                  Development scripts
```

---

## Error Reporting

Anvil emits structured, colored diagnostics in the style of rustc:

```
error[E204]: expected ';' after variable declaration, found identifier 'console'
 --> main.hlx:3:10
  |
3 | let a = 0
  |          ^
  |
```

- **Colors**: `error` in red, `warn` in yellow, source location in cyan, code in bold
- **Aligned gutter**: `|` columns adjust dynamically to the line number width
- **Accurate pointers**: `^` points to the end of the offending token, not the next statement

Implemented in [`anvil/helix/src/errors/anvil-errors.c`](anvil/helix/src/errors/anvil-errors.c).

---

## Modules

Modules live under `module/helix/<name>/` and are loaded via `import <name>` in Helix source.

Each module directory contains:
- `config.json` — module manifest listing the name, version, source directory, and exported functions
- `src/<func>.hlx` — one `.hlx` file per exported function

**Available modules:**

| Module    | Functions                      | Description                  |
|-----------|-------------------------------|------------------------------|
| `console` | `print`, `input`, `clear`, `color` | Terminal I/O and formatting |

### Import syntax

```hlx
import console              // import all functions from console
import console { print }    // selective import
```

At build time, `build.bat` copies the entire `module/` tree into `anvil/bin/lib/`
so the runtime can locate module files relative to the executable.

---

## Build Pipeline

```
source (.hlx or .c)
  └─► Lexer                   (tokenize)
        └─► Parser            (→ AST)
              └─► Sema        (type check / resolve)
                    └─► IR Builder   (AST → IR instructions)
                          └─► IR Optimizer  (constant fold, dead code, etc.)
                                └─► Codegen (IR → x86-64 ASM text, Windows x64 ABI)
                                      └─► .asm
                                            └─► Anvil Assembler (Internal x86 Encoder & COFF Writer)
                                                  └─► .obj  (COFF, Win64)
                                                        └─► ld + MinGW CRT
                                                              └─► .exe
```

Both language frontends share the same IR layer, optimizer, and codegen.
The IR is a flat list of typed instructions (loads, stores, calls, branches)
that sits between semantic analysis and assembly emit. `-dump-ir` prints it.

---

## Build Requirements

- Windows (x64)
- MSYS2 / MinGW64 toolchain
  - `gcc` — builds the Anvil compiler itself
  - `ld` — links `.obj` → `.exe` (external dependency, NASM dependency has been completely removed)

---

## Build Anvil

```bat
build.bat
```

What `build.bat` does:
1. Compiles all Anvil C sources with `gcc -std=c11 -O2`
2. Writes `anv.exe`, `fg.exe`, `4g.exe` to `anvil/bin/bin/`
3. Copies `module/` → `anvil/bin/lib/` (creates `lib/` if missing)

---

## Usage

Add `anvil\bin\bin` to your `PATH`, then use `anv`, `fg`, or `4g` interchangeably.

### Compile and run

```powershell
# Compile + link → .exe
anv src.hlx
4g src.hlx

# Compile, link, then run immediately
anv src.hlx -run
```

### Output modes

```powershell
# Emit assembly text only
anv src.hlx -o out.asm

# Compile to object file
anv src.hlx -o lib.obj

# Compile and link to executable
anv src.hlx -o out.exe
```
### Debug / inspection

```powershell
anv src.hlx -dump-tokens
anv src.hlx -dump-ast
anv src.hlx -dump-ir
```

### Cross-language linking

```powershell
# Step 1 – compile each source to an object file
anv samples\hello_mixed.hlx    -o samples\helix.obj
anv samples\mylib.c            -o samples\clib.obj

# Step 2 – link into a single exe
anv -link samples\helix.obj samples\clib.obj -o samples\mixed.exe

# Step 3 – run
samples\mixed.exe
```

You can also mix Anvil `.obj` with objects from `gcc -c`:

```powershell
gcc -c -O2 mylib.c -o mylib.obj
anv src.hlx -o lib.obj -o src.obj
anv -link src.obj mylib.obj -o program.exe
```

**ABI compatibility:**
- All Anvil-generated code targets the Windows x64 ABI
  (first 4 integer args in `rcx/rdx/r8/r9`, 32-byte shadow space, 16-byte stack alignment).
- `global` symbols are plain COFF public symbols (no leading underscore on x64).
- `extern` declarations resolve directly to matching COFF exports.
- `gcc -c` with MinGW64 produces the same COFF format and ABI — symbols link without mangling.

---

## Options Reference

| Flag              | Description                                          |
|-------------------|------------------------------------------------------|
| `-link a.obj ...` | Link one or more `.obj` files into `.exe`            |
| `-o <file>`       | Override output filename                             |
| `-run`            | Build `.exe`, then execute it                        |
| `-dump-tokens`    | Print lexer tokens and exit                          |
| `-dump-ast`       | Print parsed AST and exit                            |
| `-dump-ir`        | Print lowered IR and exit                            |
| `--help`          | Show usage                                           |
## Example Programs

### Helix

```hlx
import console

let message = "Hello from Helix!";

function greet(name) {
    console.print(name);
}

greet(message);
```

### C subset

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    int result;
    result = add(21, 21);
    return result;
}
```

---

## Roadmap

### Near-term

- [x] Helix frontend (lexer, parser, sema, codegen)
- [x] C frontend (int subset)
- [x] Shared AST and codegen
- [x] Shared IR layer between semantic analysis and codegen
- [x] Structured, colored error diagnostics with aligned source pointers
- [x] Module system (`import`, selective imports, `config.json` manifest)
- [x] `console` built-in module (`print`, `input`, `clear`, `color`)
- [x] Extension-based output selection via `-o`
- [x] `-link` multi-object linker
- [x] Auto-copy modules to `anvil/bin/lib/` on build
- [x] Auto parameter type inference for Helix parameters (string vs int)
- [ ] String literals in C frontend
- [ ] `printf`/`scanf` builtins in C frontend
- [ ] More built-in modules (e.g., `math`, `fs`)

### Long-term architecture

- One compiler core, many language frontends
- One shared IR layer for all languages
- Multiple backends (current: Internal x86-64 Assembler / COFF Writer; future: LLVM IR, direct ELF emit)
- Consistent, rich diagnostics across every language
- Go frontend (separate package-aware loader)

## Copyright and License

anv Copyright (c) 2026 MoonL1ghtSt4r.

Licensed under the MIT License or LICENSE file.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
