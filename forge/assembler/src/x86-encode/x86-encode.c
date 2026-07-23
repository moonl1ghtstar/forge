/*
 * x86-encode.c - x86-64 instruction encoder for Forge ASM
 *
 * Encoding strategy:
 *   - All jumps/calls use NEAR (32-bit relative) form so instruction
 *     sizes are fixed and predictable (no relaxation needed).
 *   - REX.W is emitted for 64-bit operands.
 *   - Extended registers (r8-r15) get REX.R or REX.B as required.
 *   - Memory refs [rbp-N] / [rsp+N] are encoded with SIB where needed.
 *   - RIP-relative [rel sym] creates a relocation entry.
 *   - Forward label references are also relocated (rel32 = 0 placeholder).
 *
 * Supported mnemonics (superset of Forge's codegen output):
 *   mov, lea, push, pop, ret, call, jmp
 *   je/jz, jne/jnz, jg, jl, jge, jle, ja, jb, jo, jno, js, jns, jp, jnp
 *   add, sub, cmp, test
 *   imul, mul, idiv, div, neg, inc, dec, cdq, cqo
 *   and, or, xor, not
 *   shl, shr, sar, rol, ror
 *   mov, movzx, movsx, lea
 *   sete/setz, setne/setnz, setg, setge, setl, setle, setb, seta, seto, sets
 *   nop
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "x86-encode.h"

/* ---- Helpers ---- */

static int fits_imm8(long v) { return v >= -128 && v <= 127; }
static int fits_imm32(long v) { return v >= INT32_MIN && v <= INT32_MAX; }

/* Emit one byte into buf[n], return 0 on overflow */
#define EMIT(b)                  \
    do {                         \
        if (n >= buf_size)       \
            return -1;           \
        buf[n++] = (uint8_t)(b); \
    } while (0)

/* Encode a REX prefix byte.  W=64-bit, R=reg>=8, X=index>=8, B=rm/base>=8 */
#define REX(W, R, X, B) (0x40 | ((W) ? 8 : 0) | ((R) ? 4 : 0) | ((X) ? 2 : 0) | ((B) ? 1 : 0))
/* Only emit REX if any bit is set */
#define EMIT_REX(W, R, X, B)            \
    do {                                \
        uint8_t rex_ = REX(W, R, X, B); \
        if (rex_ != 0x40)               \
            EMIT(rex_);                 \
    } while (0)

/* Low 3 bits of a register index */
#define REGLO(r) ((r) & 7)
/* 1 if register needs REX.B or REX.R extension */
#define REGHI(r) (((r) >> 3) & 1)

/* Encode ModRM: mod=11 (reg-reg) */
#define MODRM_RR(reg, rm) (0xC0 | (REGLO(reg) << 3) | REGLO(rm))

/* ---- Context ---- */

void x86_ctx_init(X86EncodeCtx *ctx,
                  long (*resolve_label)(const char *name, void *data),
                  void *data) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->resolve_label = resolve_label;
    ctx->ctx_data = data;
}

void x86_ctx_free(X86EncodeCtx *ctx) {
    int i;
    for (i = 0; i < ctx->reloc_count; i++)
        free(ctx->relocs[i].symbol);
    free(ctx->relocs);
    ctx->relocs = NULL;
    ctx->reloc_count = 0;
    ctx->reloc_cap = 0;
}

void x86_add_reloc(X86EncodeCtx *ctx, uint32_t offset, const char *sym) {
    if (ctx->reloc_count >= ctx->reloc_cap) {
        ctx->reloc_cap = ctx->reloc_cap ? ctx->reloc_cap * 2 : 16;
        ctx->relocs = (X86Reloc *)realloc(ctx->relocs,
                                          sizeof(X86Reloc) * ctx->reloc_cap);
    }
    ctx->relocs[ctx->reloc_count].offset = offset;
    ctx->relocs[ctx->reloc_count].symbol = strdup(sym);
    ctx->relocs[ctx->reloc_count].is_rel32 = 1;
    ctx->reloc_count++;
}

/* ---- Memory encoding helpers ---- */

/*
 * emit_mem — unified memory operand encoder.
 * Handles BASE_ONLY, BASE_DISP, and SIB kinds.
 * reg_field: reg/opcode field in ModRM.
 * Returns 0 on success, -1 on overflow.
 */
