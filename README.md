# Forge Compiler

Forge is a multi-language compiler front-end targeting Windows x64.
It compiles `.hlx` (Helix) and `.c` (C subset) source files into
x86-64 Windows assembly and drives NASM and `ld` to produce native `.exe` binaries.

---

## Language Support

### Helix (`.hlx`)

A JavaScript-style syntax language built for command-line programs:

- `function` declarations
- Top-level statements (auto-wrapped into `main`)
- Variables: `var`, `let`, `const`, `global`
- Control flow: `if`, `else`, `while`, `until`, `do`, `for`, `switch`
- Loop control: `break()`, `pass()`
- Module imports: `import <module>`
- Selective imports: `import <module> { func1, func2 }`
- `extern` function declarations (FFI)
- Struct types and field access

### C subset (`.c`)

A minimal C frontend sharing the same AST, IR, and codegen as Helix:

- `int` typed variables and parameters
- Functions with up to 4 parameters
- `if`, `while`, `for`, `break`, `continue`
- Return statements
- Arithmetic and comparison expressions

---

## Project Layout

```
forge/                        Root
  build.bat                   Builds forge.exe with gcc, then copies module/ → forge/bin/lib/
  module/                     Built-in module sources
    helix/
      console/                console module (print, input, clear, color)
        config.json           Module manifest (name, version, funcs list)
        src/                  Per-function .hlx source files
  forge/
    main.c                    Entry point, CLI, pipeline dispatcher
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
    bin/
      bin/                    forge.exe, fg.exe, 4g.exe
      lib/                    Module files (copied here by build.bat)
```

---

## Error Reporting

Forge emits structured, colored diagnostics in the style of rustc:

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

Implemented in [`forge/helix/src/errors/forge-errors.c`](forge/helix/src/errors/forge-errors.c).

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

At build time, `build.bat` copies the entire `module/` tree into `forge/bin/lib/`
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
                                            └─► Forge Assembler (Internal x86 Encoder & COFF Writer)
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
  - `gcc` — builds the Forge compiler itself
  - `ld` — links `.obj` → `.exe` (external dependency, NASM dependency has been completely removed)

---

## Build Forge

```bat
build.bat
```

What `build.bat` does:
1. Compiles all Forge C sources with `gcc -std=c11 -O2`
2. Writes `forge.exe`, `fg.exe`, `4g.exe` to `forge/bin/bin/`
3. Copies `module/` → `forge/bin/lib/` (creates `lib/` if missing)

---

## Usage

Add `forge\bin\bin` to your `PATH`, then use `forge`, `fg`, or `4g` interchangeably.

### Compile and run

```powershell
# Compile + link → .exe
forge src.hlx
4g src.hlx

# Compile, link, then run immediately
forge src.hlx -run
```

### Output modes

```powershell
# -asm: emit assembly text only
forge src.hlx -asm
forge src.hlx -asm -o out.asm

# -obj: compile → .obj via Forge Assembler (stop before link)
forge src.hlx -obj
forge src.hlx -obj -o lib.obj
```

### Debug / inspection

```powershell
forge src.hlx -dump-tokens
forge src.hlx -dump-ast
forge src.hlx -dump-ir
```

### Cross-language linking

```powershell
# Step 1 – compile each source to an object file
forge samples\hello_mixed.hlx    -obj -o samples\helix.obj
forge samples\mylib.c            -obj -o samples\clib.obj

# Step 2 – link into a single exe
forge -link samples\helix.obj samples\clib.obj -o samples\mixed.exe

# Step 3 – run
samples\mixed.exe
```

You can also mix Forge `.obj` with objects from `gcc -c`:

```powershell
gcc -c -O2 mylib.c -o mylib.obj
forge src.hlx -obj -o src.obj
forge -link src.obj mylib.obj -o program.exe
```

**ABI compatibility:**
- All Forge-generated code targets the Windows x64 ABI
  (first 4 integer args in `rcx/rdx/r8/r9`, 32-byte shadow space, 16-byte stack alignment).
- `global` symbols are plain COFF public symbols (no leading underscore on x64).
- `extern` declarations resolve directly to matching COFF exports.
- `gcc -c` with MinGW64 produces the same COFF format and ABI — symbols link without mangling.

---

## Options Reference

| Flag              | Description                                          |
|-------------------|------------------------------------------------------|
| `-asm`            | Output `.asm` text only (debug/preview)              |
| `-obj`            | Compile to `.obj` via Forge Assembler (stop before link) |
| `-link a.obj ...` | Link one or more `.obj` files into `.exe`            |
| `-o <file>`       | Override output filename                             |
| `-run`            | Build `.exe`, then execute it                        |
| `-dump-tokens`    | Print lexer tokens and exit                          |
| `-dump-ast`       | Print parsed AST and exit                            |
| `-dump-ir`        | Print lowered IR and exit                            |
| `--help`          | Show usage                                           |

---

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
- [x] `-asm` assembly preview
- [x] `-obj` object file output via internal Forge Assembler
- [x] `-link` multi-object linker
- [x] Auto-copy modules to `forge/bin/lib/` on build
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
