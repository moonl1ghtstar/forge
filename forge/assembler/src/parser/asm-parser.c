/*
 * asm-parser.c - Parser for Forge ASM (NASM-subset)
 *
 * Parses one line at a time.  Grammar (simplified):
 *
 *   line ::= [label ':'] directive NEWLINE
 *          | [label ':'] mnemonic [operand (',' operand)*] NEWLINE
 *          | label ':' NEWLINE
 *          | NEWLINE    (blank / comment line)
 *
 *   directive ::= 'section' ident
 *               | 'global'  ident
 *               | 'extern'  ident
 *               | 'db'      db_item (',' db_item)*
 *               | 'resb'    number
 *
 *   operand ::= register
 *             | immediate
 *             | '[' mem_expr ']'
 *             | ident              (label reference for jmp/call)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "asm-parser.h"

/* ---- Register table ---- */

typedef struct { const char *name; int idx; int size; } RegEntry;

static const RegEntry REG_TABLE[] = {
    /* 64-bit */
    {"rax",0,64},{"rcx",1,64},{"rdx",2,64},{"rbx",3,64},
    {"rsp",4,64},{"rbp",5,64},{"rsi",6,64},{"rdi",7,64},
    {"r8", 8,64},{"r9", 9,64},{"r10",10,64},{"r11",11,64},
    {"r12",12,64},{"r13",13,64},{"r14",14,64},{"r15",15,64},
    /* 32-bit */
    {"eax",0,32},{"ecx",1,32},{"edx",2,32},{"ebx",3,32},
    {"esp",4,32},{"ebp",5,32},{"esi",6,32},{"edi",7,32},
    {"r8d", 8,32},{"r9d", 9,32},{"r10d",10,32},{"r11d",11,32},
    {"r12d",12,32},{"r13d",13,32},{"r14d",14,32},{"r15d",15,32},
    /* 16-bit */
    {"ax",0,16},{"cx",1,16},{"dx",2,16},{"bx",3,16},
    {"sp",4,16},{"bp",5,16},{"si",6,16},{"di",7,16},
    /* 8-bit */
    {"al",0,8},{"cl",1,8},{"dl",2,8},{"bl",3,8},
    {"ah",4,8},{"ch",5,8},{"dh",6,8},{"bh",7,8},
    {"spl",4,8},{"bpl",5,8},{"sil",6,8},{"dil",7,8},
    {"r8b",8,8},{"r9b",9,8},{"r10b",10,8},{"r11b",11,8},
    {"r12b",12,8},{"r13b",13,8},{"r14b",14,8},{"r15b",15,8},
    /* SSE: XMM registers */
    {"xmm0",0,128},{"xmm1",1,128},{"xmm2",2,128},{"xmm3",3,128},
    {"xmm4",4,128},{"xmm5",5,128},{"xmm6",6,128},{"xmm7",7,128},
    {"xmm8",8,128},{"xmm9",9,128},{"xmm10",10,128},{"xmm11",11,128},
    {"xmm12",12,128},{"xmm13",13,128},{"xmm14",14,128},{"xmm15",15,128},
    {NULL,0,0}
};

int asm_lookup_reg(const char *name, int *size) {
    int i;
    for (i = 0; REG_TABLE[i].name; i++) {
        if (strcmp(name, REG_TABLE[i].name) == 0) {
            if (size) *size = REG_TABLE[i].size;
            return REG_TABLE[i].idx;
        }
    }
    return -1;
}

/* ---- Parser state ---- */

typedef struct {
    AsmLexer    lex;
    AsmToken    cur;        /* current (lookahead) token */
    AsmProgram *prog;
    int         had_error;
} Parser;

static void advance(Parser *p) {
    asm_token_free(&p->cur);
    p->cur = asm_lexer_next(&p->lex);
}

static int check(const Parser *p, AsmTokenType t) {
    return p->cur.type == t;
}

static int match_tok(Parser *p, AsmTokenType t) {
    if (check(p, t)) { advance(p); return 1; }
    return 0;
}

static void parse_error(Parser *p, const char *msg) {
    fprintf(stderr, "forge-asm: line %d: %s (got '%s')\n",
            p->cur.line, msg,
            p->cur.text ? p->cur.text : asm_token_type_name(p->cur.type));
    p->had_error = 1;
}

