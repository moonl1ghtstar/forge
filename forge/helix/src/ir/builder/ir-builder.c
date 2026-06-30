/*
 * ir-builder.c - Lower Forge AST into IRProgram.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir-builder.h"

#define MAX_LOOP_DEPTH 64

typedef struct {
    int continue_block;
    int break_block;
} LoopFrame;

typedef struct {
    char *name;
    char **fields;
    int field_count;
} StructInfo;

typedef struct {
    IRProgram *program;
    IRFunction *func;
    int current_block;
    int temp_count;
    int block_serial;
    LoopFrame loop_stack[MAX_LOOP_DEPTH];
    int loop_depth;
    StructInfo *structs;
    int struct_count;
    int struct_cap;
} IRBuilder;

static IRBasicBlock *current_block(IRBuilder *b) {
    if (!b->func || b->current_block < 0 || b->current_block >= b->func->block_count)
        return NULL;
    return &b->func->blocks[b->current_block];
}

static StructInfo *lookup_struct_info(IRBuilder *b, const char *name) {
    int i;
    for (i = 0; i < b->struct_count; i++) {
        if (strcmp(b->structs[i].name, name) == 0)
            return &b->structs[i];
    }
    return NULL;
}

static void register_struct_info(IRBuilder *b, ASTNode *node) {
    int i;
    StructInfo *info;
    if (!node || node->type != AST_STRUCT_DECL)
        return;
    if (lookup_struct_info(b, node->as.struct_decl.name))
        return;
    if (b->struct_count >= b->struct_cap) {
        b->struct_cap = b->struct_cap ? b->struct_cap * 2 : 8;
        b->structs = (StructInfo *)realloc(b->structs, sizeof(StructInfo) * b->struct_cap);
    }
    info = &b->structs[b->struct_count++];
    memset(info, 0, sizeof(*info));
    info->name = strdup(node->as.struct_decl.name);
    info->field_count = node->as.struct_decl.field_count;
    if (info->field_count > 0) {
        info->fields = (char **)calloc((size_t)info->field_count, sizeof(char *));
        for (i = 0; i < info->field_count; i++)
            info->fields[i] = strdup(node->as.struct_decl.fields[i]);
    }
}

static void register_structs_from_node(IRBuilder *b, ASTNode *node) {
    int i;
    if (!node)
        return;
    switch (node->type) {
    case AST_PROGRAM:
        for (i = 0; i < node->as.program.count; i++)
            register_structs_from_node(b, node->as.program.functions[i]);
        break;
    case AST_STRUCT_DECL:
        register_struct_info(b, node);
        break;
    case AST_FUNCTION:
        register_structs_from_node(b, node->as.function.body);
        break;
    case AST_BLOCK:
        for (i = 0; i < node->as.block.count; i++)
            register_structs_from_node(b, node->as.block.stmts[i]);
        break;
    case AST_IF:
        register_structs_from_node(b, node->as.if_stmt.then_block);
        register_structs_from_node(b, node->as.if_stmt.else_block);
        break;
    case AST_WHILE:
        register_structs_from_node(b, node->as.while_stmt.body);
        break;
    case AST_FOR:
        register_structs_from_node(b, node->as.for_stmt.init);
        register_structs_from_node(b, node->as.for_stmt.body);
        break;
    case AST_DO_WHILE:
        register_structs_from_node(b, node->as.while_stmt.body);
        break;
    default:
        break;
    }
}

static void free_struct_registry(IRBuilder *b) {
    int i, j;
    for (i = 0; i < b->struct_count; i++) {
        free(b->structs[i].name);
        for (j = 0; j < b->structs[i].field_count; j++)
            free(b->structs[i].fields[j]);
        free(b->structs[i].fields);
    }
    free(b->structs);
    b->structs = NULL;
    b->struct_count = 0;
    b->struct_cap = 0;
}

static int ensure_local(IRBuilder *b, const char *name, int size_bytes, int is_string) {
    int idx = ir_function_lookup_local(b->func, name);
    if (idx >= 0) {
        b->func->locals[idx].size_bytes = size_bytes > 0 ? size_bytes : 8;
        if (is_string >= 0)
            b->func->locals[idx].is_string = is_string;
        return idx;
    }
    return ir_function_add_local(b->func, name, size_bytes > 0 ? size_bytes : 8, is_string >= 0 ? is_string : 0);
}

static char *build_field_path(ASTNode *node) {
    if (!node)
        return NULL;
    switch (node->type) {
    case AST_VAR:
        return strdup(node->as.var.name);
    case AST_FIELD_ACCESS: {
        char *prefix = build_field_path(node->as.field_access.object);
        char *out;
        size_t len;
        if (!prefix)
            return NULL;
        len = strlen(prefix) + strlen(node->as.field_access.field_name) + 2;
        out = (char *)malloc(len);
        if (!out) {
            free(prefix);
            return NULL;
        }
        snprintf(out, len, "%s.%s", prefix, node->as.field_access.field_name);
        free(prefix);
        return out;
    }
    default:
        return NULL;
    }
}

static int new_temp(IRBuilder *b) {
    int id = b->temp_count++;
    if (b->func && b->func->temp_count < b->temp_count)
        b->func->temp_count = b->temp_count;
    return id;
}

static void ensure_block(IRBuilder *b) {
    if (!current_block(b)) {
        char name[64];
        snprintf(name, sizeof(name), "%s_dead_%d", b->func->name ? b->func->name : "fn", b->block_serial++);
        ir_function_add_block(b->func, name);
        b->current_block = b->func->block_count - 1;
    }
}

static int add_block(IRBuilder *b, const char *suffix) {
    char name[128];
    snprintf(name, sizeof(name), "%s_%s_%d", b->func->name ? b->func->name : "fn", suffix, b->block_serial++);
    ir_function_add_block(b->func, name);
    return b->func->block_count - 1;
}

static void terminate_jump(IRBuilder *b, int target_block) {
    IRInstruction ins;
    memset(&ins, 0, sizeof(ins));
    ins.op = IR_OP_JUMP;
    ins.has_result = 0;
    ins.as.jump.target_block = target_block;
    ir_block_add_instruction(current_block(b), ins);
    b->current_block = -1;
}

static void terminate_branch(IRBuilder *b, IRValue cond, int t, int f, int line) {
    IRInstruction ins;
    memset(&ins, 0, sizeof(ins));
    ins.op = IR_OP_BRANCH;
    ins.line = line;
    ins.as.branch.cond = cond;
    ins.as.branch.true_block = t;
    ins.as.branch.false_block = f;
    ir_block_add_instruction(current_block(b), ins);
    b->current_block = -1;
}

static void emit_return(IRBuilder *b, IRValue value, int has_value, int line) {
    IRInstruction ins;
    memset(&ins, 0, sizeof(ins));
    ins.op = IR_OP_RETURN;
    ins.line = line;
    ins.as.ret.value = value;
    ins.as.ret.has_value = has_value;
    ir_block_add_instruction(current_block(b), ins);
    b->current_block = -1;
}

static IRValue emit_const_string(IRBuilder *b, char *value, int line) {
    IRInstruction ins;
    IRValue out;
    memset(&ins, 0, sizeof(ins));
    ins.op = IR_OP_CONST;
    ins.line = line;
    ins.has_result = 1;
    ins.result = ir_value_temp(new_temp(b), 1);
    ins.as.constant.value = ir_value_const_string(value);
    ir_block_add_instruction(current_block(b), ins);
    out = ir_value_temp(ins.result.as.temp_id, 1);
    return out;
}

static IRValue emit_const_int(IRBuilder *b, int value, int line) {
    IRInstruction ins;
    IRValue out;
    memset(&ins, 0, sizeof(ins));
    ins.op = IR_OP_CONST;
    ins.line = line;
    ins.has_result = 1;
    ins.result = ir_value_temp(new_temp(b), 0);
    ins.as.constant.value = ir_value_const_int(value);
    ir_block_add_instruction(current_block(b), ins);
    out = ir_value_temp(ins.result.as.temp_id, 0);
    return out;
}

static IRValue lower_expr(IRBuilder *b, ASTNode *node);
static void lower_stmt(IRBuilder *b, ASTNode *node);
static void lower_block(IRBuilder *b, ASTNode *node);

static IRValue lookup_value(IRBuilder *b, const char *name, int line) {
    int idx = ensure_local(b, name, 8, -1);
    IRInstruction ins;
    memset(&ins, 0, sizeof(ins));
    ins.op = IR_OP_LOAD;
    ins.line = line;
    ins.has_result = 1;
    ins.result = ir_value_temp(new_temp(b), b->func->locals[idx].is_string);
    ins.as.load.local_index = idx;
    ins.as.load.byte_offset = 0;
    ir_block_add_instruction(current_block(b), ins);
    return ir_value_temp(ins.result.as.temp_id, ins.result.is_string);
}

static void update_local_type(IRBuilder *b, int local_index, int is_string) {
    if (local_index >= 0 && local_index < b->func->local_count)
        b->func->locals[local_index].is_string = is_string;
}

static IRValue lower_call(IRBuilder *b, ASTNode *node) {
    int i;
    const char *fname = node->as.call.name;
    IRInstruction ins;
    memset(&ins, 0, sizeof(ins));
    ins.op = IR_OP_CALL;
    ins.line = node->line;
    ins.as.call.callee = strdup(fname);
    ins.as.call.arg_count = node->as.call.arg_count;
    if (node->as.call.arg_count > 0) {
        ins.as.call.args = (IRValue *)calloc((size_t)node->as.call.arg_count, sizeof(IRValue));
        for (i = 0; i < node->as.call.arg_count; i++)
            ins.as.call.args[i] = lower_expr(b, node->as.call.args[i]);
    }

    if (strcmp(fname, "print") == 0 || strcmp(fname, "console_print") == 0 ||
        strcmp(fname, "clear") == 0 || strcmp(fname, "console_clear") == 0 ||
        strcmp(fname, "color") == 0 || strcmp(fname, "console_color") == 0) {
        ins.has_result = 0;
        ir_block_add_instruction(current_block(b), ins);
        return ir_value_const_int(0);
    }

    ins.has_result = 1;
    ins.result = ir_value_temp(new_temp(b),
                               strcmp(fname, "input") == 0 || strcmp(fname, "console_input") == 0);
    ir_block_add_instruction(current_block(b), ins);
    return ir_value_temp(ins.result.as.temp_id, ins.result.is_string);
}

static char *join_path(const char *lhs, const char *rhs) {
    size_t len = strlen(lhs) + strlen(rhs) + 2;
    char *out = (char *)malloc(len);
    if (!out)
        return NULL;
    snprintf(out, len, "%s.%s", lhs, rhs);
    return out;
}

static IRValue lower_field_access(IRBuilder *b, ASTNode *node) {
    char *path = build_field_path(node);
    IRValue out;
    if (!path) {
        fprintf(stderr, "Forge IR error: line %d: invalid field access.\n", node->line);
        return ir_value_const_int(0);
    }
    out = lookup_value(b, path, node->line);
    free(path);
    return out;
}

static void lower_struct_init(IRBuilder *b, ASTNode *node) {
    StructInfo *info = lookup_struct_info(b, node->as.struct_init.type_name);
    int i;
    if (!info) {
        fprintf(stderr, "Forge IR error: line %d: unknown struct '%s'\n", node->line, node->as.struct_init.type_name);
        return;
    }

    for (i = 0; i < info->field_count; i++) {
        ASTNode *value_node = NULL;
        const char *field_name = info->fields[i];
        IRValue value;
        char *path;
        int idx;

        if (node->as.struct_init.named) {
            int j;
            idx = -1;
            for (j = 0; j < node->as.struct_init.value_count; j++) {
                if (strcmp(node->as.struct_init.field_names[j], field_name) == 0) {
                    value_node = node->as.struct_init.values[j];
                    idx = i;
                    break;
                }
            }
        } else {
            if (i < node->as.struct_init.value_count)
                value_node = node->as.struct_init.values[i];
            idx = i;
        }

        if (idx < 0 || idx >= info->field_count) {
            fprintf(stderr, "Forge IR error: line %d: invalid field initializer.\n", node->line);
            continue;
        }
        if (!value_node)
            continue;

        value = lower_expr(b, value_node);
        path = join_path(node->as.struct_init.var_name, field_name);
        if (!path)
            continue;
        idx = ensure_local(b, path, 8, value.is_string);
        free(path);

        {
            IRInstruction ins;
            memset(&ins, 0, sizeof(ins));
            ins.op = IR_OP_STORE;
            ins.line = node->line;
            ins.as.store.local_index = idx;
            ins.as.store.byte_offset = 0;
            ins.as.store.value = value;
            ir_block_add_instruction(current_block(b), ins);
        }
    }
}

static IRValue lower_expr(IRBuilder *b, ASTNode *node) {
    int idx;
    IRValue left, right;
    IRInstruction ins;

    if (!node)
        return ir_value_const_int(0);

    ensure_block(b);

    switch (node->type) {
    case AST_NUMBER:
        return emit_const_int(b, node->as.number.value, node->line);
    case AST_STRING:
        return emit_const_string(b, strdup(node->as.string.value), node->line);
    case AST_VAR:
        return lookup_value(b, node->as.var.name, node->line);
    case AST_FIELD_ACCESS:
        return lower_field_access(b, node);
    case AST_STRUCT_INIT:
        lower_struct_init(b, node);
        return ir_value_const_int(0);
    case AST_BINARY:
        left = lower_expr(b, node->as.binary.left);
        right = lower_expr(b, node->as.binary.right);
        memset(&ins, 0, sizeof(ins));
        ins.line = node->line;
        ins.has_result = 1;
        switch (node->as.binary.op) {
        case BIN_ADD: ins.op = IR_OP_ADD; break;
        case BIN_SUB: ins.op = IR_OP_SUB; break;
        case BIN_MUL: ins.op = IR_OP_MUL; break;
        case BIN_DIV: ins.op = IR_OP_DIV; break;
        default:
            ins.op = IR_OP_CMP;
            ins.as.compare.cmp = IR_CMP_EQ;
            break;
        }
        if (node->as.binary.op == BIN_EQ || node->as.binary.op == BIN_NEQ ||
            node->as.binary.op == BIN_LT || node->as.binary.op == BIN_GT ||
            node->as.binary.op == BIN_LE || node->as.binary.op == BIN_GE) {
            ins.op = IR_OP_CMP;
            switch (node->as.binary.op) {
            case BIN_EQ: ins.as.compare.cmp = IR_CMP_EQ; break;
            case BIN_NEQ: ins.as.compare.cmp = IR_CMP_NE; break;
            case BIN_LT: ins.as.compare.cmp = IR_CMP_LT; break;
            case BIN_GT: ins.as.compare.cmp = IR_CMP_GT; break;
            case BIN_LE: ins.as.compare.cmp = IR_CMP_LE; break;
            case BIN_GE: ins.as.compare.cmp = IR_CMP_GE; break;
            default: break;
            }
            ins.as.compare.lhs = left;
            ins.as.compare.rhs = right;
            ins.result = ir_value_temp(new_temp(b), 0);
            ir_block_add_instruction(current_block(b), ins);
            return ir_value_temp(ins.result.as.temp_id, 0);
        }
        ins.as.binary.lhs = left;
        ins.as.binary.rhs = right;
        ins.result = ir_value_temp(new_temp(b), 0);
        ir_block_add_instruction(current_block(b), ins);
        return ir_value_temp(ins.result.as.temp_id, 0);
    case AST_UNARY:
        if (node->as.unary.op == UNARY_NEG) {
            memset(&ins, 0, sizeof(ins));
            ins.op = IR_OP_NEG;
            ins.line = node->line;
            ins.has_result = 1;
            ins.as.unary.operand = lower_expr(b, node->as.unary.operand);
            ins.result = ir_value_temp(new_temp(b), 0);
            ir_block_add_instruction(current_block(b), ins);
            return ir_value_temp(ins.result.as.temp_id, 0);
        }
        left = lower_expr(b, node->as.unary.operand);
        memset(&ins, 0, sizeof(ins));
        ins.op = IR_OP_CMP;
        ins.line = node->line;
        ins.has_result = 1;
        ins.as.compare.cmp = IR_CMP_EQ;
        ins.as.compare.lhs = left;
        ins.as.compare.rhs = ir_value_const_int(0);
        ins.result = ir_value_temp(new_temp(b), 0);
        ir_block_add_instruction(current_block(b), ins);
        return ir_value_temp(ins.result.as.temp_id, 0);
    case AST_CALL:
        return lower_call(b, node);
    case AST_ASSIGN:
        right = lower_expr(b, node->as.assign.value);
        idx = ir_function_lookup_local(b->func, node->as.assign.name);
        if (idx < 0) {
            fprintf(stderr, "Forge IR error: line %d: unknown variable '%s'\n", node->line, node->as.assign.name);
            return right;
        }
        update_local_type(b, idx, right.is_string);
        memset(&ins, 0, sizeof(ins));
        ins.op = IR_OP_STORE;
        ins.line = node->line;
        ins.has_result = 1;
        ins.result = ir_value_temp(new_temp(b), right.is_string);
        ins.as.store.local_index = idx;
        ins.as.store.byte_offset = 0;
        ins.as.store.value = right;
        ir_block_add_instruction(current_block(b), ins);
        return ir_value_temp(ins.result.as.temp_id, ins.result.is_string);
    default:
        return ir_value_const_int(0);
    }
}

static void lower_if(IRBuilder *b, ASTNode *node) {
    int then_block = add_block(b, "then");
    int else_block = node->as.if_stmt.else_block ? add_block(b, "else") : -1;
    int merge_block = add_block(b, "merge");
    IRValue cond = lower_expr(b, node->as.if_stmt.cond);

    terminate_branch(b, cond, then_block, node->as.if_stmt.else_block ? else_block : merge_block, node->line);

    b->current_block = then_block;
    lower_block(b, node->as.if_stmt.then_block);
    if (current_block(b))
        terminate_jump(b, merge_block);

    if (node->as.if_stmt.else_block) {
        b->current_block = else_block;
        lower_block(b, node->as.if_stmt.else_block);
        if (current_block(b))
            terminate_jump(b, merge_block);
    }

    b->current_block = merge_block;
}

static void lower_while(IRBuilder *b, ASTNode *node) {
    int cond_block = add_block(b, "while_cond");
    int body_block = add_block(b, "while_body");
    int exit_block = add_block(b, "while_end");

    terminate_jump(b, cond_block);

    b->current_block = cond_block;
    {
        IRValue cond = lower_expr(b, node->as.while_stmt.cond);
        terminate_branch(b, cond, body_block, exit_block, node->line);
    }

    if (b->loop_depth < MAX_LOOP_DEPTH) {
        b->loop_stack[b->loop_depth].continue_block = cond_block;
        b->loop_stack[b->loop_depth].break_block = exit_block;
        b->loop_depth++;
    }

    b->current_block = body_block;
    lower_block(b, node->as.while_stmt.body);
    if (current_block(b))
        terminate_jump(b, cond_block);

    if (b->loop_depth > 0)
        b->loop_depth--;
    b->current_block = exit_block;
}

static void lower_for(IRBuilder *b, ASTNode *node) {
    int cond_block = add_block(b, "for_cond");
    int body_block = add_block(b, "for_body");
    int step_block = add_block(b, "for_step");
    int exit_block = add_block(b, "for_end");

    if (node->as.for_stmt.init)
        lower_stmt(b, node->as.for_stmt.init);
    terminate_jump(b, cond_block);

    if (b->loop_depth < MAX_LOOP_DEPTH) {
        b->loop_stack[b->loop_depth].continue_block = step_block;
        b->loop_stack[b->loop_depth].break_block = exit_block;
        b->loop_depth++;
    }

    b->current_block = cond_block;
    {
        IRValue cond = node->as.for_stmt.cond ? lower_expr(b, node->as.for_stmt.cond) : ir_value_const_int(1);
        terminate_branch(b, cond, body_block, exit_block, node->line);
    }

    b->current_block = body_block;
    lower_block(b, node->as.for_stmt.body);
    if (current_block(b))
        terminate_jump(b, step_block);

    b->current_block = step_block;
    if (node->as.for_stmt.incr)
        lower_stmt(b, node->as.for_stmt.incr);
    if (current_block(b))
        terminate_jump(b, cond_block);

    if (b->loop_depth > 0)
        b->loop_depth--;
    b->current_block = exit_block;
}

static void lower_do_while(IRBuilder *b, ASTNode *node) {
    int body_block = add_block(b, "do_body");
    int cond_block = add_block(b, "do_cond");
    int exit_block = add_block(b, "do_end");

    terminate_jump(b, body_block);

    if (b->loop_depth < MAX_LOOP_DEPTH) {
        b->loop_stack[b->loop_depth].continue_block = cond_block;
        b->loop_stack[b->loop_depth].break_block = exit_block;
        b->loop_depth++;
    }

    b->current_block = body_block;
    lower_block(b, node->as.while_stmt.body);
    if (current_block(b))
        terminate_jump(b, cond_block);

    b->current_block = cond_block;
    {
        IRValue cond = lower_expr(b, node->as.while_stmt.cond);
        terminate_branch(b, cond, body_block, exit_block, node->line);
    }

    if (b->loop_depth > 0)
        b->loop_depth--;
    b->current_block = exit_block;
}

static void lower_stmt(IRBuilder *b, ASTNode *node) {
    int idx;
    IRValue value;
    IRInstruction ins;

    if (!node)
        return;
    ensure_block(b);

    switch (node->type) {
    case AST_STRUCT_DECL:
        break;
    case AST_STRUCT_INIT:
        lower_struct_init(b, node);
        break;
    case AST_VAR_DECL:
        idx = ensure_local(b, node->as.var_decl.name, 8, -1);
        value = lower_expr(b, node->as.var_decl.init);
        update_local_type(b, idx, value.is_string);
        memset(&ins, 0, sizeof(ins));
        ins.op = IR_OP_STORE;
        ins.line = node->line;
        ins.as.store.local_index = idx;
        ins.as.store.byte_offset = 0;
        ins.as.store.value = value;
        ir_block_add_instruction(current_block(b), ins);
        break;
    case AST_ASSIGN:
        value = lower_expr(b, node->as.assign.value);
        idx = ir_function_lookup_local(b->func, node->as.assign.name);
        if (idx < 0) {
            fprintf(stderr, "Forge IR error: line %d: unknown variable '%s'\n", node->line, node->as.assign.name);
            break;
        }
        update_local_type(b, idx, value.is_string);
        memset(&ins, 0, sizeof(ins));
        ins.op = IR_OP_STORE;
        ins.line = node->line;
        ins.has_result = 1;
        ins.result = ir_value_temp(new_temp(b), value.is_string);
        ins.as.store.local_index = idx;
        ins.as.store.byte_offset = 0;
        ins.as.store.value = value;
        ir_block_add_instruction(current_block(b), ins);
        break;
    case AST_EXPR_STMT:
        lower_expr(b, node->as.expr_stmt.expr);
        break;
    case AST_RETURN:
        value = lower_expr(b, node->as.return_stmt.expr);
        emit_return(b, value, 1, node->line);
        break;
    case AST_IF:
        lower_if(b, node);
        break;
    case AST_WHILE:
        lower_while(b, node);
        break;
    case AST_FOR:
        lower_for(b, node);
        break;
    case AST_DO_WHILE:
        lower_do_while(b, node);
        break;
    case AST_BREAK:
        if (b->loop_depth > 0)
            terminate_jump(b, b->loop_stack[b->loop_depth - 1].break_block);
        else
            fprintf(stderr, "Forge IR error: line %d: break() outside loop\n", node->line);
        break;
    case AST_PASS:
        if (b->loop_depth > 0)
            terminate_jump(b, b->loop_stack[b->loop_depth - 1].continue_block);
        else
            fprintf(stderr, "Forge IR error: line %d: pass() outside loop\n", node->line);
        break;
    case AST_BLOCK:
        lower_block(b, node);
        break;
    default:
        break;
    }
}

static void lower_block(IRBuilder *b, ASTNode *node) {
    int i;
    if (!node || node->type != AST_BLOCK)
        return;
    for (i = 0; i < node->as.block.count; i++) {
        if (!current_block(b))
            ensure_block(b);
        lower_stmt(b, node->as.block.stmts[i]);
    }
}

static void lower_function(IRBuilder *b, ASTNode *node) {
    int i;
    char block_name[128];
    IRFunction *func;

    b->func = ir_function_new(node->as.function.name, 0);
    for (i = 0; i < node->as.function.param_count; i++)
        ir_function_add_local(b->func, node->as.function.params[i], 8, 0);
    for (i = 0; i < node->as.function.param_count; i++) {
        b->func->params = (char **)realloc(b->func->params, sizeof(char *) * (b->func->param_count + 1));
        b->func->params[b->func->param_count++] = strdup(node->as.function.params[i]);
    }
    snprintf(block_name, sizeof(block_name), "%s_entry", node->as.function.name);
    ir_function_add_block(b->func, block_name);
    b->current_block = 0;
    b->temp_count = 0;
    b->block_serial = 0;
    b->loop_depth = 0;
    lower_block(b, node->as.function.body);
    if (b->current_block >= 0 && current_block(b)) {
        IRValue zero = ir_value_const_int(0);
        emit_return(b, zero, 1, node->line);
    }
    func = b->func;
    b->func = NULL;
    ir_program_add_function(b->program, func);
}

IRProgram *ir_build_program(ASTNode *program) {
    int i;
    IRBuilder b;

    if (!program || program->type != AST_PROGRAM)
        return NULL;

    memset(&b, 0, sizeof(b));
    b.program = ir_program_new();
    b.current_block = -1;
    register_structs_from_node(&b, program);
    for (i = 0; i < program->as.program.count; i++) {
        ASTNode *node = program->as.program.functions[i];
        if (node->type == AST_FUNCTION) {
            lower_function(&b, node);
        } else if (node->type == AST_EXTERN_FUNC) {
            IRFunction *ext = ir_function_new(node->as.extern_func.name, 1);
            int j;
            for (j = 0; j < node->as.extern_func.param_count; j++) {
                ext->params = (char **)realloc(ext->params, sizeof(char *) * (ext->param_count + 1));
                ext->params[ext->param_count++] = strdup(node->as.extern_func.param_types[j]);
            }
            ir_program_add_function(b.program, ext);
        }
    }
    free_struct_registry(&b);
    return b.program;
}
