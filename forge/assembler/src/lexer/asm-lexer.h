/*
 * asm-lexer.h - Tokenizer for Forge ASM (NASM-subset)
 *
 * Breaks a NASM-compatible assembly source text into tokens.
 * Comments (;) are skipped. Strings ('...' or "...") are returned
 * as ASM_TOK_STRING with the content (no quotes). Numbers support
 * decimal and 0x hex. Each logical line ends with ASM_TOK_NEWLINE.
 */
#ifndef FORGE_ASM_LEXER_H
#define FORGE_ASM_LEXER_H

typedef enum {
    ASM_TOK_IDENT,      /* identifier, opcode, register, keyword */
    ASM_TOK_NUMBER,     /* integer literal */
    ASM_TOK_STRING,     /* '...' or "..." literal (content only, no quotes) */
    ASM_TOK_COMMA,      /* , */
    ASM_TOK_COLON,      /* : */
    ASM_TOK_LBRACKET,   /* [ */
    ASM_TOK_RBRACKET,   /* ] */
    ASM_TOK_PLUS,       /* + */
    ASM_TOK_MINUS,      /* - */
    ASM_TOK_STAR,       /* * (scale factor in SIB addressing) */
    ASM_TOK_DOT,        /* . (start of local label) */
    ASM_TOK_NEWLINE,    /* end of logical line */
    ASM_TOK_EOF,
    ASM_TOK_ERROR
} AsmTokenType;

typedef struct {
    AsmTokenType type;
    char        *text;  /* heap-allocated copy; call asm_token_free() when done */
    long long    value; /* numeric value for ASM_TOK_NUMBER */
    int          line;
    int          col;
} AsmToken;

typedef struct {
    const char *source;
    int         pos;
    int         line;
    int         line_start;
} AsmLexer;

void     asm_lexer_init(AsmLexer *lex, const char *source);
AsmToken asm_lexer_next(AsmLexer *lex);
void     asm_token_free(AsmToken *tok);

/* Human-readable token type name (for diagnostics) */
const char *asm_token_type_name(AsmTokenType type);

#endif /* FORGE_ASM_LEXER_H */
