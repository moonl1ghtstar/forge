/*
 * codegen.h - x86-64 assembly code generation interface for the Anvil compiler
 */

#ifndef ANVIL_CODEGEN_H
#define ANVIL_CODEGEN_H

#include <stdio.h>
#include "../ir/ir/ir.h"

int codegen_emit(IRProgram *program, FILE *out);

#endif /* ANVIL_CODEGEN_H */
