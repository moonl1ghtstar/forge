/*
 * token.h - Token type definitions for the Forge lexer
 *
 * Defines all token types recognized by the Helix lexer.
 * Each token represents a lexical unit (keyword, operator, literal, etc.)
 */

#ifndef FORGE_TOKEN_H
#define FORGE_TOKEN_H

/* Token type enumeration covering all Helix lexical elements */
typedef enum {
    /* Keywords */
    TOKEN_FUNCTION,
    TOKEN_STRUCT,
    TOKEN_VAR,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_RETURN,
    TOKEN_IMPORT,
    TOKEN_EXTERN,
    TOKEN_FN,

    TOKEN_FOR,
    TOKEN_UNTIL,
    TOKEN_DO,
    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_BREAK,
    TOKEN_PASS,
    TOKEN_LET,
    TOKEN_CONST,
    TOKEN_GLOBAL,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_INCREMENT,

    /* Identifiers and literals */
    TOKEN_IDENT,
    TOKEN_INT_LITERAL,
    TOKEN_LONG_LITERAL,
    TOKEN_FLOAT_LITERAL,
    TOKEN_STRING,

    /* Delimiters */
    TOKEN_LBRACE,    /* { */
    TOKEN_RBRACE,    /* } */
    TOKEN_LPAREN,    /* ( */
    TOKEN_RPAREN,    /* ) */
    TOKEN_SEMICOLON, /* ; */
    TOKEN_COMMA,     /* , */
    TOKEN_DOT,       /* . */
    TOKEN_COLON,     /* : */

    /* Arithmetic operators */
    TOKEN_PLUS,  /* + */
    TOKEN_MINUS, /* - */
    TOKEN_STAR,  /* * */
    TOKEN_SLASH, /* / */

    /* Comparison operators */
    TOKEN_EQ,  /* == */
    TOKEN_NEQ, /* != */
    TOKEN_LT,  /* < */
    TOKEN_GT,  /* > */
    TOKEN_LE,  /* <= */
    TOKEN_GE,  /* >= */

    /* Assignment */
    TOKEN_ASSIGN, /* = */

    /* Logical operators */
    TOKEN_NOT, /* ! */

    /* Special tokens */
    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

/* Token structure holding type, lexeme text, line number, and numeric value */
typedef struct {
    TokenType type;
    char *lexeme; /* Dynamically allocated string of the token text */
    int line;     /* Source line number where this token appears */
    int col;      /* Source column number (1-based) */
    int value;    /* Numeric value for TOKEN_INT_LITERAL tokens */
    long long long_value;  /* Numeric value for TOKEN_LONG_LITERAL tokens */
    double float_value;    /* Numeric value for TOKEN_FLOAT_LITERAL tokens */
} Token;

/* Backward compatibility alias */
#define TOKEN_NUMBER TOKEN_INT_LITERAL

/* Convert a token type to a human-readable string for error messages */
const char *token_type_to_string(TokenType type);

#endif
/* FORGE_TOKEN_H */
