/*
 * sema.h - Semantic analysis interface for the Anvil compiler
 *
 * Performs symbol table construction, type checking, and validation.
 * Reports errors for undeclared variables, duplicate definitions,
 * argument count mismatches, etc.
 */

#ifndef ANVIL_SEMA_H
#define ANVIL_SEMA_H

#include "../ast/helix-ast.h"

/* Run semantic analysis on the AST. Returns 0 on success, 1 on error. */
int sema_analyze(ASTNode *program);

#endif /* ANVIL_SEMA_H */
