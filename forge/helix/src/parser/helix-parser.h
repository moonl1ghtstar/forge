/*
 * parser.h - Recursive descent parser interface for the Forge compiler
 *
 * Parses a token stream into an Abstract Syntax Tree (AST).
 * Follows the Helix grammar specification.
 */

#ifndef FORGE_PARSER_H
#define FORGE_PARSER_H

#include "../ast/helix-ast.h"
#include "../lexer/helix-lexer.h"

/* Parser state: holds the lexer and current lookahead token */
typedef struct {
    Lexer *lexer;
    Token current;  /* Current token (lookahead) */
    Token previous; /* Previous token (for error context) */
    int had_error;  /* Set to 1 if any parse error occurred */
} Parser;

/* Initialize the parser with a lexer and prime the first token */
void parser_init(Parser *parser, Lexer *lexer);

/* Parse the entire source and return the AST_PROGRAM root node */
ASTNode *parse_program(Parser *parser);

#endif /* FORGE_PARSER_H */
