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
    /* 8-bit */
    {"al",0,8},{"cl",1,8},{"dl",2,8},{"bl",3,8},
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
 *   [rbp - 8]
 *   [rsp + 40]
 *   [rel symbol]
 */
static int parse_mem_operand(Parser *p, AsmOperand *op, int line) {
    /* consume '[' is done by caller */
    op->kind = ASMOP_MEM;

    /* Check for [rel symbol] */
    if (check(p, ASM_TOK_IDENT) && p->cur.text &&
        strcmp(p->cur.text, "rel") == 0) {
        advance(p); /* consume 'rel' */
        if (!check(p, ASM_TOK_IDENT)) {
            parse_error(p, "expected symbol after 'rel'");
            return 0;
        }
        op->mem.kind   = MEMKIND_RIP_REL;
        op->mem.symbol = strdup(p->cur.text);
        op->mem.disp   = 0;
        advance(p);
        if (!match_tok(p, ASM_TOK_RBRACKET)) {
            parse_error(p, "expected ']'");
            return 0;
        }
        return 1;
    }

    /* [base_reg ± disp] */
    if (!check(p, ASM_TOK_IDENT)) {
        parse_error(p, "expected register in memory operand");
        return 0;
    }
    int reg_size = 0;
    int reg_idx  = asm_lookup_reg(p->cur.text, &reg_size);
    if (reg_idx < 0) {
        parse_error(p, "expected register in memory operand");
        return 0;
    }
    op->mem.kind      = MEMKIND_BASE_DISP;
    op->mem.base_reg  = reg_idx;
    op->mem.base_size = reg_size;
    op->mem.disp      = 0;
    op->mem.symbol    = NULL;
    advance(p);

    /* Optional displacement */
    if (check(p, ASM_TOK_PLUS) || check(p, ASM_TOK_MINUS)) {
        int neg = check(p, ASM_TOK_MINUS);
        advance(p);
        if (!check(p, ASM_TOK_NUMBER)) {
            parse_error(p, "expected number after +/- in memory operand");
            return 0;
        }
        op->mem.disp = neg ? -(p->cur.value) : p->cur.value;
        advance(p);
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

    /* db */
    if (strcmp(kw, "db") == 0) {
        AsmStatement *s = new_stmt(ASM_STMT_DB, line);
        s->label = label;
        parse_db_values(p, s);
        prog_push(p->prog, s);
        skip_to_newline(p);
        if (check(p, ASM_TOK_NEWLINE)) advance(p);
        free(first);
        return;
    }

    /* resb */
    if (strcmp(kw, "resb") == 0) {
        if (!check(p, ASM_TOK_NUMBER)) {
            parse_error(p, "expected count after 'resb'");
            free(first); free(label);
            skip_to_newline(p);
            if (check(p, ASM_TOK_NEWLINE)) advance(p);
            return;
        }
        AsmStatement *s = new_stmt(ASM_STMT_RESB, line);
        s->label      = label;
        s->resb_count = p->cur.value;
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
