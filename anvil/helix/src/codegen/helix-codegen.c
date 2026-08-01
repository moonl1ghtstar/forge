/*
 * helix-codegen.c - x86-64 assembly code generator for Anvil IR.
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
    int label_count;
    const IRProgram *program; /* for link_name lookup in calls */
    int need_strlen_extern;
    int need_console_extern;
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
    case IR_VALUE_CONST_LONG:
        emit(ctx, "    mov %s, %lld", reg, (long long)value.as.long_value);
        break;
    case IR_VALUE_CONST_FLOAT: {
        long long bits;
        memcpy(&bits, &value.as.float_value, sizeof(bits));
        emit(ctx, "    mov %s, %lld", reg, (long long)bits);
        break;
    }
    case IR_VALUE_CONST_BOOL:
        emit(ctx, "    mov %s, %d", reg, value.as.int_value ? 1 : 0);
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
    (void)ctx;
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
                if (ins->as.call.callee && strcmp(ins->as.call.callee, "strlen") == 0)
                    ctx->need_strlen_extern = 1;
                for (int k = 0; k < ins->as.call.arg_count; k++)
                    scan_value(ins->as.call.args[k]);
                break;
            case IR_OP_CONSOLE_PRINT:
                ctx->need_console_extern = 1;
                scan_value(ins->as.console_print.value);
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

static void emit_call_generic(CodegenCtx *ctx, const IRFunction *func, const IRInstruction *ins) {
    static const char *regs[] = {"rcx", "rdx", "r8", "r9"};
    int i;
    int stack_arg_count = ins->as.call.arg_count > 4 ? ins->as.call.arg_count - 4 : 0;
    int stack_bytes = stack_arg_count > 0 ? align16(stack_arg_count * 8) : 0;
    const char *callee = ins->as.call.callee;
    /* Resolve real linker symbol if this is an extern with link_name */
    if (ctx->program) {
        for (i = 0; i < ctx->program->function_count; i++) {
            const IRFunction *f = &ctx->program->functions[i];
            if (f->is_extern && f->name && strcmp(f->name, callee) == 0 && f->link_name) {
                callee = f->link_name;
                break;
            }
        }
    }
    for (i = 0; i < ins->as.call.arg_count && i < 4; i++)
        emit_value_to_reg(ctx, func, ins->as.call.args[i], regs[i]);
    if (stack_bytes > 0) {
        emit(ctx, "    sub rsp, %d", stack_bytes);
        for (i = 4; i < ins->as.call.arg_count; i++) {
            emit_value_to_reg(ctx, func, ins->as.call.args[i], "rax");
            emit(ctx, "    mov [rsp+%d], rax", 32 + (i - 4) * 8);
        }
    }
    emit(ctx, "    xor eax, eax");
    emit(ctx, "    call %s", callee);
    if (stack_bytes > 0)
        emit(ctx, "    add rsp, %d", stack_bytes);
    if (ins->has_result && ins->result.kind == IR_VALUE_TEMP)
        emit_store_result(ctx, ins->result);
}

