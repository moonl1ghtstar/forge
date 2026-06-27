# Forge Compiler

Forge is a multi-language compiler front-end targeting Windows x64.
It compiles `.hlx` (Helix) and `.c` (C subset) source files into
x86-64 Windows assembly and can drive NASM and `ld` to produce native `.exe` binaries.

---

## Language Support

### Helix

A small JavaScript-style language built for command-line programs:

- `function` declarations
- Top-level statements (auto-wrapped into `main`)
- Variables: `var`, `let`, `const`, `global`
- Control flow: `if`, `else`, `while`, `until`, `do`, `for`, `switch`
- Loop control: `break()`, `pass()`
- Module imports
- `extern` function declarations (FFI)

### C subset

A minimal C frontend sharing the same AST/IR and codegen as Helix:

- `int` typed variables and parameters
- Functions with up to 4 parameters
- `if`, `while`, `for`, `break`, `continue`
- Return statements
- Arithmetic and comparison expressions

---

## Project Layout

```
forge/
  main.c                  Entry point, CLI, pipeline dispatcher
  helix/src/
    lexer/                Helix lexer
    parser/               Helix parser
    sema/                 Semantic analysis (shared by C frontend)
    codegen/              x86-64 codegen (Windows x64 ABI, shared by both)
    ast/                  AST node types (shared IR)
  c/src/
    lexer/                C lexer
    parser/               C parser
    frontend/             C frontend (parse → sema → codegen)
docs/                     Language grammar and notes
samples/                  Sample programs (.hlx and .c)
module/                   Built-in modules
build.bat                 Builds forge.exe with gcc
```

---

## Build Pipeline

```
source (.hlx or .c)
  └─► Forge codegen_emit()    (shared codegen, Windows x64 ABI)
        └─► .asm              (NASM-compatible x86-64 text)
              └─► nasm -f win64
                    └─► .obj  (COFF object, Win64 compatible)
                          └─► ld + MinGW CRT
                                └─► .exe
```

Both language frontends produce identical `.asm` output through the same
`codegen_emit()` entry point, so the resulting `.obj` files share the same
Windows x64 ABI and can be freely linked together.

---

## Build Requirements

- Windows (x64)
- MSYS2 / MinGW64 toolchain
  - `gcc` (builds the forge compiler itself)
  - `nasm` (assembles `.asm` → `.obj`)
  - `ld` (links `.obj` → `.exe`)

---

## Build Forge

```powershell
.\build.bat
```

Or manually:

```powershell
gcc -std=c11 -O2 -Wall -Wextra `
  -Iforge\helix\src -Iforge\helix\src\ast -Iforge\helix\src\lexer `
  -Iforge\helix\src\parser -Iforge\helix\src\sema -Iforge\helix\src\codegen `
  -Iforge\c\src -Iforge\c\src\lexer -Iforge\c\src\parser -Iforge\c\src\frontend `
  forge\main.c forge\helix\src\lexer\helix-lexer.c `
  forge\helix\src\parser\helix-parser.c forge\helix\src\ast\helix-ast.c `
  forge\helix\src\sema\helix-sema.c forge\helix\src\codegen\helix-codegen.c `
  forge\c\src\lexer\c-lexer.c forge\c\src\parser\c-parser.c `
  forge\c\src\frontend\c-frontend.c -o forge\bin\forge.exe
```

---

## Usage

Add `forge\bin` to your `PATH`, then:

### Output modes

```powershell
# Default: compile + link → .exe
forge src.hlx
forge src.c

# -asm: emit assembly text only (debug / preview)
forge src.hlx -asm
forge src.c   -asm -o output.asm

# -obj: compile → .obj via nasm (stop before link)
forge src.hlx -obj
forge src.c   -obj -o lib.obj
```

### Run after build

```powershell
forge src.hlx -run
```

### Debug / inspection

```powershell
forge src.hlx -dump-tokens
forge src.c   -dump-ast
```

### Cross-language linking

Compile Helix and C sources independently to `.obj`, then link them
in a single `forge -link` call under the same Windows x64 ABI linker:

```powershell
# Step 1 – compile each source to an object file
forge samples\hello_helix.hlx -obj -o samples\helix.obj
forge samples\hello_c.c       -obj -o samples\clib.obj

# Step 2 – link both objects into one exe
forge -link samples\helix.obj samples\clib.obj -o samples\mixed.exe

# Step 3 – run
samples\mixed.exe
```

You can also mix Forge `.obj` with objects produced by `gcc -c`:

```powershell
gcc -c -O2 mylib.c -o mylib.obj
forge src.hlx -obj -o src.obj
forge -link src.obj mylib.obj -o program.exe
```

**ABI compatibility notes:**
- All Forge-generated code targets the Windows x64 ABI  
  (first 4 integer args in `rcx/rdx/r8/r9`, 32-byte shadow space, 16-byte stack alignment).
- `global` symbols in Forge `.asm` are plain COFF public symbols (no leading underscore on x64).
- `extern` declarations in Helix/C source resolve directly to matching COFF exports.
- `gcc -c` with MinGW64 produces the same COFF format and ABI, so symbols link without mangling.

---

## Options Reference

| Flag              | Description                                          |
|-------------------|------------------------------------------------------|
| `-asm`            | Output `.asm` text only (debug/preview)              |
| `-obj`            | Compile to `.obj` via nasm (stop before link)        |
| `-link a.obj ...` | Link one or more `.obj` files into `.exe`            |
| `-o <file>`       | Override output filename                             |
| `-run`            | Build `.exe`, then execute it                        |
| `-dump-tokens`    | Print lexer tokens and exit                          |
| `-dump-ast`       | Print parsed AST and exit                            |
| `--help`          | Show usage                                           |

---

## Example Programs

### Helix

```hlx
import console

function main() {
    console.print("Hello from Helix!");
    return 0;
}
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

- [x] Helix frontend
- [x] C frontend (int subset)
- [x] Shared AST/IR and codegen
- [x] `-asm` assembly preview
- [x] `-obj` object file output
- [x] `-link` multi-object linker
- [ ] String literals in C frontend
- [ ] `printf`/`scanf` builtins in C frontend

### Long-term architecture

- One compiler core, many language frontends
- One shared IR (currently Helix AST reused for C)
- Dedicate IR layer (lower both ASTs into a common IR before codegen)
- Multiple backends (current: NASM x86-64; future: LLVM IR, direct ELF/COFF emit)
- Consistent diagnostics across every language
- Go frontend (separate package-aware loader, no forced Helix model)
