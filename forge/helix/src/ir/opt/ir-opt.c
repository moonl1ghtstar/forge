/*
 * ir-opt.c - Minimal optimization passes for Forge IR.
 */

#include <stdlib.h>
#include <string.h>
#include "ir-opt.h"

static int value_is_const_int(IRValue value, int *out) {
    if (value.kind != IR_VALUE_CONST_INT)
        return 0;
    if (out)
        *out = value.as.int_value;
    return 1;
}

static int value_resolve_const_int(const IRInstruction *const *defs, int temp_count, IRValue value, int *out) {
    if (value.kind == IR_VALUE_CONST_INT)
        return value_is_const_int(value, out);
    if (value.kind == IR_VALUE_TEMP && value.as.temp_id >= 0 && value.as.temp_id < temp_count) {
        const IRInstruction *def = defs[value.as.temp_id];
        if (def && def->op == IR_OP_CONST)
            return value_resolve_const_int(defs, temp_count, def->as.constant.value, out);
    }
    return 0;
}

static int pure_op(IROp op) {
    switch (op) {
    case IR_OP_CONST:
    case IR_OP_LOAD:
    case IR_OP_ADD:
    case IR_OP_SUB:
    case IR_OP_MUL:
    case IR_OP_DIV:
    case IR_OP_MOD:
    case IR_OP_NEG:
    case IR_OP_CMP:
        return 1;
    default:
        return 0;
    }
}

static void fold_instruction(IRInstruction *ins, const IRInstruction *const *defs, int temp_count) {
    int lhs, rhs, result;
    if (!ins)
        return;
    switch (ins->op) {
    case IR_OP_ADD:
        if (value_resolve_const_int(defs, temp_count, ins->as.binary.lhs, &lhs) &&
            value_resolve_const_int(defs, temp_count, ins->as.binary.rhs, &rhs)) {
            ins->op = IR_OP_CONST;
            ins->as.constant.value = ir_value_const_int(lhs + rhs);
        }
        break;
    case IR_OP_SUB:
        if (value_resolve_const_int(defs, temp_count, ins->as.binary.lhs, &lhs) &&
            value_resolve_const_int(defs, temp_count, ins->as.binary.rhs, &rhs)) {
            ins->op = IR_OP_CONST;
            ins->as.constant.value = ir_value_const_int(lhs - rhs);
        }
        break;
    case IR_OP_MUL:
        if (value_resolve_const_int(defs, temp_count, ins->as.binary.lhs, &lhs) &&
            value_resolve_const_int(defs, temp_count, ins->as.binary.rhs, &rhs)) {
            ins->op = IR_OP_CONST;
            ins->as.constant.value = ir_value_const_int(lhs * rhs);
        }
        break;
    case IR_OP_DIV:
        if (value_resolve_const_int(defs, temp_count, ins->as.binary.lhs, &lhs) &&
            value_resolve_const_int(defs, temp_count, ins->as.binary.rhs, &rhs) && rhs != 0) {
            ins->op = IR_OP_CONST;
            ins->as.constant.value = ir_value_const_int(lhs / rhs);
        }
        break;
    case IR_OP_MOD:
        if (value_resolve_const_int(defs, temp_count, ins->as.binary.lhs, &lhs) &&
            value_resolve_const_int(defs, temp_count, ins->as.binary.rhs, &rhs) && rhs != 0) {
            ins->op = IR_OP_CONST;
            ins->as.constant.value = ir_value_const_int(lhs % rhs);
        }
        break;
    case IR_OP_NEG:
        if (value_resolve_const_int(defs, temp_count, ins->as.unary.operand, &lhs)) {
            ins->op = IR_OP_CONST;
            ins->as.constant.value = ir_value_const_int(-lhs);
        }
        break;
    case IR_OP_CMP:
        if (value_resolve_const_int(defs, temp_count, ins->as.compare.lhs, &lhs) &&
            value_resolve_const_int(defs, temp_count, ins->as.compare.rhs, &rhs)) {
            switch (ins->as.compare.cmp) {
            case IR_CMP_EQ: result = (lhs == rhs); break;
            case IR_CMP_NE: result = (lhs != rhs); break;
            case IR_CMP_LT: result = (lhs < rhs); break;
            case IR_CMP_GT: result = (lhs > rhs); break;
            case IR_CMP_LE: result = (lhs <= rhs); break;
            case IR_CMP_GE: result = (lhs >= rhs); break;
            default: result = 0; break;
            }
            ins->op = IR_OP_CONST;
            ins->as.constant.value = ir_value_const_int(result);
        }
        break;
    case IR_OP_BRANCH:
        if (value_resolve_const_int(defs, temp_count, ins->as.branch.cond, &lhs)) {
            ins->op = IR_OP_JUMP;
            ins->as.jump.target_block = lhs ? ins->as.branch.true_block : ins->as.branch.false_block;
        }
        break;
    default:
        break;
    }
}

