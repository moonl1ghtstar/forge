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
} CParser;

void c_parser_init(CParser *parser, CLexer *lexer);
ASTNode *c_parse_program(CParser *parser);

#endif
