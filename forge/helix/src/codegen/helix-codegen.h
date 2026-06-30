/*
 * codegen.h - x86-64 assembly code generation interface for the Forge compiler
 */

#ifndef FORGE_CODEGEN_H
#define FORGE_CODEGEN_H

#include <stdio.h>
#include "../ir/ir/ir.h"

int codegen_emit(IRProgram *program, FILE *out);

#endif /* FORGE_CODEGEN_H */
