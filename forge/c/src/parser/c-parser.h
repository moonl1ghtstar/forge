/*
 * c-parser.h - Parser for Forge C frontend
 */

#ifndef FORGE_C_PARSER_H
#define FORGE_C_PARSER_H

#include "../../helix/src/ast/helix-ast.h"
#include "../lexer/c-lexer.h"

typedef struct {
    CLexer *lexer;
    CToken current;
    CToken previous;
    int had_error;
    int inject_main; /* 1 = synthesize main() if absent (default); 0 = library mode */
} CParser;

void c_parser_init(CParser *parser, CLexer *lexer);
/* inject_main=1: synthesise main() if absent (executable build).
 * inject_main=0: library mode — no implicit main (use for -obj on library files). */
void c_parser_init_lib(CParser *parser, CLexer *lexer);
ASTNode *c_parse_program(CParser *parser);

#endif
