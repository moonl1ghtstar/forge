/*
 * helix-codegen.c - x86-64 assembly code generator for Forge IR.
 *
 * Emits Windows x64 ABI-compliant NASM text from IRProgram.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "helix-codegen.h"

#define SHADOW_SIZE 32

typedef struct {
    char *label;
    char *content;
} StringEntry;

typedef struct {
    FILE *out;
    int needs_printf;
    int needs_scanf;
    int label_count;
} CodegenCtx;

static StringEntry *string_table = NULL;
static int string_count = 0;
static int string_cap = 0;

static void emit(CodegenCtx *ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->out, fmt, args);
    va_end(args);
    fprintf(ctx->out, "\n");
}

static const char *string_table_add(const char *content) {
    int i;
    if (!content)
        content = "";
    for (i = 0; i < string_count; i++) {
        if (strcmp(string_table[i].content, content) == 0)
            return string_table[i].label;
    }
    if (string_count >= string_cap) {
        string_cap = string_cap ? string_cap * 2 : 16;
        string_table = (StringEntry *)realloc(string_table, sizeof(StringEntry) * string_cap);
    }
    string_table[string_count].content = strdup(content);
    string_table[string_count].label = (char *)malloc(32);
    snprintf(string_table[string_count].label, 32, "str_%d", string_count);
    string_count++;
    return string_table[string_count - 1].label;
}

static void string_table_reset(void) {
    int i;
    for (i = 0; i < string_count; i++) {
        free(string_table[i].label);
        free(string_table[i].content);
    }
    free(string_table);
    string_table = NULL;
    string_count = 0;
    string_cap = 0;
}

static int align16(int n) {
    return (n + 15) & ~15;
}

static int temp_offset(int temp_id) {
    return (temp_id + 1) * 8;
}

static int local_base_offset(const IRFunction *func, int local_index) {
    int i;
    int temp_area = func->temp_count * 8;
    int offset = temp_area + 16;
    for (i = 0; i < local_index; i++)
        offset += func->locals[i].size_bytes > 0 ? func->locals[i].size_bytes : 8;
    return offset;
}

static int local_offset(const IRFunction *func, int local_index, int byte_offset) {
    return local_base_offset(func, local_index) + byte_offset;
}

static void emit_value_to_reg(CodegenCtx *ctx, const IRFunction *func, IRValue value, const char *reg) {
    int off;
    switch (value.kind) {
    case IR_VALUE_CONST_INT:
        emit(ctx, "    mov %s, %d", reg, value.as.int_value);
        break;
    case IR_VALUE_CONST_STRING:
        emit(ctx, "    lea %s, [rel %s]", reg, string_table_add(value.as.string_value));
        break;
    case IR_VALUE_TEMP:
        off = temp_offset(value.as.temp_id);
        emit(ctx, "    mov %s, [rbp-%d]", reg, off);
        break;
    case IR_VALUE_LOCAL:
        off = local_offset(func, value.as.local_index, 0);
        emit(ctx, "    mov %s, [rbp-%d]", reg, off);
        break;
    default:
        emit(ctx, "    xor %s, %s", reg, reg);
        break;
    }
}

static void emit_store_result(CodegenCtx *ctx, const IRValue result) {
    if (result.kind == IR_VALUE_TEMP)
        emit(ctx, "    mov [rbp-%d], rax", temp_offset(result.as.temp_id));
}

static void scan_value(IRValue value) {
    if (value.kind == IR_VALUE_CONST_STRING)
        string_table_add(value.as.string_value);
}

static void scan_function(const IRFunction *func, CodegenCtx *ctx) {
    int i, j;
    if (!func || func->is_extern)
        return;
    for (i = 0; i < func->block_count; i++) {
        const IRBasicBlock *block = &func->blocks[i];
        for (j = 0; j < block->instruction_count; j++) {
            const IRInstruction *ins = &block->instructions[j];
            switch (ins->op) {
            case IR_OP_CONST:
                scan_value(ins->as.constant.value);
                break;
            case IR_OP_LOAD:
                break;
            case IR_OP_STORE:
                scan_value(ins->as.store.value);
                break;
            case IR_OP_ADD:
            case IR_OP_SUB:
            case IR_OP_MUL:
            case IR_OP_DIV:
            case IR_OP_MOD:
                scan_value(ins->as.binary.lhs);
                scan_value(ins->as.binary.rhs);
                break;
            case IR_OP_NEG:
                scan_value(ins->as.unary.operand);
                break;
            case IR_OP_CMP:
                scan_value(ins->as.compare.lhs);
                scan_value(ins->as.compare.rhs);
                break;
            case IR_OP_JUMP:
                break;
            case IR_OP_BRANCH:
                scan_value(ins->as.branch.cond);
                break;
            case IR_OP_CALL:
                if (strcmp(ins->as.call.callee, "print") == 0 ||
                    strcmp(ins->as.call.callee, "console_print") == 0 ||
                    strcmp(ins->as.call.callee, "clear") == 0 ||
                    strcmp(ins->as.call.callee, "console_clear") == 0 ||
                    strcmp(ins->as.call.callee, "color") == 0 ||
                    strcmp(ins->as.call.callee, "console_color") == 0)
                    ctx->needs_printf = 1;
                if (strcmp(ins->as.call.callee, "input") == 0 ||
                    strcmp(ins->as.call.callee, "console_input") == 0)
                    ctx->needs_scanf = 1;
                for (int k = 0; k < ins->as.call.arg_count; k++)
                    scan_value(ins->as.call.args[k]);
                break;
            case IR_OP_RETURN:
                if (ins->as.ret.has_value)
                    scan_value(ins->as.ret.value);
                break;
            default:
                break;
            }
        }
    }
}

static void scan_program(const IRProgram *program, CodegenCtx *ctx) {
    int i;
    for (i = 0; i < program->function_count; i++)
        scan_function(&program->functions[i], ctx);
}

static void emit_cmp_setcc(CodegenCtx *ctx, const char *cond) {
    emit(ctx, "    cmp rax, rbx");
    emit(ctx, "    %s al", cond);
    emit(ctx, "    movzx eax, al");
}

static void emit_binary(CodegenCtx *ctx, const IRFunction *func, const IRInstruction *ins) {
    emit_value_to_reg(ctx, func, ins->as.binary.lhs, "rax");
    emit_value_to_reg(ctx, func, ins->as.binary.rhs, "rbx");
    switch (ins->op) {
    case IR_OP_ADD:
        emit(ctx, "    add rax, rbx");
        break;
    case IR_OP_SUB:
        emit(ctx, "    sub rax, rbx");
        break;
    case IR_OP_MUL:
        emit(ctx, "    imul rax, rbx");
        break;
    case IR_OP_DIV:
        emit(ctx, "    cqo");
        emit(ctx, "    idiv rbx");
        break;
    case IR_OP_MOD:
        emit(ctx, "    cqo");
        emit(ctx, "    idiv rbx");
        emit(ctx, "    mov rax, rdx");
        break;
    default:
        break;
    }
}

static void emit_compare(CodegenCtx *ctx, const IRFunction *func, const IRInstruction *ins) {
    emit_value_to_reg(ctx, func, ins->as.compare.lhs, "rax");
    emit_value_to_reg(ctx, func, ins->as.compare.rhs, "rbx");
    switch (ins->as.compare.cmp) {
    case IR_CMP_EQ: emit_cmp_setcc(ctx, "sete"); break;
    case IR_CMP_NE: emit_cmp_setcc(ctx, "setne"); break;
    case IR_CMP_LT: emit_cmp_setcc(ctx, "setl"); break;
    case IR_CMP_GT: emit_cmp_setcc(ctx, "setg"); break;
    case IR_CMP_LE: emit_cmp_setcc(ctx, "setle"); break;
    case IR_CMP_GE: emit_cmp_setcc(ctx, "setge"); break;
    }
}

static void emit_call_builtin(CodegenCtx *ctx, const IRFunction *func, const IRInstruction *ins) {
    const char *fname = ins->as.call.callee;
    if ((strcmp(fname, "print") == 0 || strcmp(fname, "console_print") == 0) && ins->as.call.arg_count > 0) {
        IRValue arg = ins->as.call.args[0];
        emit_value_to_reg(ctx, func, arg, "rax");
        if (arg.is_string) {
            emit(ctx, "    lea rcx, [rel fmt_str_out]");
            emit(ctx, "    mov rdx, rax");
        } else {
            emit(ctx, "    lea rcx, [rel fmt_int_out]");
            emit(ctx, "    mov rdx, rax");
        }
        emit(ctx, "    xor eax, eax");
        emit(ctx, "    call printf");
        return;
    }

    if (strcmp(fname, "clear") == 0 || strcmp(fname, "console_clear") == 0) {
        emit(ctx, "    lea rcx, [rel fmt_clear]");
        emit(ctx, "    xor eax, eax");
        emit(ctx, "    call printf");
        return;
    }

    if (strcmp(fname, "color") == 0 || strcmp(fname, "console_color") == 0) {
        if (ins->as.call.arg_count > 0 && ins->as.call.args[0].kind == IR_VALUE_CONST_INT) {
            int value = ins->as.call.args[0].as.int_value;
            int r = (value >> 16) & 0xFF;
            int g = (value >> 8) & 0xFF;
            int b = value & 0xFF;
            emit(ctx, "    lea rcx, [rel fmt_color]");
            emit(ctx, "    mov edx, %d", r);
            emit(ctx, "    mov r8d, %d", g);
            emit(ctx, "    mov r9d, %d", b);
            emit(ctx, "    xor eax, eax");
            emit(ctx, "    call printf");
            return;
        }
        emit(ctx, "    lea rcx, [rel fmt_clear]");
        emit(ctx, "    xor eax, eax");
        emit(ctx, "    call printf");
        return;
    }

    if (strcmp(fname, "input") == 0 || strcmp(fname, "console_input") == 0) {
        emit(ctx, "    lea rcx, [rel fmt_str_in]");
        emit(ctx, "    lea rdx, [rel input_buf]");
        emit(ctx, "    xor eax, eax");
        emit(ctx, "    call scanf");
        emit(ctx, "    lea rax, [rel input_buf]");
        return;
    }
}

static void emit_call_generic(CodegenCtx *ctx, const IRFunction *func, const IRInstruction *ins) {
    static const char *regs[] = {"rcx", "rdx", "r8", "r9"};
    int i;
    for (i = 0; i < ins->as.call.arg_count && i < 4; i++)
        emit_value_to_reg(ctx, func, ins->as.call.args[i], regs[i]);
    emit(ctx, "    xor eax, eax");
    emit(ctx, "    call %s", ins->as.call.callee);
    if (ins->has_result && ins->result.kind == IR_VALUE_TEMP)
        emit_store_result(ctx, ins->result);
}

static void emit_instruction(CodegenCtx *ctx, const IRFunction *func, const IRInstruction *ins, int exit_label) {
    switch (ins->op) {
    case IR_OP_CONST:
        emit_value_to_reg(ctx, func, ins->as.constant.value, "rax");
        emit_store_result(ctx, ins->result);
        break;
    case IR_OP_LOAD:
        emit_value_to_reg(ctx, func, ir_value_local(ins->as.load.local_index, func->locals[ins->as.load.local_index].is_string), "rax");
        emit_store_result(ctx, ins->result);
        break;
    case IR_OP_STORE:
        emit_value_to_reg(ctx, func, ins->as.store.value, "rax");
        emit(ctx, "    mov [rbp-%d], rax", local_offset(func, ins->as.store.local_index, ins->as.store.byte_offset));
        if (ins->has_result)
            emit_store_result(ctx, ins->result);
        break;
    case IR_OP_ADD:
    case IR_OP_SUB:
    case IR_OP_MUL:
    case IR_OP_DIV:
    case IR_OP_MOD:
        emit_binary(ctx, func, ins);
        emit_store_result(ctx, ins->result);
        break;
    case IR_OP_NEG:
        emit_value_to_reg(ctx, func, ins->as.unary.operand, "rax");
        emit(ctx, "    neg rax");
        emit_store_result(ctx, ins->result);
        break;
    case IR_OP_CMP:
        emit_compare(ctx, func, ins);
        emit_store_result(ctx, ins->result);
        break;
    case IR_OP_JUMP:
        emit(ctx, "    jmp %s", func->blocks[ins->as.jump.target_block].name);
        break;
    case IR_OP_BRANCH:
        emit_value_to_reg(ctx, func, ins->as.branch.cond, "rax");
        emit(ctx, "    cmp rax, 0");
        emit(ctx, "    jne %s", func->blocks[ins->as.branch.true_block].name);
        emit(ctx, "    jmp %s", func->blocks[ins->as.branch.false_block].name);
        break;
    case IR_OP_CALL:
        if (strcmp(ins->as.call.callee, "print") == 0 ||
            strcmp(ins->as.call.callee, "console_print") == 0 ||
            strcmp(ins->as.call.callee, "clear") == 0 ||
            strcmp(ins->as.call.callee, "console_clear") == 0 ||
            strcmp(ins->as.call.callee, "color") == 0 ||
            strcmp(ins->as.call.callee, "console_color") == 0 ||
            strcmp(ins->as.call.callee, "input") == 0 ||
            strcmp(ins->as.call.callee, "console_input") == 0) {
            emit_call_builtin(ctx, func, ins);
            if (ins->has_result)
                emit_store_result(ctx, ins->result);
        } else {
            emit_call_generic(ctx, func, ins);
        }
        break;
    case IR_OP_RETURN:
        if (ins->as.ret.has_value)
            emit_value_to_reg(ctx, func, ins->as.ret.value, "rax");
        emit(ctx, "    jmp .Lexit%d", exit_label);
        break;
    case IR_OP_NOP:
    default:
        break;
    }
}

static void emit_function(CodegenCtx *ctx, const IRFunction *func) {
    int i, j;
    int frame_size;
    int temp_area = func->temp_count * 8;
    int local_area = 0;
    for (i = 0; i < func->local_count; i++)
        local_area += func->locals[i].size_bytes > 0 ? func->locals[i].size_bytes : 8;
    int total = SHADOW_SIZE + 16 + temp_area + local_area;
    int exit_label = ctx->label_count++;

    frame_size = align16(total);
    if (frame_size < SHADOW_SIZE + 16)
        frame_size = SHADOW_SIZE + 16;

    emit(ctx, "");
    emit(ctx, "global %s", func->name);
    emit(ctx, "%s:", func->name);
    emit(ctx, "    push rbp");
    emit(ctx, "    mov rbp, rsp");
    emit(ctx, "    sub rsp, %d", frame_size);

    for (i = 0; i < func->param_count && i < 4; i++) {
        static const char *regs[] = {"rcx", "rdx", "r8", "r9"};
        emit(ctx, "    mov [rbp-%d], %s", local_offset(func, i, 0), regs[i]);
    }

    for (i = 0; i < func->block_count; i++) {
        const IRBasicBlock *block = &func->blocks[i];
        emit(ctx, "%s:", block->name);
        for (j = 0; j < block->instruction_count; j++)
            emit_instruction(ctx, func, &block->instructions[j], exit_label);
    }

    emit(ctx, ".Lexit%d:", exit_label);
    emit(ctx, "    mov rsp, rbp");
    emit(ctx, "    pop rbp");
    emit(ctx, "    ret");
}

int codegen_emit(IRProgram *program, FILE *out) {
    int i;
    CodegenCtx ctx;

    if (!program || !out)
        return 1;

    ctx.out = out;
    ctx.needs_printf = 0;
    ctx.needs_scanf = 0;
    ctx.label_count = 0;

    string_table_reset();
    scan_program(program, &ctx);

    fprintf(out, "; Forge-generated x86-64 assembly (Windows x64 ABI)\n");
    fprintf(out, "; Assemble with: nasm -f win64 <file>.asm\n");
    fprintf(out, "; Link with:     ld <file>.o -o <file>.exe\n\n");

    fprintf(out, "section .data\n");
    if (ctx.needs_printf) {
        fprintf(out, "    fmt_int_out: db \"%%d\", 10, 0\n");
        fprintf(out, "    fmt_str_out: db \"%%s\", 10, 0\n");
        fprintf(out, "    fmt_clear: db 27, '[2J', 27, '[H', 0\n");
        fprintf(out, "    fmt_color: db 27, '[38;2;%%d;%%d;%%dm', 0\n");
    }
    if (ctx.needs_scanf)
        fprintf(out, "    fmt_str_in:  db \" %%255s\", 0\n");
    for (i = 0; i < string_count; i++) {
        const char *s = string_table[i].content;
        int first = 1;
        fprintf(out, "    %s: db ", string_table[i].label);
        while (*s) {
            if (!first)
                fprintf(out, ", ");
            fprintf(out, "%d", (unsigned char)*s);
            first = 0;
            s++;
        }
        if (!first)
            fprintf(out, ", ");
        fprintf(out, "0\n");
    }

    if (ctx.needs_scanf) {
        fprintf(out, "\nsection .bss\n");
        fprintf(out, "    input_buf: resb 256\n");
    }

    fprintf(out, "\nsection .text\n");
    if (ctx.needs_printf)
        fprintf(out, "    extern printf\n");
    if (ctx.needs_scanf)
        fprintf(out, "    extern scanf\n");
    for (i = 0; i < program->function_count; i++) {
        if (program->functions[i].is_extern)
            fprintf(out, "    extern %s\n", program->functions[i].name);
    }

    fprintf(out, "\n");
    for (i = 0; i < program->function_count; i++) {
        if (!program->functions[i].is_extern)
            emit_function(&ctx, &program->functions[i]);
    }

    string_table_reset();
    return 0;
}
