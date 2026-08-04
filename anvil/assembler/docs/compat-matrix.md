# Anvil built-in assembler — NASM compatibility matrix

Reference: `nasm -f win64` (v3.02rc9), Anvil assembler sources under
`anvil/assembler/src/`. Verification harnesses:

- `tests/test_asm_e2e.py` — whole-object comparison (`.text` bytes,
  relocations, external symbols) over the asm emitted by both code
  generators for all samples. **Result: 12/12 PASS.**
- `tests/test_asm_compat.py` — per-instruction `.text` byte comparison
  against NASM. **Result: 34/34 PASS.**

`anvil/main.c` `assemble_object()` now calls `anv_assemble_file()` directly;
NASM is no longer invoked anywhere in the compiler pipeline. All 10 sample
programs (Helix + C) compile, link, and run correctly.

## Supported instruction set (byte-identical to NASM)

### Moves
| Form | Encoding | Notes |
|---|---|---|
| mov reg, reg (mr form) | `89 /r` (`88 /r` 8-bit) | first op = r/m, second = reg |
| mov reg, imm (32-bit dst) | `B8+rd id` | incl. r8d-r15d via REX.B |
| mov reg, imm (8-bit dst) | `B0+rd ib` | |
| mov r64, imm, fits unsigned 32 | `B8+rd id` (no REX.W) | zero-extend opt, NASM default |
| mov r64, imm, fits signed 32 (<0) | `REX.W C7 /0 id` | sign-extend |
| mov r64, imm64 | `REX.W B8+rd io` | |
| mov reg, [mem] / [mem], reg | `8B/89` (`8A/88` 8-bit) | incl. RIP-relative reloc |
| mov [mem], imm8/16/32/64 | `C6/C7` + size prefix | |
| movzx / movsx | `0F B6/B7`, `0F BE/BF` | reg or mem src |
| movq xmm, r64 / r64, xmm | `66 0F 6E/7E` | |
| movq xmm, [mem] / [mem], xmm | `66 0F 6E/7E` | |
| movsd xmm, xmm | `F2 0F 10 /r` | |
| movsd xmm, [mem] / [mem], xmm | `F2 0F 10/11 /r` | |

### ALU (mr forms, accumulator special cases)
| Form | Encoding | Notes |
|---|---|---|
| add/or/and/sub/xor/cmp reg, reg | `01/09/21/29/31/39 /r` | 8-bit: `00/08/20/28/30/38` |
| reg, imm fits imm8 | `83 /opext ib` | |
| reg, imm (not imm8) | `81 /opext id` | |
| AL/EAX/RAX, imm8 | `04/0C/24/2C/34/3C ib` | accumulator short form |
| EAX/RAX, imm (not imm8) | `05/0D/25/2D/35/3D id` | accumulator long form |
| test reg, reg | `85 /r` (`84 /r` 8-bit) | |
| test reg, imm | `F7 /0 id` | |
| inc/dec/neg/not | `FF /0,/1`, `F7 /3,/2` | |
| imul reg, reg | `0F AF /r` | |
| idiv/div/mul | `F7 /7,/6,/4` | |
| shifts shl/shr/sar/rol/ror | `C1 /opext ib`, `D3` (cl) | |
| cdq / cqo / nop / ret | `99`, `48 99`, `90`, `C3` | |

### Control flow
| Form | Encoding | Notes |
|---|---|---|
| call label / reg | `E8 rel32`, `FF D0-7` | extern calls → reloc |
| jmp short (fits rel8) | `EB rel8` | relaxed, matches NASM |
| jmp near | `E9 rel32` | |
| jcc short (fits rel8) | `70-7F rel8` | all 16 conditions |
| jcc near | `0F 80-8F rel32` | |
| setcc reg8 / byte [mem] | `0F 90-9F /r` | incl. `[rip+disp]` reloc |

### SSE2 scalar
| Form | Encoding | Notes |
|---|---|---|
| comisd / subsd / mulsd xmm, xmm | `66 0F 2F`, `F2 0F 5C`, `F2 0F 59` | |
| cvtsi2sd xmm, r64/r32 | `F2 0F 2A /r` | |
| cvttsd2si r64/r32, xmm | `F2 0F 2C /r` | |
| xorpd xmm, xmm | `66 0F 57 /r` | |
| lea reg, [mem] | `8D /r` | incl. `[rel sym]` → section-symbol reloc |

### Addressing / prefixes
| Feature | Notes |
|---|---|
| 64-bit addressing `[rbp-8]`, `[rsp+40]`, `[base+idx*scale±disp]` | full ModRM/SIB selection incl. rsp/r12 SIB and rbp/r13 disp8 |
| RIP-relative `[rel label]` | reloc against `.data`/`.bss` section symbol + addend (NASM convention) |
| 32-bit base/index (`[edx+48]`) | `67` address-size override, emitted before legacy/REX prefixes |
| byte/word/dword/qword size prefixes | `mov [mem], imm` requires explicit size |
| `extern` / `global` / `section` / `db/dw/dd/dq/resb..resq` | unused externs dropped (NASM behavior) |

## Known gaps / non-goals

- Only the instruction subset emitted by the Helix/C code generators is
  implemented (about 50 mnemonics). Anything else fails with a clear
  `unsupported mnemonic` error.
- NASM preprocessor (macros, `%if`, `times`, multi-line), directives
  (`align`, `bits`), and `-g` debug output are not supported. Generated
  asm never uses them.
- `push/pop imm` uses `6A/68`; `movsxd`, 16-bit addressing, AVX, and
  SSE4 are not implemented (codegen does not emit them).
- COFF string table is written immediately after the symbol table
  (NASM 3.02rc9 convention; no padding). Symbol table is always
  present; no `.file` aux records (not needed by ld).

## Pipeline change

- `anvil/main.c:811` `assemble_object()` → `anv_assemble_file(asm_path, obj_path)`.
- NASM dependency fully removed from the compiler build (no `run_process("nasm")`).