static int emit_mem(uint8_t *buf, int *n_ptr, int buf_size,
                    int reg_field, const AsmMemOp *mem) {
    int n = *n_ptr;

    if (mem->kind == MEMKIND_BASE_ONLY) {
        int base = mem->base_reg;
        int need_sib = (REGLO(base) == 4); /* rsp/r12 always need SIB */
        /* mod=00, but rbp/r13 with mod=00 means disp32 — force mod=01 disp=0 */
        int mod = (REGLO(base) == 5) ? 1 : 0;
        if (need_sib) {
            EMIT((mod << 6) | (REGLO(reg_field) << 3) | 4);
            EMIT((0 << 6) | (4 << 3) | REGLO(base)); /* no index */
        } else {
            EMIT((mod << 6) | (REGLO(reg_field) << 3) | REGLO(base));
        }
        if (mod == 1)
            EMIT(0); /* disp8=0 for rbp/r13 */
        *n_ptr = n;
        return 0;
    }

    if (mem->kind == MEMKIND_SIB) {
        int base = mem->base_reg;
        int index = mem->index_reg; /* -1 = none */
        long disp = mem->disp;
        int scale = mem->scale; /* 1,2,4,8 */
        int ss = (scale == 8) ? 3 : (scale == 4) ? 2
                                : (scale == 2)   ? 1
                                                 : 0;
        int mod;
        if (disp == 0 && REGLO(base) != 5)
            mod = 0;
        else if (fits_imm8(disp))
            mod = 1;
        else
            mod = 2;
        if (mod == 0 && REGLO(base) == 5)
            mod = 1;

        EMIT((mod << 6) | (REGLO(reg_field) << 3) | 4); /* SIB follows */
        int idx_field = (index < 0) ? 4 : REGLO(index); /* 4 = no index */
        EMIT((ss << 6) | (idx_field << 3) | REGLO(base));

        if (mod == 1) {
            EMIT((uint8_t)(disp & 0xFF));
        } else if (mod == 2) {
            uint32_t d = (uint32_t)(int32_t)disp;
            EMIT(d & 0xFF);
            EMIT((d >> 8) & 0xFF);
            EMIT((d >> 16) & 0xFF);
            EMIT((d >> 24) & 0xFF);
        }
        *n_ptr = n;
        return 0;
    }

    /* MEMKIND_BASE_DISP */
    {
        int base = mem->base_reg;
        long disp = mem->disp;
        int need_sib = (REGLO(base) == 4); /* rsp/r12 need SIB */
        int mod;
        if (disp == 0 && REGLO(base) != 5)
            mod = 0;
        else if (fits_imm8(disp))
            mod = 1;
        else
            mod = 2;
        if (mod == 0 && REGLO(base) == 5)
            mod = 1;

        if (need_sib) {
            EMIT((mod << 6) | (REGLO(reg_field) << 3) | 4);
            EMIT((0 << 6) | (4 << 3) | REGLO(base));
        } else {
            EMIT((mod << 6) | (REGLO(reg_field) << 3) | REGLO(base));
        }
        if (mod == 1) {
            EMIT((uint8_t)(disp & 0xFF));
        } else if (mod == 2) {
            uint32_t d = (uint32_t)(int32_t)disp;
            EMIT(d & 0xFF);
            EMIT((d >> 8) & 0xFF);
            EMIT((d >> 16) & 0xFF);
            EMIT((d >> 24) & 0xFF);
        }
        *n_ptr = n;
        return 0;
    }
}

/* REX bits for a full SIB/BASE_ONLY/BASE_DISP mem operand */
static uint8_t rex_for_mem_full(int W, int reg_op, const AsmMemOp *mem) {
    int R = REGHI(reg_op);
    int B = 0, X = 0;
    if (mem->kind == MEMKIND_BASE_DISP || mem->kind == MEMKIND_BASE_ONLY ||
        mem->kind == MEMKIND_SIB) {
        B = REGHI(mem->base_reg);
        if (mem->kind == MEMKIND_SIB && mem->index_reg >= 0)
            X = REGHI(mem->index_reg);
    }
    return REX(W, R, X, B);
}

/* ---- REX prefix for a register-memory instruction ----
 * reg_op: the reg field (destination or opcode-extension)
 * mem:    memory operand
 * W:      1 for 64-bit operand
 */
static uint8_t rex_for_mem(int W, int reg_op, const AsmMemOp *mem) {
    if (mem->kind == MEMKIND_BASE_DISP || mem->kind == MEMKIND_BASE_ONLY ||
        mem->kind == MEMKIND_SIB)
        return rex_for_mem_full(W, reg_op, mem);
    /* RIP_REL: B=0, X=0 */
    return REX(W, REGHI(reg_op), 0, 0);
}

/* ---- Emit rel32 for a label (forward/backward/extern) ---- */
static int emit_rel32_label(X86EncodeCtx *ctx, uint8_t *buf, int *n_ptr,
                            int buf_size, const char *label,
                            uint32_t instr_offset) {
    int n = *n_ptr;
    /* The rel32 offset = target - (instr_offset + 4) */
    long target = ctx->resolve_label ? ctx->resolve_label(label, ctx->ctx_data) : -1;
    int32_t rel = 0;
    if (target >= 0) {
        /* Local backward (or known) label */
        long after = (long)(instr_offset + 4);
        rel = (int32_t)(target - after);
    } else {
        /* Unknown / extern: emit 0 placeholder and add relocation */
        x86_add_reloc(ctx, instr_offset, label);
        rel = 0;
    }
    EMIT(rel & 0xFF);
    EMIT((rel >> 8) & 0xFF);
    EMIT((rel >> 16) & 0xFF);
    EMIT((rel >> 24) & 0xFF);
    *n_ptr = n;
    return 0;
}

/* ---- Instruction encoding ---- */

/*
 * Encode opcode for MOV with a register destination and memory source
 * or store (dest=mem, src=reg).
 *
 * load:  MOV reg, [mem]  → 8B /r
 * store: MOV [mem], reg  → 89 /r
 */