/* Skip until NEWLINE or EOF (error recovery) */
static void skip_to_newline(Parser *p) {
    while (!check(p, ASM_TOK_NEWLINE) && !check(p, ASM_TOK_EOF))
        advance(p);
}

/* ---- Statement allocation ---- */

static AsmStatement *new_stmt(AsmStmtKind kind, int line) {
    AsmStatement *s = (AsmStatement *)calloc(1, sizeof(AsmStatement));
    s->kind = kind;
    s->line = line;
    return s;
}

static void prog_push(AsmProgram *prog, AsmStatement *s) {
    if (prog->count >= prog->cap) {
        prog->cap = prog->cap ? prog->cap * 2 : 64;
        prog->stmts = (AsmStatement *)realloc(prog->stmts,
                       sizeof(AsmStatement) * prog->cap);
    }
    prog->stmts[prog->count++] = *s;
    free(s); /* struct is copied; ownership transferred */
}

/* ---- Operand parsing ---- */

/* Parse a memory expression inside [...]:
 *   Forms supported:
 *     [base]                  e.g. [rax]
 *     [base + disp]           e.g. [rbp-8], [rsp+40]
 *     [base + index]          e.g. [rax+rbx]
 *     [base + index*scale]    e.g. [rax+rbx*4]
 *     [base + index*scale ± disp]  e.g. [rbp+rcx*8-16]
 *     [rel symbol]            RIP-relative
 */
static int parse_mem_operand(Parser *p, AsmOperand *op, int line) {
    op->kind = ASMOP_MEM;

    /* [rel symbol] */
    if (check(p, ASM_TOK_IDENT) && p->cur.text &&
        strcmp(p->cur.text, "rel") == 0) {
        advance(p);
        if (!check(p, ASM_TOK_IDENT)) {
            parse_error(p, "expected symbol after 'rel'");
            return 0;
        }
        op->mem.kind   = MEMKIND_RIP_REL;
        op->mem.symbol = strdup(p->cur.text);
        op->mem.disp   = 0;
        op->mem.index_reg = -1;
        op->mem.scale     = 1;
        advance(p);
        if (!match_tok(p, ASM_TOK_RBRACKET)) {
            parse_error(p, "expected ']'");
            return 0;
        }
        return 1;
    }

    /* Must start with a base register */
    if (!check(p, ASM_TOK_IDENT)) {
        parse_error(p, "expected register in memory operand");
        return 0;
    }
    int reg_size = 0;
    int base_idx = asm_lookup_reg(p->cur.text, &reg_size);
    if (base_idx < 0) {
        parse_error(p, "expected register in memory operand");
        return 0;
    }
    op->mem.base_reg  = base_idx;
    op->mem.base_size = reg_size;
    op->mem.index_reg = -1;
    op->mem.scale     = 1;
    op->mem.disp      = 0;
    op->mem.symbol    = NULL;
    advance(p);

    /* Check what follows: ']', '+', or '-' */
    if (check(p, ASM_TOK_RBRACKET)) {
        /* [base] only */
        op->mem.kind = MEMKIND_BASE_ONLY;
        advance(p);
        return 1;
    }

    if (check(p, ASM_TOK_PLUS) || check(p, ASM_TOK_MINUS)) {
        int neg = check(p, ASM_TOK_MINUS);
        advance(p);

        if (check(p, ASM_TOK_NUMBER)) {
            /* [base ± imm] */
            op->mem.disp = neg ? -(p->cur.value) : p->cur.value;
            op->mem.kind = MEMKIND_BASE_DISP;
            advance(p);
        } else if (check(p, ASM_TOK_IDENT)) {
            /* Could be [base + index] or [base + index*scale] or [base + index*scale ± disp] */
            int idx_size = 0;
            int idx_reg  = asm_lookup_reg(p->cur.text, &idx_size);
            if (idx_reg < 0) {
                parse_error(p, "expected register or number after +/- in memory operand");
                return 0;
            }
            op->mem.index_reg  = neg ? -1 : idx_reg; /* subtraction not valid for index */
            op->mem.index_size = idx_size;
            op->mem.scale      = 1;
            advance(p);

            /* Optional *scale */
            if (check(p, ASM_TOK_STAR)) {
                advance(p); /* consume '*' */
                if (!check(p, ASM_TOK_NUMBER)) {
                    parse_error(p, "expected scale factor after '*'");
                    return 0;
                }
                int sc = (int)p->cur.value;
                if (sc != 1 && sc != 2 && sc != 4 && sc != 8) {
                    parse_error(p, "scale must be 1, 2, 4, or 8");
                    return 0;
                }
                op->mem.scale = sc;
                advance(p);
            }

            /* Optional ± disp after index[*scale] */
            if (check(p, ASM_TOK_PLUS) || check(p, ASM_TOK_MINUS)) {
                int dneg = check(p, ASM_TOK_MINUS);
                advance(p);
                if (!check(p, ASM_TOK_NUMBER)) {
                    parse_error(p, "expected displacement after +/- in memory operand");
                    return 0;
                }
                op->mem.disp = dneg ? -(p->cur.value) : p->cur.value;
                advance(p);
            }

            /* Determine kind */
            if (op->mem.index_reg >= 0 && (op->mem.scale > 1 || op->mem.disp != 0)) {
                op->mem.kind = MEMKIND_SIB;
            } else if (op->mem.index_reg >= 0) {
                op->mem.kind = MEMKIND_SIB; /* [base+index*1+0] still needs SIB */
            } else {
                op->mem.kind = MEMKIND_BASE_DISP;
            }
        } else {
            parse_error(p, "expected register or number after +/- in memory operand");
            return 0;
        }
    } else {
        /* Nothing after base before ']' — shouldn't reach here, caught above */
        op->mem.kind = MEMKIND_BASE_ONLY;
    }

    if (!match_tok(p, ASM_TOK_RBRACKET)) {
        parse_error(p, "expected ']'");
        return 0;
    }
    return 1;
    (void)line;
}

