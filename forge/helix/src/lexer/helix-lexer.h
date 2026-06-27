/*
 * lexer.h - Lexer (tokenizer) interface for the Forge compiler
 *
 * Reads Helix source code and produces a stream of tokens.
 * Handles keywords, identifiers, numbers, strings, operators, and comments.
 */

#ifndef FORGE_LEXER_H
#define FORGE_LEXER_H

#include "helix-token.h"

/* Lexer state: tracks source text, current position, and line number */
typedef struct {
    const char *source; /* Full source code string (not owned) */
    int pos;            /* Current character index */
    int line;           /* Current line number (1-based) */
} Lexer;

/* Initialize a lexer with the given source code string */
void lexer_init(Lexer *lexer, const char *source);

/* Return the next token from the source stream */
Token lexer_next(Lexer *lexer);

/* Free memory allocated for a token's lexeme */
void token_free(Token *token);

#endif /* FORGE_LEXER_H */
