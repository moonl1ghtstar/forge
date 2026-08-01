/*
 * ir-builder.h - AST to IR lowering.
 */

#ifndef ANVIL_IR_BUILDER_H
#define ANVIL_IR_BUILDER_H

#include "../ir/ir.h"

IRProgram *ir_build_program(ASTNode *program);

#endif /* ANVIL_IR_BUILDER_H */
