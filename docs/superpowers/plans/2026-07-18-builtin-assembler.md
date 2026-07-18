# Forge Built-in Assembler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Implement a built-in Assembler in Forge that converts `.asm` into COFF `.obj` directly, eliminating the dependency on `nasm` for compiling Helix/C frontend outputs and raw user ASM inputs.

**Architecture:** A handwritten Lexer tokenizes the assembly stream; a Parser parses instructions, directives, and operands; an Encoder translates operands and mnemonics to machine code bytes; and a COFF Writer packs them into a valid `.obj` file.

**Tech Stack:** C11, standard platform libraries.

## Global Constraints
- Target format: Windows x64 PE/COFF object file.
- Direct command extension-based routing (`forge main.asm` / `forge main.asm -obj`).
- Shared assembler engine between compiler backend output and user ASM input.

---

### Task 1: Lexer and Parser Verification

Verify existing ASM lexer/parser subset compatibility with Forge codegen ASM output format.

**Files:**
- Modify: `forge/assembler/asm-lexer.c`
- Modify: `forge/assembler/asm-parser.c`

**Interfaces:**
- Consumes: Raw source string.
- Produces: `AsmProgram` AST structure.

- [x] **Step 1: Check keyword parsing**
Verify `asm-parser.c` handles all directives (`section`, `global`, `extern`, `db`, `resb`) and instructions.
- [x] **Step 2: Run verification**
Compile `samples/hello_out.asm` via `forge` assembler and verify there are no parse errors.
- [x] **Step 3: Commit**
```bash
git add forge/assembler/asm-lexer.c forge/assembler/asm-parser.c
git commit -m "verify asm parser/lexer"
```

---

### Task 2: Encoder Bugfixes and Shift Instructions

Fix the 32-bit extended register REX prefix bug in `encode_alu_regimm` and add encoding support for `shr` and `shl`.

**Files:**
- Modify: `forge/assembler/x86-encode.c`

**Interfaces:**
- Consumes: `AsmStatement` AST nodes.
- Produces: Encoded instruction byte stream.

- [x] **Step 1: Fix REX prefix check in encode_alu_regimm**
Check:
```c
uint8_t rex = REX(W, 0, 0, REGHI(rm_reg));
if (rex != 0x40 || W)
    EMIT(rex);
```
- [x] **Step 2: Add encode_shift support**
Implement:
```c
static int encode_shift(const AsmStatement *stmt, uint8_t *buf, int buf_size, int opext) {
    int n = 0;
    const AsmOperand *dst = &stmt->operands[0];
    const AsmOperand *src = &stmt->operands[1];
    int W = (dst->reg_size == 64);
    uint8_t rex = REX(W, 0, 0, REGHI(dst->reg_idx));
    if (rex != 0x40 || W) EMIT(rex);
    EMIT(0xC1);
    EMIT(0xC0 | (opext << 3) | REGLO(dst->reg_idx));
    EMIT((uint8_t)(src->imm_val & 0xFF));
    return n;
}
```
Register `shr` (opext=5) and `shl` (opext=4) in `x86_encode` dispatch.
- [x] **Step 3: Compile and verify**
Run: `cmd.exe /c build.bat`
- [x] **Step 4: Commit**
```bash
git add forge/assembler/x86-encode.c
git commit -m "fix alu rex, add shr/shl"
```

---

### Task 3: CLI Suffix-based Suffix Routing

Ensure `main.c` executes the built-in assembler directly based on the `.asm` file extension, removing standalone subcommands.

**Files:**
- Modify: `forge/main.c`

**Interfaces:**
- Consumes: CLI input arguments.
- Produces: Program compilation/linking invocation.

- [x] **Step 1: Check main.c file extension handling**
Verify `.asm` routing in `main.c` maps directly to `assemble_object(source_path, work_obj_path)` without intermediate subcommands.
- [x] **Step 2: Rebuild and run sample test suites**
Run:
```bash
forge\bin\bin\forge.exe samples\hello.hlx -run
forge\bin\bin\forge.exe samples\loop.hlx -run
```
- [x] **Step 3: Commit**
```bash
git add forge/main.c
git commit -m "verify cli suffix routing"
```