static int encode_mov(X86EncodeCtx *ctx, const AsmStatement *stmt,
                      uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *dst = &stmt->operands[0];
    const AsmOperand *src = &stmt->operands[1];

    /* MOV reg, reg */
    if (dst->kind == ASMOP_REG && src->kind == ASMOP_REG) {
        int W = (dst->reg_size == 64);
        /* Use 8B /r: mov r64, r/m64 (RM form) */
        uint8_t rex = REX(W, REGHI(dst->reg_idx), 0, REGHI(src->reg_idx));
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0x8B);
        EMIT(MODRM_RR(dst->reg_idx, src->reg_idx));
        return n;
    }

    /* MOV reg, imm */
    if (dst->kind == ASMOP_REG && src->kind == ASMOP_IMM) {
        long val = src->imm_val;
        int W = (dst->reg_size == 64);
        if (W) {
            /* Use REX.W C7 /0 imm32 (sign-extends) for values fitting int32 */
            if (fits_imm32(val)) {
                EMIT(REX(1, 0, 0, REGHI(dst->reg_idx)));
                EMIT(0xC7);
                EMIT(0xC0 | REGLO(dst->reg_idx)); /* mod=11, /0, rm=dst */
                int32_t v32 = (int32_t)val;
                EMIT(v32 & 0xFF);
                EMIT((v32 >> 8) & 0xFF);
                EMIT((v32 >> 16) & 0xFF);
                EMIT((v32 >> 24) & 0xFF);
            } else {
                /* Full 64-bit: REX.W B8+rd imm64 */
                EMIT(REX(1, 0, 0, REGHI(dst->reg_idx)));
                EMIT(0xB8 | REGLO(dst->reg_idx));
                uint64_t v64 = (uint64_t)val;
                int k;
                for (k = 0; k < 8; k++)
                    EMIT((v64 >> (k * 8)) & 0xFF);
            }
        } else {
            /* 32-bit MOV: B8+rd imm32 */
            if (REGHI(dst->reg_idx))
                EMIT(REX(0, 0, 0, 1));
            EMIT(0xB8 | REGLO(dst->reg_idx));
            uint32_t v32 = (uint32_t)(int32_t)val;
            EMIT(v32 & 0xFF);
            EMIT((v32 >> 8) & 0xFF);
            EMIT((v32 >> 16) & 0xFF);
            EMIT((v32 >> 24) & 0xFF);
        }
        return n;
    }

    /* MOV reg, [mem] — load */
    if (dst->kind == ASMOP_REG && src->kind == ASMOP_MEM) {
        int W = (dst->reg_size == 64);
        if (src->mem.kind == MEMKIND_RIP_REL) {
            uint8_t rex = REX(W, REGHI(dst->reg_idx), 0, 0);
            if (rex != 0x40 || W)
                EMIT(rex);
            EMIT(0x8B);
            EMIT((0 << 6) | (REGLO(dst->reg_idx) << 3) | 5 /* RIP */);
            uint32_t rel_off = ctx->section_offset + n;
            x86_add_reloc(ctx, rel_off, src->mem.symbol);
            EMIT(0);
            EMIT(0);
            EMIT(0);
            EMIT(0);
        } else {
            uint8_t rex = rex_for_mem(W, dst->reg_idx, &src->mem);
            if (rex != 0x40 || W)
                EMIT(rex);
            EMIT(0x8B);
            if (emit_mem(buf, &n, buf_size, dst->reg_idx, &src->mem) < 0)
                return -1;
        }
        return n;
    }

    /* MOV [mem], reg — store */
    if (dst->kind == ASMOP_MEM && src->kind == ASMOP_REG) {
        int W = (src->reg_size == 64);
        if (dst->mem.kind == MEMKIND_RIP_REL) {
            uint8_t rex = REX(W, REGHI(src->reg_idx), 0, 0);
            if (rex != 0x40 || W)
                EMIT(rex);
            EMIT(0x89);
            EMIT((0 << 6) | (REGLO(src->reg_idx) << 3) | 5);
            uint32_t rel_off = ctx->section_offset + n;
            x86_add_reloc(ctx, rel_off, dst->mem.symbol);
            EMIT(0);
            EMIT(0);
            EMIT(0);
            EMIT(0);
        } else {
            uint8_t rex = rex_for_mem(W, src->reg_idx, &dst->mem);
            if (rex != 0x40 || W)
                EMIT(rex);
            EMIT(0x89);
            if (emit_mem(buf, &n, buf_size, src->reg_idx, &dst->mem) < 0)
                return -1;
        }
        return n;
    }

    fprintf(stderr, "forge-asm: line %d: unsupported MOV form\n", stmt->line);
    return -1;
}

