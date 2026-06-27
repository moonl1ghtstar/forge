/*
 * c-lexer.c - Lexer for Forge C frontend
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c-lexer.h"

static char peek(CLexer *lex) {
    return lex->source[lex->pos];
}

static char peek_next(CLexer *lex) {
    if (lex->source[lex->pos] == '\0')
        return '\0';
    return lex->source[lex->pos + 1];
}

static char advance(CLexer *lex) {
    char c = lex->source[lex->pos++];
    if (c == '\n')
        lex->line++;
    return c;
}

void c_lexer_init(CLexer *lexer, const char *source) {
    lexer->source = source;
    lexer->pos = 0;
    lexer->line = 1;
}

static void skip_ws_and_comments(CLexer *lex) {
    while (peek(lex) != '\0') {
        char c = peek(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
            continue;
        }
        if (c == '/' && peek_next(lex) == '/') {
            while (peek(lex) != '\0' && peek(lex) != '\n')
                advance(lex);
            continue;
        }
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

static CToken make_token(CTokenType type, int line) {
    CToken t;
    t.type = type;
    t.lexeme = NULL;
    t.line = line;
    t.value = 0;
    return t;
}

static CToken make_token_str(CTokenType type, const char *lex, int line) {
    CToken t;
    t.type = type;
    t.lexeme = strdup(lex);
    t.line = line;
    t.value = 0;
    return t;
}

static CToken error_token(const char *msg, int line) {
    CToken t;
    t.type = C_TOK_ERROR;
    t.lexeme = strdup(msg);
    t.line = line;
    t.value = 0;
    return t;
}

static CTokenType keyword_type(const char *word) {
    if (strcmp(word, "int") == 0)
        return C_TOK_INT;
    if (strcmp(word, "return") == 0)
        return C_TOK_RETURN;
    if (strcmp(word, "if") == 0)
        return C_TOK_IF;
    if (strcmp(word, "else") == 0)
        return C_TOK_ELSE;
    if (strcmp(word, "while") == 0)
        return C_TOK_WHILE;
    if (strcmp(word, "for") == 0)
        return C_TOK_FOR;
    if (strcmp(word, "break") == 0)
        return C_TOK_BREAK;
    if (strcmp(word, "continue") == 0)
        return C_TOK_CONTINUE;
    if (strcmp(word, "extern") == 0)
        return C_TOK_EXTERN;
    return C_TOK_IDENT;
}

static CToken read_ident(CLexer *lex) {
    int start = lex->pos;
    int line = lex->line;
    while (isalnum((unsigned char)peek(lex)) || peek(lex) == '_')
        advance(lex);
    {
        int len = lex->pos - start;
        char *buf = (char *)malloc(len + 1);
        memcpy(buf, lex->source + start, len);
        buf[len] = '\0';
        {
            CTokenType type = keyword_type(buf);
            CToken t;
            t.type = type;
            t.lexeme = buf;
            t.line = line;
            t.value = 0;
            return t;
        }
    }
}

static CToken read_number(CLexer *lex) {
    int start = lex->pos;
    int line = lex->line;
    while (isdigit((unsigned char)peek(lex)))
        advance(lex);
    {
        int len = lex->pos - start;
        char *buf = (char *)malloc(len + 1);
        memcpy(buf, lex->source + start, len);
        buf[len] = '\0';
        {
            CToken t;
            t.type = C_TOK_NUMBER;
            t.lexeme = buf;
            t.line = line;
            t.value = (int)strtol(buf, NULL, 10);
            return t;
        }
    }
}

CToken c_lexer_next(CLexer *lex) {
    skip_ws_and_comments(lex);

    if (peek(lex) == '\0')
        return make_token(C_TOK_EOF, lex->line);

    {
        int line = lex->line;
        char c = peek(lex);
        if (isalpha((unsigned char)c) || c == '_')
            return read_ident(lex);
        if (isdigit((unsigned char)c))
            return read_number(lex);
        advance(lex);
        switch (c) {
        case '{': return make_token(C_TOK_LBRACE, line);
        case '}': return make_token(C_TOK_RBRACE, line);
        case '(': return make_token(C_TOK_LPAREN, line);
        case ')': return make_token(C_TOK_RPAREN, line);
        case ';': return make_token(C_TOK_SEMICOLON, line);
        case ',': return make_token(C_TOK_COMMA, line);
        case '+': return make_token(C_TOK_PLUS, line);
        case '-': return make_token(C_TOK_MINUS, line);
        case '*': return make_token(C_TOK_STAR, line);
        case '/': return make_token(C_TOK_SLASH, line);
        case '=':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token_str(C_TOK_EQ, "==", line);
            }
            return make_token(C_TOK_ASSIGN, line);
        case '!':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token_str(C_TOK_NEQ, "!=", line);
            }
            return make_token(C_TOK_NOT, line);
        case '<':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token_str(C_TOK_LE, "<=", line);
            }
            return make_token(C_TOK_LT, line);
        case '>':
            if (peek(lex) == '=') {
                advance(lex);
                return make_token_str(C_TOK_GE, ">=", line);
            }
            return make_token(C_TOK_GT, line);
        default:
            break;
        }
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
            return error_token(msg, line);
        }
    }
}

void c_token_free(CToken *token) {
    if (token && token->lexeme) {
        free(token->lexeme);
        token->lexeme = NULL;
    }
}

const char *c_token_type_to_string(CTokenType type) {
    switch (type) {
    case C_TOK_INT: return "int";
    case C_TOK_RETURN: return "return";
    case C_TOK_IF: return "if";
    case C_TOK_ELSE: return "else";
    case C_TOK_WHILE: return "while";
    case C_TOK_FOR: return "for";
    case C_TOK_BREAK: return "break";
    case C_TOK_CONTINUE: return "continue";
    case C_TOK_EXTERN: return "extern";
    case C_TOK_IDENT: return "identifier";
    case C_TOK_NUMBER: return "number";
    case C_TOK_LBRACE: return "{";
    case C_TOK_RBRACE: return "}";
    case C_TOK_LPAREN: return "(";
    case C_TOK_RPAREN: return ")";
    case C_TOK_SEMICOLON: return ";";
    case C_TOK_COMMA: return ",";
    case C_TOK_PLUS: return "+";
    case C_TOK_MINUS: return "-";
    case C_TOK_STAR: return "*";
    case C_TOK_SLASH: return "/";
    case C_TOK_ASSIGN: return "=";
    case C_TOK_EQ: return "==";
    case C_TOK_NEQ: return "!=";
    case C_TOK_LT: return "<";
    case C_TOK_GT: return ">";
    case C_TOK_LE: return "<=";
    case C_TOK_GE: return ">=";
    case C_TOK_NOT: return "!";
    case C_TOK_EOF: return "end of file";
    case C_TOK_ERROR: return "error";
    }
    return "unknown";
}
