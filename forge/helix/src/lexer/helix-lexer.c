/*
 * lexer.c - Lexer implementation for the Forge compiler
 *
 * Scans Helix source code character by character and produces tokens.
 * Handles single-line (//) and multi-line comments, keywords vs identifiers,
 * numeric literals, string literals, and all operators including two-character
 * ones (==, !=, <=, >=).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// h
#include "helix-lexer.h"

/* Initialize the lexer with source code text */
void lexer_init(Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->pos = 0;
    lexer->line = 1;
}

/* Peek at the current character without advancing */
static char peek(Lexer *lex) {
    return lex->source[lex->pos];
}

/* Peek at the next character (one ahead) without advancing */
static char peek_next(Lexer *lex) {
    if (lex->source[lex->pos] == '\0')
        return '\0';
    return lex->source[lex->pos + 1];
}

/* Advance the position and return the consumed character */
static char advance(Lexer *lex) {
    char c = lex->source[lex->pos++];
    if (c == '\n')
        lex->line++;
    return c;
}

/* Skip whitespace and comments, leaving the lexer at the next meaningful char */
static void skip_whitespace_and_comments(Lexer *lex) {
    while (peek(lex) != '\0') {
        char c = peek(lex);
        /* Skip whitespace */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
            continue;
        }
        /* Single-line comment: skip until end of line */
        if (c == '/' && peek_next(lex) == '/') {
            advance(lex);
            advance(lex);
            while (peek(lex) != '\0' && peek(lex) != '\n')
                advance(lex);
            continue;
        }
        /* Multi-line comment: skip until closing star-slash */
        if (c == '/' && peek_next(lex) == '*') {
            advance(lex);
            advance(lex);
            while (peek(lex) != '\0') {
                if (peek(lex) == '*' && peek_next(lex) == '/') {
                    advance(lex);
                    advance(lex);
                    break;
                }
                advance(lex);
            }
            continue;
        }
        break;
    }
}

/* Create a token with the given type and a single-character lexeme */
static Token make_token(TokenType type, int line) {
    Token t;
    t.type = type;
    t.lexeme = NULL;
    t.line = line;
    t.value = 0;
    return t;
}

/* Create a token with a dynamically allocated lexeme string */
static Token make_token_str(TokenType type, const char *lex, int line) {
    Token t;
    t.type = type;
    t.lexeme = strdup(lex);
    t.line = line;
    t.value = 0;
    return t;
}

/* Create an error token with a message */
static Token error_token(const char *msg, int line) {
    Token t;
    t.type = TOKEN_ERROR;
    t.lexeme = strdup(msg);
    t.line = line;
    t.value = 0;
    return t;
}

/* Check if a string is a keyword and return the corresponding token type */
static TokenType check_keyword(const char *word) {
    if (strcmp(word, "function") == 0)
        return TOKEN_FUNCTION;
    if (strcmp(word, "struct") == 0)
        return TOKEN_STRUCT;
    if (strcmp(word, "var") == 0)
        return TOKEN_VAR;
    if (strcmp(word, "if") == 0)
        return TOKEN_IF;
    if (strcmp(word, "else") == 0)
        return TOKEN_ELSE;
    if (strcmp(word, "while") == 0)
        return TOKEN_WHILE;
    if (strcmp(word, "return") == 0)
        return TOKEN_RETURN;
    if (strcmp(word, "import") == 0)
        return TOKEN_IMPORT;
    if (strcmp(word, "extern") == 0)
        return TOKEN_EXTERN;
    if (strcmp(word, "fn") == 0)
        return TOKEN_FN;
    if (strcmp(word, "for") == 0)
        return TOKEN_FOR;
    if (strcmp(word, "until") == 0)
        return TOKEN_UNTIL;
    if (strcmp(word, "do") == 0)
        return TOKEN_DO;
    if (strcmp(word, "switch") == 0)
        return TOKEN_SWITCH;
    if (strcmp(word, "case") == 0)
        return TOKEN_CASE;
    if (strcmp(word, "break") == 0)
        return TOKEN_BREAK;
    if (strcmp(word, "pass") == 0)
        return TOKEN_PASS;
    if (strcmp(word, "let") == 0)
        return TOKEN_LET;
    if (strcmp(word, "const") == 0)
        return TOKEN_CONST;
    if (strcmp(word, "global") == 0)
        return TOKEN_GLOBAL;
    if (strcmp(word, "True") == 0)
        return TOKEN_TRUE;
    if (strcmp(word, "False") == 0)
        return TOKEN_FALSE;
    return TOKEN_IDENT;
}

