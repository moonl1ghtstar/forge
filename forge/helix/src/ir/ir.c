/*
 * ir.c - Forge IR helpers, ownership, and debug dump.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"

static char *ir_strdup(const char *s) {
    return s ? strdup(s) : NULL;
}

IRValue ir_value_none(void) {
    IRValue v;
    v.kind = IR_VALUE_NONE;
    v.is_string = 0;
    v.as.int_value = 0;
    return v;
}

IRValue ir_value_const_int(int value) {
    IRValue v;
    v.kind = IR_VALUE_CONST_INT;
    v.is_string = 0;
    v.as.int_value = value;
    return v;
}

IRValue ir_value_const_string(char *value) {
    IRValue v;
    v.kind = IR_VALUE_CONST_STRING;
    v.is_string = 1;
    v.as.string_value = value;
    return v;
}

IRValue ir_value_temp(int temp_id, int is_string) {
    IRValue v;
    v.kind = IR_VALUE_TEMP;
    v.is_string = is_string;
    v.as.temp_id = temp_id;
    return v;
}

IRValue ir_value_local(int local_index, int is_string) {
    IRValue v;
    v.kind = IR_VALUE_LOCAL;
    v.is_string = is_string;
    v.as.local_index = local_index;
    return v;
}

IRProgram *ir_program_new(void) {
    IRProgram *program = (IRProgram *)calloc(1, sizeof(IRProgram));
    if (!program) {
        fprintf(stderr, "Forge error: out of memory\n");
        exit(1);
    }
    return program;
}

static void ir_instruction_free(IRInstruction *ins) {
    if (!ins)
        return;
    switch (ins->op) {
    case IR_OP_CONST:
        if (ins->as.constant.value.kind == IR_VALUE_CONST_STRING)
            free(ins->as.constant.value.as.string_value);
        break;
    case IR_OP_CALL:
        free(ins->as.call.callee);
        free(ins->as.call.args);
        break;
    default:
        break;
    }
}

static void ir_block_free(IRBasicBlock *block) {
    int i;
    if (!block)
        return;
    free(block->name);
    for (i = 0; i < block->instruction_count; i++)
        ir_instruction_free(&block->instructions[i]);
    free(block->instructions);
}

void ir_function_free(IRFunction *func) {
    int i;
    if (!func)
        return;
    free(func->name);
    for (i = 0; i < func->param_count; i++)
        free(func->params[i]);
    free(func->params);
    for (i = 0; i < func->local_count; i++)
        free(func->locals[i].name);
    free(func->locals);
    for (i = 0; i < func->block_count; i++)
        ir_block_free(&func->blocks[i]);
    free(func->blocks);
}

void ir_program_free(IRProgram *program) {
    int i;
    if (!program)
        return;
    for (i = 0; i < program->function_count; i++)
        ir_function_free(&program->functions[i]);
    free(program->functions);
    free(program);
}

IRFunction *ir_function_new(const char *name, int is_extern) {
    IRFunction *func = (IRFunction *)calloc(1, sizeof(IRFunction));
    if (!func) {
        fprintf(stderr, "Forge error: out of memory\n");
        exit(1);
    }
    func->name = ir_strdup(name);
    func->is_extern = is_extern;
    return func;
}

IRBasicBlock *ir_function_add_block(IRFunction *func, const char *name) {
    IRBasicBlock *block;
    if (!func)
        return NULL;
    if (func->block_count >= func->block_cap) {
        func->block_cap = func->block_cap ? func->block_cap * 2 : 8;
        func->blocks = (IRBasicBlock *)realloc(func->blocks, sizeof(IRBasicBlock) * func->block_cap);
    }
    block = &func->blocks[func->block_count++];
    memset(block, 0, sizeof(*block));
    block->name = ir_strdup(name);
    return block;
}

IRInstruction *ir_block_add_instruction(IRBasicBlock *block, IRInstruction instruction) {
    if (!block)
        return NULL;
    if (block->instruction_count >= block->instruction_cap) {
        block->instruction_cap = block->instruction_cap ? block->instruction_cap * 2 : 8;
        block->instructions = (IRInstruction *)realloc(block->instructions, sizeof(IRInstruction) * block->instruction_cap);
    }
    block->instructions[block->instruction_count] = instruction;
    return &block->instructions[block->instruction_count++];
}

int ir_function_add_local(IRFunction *func, const char *name, int size_bytes, int is_string) {
    int i;
    if (!func)
        return -1;
    for (i = 0; i < func->local_count; i++) {
        if (strcmp(func->locals[i].name, name) == 0) {
            func->locals[i].is_string = is_string;
            return i;
        }
    }
    if (func->local_count >= func->local_cap) {
        func->local_cap = func->local_cap ? func->local_cap * 2 : 8;
        func->locals = (IRLocal *)realloc(func->locals, sizeof(IRLocal) * func->local_cap);
    }
    func->locals[func->local_count].name = ir_strdup(name);
    func->locals[func->local_count].is_string = is_string;
    func->locals[func->local_count].size_bytes = size_bytes > 0 ? size_bytes : 8;
    return func->local_count++;
}

int ir_function_lookup_local(const IRFunction *func, const char *name) {
    int i;
    if (!func)
        return -1;
    for (i = 0; i < func->local_count; i++) {
        if (strcmp(func->locals[i].name, name) == 0)
            return i;
    }
    return -1;
}

void ir_program_add_function(IRProgram *program, IRFunction *func) {
    if (!program || !func)
        return;
    if (program->function_count >= program->function_cap) {
        program->function_cap = program->function_cap ? program->function_cap * 2 : 8;
        program->functions = (IRFunction *)realloc(program->functions, sizeof(IRFunction) * program->function_cap);
    }
    program->functions[program->function_count++] = *func;
    free(func);
}

static void dump_value(FILE *out, IRValue value) {
    switch (value.kind) {
    case IR_VALUE_CONST_INT:
        fprintf(out, "%d", value.as.int_value);
        break;
    case IR_VALUE_CONST_STRING:
        fprintf(out, "\"%s\"", value.as.string_value ? value.as.string_value : "");
        break;
    case IR_VALUE_TEMP:
        fprintf(out, "t%d", value.as.temp_id);
        break;
    case IR_VALUE_LOCAL:
        fprintf(out, "local%d", value.as.local_index);
        break;
    default:
        fprintf(out, "<none>");
        break;
    }
}

static const char *op_name(IROp op) {
    switch (op) {
    case IR_OP_CONST: return "const";
    case IR_OP_LOAD: return "load";
    case IR_OP_STORE: return "store";
    case IR_OP_ADD: return "add";
    case IR_OP_SUB: return "sub";
    case IR_OP_MUL: return "mul";
    case IR_OP_DIV: return "div";
    case IR_OP_MOD: return "mod";
    case IR_OP_NEG: return "neg";
    case IR_OP_CMP: return "cmp";
    case IR_OP_JUMP: return "jump";
    case IR_OP_BRANCH: return "branch";
    case IR_OP_CALL: return "call";
    case IR_OP_RETURN: return "return";
    default: return "nop";
    }
}

static const char *cmp_name(IRCmpOp cmp) {
    switch (cmp) {
    case IR_CMP_EQ: return "eq";
    case IR_CMP_NE: return "ne";
    case IR_CMP_LT: return "lt";
    case IR_CMP_GT: return "gt";
    case IR_CMP_LE: return "le";
    case IR_CMP_GE: return "ge";
    }
    return "?";
}

void ir_dump_program(const IRProgram *program, FILE *out) {
    int i, j, k;
    if (!program || !out)
        return;

    fprintf(out, "ir_program\n");
    for (i = 0; i < program->function_count; i++) {
        const IRFunction *func = &program->functions[i];
        fprintf(out, "function %s%s\n", func->is_extern ? "extern " : "", func->name ? func->name : "<anon>");
        if (func->param_count > 0) {
            fprintf(out, "  params");
            for (j = 0; j < func->param_count; j++)
                fprintf(out, " %s", func->params[j]);
            fprintf(out, "\n");
        }
        if (func->local_count > 0) {
            fprintf(out, "  locals\n");
            for (j = 0; j < func->local_count; j++)
                fprintf(out, "    %d: %s size=%d%s\n", j, func->locals[j].name, func->locals[j].size_bytes,
                        func->locals[j].is_string ? " [str]" : "");
        }
        for (j = 0; j < func->block_count; j++) {
            const IRBasicBlock *block = &func->blocks[j];
            fprintf(out, "  block %s\n", block->name ? block->name : "<bb>");
            for (k = 0; k < block->instruction_count; k++) {
                const IRInstruction *ins = &block->instructions[k];
                if (ins->has_result) {
                    dump_value(out, ins->result);
                    fprintf(out, " = ");
                } else {
                    fprintf(out, "    ");
                }
                fprintf(out, "%s", op_name(ins->op));
                switch (ins->op) {
                case IR_OP_CONST:
                    fprintf(out, " ");
                    dump_value(out, ins->as.constant.value);
                    break;
                case IR_OP_LOAD:
                    fprintf(out, " local%d", ins->as.load.local_index);
                    if (ins->as.load.byte_offset)
                        fprintf(out, "+%d", ins->as.load.byte_offset);
                    break;
                case IR_OP_STORE:
                    fprintf(out, " local%d", ins->as.store.local_index);
                    if (ins->as.store.byte_offset)
                        fprintf(out, "+%d", ins->as.store.byte_offset);
                    fprintf(out, ", ");
                    dump_value(out, ins->as.store.value);
                    break;
                case IR_OP_ADD:
                case IR_OP_SUB:
                case IR_OP_MUL:
                case IR_OP_DIV:
                case IR_OP_MOD:
                    fprintf(out, " ");
                    dump_value(out, ins->as.binary.lhs);
                    fprintf(out, ", ");
                    dump_value(out, ins->as.binary.rhs);
                    break;
                case IR_OP_NEG:
                    fprintf(out, " ");
                    dump_value(out, ins->as.unary.operand);
                    break;
                case IR_OP_CMP:
                    fprintf(out, " %s ", cmp_name(ins->as.compare.cmp));
                    dump_value(out, ins->as.compare.lhs);
                    fprintf(out, ", ");
                    dump_value(out, ins->as.compare.rhs);
                    break;
                case IR_OP_JUMP:
                    fprintf(out, " %s", func->blocks[ins->as.jump.target_block].name);
                    break;
                case IR_OP_BRANCH:
                    fprintf(out, " ");
                    dump_value(out, ins->as.branch.cond);
                    fprintf(out, ", %s, %s",
                            func->blocks[ins->as.branch.true_block].name,
                            func->blocks[ins->as.branch.false_block].name);
                    break;
                case IR_OP_CALL:
                    fprintf(out, " %s(", ins->as.call.callee);
                    for (k = 0; k < ins->as.call.arg_count; k++) {
                        if (k)
                            fprintf(out, ", ");
                        dump_value(out, ins->as.call.args[k]);
                    }
                    fprintf(out, ")");
                    break;
                case IR_OP_RETURN:
                    if (ins->as.ret.has_value) {
                        fprintf(out, " ");
                        dump_value(out, ins->as.ret.value);
                    }
                    break;
                default:
                    break;
                }
                fprintf(out, "\n");
            }
        }
    }
}
