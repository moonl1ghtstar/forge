/*
 * asm-lexer.c - Tokenizer for Anvil ASM (NASM-subset)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "asm-lexer.h"

void asm_lexer_init(AsmLexer *lex, const char *source) {
    lex->source     = source;
    lex->pos        = 0;
    lex->line       = 1;
    lex->line_start = 0;
}

static char lpeek(const AsmLexer *lex) {
    return lex->source[lex->pos];
}

static char ladvance(AsmLexer *lex) {
    char c = lex->source[lex->pos++];
    if (c == '\n') {
        lex->line++;
        lex->line_start = lex->pos;
    }
    return c;
}

static AsmToken make_tok1(AsmTokenType type, int line, int col) {
    AsmToken t;
    t.type  = type;
    t.text  = NULL;
    t.value = 0;
    t.line  = line;
    t.col   = col;
    return t;
}

static AsmToken make_tok_str(AsmTokenType type, const char *text,
                              long long value, int line, int col) {
    AsmToken t;
    t.type  = type;
    t.text  = strdup(text);
    t.value = value;
    t.line  = line;
    t.col   = col;
    return t;
}

static AsmToken error_tok(const char *msg, int line, int col) {
    return make_tok_str(ASM_TOK_ERROR, msg, 0, line, col);
}

/* Read a string literal delimited by `quote` (' or ").
   Returns ASM_TOK_STRING with the raw bytes (no quotes). */