/* Parse a single operand: reg, imm, [mem], or label */
static int parse_operand(Parser *p, AsmOperand *op) {
    int line = p->cur.line;
    memset(op, 0, sizeof(*op));

    /* Memory operand */
    if (check(p, ASM_TOK_LBRACKET)) {
        advance(p);
        return parse_mem_operand(p, op, line);
    }

    /* Immediate: optional minus followed by number */
    if (check(p, ASM_TOK_MINUS)) {
        advance(p);
        if (!check(p, ASM_TOK_NUMBER)) {
            parse_error(p, "expected number after '-'");
            return 0;
        }
        op->kind    = ASMOP_IMM;
        op->imm_val = -(p->cur.value);
        advance(p);
        return 1;
    }
    if (check(p, ASM_TOK_NUMBER)) {
        op->kind    = ASMOP_IMM;
        op->imm_val = p->cur.value;
        advance(p);
        return 1;
    }

    /* Identifier: register or label */
    if (check(p, ASM_TOK_IDENT)) {
        int reg_size = 0;
        int reg_idx  = asm_lookup_reg(p->cur.text, &reg_size);
        if (reg_idx >= 0) {
            op->kind     = ASMOP_REG;
            op->reg_idx  = reg_idx;
            op->reg_size = reg_size;
            advance(p);
            return 1;
        }
        /* Treat as label/symbol reference */
        op->kind  = ASMOP_LABEL;
        op->label = strdup(p->cur.text);
        advance(p);
        return 1;
    }

    parse_error(p, "expected operand");
    return 0;
}

/* ---- DB entry parsing ---- */

static void db_push(AsmStatement *s, AsmDbEntry e) {
    if (s->db_count >= s->db_cap) {
        s->db_cap = s->db_cap ? s->db_cap * 2 : 16;
        s->db_entries = (AsmDbEntry *)realloc(s->db_entries,
                         sizeof(AsmDbEntry) * s->db_cap);
    }
    s->db_entries[s->db_count++] = e;
}

/* Parse a `db` directive's value list */
static void parse_db_values(Parser *p, AsmStatement *s) {
    for (;;) {
        AsmDbEntry e;
        memset(&e, 0, sizeof(e));

        if (check(p, ASM_TOK_NUMBER) || check(p, ASM_TOK_MINUS)) {
            long val = 0;
            if (check(p, ASM_TOK_MINUS)) {
                advance(p);
                val = -(p->cur.value);
                advance(p);
            } else {
                val = p->cur.value;
                advance(p);
            }
            e.is_string      = 0;
            e.as.byte_val    = val;
            db_push(s, e);
        } else if (check(p, ASM_TOK_STRING)) {
            e.is_string    = 1;
            e.as.str.data  = strdup(p->cur.text);
            e.as.str.len   = (int)strlen(p->cur.text);
            advance(p);
            db_push(s, e);
        } else {
            parse_error(p, "expected byte value or string in db");
            skip_to_newline(p);
            return;
        }

        if (!match_tok(p, ASM_TOK_COMMA))
            break;
    }
}