static int encode_lea(X86EncodeCtx *ctx, const AsmStatement *stmt,
                      uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *dst = &stmt->operands[0];
    const AsmOperand *src = &stmt->operands[1];
    if (dst->kind != ASMOP_REG || src->kind != ASMOP_MEM) {
        fprintf(stderr, "forge-asm: line %d: LEA requires reg, [mem]\n", stmt->line);
        return -1;
    }
    int W = (dst->reg_size == 64);
    if (src->mem.kind == MEMKIND_RIP_REL) {
        uint8_t rex = REX(W, REGHI(dst->reg_idx), 0, 0);
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0x8D);
        EMIT((0 << 6) | (REGLO(dst->reg_idx) << 3) | 5 /* RIP */);
        uint32_t rel_off = ctx->section_offset + n;
        x86_add_reloc(ctx, rel_off, src->mem.symbol);
        EMIT(0);
        EMIT(0);
        EMIT(0);
        EMIT(0);
    } else {
        uint8_t rex = rex_for_mem(W, dst->reg_idx, &src->mem);
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0x8D);
        if (emit_mem(buf, &n, buf_size, dst->reg_idx, &src->mem) < 0)
            return -1;
    }
    return n;
}

/* ADD / SUB / CMP / AND / OR / XOR (ALU ops) */
static int encode_alu_regimm(uint8_t *buf, int *n_ptr, int buf_size,
                             int W, int rm_reg, int opext, long imm) {
    int n = *n_ptr;
    /* opext: 0=ADD,1=OR,4=AND,5=SUB,6=XOR,7=CMP */
    uint8_t rex = REX(W, 0, 0, REGHI(rm_reg));
    if (rex != 0x40 || W)
        EMIT(rex);
    if (fits_imm8(imm)) {
        EMIT(0x83);
        EMIT(0xC0 | (opext << 3) | REGLO(rm_reg));
        EMIT((uint8_t)(imm & 0xFF));
    } else {
        EMIT(0x81);
        EMIT(0xC0 | (opext << 3) | REGLO(rm_reg));
        int32_t v = (int32_t)imm;
        EMIT(v & 0xFF);
        EMIT((v >> 8) & 0xFF);
        EMIT((v >> 16) & 0xFF);
        EMIT((v >> 24) & 0xFF);
    }
    *n_ptr = n;
    return 0;
}

static int encode_alu_regreg(uint8_t *buf, int *n_ptr, int buf_size,
                             int W, int dst, int src, uint8_t opcode) {
    int n = *n_ptr;
    /* opcode: ADD=03, OR=0B, AND=23, SUB=2B, XOR=33, CMP=3B */
    uint8_t rex = REX(W, REGHI(dst), 0, REGHI(src));
    if (rex != 0x40 || W)
        EMIT(rex);
    EMIT(opcode);
    EMIT(MODRM_RR(dst, src));
    *n_ptr = n;
    return 0;
}

static int encode_alu(const AsmStatement *stmt, uint8_t *buf, int buf_size,
                      int opext_imm, uint8_t opcode_rr) {
    int n = 0;
    const AsmOperand *dst = &stmt->operands[0];
    const AsmOperand *src = &stmt->operands[1];
    int W = (dst->kind == ASMOP_REG && dst->reg_size == 64);

    if (dst->kind == ASMOP_REG && src->kind == ASMOP_IMM) {
        if (encode_alu_regimm(buf, &n, buf_size, W,
                              dst->reg_idx, opext_imm, src->imm_val) < 0)
            return -1;
        return n;
    }
    if (dst->kind == ASMOP_REG && src->kind == ASMOP_REG) {
        if (encode_alu_regreg(buf, &n, buf_size, W,
                              dst->reg_idx, src->reg_idx, opcode_rr) < 0)
            return -1;
        return n;
    }
    /* dst=reg, src=[mem] */
    if (dst->kind == ASMOP_REG && src->kind == ASMOP_MEM) {
        /* rm form: opcode_rr - 8 (e.g. ADD 03->03, SUB 2B->2B, already rm form) */
        uint8_t rex = rex_for_mem(W, dst->reg_idx, &src->mem);
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(opcode_rr); /* these are already the /r (reg,rm) form */
        if (emit_mem(buf, &n, buf_size, dst->reg_idx, &src->mem) < 0)
            return -1;
        return n;
    }
    /* dst=[mem], src=reg */
    if (dst->kind == ASMOP_MEM && src->kind == ASMOP_REG) {
        /* mr form: opcode_rr - 3 (03->01=ADD, 0B->09=OR, 23->21=AND, 2B->29=SUB, 33->31=XOR, 3B->39=CMP) */
        uint8_t mr_op = opcode_rr - 2; /* reg field is src, rm is dst */
        uint8_t rex = rex_for_mem(W, src->reg_idx, &dst->mem);
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(mr_op);
        if (emit_mem(buf, &n, buf_size, src->reg_idx, &dst->mem) < 0)
            return -1;
        return n;
    }
    fprintf(stderr, "forge-asm: line %d: unsupported ALU form for '%s'\n",
            stmt->line, stmt->mnemonic);
    return -1;
}