static void optimize_function(IRFunction *func) {
    int i, j, k;
    int changed;
    if (!func || func->is_extern)
        return;

    do {
        changed = 0;

        {
            int *uses = NULL;
            const IRInstruction **defs = NULL;
            if (func->temp_count > 0) {
                uses = (int *)calloc((size_t)func->temp_count, sizeof(int));
                defs = (const IRInstruction **)calloc((size_t)func->temp_count, sizeof(IRInstruction *));
            }

            for (i = 0; i < func->block_count; i++) {
                IRBasicBlock *block = &func->blocks[i];
                for (j = 0; j < block->instruction_count; j++) {
                    IRInstruction *ins = &block->instructions[j];
                    if (ins->has_result && ins->result.kind == IR_VALUE_TEMP &&
                        ins->result.as.temp_id >= 0 && ins->result.as.temp_id < func->temp_count)
                        defs[ins->result.as.temp_id] = ins;
                }
            }

            for (i = 0; i < func->block_count; i++) {
                IRBasicBlock *block = &func->blocks[i];
                for (j = 0; j < block->instruction_count; j++)
                    fold_instruction(&block->instructions[j], defs, func->temp_count);
            }

            for (i = 0; i < func->block_count; i++) {
                IRBasicBlock *block = &func->blocks[i];
                for (j = 0; j < block->instruction_count; j++) {
                    IRInstruction *ins = &block->instructions[j];
                    switch (ins->op) {
                    case IR_OP_ADD:
                    case IR_OP_SUB:
                    case IR_OP_MUL:
                    case IR_OP_DIV:
                    case IR_OP_MOD:
                        if (ins->as.binary.lhs.kind == IR_VALUE_TEMP && uses && ins->as.binary.lhs.as.temp_id < func->temp_count)
                            uses[ins->as.binary.lhs.as.temp_id]++;
                        if (ins->as.binary.rhs.kind == IR_VALUE_TEMP && uses && ins->as.binary.rhs.as.temp_id < func->temp_count)
                            uses[ins->as.binary.rhs.as.temp_id]++;
                        break;
                    case IR_OP_NEG:
                        if (ins->as.unary.operand.kind == IR_VALUE_TEMP && uses && ins->as.unary.operand.as.temp_id < func->temp_count)
                            uses[ins->as.unary.operand.as.temp_id]++;
                        break;
                    case IR_OP_CMP:
                        if (ins->as.compare.lhs.kind == IR_VALUE_TEMP && uses && ins->as.compare.lhs.as.temp_id < func->temp_count)
                            uses[ins->as.compare.lhs.as.temp_id]++;
                        if (ins->as.compare.rhs.kind == IR_VALUE_TEMP && uses && ins->as.compare.rhs.as.temp_id < func->temp_count)
                            uses[ins->as.compare.rhs.as.temp_id]++;
                        break;
                    case IR_OP_STORE:
                        if (ins->as.store.value.kind == IR_VALUE_TEMP && uses && ins->as.store.value.as.temp_id < func->temp_count)
                            uses[ins->as.store.value.as.temp_id]++;
                        break;
                    case IR_OP_BRANCH:
                        if (ins->as.branch.cond.kind == IR_VALUE_TEMP && uses && ins->as.branch.cond.as.temp_id < func->temp_count)
                            uses[ins->as.branch.cond.as.temp_id]++;
                        break;
                    case IR_OP_RETURN:
                        if (ins->as.ret.has_value && ins->as.ret.value.kind == IR_VALUE_TEMP && uses && ins->as.ret.value.as.temp_id < func->temp_count)
                            uses[ins->as.ret.value.as.temp_id]++;
                        break;
                    case IR_OP_CALL:
                        for (k = 0; k < ins->as.call.arg_count; k++) {
                            if (ins->as.call.args[k].kind == IR_VALUE_TEMP && uses && ins->as.call.args[k].as.temp_id < func->temp_count)
                                uses[ins->as.call.args[k].as.temp_id]++;
                        }
                        break;
                    default:
                        break;
                    }
                }
            }

            for (i = 0; i < func->block_count; i++) {
                IRBasicBlock *block = &func->blocks[i];
                int write = 0;
                for (j = 0; j < block->instruction_count; j++) {
                    IRInstruction *ins = &block->instructions[j];
                    int remove = 0;
                    if (ins->has_result && ins->result.kind == IR_VALUE_TEMP &&
                        ins->result.as.temp_id < func->temp_count &&
                        uses && uses[ins->result.as.temp_id] == 0 &&
                        pure_op(ins->op)) {
                        remove = 1;
                    }
                    if (!remove) {
                        if (write != j)
                            block->instructions[write] = block->instructions[j];
                        write++;
                    } else {
                        int old = block->instruction_count;
                        (void)old;
                        changed = 1;
                    }
                }
                block->instruction_count = write;
            }

            free(uses);
            free(defs);
        }
    } while (changed);
}

void ir_optimize(IRProgram *program) {
    int i;
    if (!program)
        return;
    for (i = 0; i < program->function_count; i++)
        optimize_function(&program->functions[i]);
}
