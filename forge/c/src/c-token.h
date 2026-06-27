/*
 * c-token.h - Token definitions for Forge C frontend
 */

#ifndef FORGE_C_TOKEN_H
#define FORGE_C_TOKEN_H

typedef enum {
    C_TOK_INT,
    C_TOK_RETURN,
    C_TOK_IF,
    C_TOK_ELSE,
    C_TOK_WHILE,
    C_TOK_FOR,
    C_TOK_BREAK,
    C_TOK_CONTINUE,
    C_TOK_EXTERN,
    C_TOK_IDENT,
    C_TOK_NUMBER,
    C_TOK_LBRACE,
    C_TOK_RBRACE,
    C_TOK_LPAREN,
    C_TOK_RPAREN,
    C_TOK_SEMICOLON,
    C_TOK_COMMA,
    C_TOK_PLUS,
    C_TOK_MINUS,
    C_TOK_STAR,
    C_TOK_SLASH,
    C_TOK_ASSIGN,
    C_TOK_EQ,
    C_TOK_NEQ,
    C_TOK_LT,
    C_TOK_GT,
    C_TOK_LE,
    C_TOK_GE,
    C_TOK_NOT,
    C_TOK_EOF,
    C_TOK_ERROR
} CTokenType;

typedef struct {
    CTokenType type;
    char *lexeme;
    int line;
    int value;
} CToken;

const char *c_token_type_to_string(CTokenType type);

#endif
