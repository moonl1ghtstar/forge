/*
 * sema.h - Semantic analysis interface for the Forge compiler
 *
 * Performs symbol table construction, type checking, and validation.
 * Reports errors for undeclared variables, duplicate definitions,
 * argument count mismatches, etc.
 */

#ifndef FORGE_SEMA_H
#define FORGE_SEMA_H

#include "../ast/helix-ast.h"

/* Run semantic analysis on the AST. Returns 0 on success, 1 on error. */
int sema_analyze(ASTNode *program);

#endif /* FORGE_SEMA_H */