/* ---- Statement parsing ---- */

static void parse_line(Parser *p) {
    /* Skip blank lines */
    if (check(p, ASM_TOK_NEWLINE)) {
        advance(p);
        return;
    }
    if (check(p, ASM_TOK_EOF))
        return;

    if (!check(p, ASM_TOK_IDENT)) {
        parse_error(p, "expected label or mnemonic");
        skip_to_newline(p);
        if (check(p, ASM_TOK_NEWLINE)) advance(p);
        return;
    }

    int   line     = p->cur.line;
    char *first    = strdup(p->cur.text);
    advance(p);

    /* Determine if `first` is a label (followed by ':') */
    char *label = NULL;
    if (check(p, ASM_TOK_COLON)) {
        advance(p); /* consume ':' */
        label = first;
        first = NULL;

        /* Standalone label line */
        if (check(p, ASM_TOK_NEWLINE) || check(p, ASM_TOK_EOF)) {
            AsmStatement *s = new_stmt(ASM_STMT_LABEL, line);
            s->sym_name = label;
            prog_push(p->prog, s);
            if (check(p, ASM_TOK_NEWLINE)) advance(p);
            return;
        }

        /* Label followed by directive/instruction on same line */
        if (!check(p, ASM_TOK_IDENT)) {
            parse_error(p, "expected directive or mnemonic after label");
            free(label);
            skip_to_newline(p);
            if (check(p, ASM_TOK_NEWLINE)) advance(p);
            return;
        }
        first = strdup(p->cur.text);
        advance(p);
    }

    /* `first` is now the directive or mnemonic */
    const char *kw = first;

    /* section */
    if (strcmp(kw, "section") == 0) {
        if (!check(p, ASM_TOK_IDENT)) {
            parse_error(p, "expected section name");
            free(first); free(label);
            skip_to_newline(p);
            if (check(p, ASM_TOK_NEWLINE)) advance(p);
            return;
        }
        AsmStatement *s = new_stmt(ASM_STMT_SECTION, line);
        s->section_name = strdup(p->cur.text);
        s->label        = label;
        advance(p);
        prog_push(p->prog, s);
        skip_to_newline(p);
        if (check(p, ASM_TOK_NEWLINE)) advance(p);
        free(first);
        return;
    }

    /* global */
    if (strcmp(kw, "global") == 0) {
        if (!check(p, ASM_TOK_IDENT)) {
            parse_error(p, "expected symbol name after 'global'");
            free(first); free(label);
            skip_to_newline(p);
            if (check(p, ASM_TOK_NEWLINE)) advance(p);
            return;
        }
        AsmStatement *s = new_stmt(ASM_STMT_GLOBAL, line);
        s->sym_name = strdup(p->cur.text);
        s->label    = label;
        advance(p);
        prog_push(p->prog, s);
        skip_to_newline(p);
        if (check(p, ASM_TOK_NEWLINE)) advance(p);
        free(first);
        return;
    }

    /* extern */
    if (strcmp(kw, "extern") == 0) {
        if (!check(p, ASM_TOK_IDENT)) {
            parse_error(p, "expected symbol name after 'extern'");
            free(first); free(label);
            skip_to_newline(p);
            if (check(p, ASM_TOK_NEWLINE)) advance(p);
            return;
        }
        AsmStatement *s = new_stmt(ASM_STMT_EXTERN, line);
        s->sym_name = strdup(p->cur.text);
        s->label    = label;
        advance(p);
        prog_push(p->prog, s);
        skip_to_newline(p);
        if (check(p, ASM_TOK_NEWLINE)) advance(p);
        free(first);
        return;
    }

    /* db / dw / dd / dq */
    if (strcmp(kw, "db") == 0 || strcmp(kw, "dw") == 0 ||
        strcmp(kw, "dd") == 0 || strcmp(kw, "dq") == 0) {
        AsmStmtKind dk = (strcmp(kw,"db")==0) ? ASM_STMT_DB :
                         (strcmp(kw,"dw")==0) ? ASM_STMT_DW :
                         (strcmp(kw,"dd")==0) ? ASM_STMT_DD : ASM_STMT_DQ;
        AsmStatement *s = new_stmt(dk, line);
        s->label = label;
        parse_db_values(p, s);
        prog_push(p->prog, s);
        skip_to_newline(p);
        if (check(p, ASM_TOK_NEWLINE)) advance(p);
        free(first);
        return;
    }

    /* resb / resw / resd / resq */
    if (strcmp(kw, "resb") == 0 || strcmp(kw, "resw") == 0 ||
        strcmp(kw, "resd") == 0 || strcmp(kw, "resq") == 0) {
        if (!check(p, ASM_TOK_NUMBER)) {
            parse_error(p, "expected count after res* directive");
            free(first); free(label);
            skip_to_newline(p);
            if (check(p, ASM_TOK_NEWLINE)) advance(p);
            return;
        }
        AsmStmtKind rk = (strcmp(kw,"resb")==0) ? ASM_STMT_RESB :
                         (strcmp(kw,"resw")==0) ? ASM_STMT_RESW :
                         (strcmp(kw,"resd")==0) ? ASM_STMT_RESD : ASM_STMT_RESQ;
        int esz = (strcmp(kw,"resb")==0) ? 1 :
                  (strcmp(kw,"resw")==0) ? 2 :
                  (strcmp(kw,"resd")==0) ? 4 : 8;
        AsmStatement *s = new_stmt(rk, line);
        s->label         = label;
        s->resb_count    = p->cur.value;
        s->res_elem_size = esz;
        advance(p);
        prog_push(p->prog, s);
        skip_to_newline(p);
        if (check(p, ASM_TOK_NEWLINE)) advance(p);
        free(first);
        return;
    }

    /* Instruction */
    {
        AsmStatement *s = new_stmt(ASM_STMT_INSTR, line);
        s->label    = label;
        s->mnemonic = first; /* ownership transferred */
        first       = NULL;

        /* Parse operands */
        if (!check(p, ASM_TOK_NEWLINE) && !check(p, ASM_TOK_EOF)) {
            AsmOperand op;
            if (parse_operand(p, &op)) {
                s->operands[s->operand_count++] = op;
                while (match_tok(p, ASM_TOK_COMMA) &&
                       s->operand_count < 4) {
                    if (!parse_operand(p, &op))
                        break;
                    s->operands[s->operand_count++] = op;
                }
            } else {
                skip_to_newline(p);
            }
        }

        skip_to_newline(p);
        if (check(p, ASM_TOK_NEWLINE)) advance(p);
        prog_push(p->prog, s);
    }

    free(first);
}