/* MOVZX: movzx r32/64, r/m8  — zero-extend byte to 32 or 64 bit */
static int encode_movzx(const AsmStatement *stmt,
                        uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *dst = &stmt->operands[0];
    const AsmOperand *src = &stmt->operands[1];
    if (dst->kind != ASMOP_REG) {
        fprintf(stderr, "forge-asm: line %d: MOVZX requires reg dst\n", stmt->line);
        return -1;
    }
    int W = (dst->reg_size == 64);
    if (src->kind == ASMOP_REG) {
        /* movzx r32/64, r/m8: 0F B6 /r  (zero-extends to 64-bit) */
        uint8_t rex = REX(W, REGHI(dst->reg_idx), 0, REGHI(src->reg_idx));
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0x0F);
        /* B6 = 8-bit src, B7 = 16-bit src */
        EMIT(src->reg_size == 16 ? 0xB7 : 0xB6);
        EMIT(MODRM_RR(dst->reg_idx, src->reg_idx));
    } else if (src->kind == ASMOP_MEM) {
        uint8_t rex = rex_for_mem(W, dst->reg_idx, &src->mem);
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0x0F);
        EMIT(0xB6);
        if (emit_mem(buf, &n, buf_size, dst->reg_idx, &src->mem) < 0)
            return -1;
    } else {
        fprintf(stderr, "forge-asm: line %d: MOVZX: unsupported operand form\n", stmt->line);
        return -1;
    }
    return n;
}

/* MOVSX: movsx r32/64, r/m8/16 — sign-extend */
static int encode_movsx(const AsmStatement *stmt,
                        uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *dst = &stmt->operands[0];
    const AsmOperand *src = &stmt->operands[1];
    if (dst->kind != ASMOP_REG) {
        fprintf(stderr, "forge-asm: line %d: MOVSX requires reg dst\n", stmt->line);
        return -1;
    }
    int W = (dst->reg_size == 64);
    if (src->kind == ASMOP_REG) {
        uint8_t rex = REX(W, REGHI(dst->reg_idx), 0, REGHI(src->reg_idx));
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0x0F);
        EMIT(src->reg_size == 16 ? 0xBF : 0xBE); /* BF=16->32/64, BE=8->32/64 */
        EMIT(MODRM_RR(dst->reg_idx, src->reg_idx));
    } else if (src->kind == ASMOP_MEM) {
        uint8_t rex = rex_for_mem(W, dst->reg_idx, &src->mem);
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0x0F);
        EMIT(0xBE);
        if (emit_mem(buf, &n, buf_size, dst->reg_idx, &src->mem) < 0)
            return -1;
    } else {
        fprintf(stderr, "forge-asm: line %d: MOVSX: unsupported operand form\n", stmt->line);
        return -1;
    }
    return n;
}

/* TEST — reg/mem, reg or reg, imm */
static int encode_test(const AsmStatement *stmt,
                       uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *a = &stmt->operands[0];
    const AsmOperand *b = &stmt->operands[1];
    int W = (a->kind == ASMOP_REG && a->reg_size == 64);
    if (a->kind == ASMOP_REG && b->kind == ASMOP_REG) {
        /* TEST r/m, r: 85 /r */
        uint8_t rex = REX(W, REGHI(b->reg_idx), 0, REGHI(a->reg_idx));
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0x85);
        EMIT(MODRM_RR(b->reg_idx, a->reg_idx));
        return n;
    }
    if (a->kind == ASMOP_REG && b->kind == ASMOP_IMM) {
        /* TEST r/m, imm: F7 /0 imm32 */
        uint8_t rex = REX(W, 0, 0, REGHI(a->reg_idx));
        if (rex != 0x40 || W)
            EMIT(rex);
        EMIT(0xF7);
        EMIT(0xC0 | REGLO(a->reg_idx));
        int32_t v = (int32_t)b->imm_val;
        EMIT(v & 0xFF);
        EMIT((v >> 8) & 0xFF);
        EMIT((v >> 16) & 0xFF);
        EMIT((v >> 24) & 0xFF);
        return n;
    }
    fprintf(stderr, "forge-asm: line %d: TEST requires reg,reg or reg,imm\n", stmt->line);
    return -1;
}

/* PUSH reg or imm8/imm32 */
static int encode_push(const AsmStatement *stmt,
                       uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *op = &stmt->operands[0];
    if (op->kind == ASMOP_REG) {
        if (REGHI(op->reg_idx))
            EMIT(REX(0, 0, 0, 1));
        EMIT(0x50 | REGLO(op->reg_idx));
        return n;
    }
    if (op->kind == ASMOP_IMM) {
        long v = op->imm_val;
        if (fits_imm8(v)) {
            EMIT(0x6A);
            EMIT((uint8_t)(v & 0xFF));
        } else {
            EMIT(0x68);
            int32_t v32 = (int32_t)v;
            EMIT(v32 & 0xFF);
            EMIT((v32 >> 8) & 0xFF);
            EMIT((v32 >> 16) & 0xFF);
            EMIT((v32 >> 24) & 0xFF);
        }
        return n;
    }
    fprintf(stderr, "forge-asm: line %d: PUSH requires register or immediate\n", stmt->line);
    return -1;
}

static int encode_pop(const AsmStatement *stmt,
                      uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *op = &stmt->operands[0];
    if (op->kind != ASMOP_REG) {
        fprintf(stderr, "forge-asm: line %d: POP requires register\n", stmt->line);
        return -1;
    }
    if (REGHI(op->reg_idx))
        EMIT(REX(0, 0, 0, 1));
    EMIT(0x58 | REGLO(op->reg_idx));
    return n;
}