/* Read a number literal (integer only in current version) */
static Token read_number(Lexer *lex) {
    int start = lex->pos;
    int line = lex->line;
    int base = 10;
    if (peek(lex) == '0' && (peek_next(lex) == 'x' || peek_next(lex) == 'X')) {
        advance(lex);
        advance(lex);
        base = 16;
        start = lex->pos;
        while (isxdigit((unsigned char)peek(lex)))
            advance(lex);
    } else {
        while (isdigit((unsigned char)peek(lex)))
            advance(lex);
    }
    int len = lex->pos - start;
    char *buf = (char *)malloc(len + 1);
    memcpy(buf, lex->source + start, len);
    buf[len] = '\0';
    Token t;
    t.type = TOKEN_NUMBER;
    t.lexeme = buf;
    t.line = line;
    t.value = (int)strtol(buf, NULL, base);
    return t;
}

/* Read an identifier or keyword */
static Token read_ident(Lexer *lex) {
    int start = lex->pos;
    int line = lex->line;
    while (isalnum((unsigned char)peek(lex)) || peek(lex) == '_')
        advance(lex);
    int len = lex->pos - start;
    char *buf = (char *)malloc(len + 1);
    memcpy(buf, lex->source + start, len);
    buf[len] = '\0';
    TokenType type = check_keyword(buf);
    Token t;
    t.type = type;
    t.lexeme = buf;
    t.line = line;
    t.value = 0;
    return t;
}

/* Read a string literal enclosed in double quotes */
static Token read_string(Lexer *lex) {
    int line = lex->line;
    advance(lex); /* consume opening quote */
    int start = lex->pos;
    while (peek(lex) != '\0' && peek(lex) != '"') {
        if (peek(lex) == '\\')
            advance(lex); /* skip escaped char */
        advance(lex);
    }
    if (peek(lex) == '\0')
        return error_token("unterminated string literal", line);
    int len = lex->pos - start;
    char *buf = (char *)malloc(len + 1);
    memcpy(buf, lex->source + start, len);
    buf[len] = '\0';
    advance(lex); /* consume closing quote */
    Token t;
    t.type = TOKEN_STRING;
    t.lexeme = buf;
    t.line = line;
    t.value = 0;
    return t;
}

