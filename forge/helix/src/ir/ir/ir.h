/*
 * ir.h - Forge intermediate representation
 *
 * Minimal SSA-style three-address IR with basic blocks and CFG-friendly
 * control flow. Frontends lower AST into this layer before codegen.
 */

#ifndef FORGE_IR_H
#define FORGE_IR_H

#include <stdio.h>
#include "../ast/helix-ast.h"

typedef enum {
    IR_VALUE_NONE,
    IR_VALUE_CONST_INT,
    IR_VALUE_CONST_STRING,
    IR_VALUE_TEMP,
    IR_VALUE_LOCAL
} IRValueKind;

typedef struct {
    IRValueKind kind;
    int is_string;
    union {
        int int_value;
        int temp_id;
        int local_index;
        char *string_value;
    } as;
} IRValue;

typedef enum {
    IR_OP_CONST,
    IR_OP_LOAD,
    IR_OP_STORE,
    IR_OP_ADD,
    IR_OP_SUB,
    IR_OP_MUL,
    IR_OP_DIV,
    IR_OP_MOD,
    IR_OP_NEG,
    IR_OP_CMP,
    IR_OP_JUMP,
    IR_OP_BRANCH,
    IR_OP_CALL,
    IR_OP_RETURN,
    IR_OP_NOP
} IROp;

typedef enum {
    IR_CMP_EQ,
    IR_CMP_NE,
    IR_CMP_LT,
    IR_CMP_GT,
    IR_CMP_LE,
    IR_CMP_GE
} IRCmpOp;

typedef struct {
    char *name;
    int is_string;
    int size_bytes;
} IRLocal;

typedef struct IRInstruction IRInstruction;

struct IRInstruction {
    IROp op;
    int line;
    int has_result;
    IRValue result;
    union {
        struct {
            IRValue value;
        } constant;
        struct {
            int local_index;
            int byte_offset;
        } load;
        struct {
            int local_index;
            int byte_offset;
            IRValue value;
        } store;
        struct {
            IRValue lhs;
            IRValue rhs;
        } binary;
        struct {
            IRValue operand;
        } unary;
        struct {
            IRCmpOp cmp;
            IRValue lhs;
            IRValue rhs;
        } compare;
        struct {
            int target_block;
        } jump;
        struct {
            IRValue cond;
            int true_block;
            int false_block;
        } branch;
        struct {
            char *callee;
            IRValue *args;
            int arg_count;
        } call;
        struct {
            IRValue value;
            int has_value;
        } ret;
    } as;
};

typedef struct {
    char *name;
    IRInstruction *instructions;
    int instruction_count;
    int instruction_cap;
} IRBasicBlock;

typedef struct {
    char *name;
    int is_extern;
    char **params;
    int param_count;
    IRLocal *locals;
    int local_count;
    int local_cap;
    IRBasicBlock *blocks;
    int block_count;
    int block_cap;
    int temp_count;
} IRFunction;

typedef struct {
    IRFunction *functions;
    int function_count;
    int function_cap;
} IRProgram;

IRValue ir_value_none(void);
IRValue ir_value_const_int(int value);
IRValue ir_value_const_string(char *value);
IRValue ir_value_temp(int temp_id, int is_string);
IRValue ir_value_local(int local_index, int is_string);

IRProgram *ir_program_new(void);
void ir_program_free(IRProgram *program);

IRFunction *ir_function_new(const char *name, int is_extern);
void ir_function_free(IRFunction *func);

IRBasicBlock *ir_function_add_block(IRFunction *func, const char *name);
IRInstruction *ir_block_add_instruction(IRBasicBlock *block, IRInstruction instruction);

int ir_function_add_local(IRFunction *func, const char *name, int size_bytes, int is_string);
int ir_function_lookup_local(const IRFunction *func, const char *name);

void ir_program_add_function(IRProgram *program, IRFunction *func);

void ir_dump_program(const IRProgram *program, FILE *out);

#endif /* FORGE_IR_H */