static void emit_console_print(CodegenCtx *ctx, const IRFunction *func, const IRInstruction *ins) {
    int L = ctx->label_count++;
    IRValue value = ins->as.console_print.value;
    emit(ctx, "");
    emit(ctx, "    sub rsp, 120");
    emit(ctx, "    mov ecx, -11");
    emit(ctx, "    call GetStdHandle");
    emit(ctx, "    mov [rsp+112], rax");

    switch (ins->as.console_print.print_type) {
    case CONSOLE_PRINT_INT:
        emit_value_to_reg(ctx, func, value, "rax");
        emit(ctx, "    lea rdi, [rsp+60]");
        emit(ctx, "    mov rbx, rdi");
        emit(ctx, "    mov ecx, 10");
        emit(ctx, "    mov r8d, eax");
        emit(ctx, "    cmp eax, 0");
        emit(ctx, "    jge .Lcp%d_ipos", L);
        emit(ctx, "    neg eax");
        emit(ctx, ".Lcp%d_ipos:", L);
        emit(ctx, ".Lcp%d_iloop:", L);
        emit(ctx, "    xor edx, edx");
        emit(ctx, "    div ecx");
        emit(ctx, "    add dl, 48");
        emit(ctx, "    mov [rdi], dl");
        emit(ctx, "    dec rdi");
        emit(ctx, "    cmp eax, 0");
        emit(ctx, "    jne .Lcp%d_iloop", L);
        emit(ctx, "    test r8d, r8d");
        emit(ctx, "    jns .Lcp%d_inosign", L);
        emit(ctx, "    mov al, 45");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    dec rdi");
        emit(ctx, ".Lcp%d_inosign:", L);
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov rdx, rdi");
        emit(ctx, "    mov r8, rbx");
        emit(ctx, "    sub r8, rdx");
        emit(ctx, "    inc r8");
        goto print_common;
    case CONSOLE_PRINT_BOOL:
        emit_value_to_reg(ctx, func, value, "rax");
        emit(ctx, "    lea rdi, [rsp+60]");
        emit(ctx, "    cmp rax, 0");
        emit(ctx, "    je .Lcp%d_bfalse", L);
        emit(ctx, "    mov eax, 0x65757274");
        emit(ctx, "    mov [rdi], eax");
        emit(ctx, "    mov r8, 4");
        emit(ctx, "    jmp .Lcp%d_bcont", L);
        emit(ctx, ".Lcp%d_bfalse:", L);
        emit(ctx, "    mov eax, 0x736c6166");
        emit(ctx, "    mov [rdi], eax");
        emit(ctx, "    mov byte [rdi+4], 101");
        emit(ctx, "    mov r8, 5");
        emit(ctx, ".Lcp%d_bcont:", L);
        emit(ctx, "    mov rdx, rdi");
        goto print_common;
    case CONSOLE_PRINT_LONG:
        emit_value_to_reg(ctx, func, value, "rax");
        emit(ctx, "    lea rdi, [rsp+60]");
        emit(ctx, "    mov rbx, rdi");
        emit(ctx, "    mov rcx, 10");
        emit(ctx, "    mov r8, rax");
        emit(ctx, "    cmp rax, 0");
        emit(ctx, "    jge .Lcp%d_lpos", L);
        emit(ctx, "    neg rax");
        emit(ctx, ".Lcp%d_lpos:", L);
        emit(ctx, ".Lcp%d_lloop:", L);
        emit(ctx, "    xor edx, edx");
        emit(ctx, "    div rcx");
        emit(ctx, "    add dl, 48");
        emit(ctx, "    mov [rdi], dl");
        emit(ctx, "    dec rdi");
        emit(ctx, "    cmp rax, 0");
        emit(ctx, "    jne .Lcp%d_lloop", L);
        emit(ctx, "    test r8, r8");
        emit(ctx, "    jns .Lcp%d_lnosign", L);
        emit(ctx, "    mov al, 45");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    dec rdi");
        emit(ctx, ".Lcp%d_lnosign:", L);
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov rdx, rdi");
        emit(ctx, "    mov r8, rbx");
        emit(ctx, "    sub r8, rdx");
        emit(ctx, "    inc r8");
        goto print_common;
    case CONSOLE_PRINT_FLOAT: {
        emit_value_to_reg(ctx, func, value, "rax");
        emit(ctx, "    movq xmm0, rax");
        emit(ctx, "    movsd [rsp+88], xmm0");
        emit(ctx, "    xorpd xmm1, xmm1");
        emit(ctx, "    comisd xmm0, xmm1");
        emit(ctx, "    setb byte [rsp+116]");
        emit(ctx, "    jae .Lcp%d_fabs", L);
        emit(ctx, "    subsd xmm1, xmm0");
        emit(ctx, "    movsd xmm0, xmm1");
        emit(ctx, ".Lcp%d_fabs:", L);
        emit(ctx, "    lea rdi, [rsp+60]");
        emit(ctx, "    mov rbx, rdi");
        emit(ctx, "    cvttsd2si rax, xmm0");
        emit(ctx, "    mov ecx, 10");
        emit(ctx, ".Lcp%d_floop1:", L);
        emit(ctx, "    xor edx, edx");
        emit(ctx, "    div rcx");
        emit(ctx, "    add dl, 48");
        emit(ctx, "    mov [rdi], dl");
        emit(ctx, "    dec rdi");
        emit(ctx, "    cmp rax, 0");
        emit(ctx, "    jne .Lcp%d_floop1", L);
        emit(ctx, "    mov eax, [rsp+116]");
        emit(ctx, "    test eax, eax");
        emit(ctx, "    je .Lcp%d_fnosign", L);
        emit(ctx, "    mov al, 45");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    dec rdi");
        emit(ctx, ".Lcp%d_fnosign:", L);
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov rdx, rdi");
        emit(ctx, "    mov r8, rbx");
        emit(ctx, "    sub r8, rdx");
        emit(ctx, "    inc r8");
        emit(ctx, "    mov r9, rdx");
        emit(ctx, "    movsd xmm0, [rsp+88]");
        emit(ctx, "    cvttsd2si rax, xmm0");
        emit(ctx, "    cvtsi2sd xmm1, rax");
        emit(ctx, "    subsd xmm0, xmm1");
        emit(ctx, "    mov rax, 0x412E848000000000");
        emit(ctx, "    movq xmm1, rax");
        emit(ctx, "    mulsd xmm0, xmm1");
        emit(ctx, "    cvttsd2si rax, xmm0");
        emit(ctx, "    test rax, rax");
        emit(ctx, "    jns .Lcp%d_fracpos", L);
        emit(ctx, "    neg rax");
        emit(ctx, ".Lcp%d_fracpos:", L);
        emit(ctx, "    mov rdi, rbx");
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov byte [rdi], 46");
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov ecx, 100000");
        emit(ctx, "    xor edx, edx");
        emit(ctx, "    div ecx");
        emit(ctx, "    add al, 48");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov eax, edx");
        emit(ctx, "    mov ecx, 10000");
        emit(ctx, "    xor edx, edx");
        emit(ctx, "    div ecx");
        emit(ctx, "    add al, 48");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov eax, edx");
        emit(ctx, "    mov ecx, 1000");
        emit(ctx, "    xor edx, edx");
        emit(ctx, "    div ecx");
        emit(ctx, "    add al, 48");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov eax, edx");
        emit(ctx, "    mov ecx, 100");
        emit(ctx, "    xor edx, edx");
        emit(ctx, "    div ecx");
        emit(ctx, "    add al, 48");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov eax, edx");
        emit(ctx, "    mov ecx, 10");
        emit(ctx, "    xor edx, edx");
        emit(ctx, "    div ecx");
        emit(ctx, "    add al, 48");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    inc rdi");
        emit(ctx, "    lea eax, [edx+48]");
        emit(ctx, "    mov [rdi], al");
        emit(ctx, "    inc rdi");
        emit(ctx, "    mov rdx, r9");
        emit(ctx, "    mov r8, rdi");
        emit(ctx, "    sub r8, rdx");
        goto print_common;
    }
    case CONSOLE_PRINT_STR:
        emit_value_to_reg(ctx, func, value, "r15");
        emit(ctx, "    mov rdi, r15");
        emit(ctx, "    xor ecx, ecx");
        emit(ctx, ".Lcp%d_sloop:", L);
        emit(ctx, "    mov al, [rdi]");
        emit(ctx, "    test al, al");
        emit(ctx, "    je .Lcp%d_sdone", L);
        emit(ctx, "    inc rdi");
        emit(ctx, "    inc rcx");
        emit(ctx, "    jmp .Lcp%d_sloop", L);
        emit(ctx, ".Lcp%d_sdone:", L);
        emit(ctx, "    mov r8, rcx");
        emit(ctx, "    mov rdx, r15");
        goto print_common;
    default:
        goto print_done;
    }

print_common:
    emit(ctx, "    mov rcx, [rsp+112]");
    emit(ctx, "    lea r9, [rsp+104]");
    emit(ctx, "    xor eax, eax");
    emit(ctx, "    mov [rsp+32], rax");
    emit(ctx, "    call WriteFile");

print_done:
    emit(ctx, "    add rsp, 120");
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
    case IR_OP_CONSOLE_PRINT:
        emit_console_print(ctx, func, ins);
        break;
    case IR_OP_CALL:
        emit_call_generic(ctx, func, ins);
        break;
    case IR_OP_RETURN:
        if (ins->as.ret.has_value)
            emit_value_to_reg(ctx, func, ins->as.ret.value, "rax");
        emit(ctx, "    jmp %s_exit%d", func->name, exit_label);
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
    for (i = 4; i < func->param_count; i++) {
        emit(ctx, "    mov rax, [rbp+%d]", 48 + (i - 4) * 8);
        emit(ctx, "    mov [rbp-%d], rax", local_offset(func, i, 0));
    }

    for (i = 0; i < func->block_count; i++) {
        const IRBasicBlock *block = &func->blocks[i];
        emit(ctx, "%s:", block->name);
        for (j = 0; j < block->instruction_count; j++)
            emit_instruction(ctx, func, &block->instructions[j], exit_label);
    }

    emit(ctx, "%s_exit%d:", func->name, exit_label);
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
    ctx.label_count = 0;
    ctx.program = program;
    ctx.need_strlen_extern = 0;
    ctx.need_console_extern = 0;

    string_table_reset();
    scan_program(program, &ctx);

    fprintf(out, "; Anvil-generated x86-64 assembly (Windows x64 ABI)\n");
    fprintf(out, "; Assemble with: nasm -f win64 <file>.asm\n");
    fprintf(out, "; Link with:     ld <file>.o -o <file>.exe\n\n");

    fprintf(out, "section .data\n");
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

    fprintf(out, "\nsection .text\n");
    if (ctx.need_strlen_extern)
        fprintf(out, "    extern strlen\n");
    if (ctx.need_console_extern) {
        fprintf(out, "    extern GetStdHandle\n");
        fprintf(out, "    extern WriteFile\n");
    }
    for (i = 0; i < program->function_count; i++) {
        if (program->functions[i].is_extern) {
            const char *sym = program->functions[i].link_name
                              ? program->functions[i].link_name
                              : program->functions[i].name;
            fprintf(out, "    extern %s\n", sym);
        }
    }

    fprintf(out, "\n");
    for (i = 0; i < program->function_count; i++) {
        if (!program->functions[i].is_extern)
            emit_function(&ctx, &program->functions[i]);
    }

    string_table_reset();
    return 0;
}