static AsmToken read_string_lit(AsmLexer *lex, char quote, int line, int col) {
    ladvance(lex); /* consume opening quote */

    int  cap = 64, len = 0;
    char *buf = (char *)malloc(cap);

    while (lpeek(lex) != '\0' && lpeek(lex) != quote &&
           lpeek(lex) != '\n') {
        if (len + 2 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        buf[len++] = ladvance(lex);
    }
    buf[len] = '\0';

    if (lpeek(lex) != quote) {
        free(buf);
        return error_tok("unterminated string literal", line, col);
    }
    ladvance(lex); /* consume closing quote */

    AsmToken t;
    t.type  = ASM_TOK_STRING;
    t.text  = buf;   /* caller owns */
    t.value = 0;
    t.line  = line;
    t.col   = col;
    return t;
}

/* Read a decimal or 0x hex number from current position. */
static AsmToken read_number(AsmLexer *lex, int line, int col) {
    /* Collect raw digits into a buffer, then use strtol */
    int  cap = 32, len = 0;
    char *buf = (char *)malloc(cap);
    int  base = 10;

    if (lpeek(lex) == '0') {
        buf[len++] = ladvance(lex);
        if (lpeek(lex) == 'x' || lpeek(lex) == 'X') {
            buf[len++] = ladvance(lex);
            base = 16;
            while (isxdigit((unsigned char)lpeek(lex))) {
                if (len + 2 >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
                buf[len++] = ladvance(lex);
            }
        } else {
            while (isdigit((unsigned char)lpeek(lex))) {
                if (len + 2 >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
                buf[len++] = ladvance(lex);
            }
        }
    } else {
        while (isdigit((unsigned char)lpeek(lex))) {
            if (len + 2 >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
            buf[len++] = ladvance(lex);
        }
    }
    buf[len] = '\0';

    long long value = strtoll(buf, NULL, base);
    char disp[32];
    snprintf(disp, sizeof(disp), "%lld", value);
    free(buf);

    return make_tok_str(ASM_TOK_NUMBER, disp, value, line, col);
}

/* Read an identifier (alphanum + underscore) from current position. */
static AsmToken read_ident(AsmLexer *lex, int line, int col) {
    int  start = lex->pos;
    while (isalnum((unsigned char)lpeek(lex)) || lpeek(lex) == '_')
        ladvance(lex);
    int  len = lex->pos - start;
    char *buf = (char *)malloc(len + 1);
    memcpy(buf, lex->source + start, len);
    buf[len] = '\0';
    AsmToken t;
    t.type  = ASM_TOK_IDENT;
    t.text  = buf;
    t.value = 0;
    t.line  = line;
    t.col   = col;
    return t;
}

AsmToken asm_lexer_next(AsmLexer *lex) {
    /* Skip horizontal whitespace */
    while (lpeek(lex) == ' ' || lpeek(lex) == '\t' || lpeek(lex) == '\r')
        ladvance(lex);

    int  line = lex->line;
    int  col  = lex->pos - lex->line_start + 1;
    char c    = lpeek(lex);

    if (c == '\0')
        return make_tok1(ASM_TOK_EOF, line, col);

    if (c == '\n') {
        ladvance(lex);
        return make_tok1(ASM_TOK_NEWLINE, line, col);
    }

    /* Comment: skip to end of line, emit NEWLINE so parser ends current stmt */
    if (c == ';') {
        while (lpeek(lex) != '\0' && lpeek(lex) != '\n')
            ladvance(lex);
        return make_tok1(ASM_TOK_NEWLINE, line, col);
    }

    /* String literals */
    if (c == '\'' || c == '"')
        return read_string_lit(lex, c, line, col);

    /* Dot prefix: local label (.Lexit0) or directive (.text) */
    if (c == '.') {
        ladvance(lex); /* consume '.' */
        if (isalpha((unsigned char)lpeek(lex)) || lpeek(lex) == '_' ||
            isdigit((unsigned char)lpeek(lex))) {
            /* Read rest of name, prepend '.' */
            int  start = lex->pos;
            while (isalnum((unsigned char)lpeek(lex)) || lpeek(lex) == '_')
                ladvance(lex);
            int  len = lex->pos - start;
            char *buf = (char *)malloc(len + 2);
            buf[0] = '.';
            memcpy(buf + 1, lex->source + start, len);
            buf[len + 1] = '\0';
            AsmToken t;
            t.type  = ASM_TOK_IDENT;
            t.text  = buf;
            t.value = 0;
            t.line  = line;
            t.col   = col;
            return t;
        }
        return make_tok1(ASM_TOK_DOT, line, col);
    }

    /* Identifiers */
    if (isalpha((unsigned char)c) || c == '_')
        return read_ident(lex, line, col);

    /* Numbers */
    if (isdigit((unsigned char)c))
        return read_number(lex, line, col);

    /* Single-character punctuation */
    ladvance(lex);
    switch (c) {
    case ',': return make_tok1(ASM_TOK_COMMA,    line, col);
    case ':': return make_tok1(ASM_TOK_COLON,    line, col);
    case '[': return make_tok1(ASM_TOK_LBRACKET, line, col);
    case ']': return make_tok1(ASM_TOK_RBRACKET, line, col);
    case '+': return make_tok1(ASM_TOK_PLUS,     line, col);
    case '-': return make_tok1(ASM_TOK_MINUS,    line, col);
    case '*': return make_tok1(ASM_TOK_STAR,     line, col);
    default:
        break;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
    return error_tok(msg, line, col);
}

void asm_token_free(AsmToken *tok) {
    if (tok && tok->text) {
        free(tok->text);
        tok->text = NULL;
    }
}

const char *asm_token_type_name(AsmTokenType type) {
    switch (type) {
    case ASM_TOK_IDENT:    return "ident";
    case ASM_TOK_NUMBER:   return "number";
    case ASM_TOK_STRING:   return "string";
    case ASM_TOK_COMMA:    return ",";
    case ASM_TOK_COLON:    return ":";
    case ASM_TOK_LBRACKET: return "[";
    case ASM_TOK_RBRACKET: return "]";
    case ASM_TOK_PLUS:     return "+";
    case ASM_TOK_MINUS:    return "-";
    case ASM_TOK_STAR:     return "*";
    case ASM_TOK_DOT:      return ".";
    case ASM_TOK_NEWLINE:  return "newline";
    case ASM_TOK_EOF:      return "EOF";
    case ASM_TOK_ERROR:    return "error";
    }
    return "?";
}
