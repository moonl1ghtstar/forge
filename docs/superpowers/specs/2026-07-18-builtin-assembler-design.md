# Forge Built-in Assembler Design Specification

This document details the architecture and implementation of the built-in x86-64 Assembler for the Forge compiler. 

The built-in assembler allows Forge to translate assembly language input (`.asm`) directly into COFF object files (`.obj`) without requiring external dependencies like `nasm`. The Helix/C codegen and user-supplied ASM inputs share the same internal assembler engine.

---

## 1. Lexer (asm-lexer.c / asm-lexer.h)

The lexer performs lexical analysis on the raw assembly source text. It tokenizes the input stream into discrete tokens for the parser.

*   **Punctuation Support**: `,`, `:`, `[`, `]`, `+`, `-`, `.`.
*   **Comments**: Skips anything starting with `;` to the end of the line, generating a `NEWLINE` token to separate assembly statements.
*   **Strings**: Supports `'...'` and `\"...\"` character sequences, stripping the enclosing quotes and exposing raw byte lists (e.g. for `db` data definition directives).
*   **Numbers**: Supports base-10 decimal and base-16 hexadecimal (`0x...`) integer formats.
*   **Local Labels**: Lexes local dot-prefixed symbols (e.g. `.Lexit0`) directly as identifiers.

---

## 2. Parser (asm-parser.c / asm-parser.h)

The parser processes token streams statement-by-statement, building a flat list of `AsmStatement` structures that represent the AST of the assembly code.

*   **Statement Types**:
    *   `ASM_STMT_SECTION`: Segment declarations (`section .text`, `section .data`, `section .bss`).
    *   `ASM_STMT_GLOBAL` / `ASM_STMT_EXTERN`: Linkage directives.
    *   `ASM_STMT_LABEL`: Standalone label definitions.
    *   `ASM_STMT_DB` / `ASM_STMT_RESB`: Initialized and uninitialized data segment allocations.
    *   `ASM_STMT_INSTR`: Instruction mnemonic and operand lists.
*   **Operand Representation**:
    *   `ASMOP_REG`: Registers mapped by size (64, 32, 16, 8-bit) and index (0-15).
    *   `ASMOP_IMM`: Signed immediate values.
    *   `ASMOP_MEM`: Memory references supporting displacement offsets (`[rbp - 8]`) or RIP-relative addressing (`[rel symbol]`).
    *   `ASMOP_LABEL`: Jumps/calls referencing a label symbol.

---

## 3. Encoder (x86-encode.c / x86-encode.h)

The encoder translates assembly instructions into raw x86-64 machine code bytes.

### 3.1. REX Prefix Bug Fix
A bug was identified in `encode_alu_regimm` where the REX prefix was only emitted for 64-bit operands (`W == 1`). If a 32-bit extended register (e.g. `r8d` to `r15d`) was used with an immediate operand, the REX prefix (`0x41`) was omitted. This caused the CPU to decode the instruction using a low register index (e.g. `ecx` instead of `r9d`).

This has been corrected to dynamically compute the REX prefix and emit it if either the operand width is 64-bit or the register is an extended register:
```c
uint8_t rex = REX(W, 0, 0, REGHI(rm_reg));
if (rex != 0x40 || W)
    EMIT(rex);
```
Unconditional REX emissions in `idiv` and `neg` have also been optimized to only emit if needed.

### 3.2. Shift Instructions Addition
Support for `shr` (logical shift right) and `shl` (logical shift left) has been added to support variable colors in builtins (VT100 code rendering):
*   **Opcode**: `C1 /opext ib`
*   **Extensions**: `opext = 4` for `shl`, `opext = 5` for `shr`.

---

## 4. COFF Writer (coff-writer.c / coff-writer.h)

The COFF writer gathers compiled section bytes, relocations, and the symbol table, formatting them into a standard Windows x64 PE/COFF object file (`.obj`).

*   **Section Headers**: Configures virtual sections (`.text`, `.data`, `.bss`) with proper alignment (16-byte for code, 4-byte for data) and characteristics flags.
*   **Relocation Entries**: Translates unresolved label references or `[rel symbol]` memory targets into AMD64 REL32/ADDR32NB relocation records.
*   **Symbol Table**: Builds static and external public symbol entries, utilizing a dynamic string table for symbols exceeding 8 characters.

---

## 5. Verification Results

All tests completed successfully:
1.  **Direct Execution**: `forge main.hlx` correctly outputs the assembly, compiles it to `.obj` internally, links it using `ld`, and produces a working executable.
2.  **Extension-Based Execution**: `forge main.asm` and `forge main.asm -obj` run the assembler engine correctly.
3.  **Cross-Language Linkage**: Mixed compilation of Helix objects and C-compiled objects works correctly.
