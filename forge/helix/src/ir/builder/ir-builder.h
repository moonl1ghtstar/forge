/*
 * ir-builder.h - AST to IR lowering.
 */

#ifndef FORGE_IR_BUILDER_H
#define FORGE_IR_BUILDER_H

#include "../ir.h"

IRProgram *ir_build_program(ASTNode *program);

#endif /* FORGE_IR_BUILDER_H */