/* CALL: label or register (call rax) */
static int encode_call(X86EncodeCtx *ctx, const AsmStatement *stmt,
                       uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *op = &stmt->operands[0];
    if (op->kind == ASMOP_LABEL) {
        EMIT(0xE8);
        uint32_t rel_off = ctx->section_offset + n;
        if (emit_rel32_label(ctx, buf, &n, buf_size, op->label, rel_off) < 0)
            return -1;
        return n;
    }
    if (op->kind == ASMOP_REG) {
        /* FF /2: call r/m64 */
        if (REGHI(op->reg_idx))
            EMIT(REX(0, 0, 0, 1));
        EMIT(0xFF);
        EMIT(0xD0 | REGLO(op->reg_idx));
        return n;
    }
    fprintf(stderr, "forge-asm: line %d: CALL requires label or register\n", stmt->line);
    return -1;
}

/* JMP / conditional jumps — all use near (32-bit relative) form */
static int encode_jmp(X86EncodeCtx *ctx, const AsmStatement *stmt,
                      uint8_t *buf, int buf_size, uint8_t opcode) {
    int n = 0;
    const AsmOperand *op = &stmt->operands[0];
    if (op->kind != ASMOP_LABEL) {
        fprintf(stderr, "forge-asm: line %d: JMP/Jcc requires label\n", stmt->line);
        return -1;
    }
    EMIT(opcode);
    uint32_t rel_off = ctx->section_offset + n;
    if (emit_rel32_label(ctx, buf, &n, buf_size, op->label, rel_off) < 0)
        return -1;
    return n;
}

static int encode_cjmp(X86EncodeCtx *ctx, const AsmStatement *stmt,
                       uint8_t *buf, int buf_size, uint8_t cc) {
    int n = 0;
    const AsmOperand *op = &stmt->operands[0];
    if (op->kind != ASMOP_LABEL) {
        fprintf(stderr, "forge-asm: line %d: Jcc requires label\n", stmt->line);
        return -1;
    }
    EMIT(0x0F);
    EMIT(0x80 | cc);
    uint32_t rel_off = ctx->section_offset + n;
    if (emit_rel32_label(ctx, buf, &n, buf_size, op->label, rel_off) < 0)
        return -1;
    return n;
}

/* SETcc al */
static int encode_setcc(const AsmStatement *stmt,
                        uint8_t *buf, int buf_size, uint8_t cc) {
    int n = 0;
    const AsmOperand *op = &stmt->operands[0];
    if (op->kind != ASMOP_REG || op->reg_size != 8) {
        fprintf(stderr, "forge-asm: line %d: SETcc requires 8-bit register\n", stmt->line);
        return -1;
    }
    /* For al/cl/dl/bl: no REX needed (but we emit REX if reg >= 4 to access spl/bpl) */
    EMIT(0x0F);
    EMIT(0x90 | cc);
    EMIT(0xC0 | REGLO(op->reg_idx));
    return n;
}

/* IMUL dst, src */
static int encode_imul(const AsmStatement *stmt,
                       uint8_t *buf, int buf_size) {
    int n = 0;
    if (stmt->operand_count != 2) {
        fprintf(stderr, "forge-asm: line %d: IMUL requires 2 operands\n", stmt->line);
        return -1;
    }
    const AsmOperand *dst = &stmt->operands[0];
    const AsmOperand *src = &stmt->operands[1];
    if (dst->kind != ASMOP_REG || src->kind != ASMOP_REG) {
        fprintf(stderr, "forge-asm: line %d: IMUL requires reg, reg\n", stmt->line);
        return -1;
    }
    /* IMUL r64, r/m64: REX.W 0F AF /r */
    EMIT(REX(1, REGHI(dst->reg_idx), 0, REGHI(src->reg_idx)));
    EMIT(0x0F);
    EMIT(0xAF);
    EMIT(MODRM_RR(dst->reg_idx, src->reg_idx));
    return n;
}

/* IDIV rbx — divides rdx:rax by src */
static int encode_idiv(const AsmStatement *stmt,
                       uint8_t *buf, int buf_size) {
    int n = 0;
    const AsmOperand *src = &stmt->operands[0];
    if (src->kind != ASMOP_REG) {
        fprintf(stderr, "forge-asm: line %d: IDIV requires register\n", stmt->line);
        return -1;
    }
    int W = (src->reg_size == 64);
    uint8_t rex = REX(W, 0, 0, REGHI(src->reg_idx));
    if (rex != 0x40 || W)
        EMIT(rex);
    EMIT(0xF7);
    EMIT(0xC0 | (7 << 3) | REGLO(src->reg_idx)); /* /7 */
    return n;
}

/* NEG / NOT — unary F7 group */
static int encode_unary_f7(const AsmStatement *stmt,
                           uint8_t *buf, int buf_size, int opext) {
    int n = 0;
    const AsmOperand *op = &stmt->operands[0];
    if (op->kind != ASMOP_REG) {
        fprintf(stderr, "forge-asm: line %d: requires register\n", stmt->line);
        return -1;
    }
    int W = (op->reg_size == 64);
    uint8_t rex = REX(W, 0, 0, REGHI(op->reg_idx));
    if (rex != 0x40 || W)
        EMIT(rex);
    EMIT(0xF7);
    EMIT(0xC0 | (opext << 3) | REGLO(op->reg_idx));
    return n;
}

