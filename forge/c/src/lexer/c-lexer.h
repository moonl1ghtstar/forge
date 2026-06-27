/*
 * c-lexer.h - Lexer for Forge C frontend
 */

#ifndef FORGE_C_LEXER_H
#define FORGE_C_LEXER_H

#include "c-token.h"

typedef struct {
    const char *source;
    int pos;
    int line;
} CLexer;

void c_lexer_init(CLexer *lexer, const char *source);
CToken c_lexer_next(CLexer *lexer);
void c_token_free(CToken *token);

#endif
