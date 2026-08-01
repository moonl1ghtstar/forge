/*
 * asm-parser.h - Parser for Anvil ASM (NASM-subset)
 *
 * Converts a stream of AsmTokens into a flat list of AsmStatement
 * structures representing directives, labels, and instructions.
 */
#ifndef ANVIL_ASM_PARSER_H
#define ANVIL_ASM_PARSER_H

#include "asm-lexer.h"

/* ---- Operand types ---- */

typedef enum {
    ASMOP_NONE,
    ASMOP_REG,    /* register operand  (e.g. rax, ecx, al) */
    ASMOP_IMM,    /* immediate integer  (e.g. 42, -11)      */
    ASMOP_MEM,    /* memory reference  (e.g. [rbp-8])       */
    ASMOP_LABEL   /* bare label/symbol (e.g. printf, .Lex)  */
} AsmOpKind;

/* How a memory reference is formed */
typedef enum {
    MEMKIND_BASE_DISP,  /* [base + disp]              e.g. [rbp-8]          */
    MEMKIND_SIB,        /* [base + index*scale + disp] e.g. [rax+rbx*4-16]  */
    MEMKIND_BASE_ONLY,  /* [base]                      e.g. [rax]            */
    MEMKIND_RIP_REL     /* [rel symbol]                e.g. [rel str_0]      */
} AsmMemKind;

typedef struct {
    AsmMemKind kind;
    int        base_reg;   /* base register index (MEMKIND_BASE_DISP / SIB / BASE_ONLY) */
    int        base_size;  /* size of base register (always 64 here) */
    int        index_reg;  /* SIB index register index (-1 = none) */
    int        index_size; /* size of index register */
    int        scale;      /* SIB scale: 1, 2, 4, or 8 */
    long       disp;       /* displacement (may be negative) */
    char      *symbol;     /* symbol name (MEMKIND_RIP_REL) */
} AsmMemOp;

typedef struct {
    AsmOpKind kind;
    /* REG */
    int  reg_idx;    /* 0-15 */
    int  reg_size;   /* 64, 32, 16, 8 */
    /* IMM */
    long long imm_val;
    /* MEM */
    AsmMemOp mem;
    /* LABEL */
    char *label;     /* heap-allocated */
} AsmOperand;

/* ---- Statement types ---- */

typedef enum {
    ASM_STMT_SECTION,  /* section .text / .data / .bss */
    ASM_STMT_GLOBAL,   /* global <sym> */
    ASM_STMT_EXTERN,   /* extern <sym> */
    ASM_STMT_LABEL,    /* <name>:  (standalone label) */
    ASM_STMT_DB,       /* [label:] db  byte,  ...  (1-byte data)  */
    ASM_STMT_DW,       /* [label:] dw  word,  ...  (2-byte data)  */
    ASM_STMT_DD,       /* [label:] dd  dword, ...  (4-byte data)  */
    ASM_STMT_DQ,       /* [label:] dq  qword, ...  (8-byte data)  */
    ASM_STMT_RESB,     /* [label:] resb count      (1-byte bss)   */
    ASM_STMT_RESW,     /* [label:] resw count      (2-byte bss)   */
    ASM_STMT_RESD,     /* [label:] resd count      (4-byte bss)   */
    ASM_STMT_RESQ,     /* [label:] resq count      (8-byte bss)   */
    ASM_STMT_INSTR,    /* [label:] mnemonic [op1[, op2[, op3]]] */
} AsmStmtKind;

/* Byte entry in a DB statement */
typedef struct {
    int is_string; /* 1 = string bytes follow; 0 = single byte value */
    union {
        long byte_val;    /* single byte (is_string==0) */
        struct {
            char *data;   /* string content (heap) */
            int   len;
        } str;
    } as;
} AsmDbEntry;

typedef struct {
    AsmStmtKind kind;
    int         line;     /* source line for error reporting */

    char *label;          /* optional leading label (for INSTR, DB, RESB) */

    /* ASM_STMT_SECTION */
    char *section_name;   /* ".text", ".data", ".bss" */

    /* ASM_STMT_GLOBAL / ASM_STMT_EXTERN / ASM_STMT_LABEL */
    char *sym_name;

    /* ASM_STMT_DB */
    AsmDbEntry *db_entries;
    int         db_count;
    int         db_cap;

    /* ASM_STMT_RESB / RESW / RESD / RESQ */
    long resb_count;   /* count in *elements* (multiply by element size) */
    int  res_elem_size; /* 1/2/4/8 bytes per element */

    /* ASM_STMT_INSTR */
    char       *mnemonic;
    AsmOperand  operands[4];
    int         operand_count;
} AsmStatement;

/* ---- Program (flat list of statements) ---- */

typedef struct {
    AsmStatement *stmts;
    int           count;
    int           cap;
    int           had_error;
} AsmProgram;

/* ---- API ---- */

/* Parse ASM source text into a program.
   Returns a heap-allocated AsmProgram; call asm_program_free() when done. */
AsmProgram *asm_parse(const char *source);

void        asm_program_free(AsmProgram *prog);
void        asm_statement_free(AsmStatement *stmt);
void        asm_operand_free(AsmOperand *op);

/* Utility: look up register name, return index 0-15 or -1.
   Sets *size to 64 / 32 / 8. */
int asm_lookup_reg(const char *name, int *size);

#endif /* ANVIL_ASM_PARSER_H */