/* MUL: unsigned multiply rdx:rax = rax * src  (F7 /4) */
static int encode_mul(const AsmStatement *stmt,
                      uint8_t *buf, int buf_size) {
    return encode_unary_f7(stmt, buf, buf_size, 4);
}

/* DIV: unsigned divide rdx:rax / src  (F7 /6) */
static int encode_div(const AsmStatement *stmt,
                      uint8_t *buf, int buf_size) {
    return encode_unary_f7(stmt, buf, buf_size, 6);
}

/* INC / DEC — FF group */
static int encode_incdec(const AsmStatement *stmt,
                         uint8_t *buf, int buf_size, int opext) {
    int n = 0;
    const AsmOperand *op = &stmt->operands[0];
    if (op->kind != ASMOP_REG) {
        fprintf(stderr, "forge-asm: line %d: INC/DEC requires register\n", stmt->line);
        return -1;
    }
    int W = (op->reg_size == 64);
    uint8_t rex = REX(W, 0, 0, REGHI(op->reg_idx));
    if (rex != 0x40 || W)
        EMIT(rex);
    EMIT(0xFF);
    EMIT(0xC0 | (opext << 3) | REGLO(op->reg_idx));
    return n;
}

/* SHL / SHR / SAR / ROL / ROR — shift group (C1/D3) */
static int encode_shift(const AsmStatement *stmt, uint8_t *buf, int buf_size, int opext) {
    int n = 0;
    const AsmOperand *dst = &stmt->operands[0];
    const AsmOperand *src = &stmt->operands[1];
    if (dst->kind != ASMOP_REG) {
        fprintf(stderr, "forge-asm: line %d: shift requires reg dst\n", stmt->line);
        return -1;
    }
    int W = (dst->reg_size == 64);
    uint8_t rex = REX(W, 0, 0, REGHI(dst->reg_idx));
    if (rex != 0x40 || W)
        EMIT(rex);
    if (src->kind == ASMOP_IMM) {
        EMIT(0xC1);
        EMIT(0xC0 | (opext << 3) | REGLO(dst->reg_idx));
        EMIT((uint8_t)(src->imm_val & 0x3F));
    } else if (src->kind == ASMOP_REG && src->reg_idx == 1 /* cl */) {
        /* shift by CL: D3 /opext */
        EMIT(0xD3);
        EMIT(0xC0 | (opext << 3) | REGLO(dst->reg_idx));
    } else {
        fprintf(stderr, "forge-asm: line %d: shift src must be imm or cl\n", stmt->line);
        return -1;
    }
    return n;
}

/* ---- Main encode dispatch ---- */