/* ---- Public API ---- */

AsmProgram *asm_parse(const char *source) {
    Parser p;
    memset(&p, 0, sizeof(p));
    asm_lexer_init(&p.lex, source);
    p.prog = (AsmProgram *)calloc(1, sizeof(AsmProgram));

    /* Prime the lookahead */
    p.cur = asm_lexer_next(&p.lex);

    while (!check(&p, ASM_TOK_EOF))
        parse_line(&p);

    asm_token_free(&p.cur);

    if (p.had_error)
        p.prog->had_error = 1;

    return p.prog;
}

void asm_operand_free(AsmOperand *op) {
    if (!op) return;
    if (op->kind == ASMOP_MEM && op->mem.symbol)
        free(op->mem.symbol);
    if (op->kind == ASMOP_LABEL && op->label)
        free(op->label);
}

void asm_statement_free(AsmStatement *stmt) {
    int i;
    if (!stmt) return;
    free(stmt->label);
    free(stmt->section_name);
    free(stmt->sym_name);
    free(stmt->mnemonic);
    for (i = 0; i < stmt->db_count; i++) {
        if (stmt->db_entries[i].is_string)
            free(stmt->db_entries[i].as.str.data);
    }
    free(stmt->db_entries);
    for (i = 0; i < stmt->operand_count; i++)
        asm_operand_free(&stmt->operands[i]);
}

void asm_program_free(AsmProgram *prog) {
    int i;
    if (!prog) return;
    for (i = 0; i < prog->count; i++)
        asm_statement_free(&prog->stmts[i]);
    free(prog->stmts);
    free(prog);
}
