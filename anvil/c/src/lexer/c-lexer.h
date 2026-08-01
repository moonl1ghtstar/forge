/*
 * c-lexer.h - Lexer for Anvil C frontend
 */

#ifndef ANVIL_C_LEXER_H
#define ANVIL_C_LEXER_H

#include "../c-token.h"

typedef struct {
    const char *source;
    int pos;
    int line;
    int line_start;
} CLexer;

void c_lexer_init(CLexer *lexer, const char *source);
CToken c_lexer_next(CLexer *lexer);
void c_token_free(CToken *token);

#endif