/* Get the next token from the source stream */
Token lexer_next(Lexer *lex) {
    skip_whitespace_and_comments(lex);

    if (peek(lex) == '\0')
        return make_token(TOKEN_EOF, lex->line);

    int line = lex->line;
    char c = peek(lex);

    /* Identifiers and keywords */
    if (isalpha((unsigned char)c) || c == '_')
        return read_ident(lex);

    /* Number literals */
    if (isdigit((unsigned char)c))
        return read_number(lex);

    /* String literals */
    if (c == '"')
        return read_string(lex);

    /* Single-character tokens and two-character operators */
    advance(lex);
    switch (c) {
    case '{':
        return make_token(TOKEN_LBRACE, line);
    case '}':
        return make_token(TOKEN_RBRACE, line);
    case '(':
        return make_token(TOKEN_LPAREN, line);
    case ')':
        return make_token(TOKEN_RPAREN, line);
    case ';':
        return make_token(TOKEN_SEMICOLON, line);
    case ',':
        return make_token(TOKEN_COMMA, line);
    case '.':
        return make_token(TOKEN_DOT, line);
    case ':':
        return make_token(TOKEN_COLON, line);
    case '+':
        if (peek(lex) == '+') {
            advance(lex);
            return make_token_str(TOKEN_INCREMENT, "++", line);
        }
        return make_token(TOKEN_PLUS, line);
    case '-':
        return make_token(TOKEN_MINUS, line);
    case '*':
        return make_token(TOKEN_STAR, line);
    case '/':
        return make_token(TOKEN_SLASH, line);
    case '=':
        if (peek(lex) == '=') {
            advance(lex);
            return make_token_str(TOKEN_EQ, "==", line);
        }
        return make_token(TOKEN_ASSIGN, line);
    case '!':
        if (peek(lex) == '=') {
            advance(lex);
            return make_token_str(TOKEN_NEQ, "!=", line);
        }
        return make_token(TOKEN_NOT, line);
    case '<':
        if (peek(lex) == '=') {
            advance(lex);
            return make_token_str(TOKEN_LE, "<=", line);
        }
        return make_token(TOKEN_LT, line);
    case '>':
        if (peek(lex) == '=') {
            advance(lex);
            return make_token_str(TOKEN_GE, ">=", line);
        }
        return make_token(TOKEN_GT, line);
    default:
        break;
    }

    /* Unknown character: produce an error token */
    char msg[64];
    snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
    return error_token(msg, line);
}

/* Free the dynamically allocated lexeme of a token */
void token_free(Token *token) {
    if (token->lexeme) {
        free(token->lexeme);
        token->lexeme = NULL;
    }
}

/* Convert a token type enum to a readable string (for diagnostics) */
const char *token_type_to_string(TokenType type) {
    switch (type) {
    case TOKEN_FUNCTION:
        return "function";
    case TOKEN_STRUCT:
        return "struct";
    case TOKEN_VAR:
        return "var";
    case TOKEN_IF:
        return "if";
    case TOKEN_ELSE:
        return "else";
    case TOKEN_WHILE:
        return "while";
    case TOKEN_RETURN:
        return "return";
    case TOKEN_IMPORT:
        return "import";
    case TOKEN_EXTERN:
        return "extern";
    case TOKEN_FN:
        return "fn";
    case TOKEN_FOR:
        return "for";
    case TOKEN_UNTIL:
        return "until";
    case TOKEN_DO:
        return "do";
    case TOKEN_SWITCH:
        return "switch";
    case TOKEN_CASE:
        return "case";
    case TOKEN_BREAK:
        return "break";
    case TOKEN_PASS:
        return "pass";
    case TOKEN_LET:
        return "let";
    case TOKEN_CONST:
        return "const";
    case TOKEN_GLOBAL:
        return "global";
    case TOKEN_TRUE:
        return "True";
    case TOKEN_FALSE:
        return "False";
    case TOKEN_INCREMENT:
        return "++";
    case TOKEN_IDENT:
        return "identifier";
    case TOKEN_NUMBER:
        return "number";
    case TOKEN_STRING:
        return "string";
    case TOKEN_LBRACE:
        return "{";
    case TOKEN_RBRACE:
        return "}";
    case TOKEN_LPAREN:
        return "(";
    case TOKEN_RPAREN:
        return ")";
    case TOKEN_SEMICOLON:
        return ";";
    case TOKEN_COMMA:
        return ",";
    case TOKEN_DOT:
        return ".";
    case TOKEN_COLON:
        return ":";
    case TOKEN_PLUS:
        return "+";
    case TOKEN_MINUS:
        return "-";
    case TOKEN_STAR:
        return "*";
    case TOKEN_SLASH:
        return "/";
    case TOKEN_EQ:
        return "==";
    case TOKEN_NEQ:
        return "!=";
    case TOKEN_LT:
        return "<";
    case TOKEN_GT:
        return ">";
    case TOKEN_LE:
        return "<=";
    case TOKEN_GE:
        return ">=";
    case TOKEN_ASSIGN:
        return "=";
    case TOKEN_NOT:
        return "!";
    case TOKEN_EOF:
        return "end of file";
    case TOKEN_ERROR:
        return "error";
    }
    return "unknown";
}