int x86_encode(X86EncodeCtx *ctx, const AsmStatement *stmt,
               uint8_t *buf, int buf_size) {
    if (!stmt || stmt->kind != ASM_STMT_INSTR)
        return -1;
    const char *m = stmt->mnemonic;

    if (strcmp(m, "mov") == 0)
        return encode_mov(ctx, stmt, buf, buf_size);
    if (strcmp(m, "lea") == 0)
        return encode_lea(ctx, stmt, buf, buf_size);

    if (strcmp(m, "add") == 0)
        return encode_alu(stmt, buf, buf_size, 0, 0x03);
    if (strcmp(m, "or") == 0)
        return encode_alu(stmt, buf, buf_size, 1, 0x0B);
    if (strcmp(m, "and") == 0)
        return encode_alu(stmt, buf, buf_size, 4, 0x23);
    if (strcmp(m, "sub") == 0)
        return encode_alu(stmt, buf, buf_size, 5, 0x2B);
    if (strcmp(m, "xor") == 0)
        return encode_alu(stmt, buf, buf_size, 6, 0x33);
    if (strcmp(m, "cmp") == 0)
        return encode_alu(stmt, buf, buf_size, 7, 0x3B);

    if (strcmp(m, "test") == 0)
        return encode_test(stmt, buf, buf_size);
    if (strcmp(m, "movzx") == 0)
        return encode_movzx(stmt, buf, buf_size);
    if (strcmp(m, "movsx") == 0)
        return encode_movsx(stmt, buf, buf_size);
    if (strcmp(m, "push") == 0)
        return encode_push(stmt, buf, buf_size);
    if (strcmp(m, "pop") == 0)
        return encode_pop(stmt, buf, buf_size);
    if (strcmp(m, "imul") == 0)
        return encode_imul(stmt, buf, buf_size);
    if (strcmp(m, "idiv") == 0)
        return encode_idiv(stmt, buf, buf_size);
    if (strcmp(m, "mul") == 0)
        return encode_mul(stmt, buf, buf_size);
    if (strcmp(m, "div") == 0)
        return encode_div(stmt, buf, buf_size);
    if (strcmp(m, "inc") == 0)
        return encode_incdec(stmt, buf, buf_size, 0);
    if (strcmp(m, "dec") == 0)
        return encode_incdec(stmt, buf, buf_size, 1);
    if (strcmp(m, "neg") == 0)
        return encode_unary_f7(stmt, buf, buf_size, 3);
    if (strcmp(m, "not") == 0)
        return encode_unary_f7(stmt, buf, buf_size, 2);
    if (strcmp(m, "shr") == 0)
        return encode_shift(stmt, buf, buf_size, 5);
    if (strcmp(m, "shl") == 0)
        return encode_shift(stmt, buf, buf_size, 4);
    if (strcmp(m, "sar") == 0)
        return encode_shift(stmt, buf, buf_size, 7);
    if (strcmp(m, "rol") == 0)
        return encode_shift(stmt, buf, buf_size, 0);
    if (strcmp(m, "ror") == 0)
        return encode_shift(stmt, buf, buf_size, 1);

    if (strcmp(m, "ret") == 0) {
        int n = 0;
        EMIT(0xC3);
        return n;
    }
    if (strcmp(m, "cdq") == 0) {
        int n = 0;
        EMIT(0x99);
        return n;
    }
    if (strcmp(m, "cqo") == 0) {
        int n = 0;
        EMIT(0x48);
        EMIT(0x99);
        return n;
    }
    if (strcmp(m, "nop") == 0) {
        int n = 0;
        EMIT(0x90);
        return n;
    }

    if (strcmp(m, "call") == 0)
        return encode_call(ctx, stmt, buf, buf_size);

    /* Unconditional JMP */
    if (strcmp(m, "jmp") == 0)
        return encode_jmp(ctx, stmt, buf, buf_size, 0xE9);

    /* Conditional jumps (near form: 0F 8x rel32) */
    if (strcmp(m, "jo") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x00);
    if (strcmp(m, "jno") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x01);
    if (strcmp(m, "jb") == 0 || strcmp(m, "jnae") == 0 || strcmp(m, "jc") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x02);
    if (strcmp(m, "jae") == 0 || strcmp(m, "jnb") == 0 || strcmp(m, "jnc") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x03);
    if (strcmp(m, "je") == 0 || strcmp(m, "jz") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x04);
    if (strcmp(m, "jne") == 0 || strcmp(m, "jnz") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x05);
    if (strcmp(m, "jbe") == 0 || strcmp(m, "jna") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x06);
    if (strcmp(m, "ja") == 0 || strcmp(m, "jnbe") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x07);
    if (strcmp(m, "js") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x08);
    if (strcmp(m, "jns") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x09);
    if (strcmp(m, "jp") == 0 || strcmp(m, "jpe") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x0A);
    if (strcmp(m, "jnp") == 0 || strcmp(m, "jpo") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x0B);
    if (strcmp(m, "jl") == 0 || strcmp(m, "jnge") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x0C);
    if (strcmp(m, "jge") == 0 || strcmp(m, "jnl") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x0D);
    if (strcmp(m, "jle") == 0 || strcmp(m, "jng") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x0E);
    if (strcmp(m, "jg") == 0 || strcmp(m, "jnle") == 0)
        return encode_cjmp(ctx, stmt, buf, buf_size, 0x0F);

    /* SETcc */
    if (strcmp(m, "seto") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x00);
    if (strcmp(m, "setb") == 0 || strcmp(m, "setnae") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x02);
    if (strcmp(m, "sete") == 0 || strcmp(m, "setz") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x04);
    if (strcmp(m, "setne") == 0 || strcmp(m, "setnz") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x05);
    if (strcmp(m, "setbe") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x06);
    if (strcmp(m, "seta") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x07);
    if (strcmp(m, "sets") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x08);
    if (strcmp(m, "setl") == 0 || strcmp(m, "setnge") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x0C);
    if (strcmp(m, "setge") == 0 || strcmp(m, "setnl") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x0D);
    if (strcmp(m, "setle") == 0 || strcmp(m, "setng") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x0E);
    if (strcmp(m, "setg") == 0 || strcmp(m, "setnle") == 0)
        return encode_setcc(stmt, buf, buf_size, 0x0F);

    fprintf(stderr, "forge-asm: line %d: unsupported mnemonic '%s'\n",
            stmt->line, m);
    return -1;
}

/* ---- Measure (conservative, near form) ---- */

int x86_measure(const AsmStatement *stmt) {
    if (!stmt || stmt->kind != ASM_STMT_INSTR)
        return 0;
    const char *m = stmt->mnemonic;

    /* Use a temp buffer to encode and measure actual size */
    uint8_t tmp[64];
    X86EncodeCtx dummy;
    memset(&dummy, 0, sizeof(dummy));
    int r = x86_encode(&dummy, stmt, tmp, sizeof(tmp));
    /* Free any relocs allocated in dummy */
    int i;
    for (i = 0; i < dummy.reloc_count; i++)
        free(dummy.relocs[i].symbol);
    free(dummy.relocs);
    if (r < 0) {
        /* Fallback: near jump is 5 (jmp) or 6 (jcc), call=5 */
        if (strncmp(m, "j", 1) == 0)
            return 6;
        if (strcmp(m, "call") == 0)
            return 5;
        return 7; /* worst-case estimate */
    }
    return r;
}
