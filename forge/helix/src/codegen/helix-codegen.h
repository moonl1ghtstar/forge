/*
 * codegen.h - x86-64 assembly code generation interface for the Forge compiler
 */

#ifndef FORGE_CODEGEN_H
#define FORGE_CODEGEN_H

#include <stdio.h>
#include "../ast/helix-ast.h"

#define MAX_LOOP_DEPTH 64

typedef struct {
    int start_lbl; /* loop top label (pass/continue target) */
    int end_lbl;   /* loop exit label (break target) */
} LoopFrame;

/* Code generation context: output file, label counter, state flags */
typedef struct {
    FILE *out;
    int label_count;
    int needs_printf;
    int needs_scanf;
    int stack_offset;
    int exit_label;
    int temp_slot;
    int frame_size;
    LoopFrame loop_stack[MAX_LOOP_DEPTH];
    int loop_depth;
} CodegenCtx;

int codegen_emit(ASTNode *program, FILE *out);

#endif /* FORGE_CODEGEN_H */
