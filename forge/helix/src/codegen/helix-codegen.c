/*
 * codegen.c - x86-64 assembly code generator for the Forge compiler
 *
 * Walks the AST and emits Windows x64 ABI-compliant assembly.
 *
 * Win64 ABI key rules:
 *   - First 4 integer args: rcx, rdx, r8, r9
 *   - Caller must allocate 32 bytes of "shadow space" before each call
 *   - Shadow space must be at [rsp+0]..[rsp+31] at the CALL instruction
 *   - Stack must be 16-byte aligned before CALL (i.e. rsp % 16 == 0 at CALL)
 *   - Return value in rax/eax
 *   - Volatile: rax, rcx, rdx, r8, r9, r10, r11
 *   - Non-volatile: rbx, rbp, rdi, rsi, r12-r15
 *
 * Stack frame layout (offsets from rbp after prologue):
 *   [rbp+0]          : saved rbp
 *   [rbp+8]          : return address
 *   [rbp-8]..[rbp-72]: 8 temp slots (8 bytes each) for intermediate values
 *   [rbp-80]...      : local variables (4 bytes each, int)
 *   ...               : padding for 16-byte alignment
 *   [rbp-frame_size] : start of shadow space (32 bytes) for callees
 *
 * Shadow space is written at the bottom of the frame so that at the CALL
 * instruction, rsp points to [rbp-frame_size], making [rsp+0]..[rsp+31]
 * the shadow space the callee expects.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "helix-codegen.h"

/* Shadow space size required by Win64 ABI */
#define SHADOW_SIZE 32

/* Number of temp slots for saving intermediate values during expression eval */
#define NUM_TEMP_SLOTS 8

/* Total bytes for temp slots (8 slots * 8 bytes = 64) */
#define TEMP_AREA_SIZE (NUM_TEMP_SLOTS * 8)

/* Starting offset for local variables (after temp slots, no overlap) */
#define LOCAL_BASE_OFFSET (TEMP_AREA_SIZE + 16) /* rbp-80 */

/* Maximum call arguments supported */
#define MAX_CALL_ARGS 4

/* ---- String table for string literals ---- */

typedef struct {
    char *label;   /* Assembly label, e.g. "str_0" */
    char *content; /* String content */
} StringEntry;

static StringEntry *string_table = NULL;
static int string_count = 0;
static int string_cap = 0;

/* Look up or create a label for a string literal */
static const char *string_table_add(const char *content) {
    int i;
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

/* ---- Internal helpers ---- */

/* Emit a formatted line of assembly */
static void emit(CodegenCtx *ctx, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->out, fmt, args);
    va_end(args);
    fprintf(ctx->out, "\n");
}

/* Generate a unique label using an incrementing counter */
static int new_label(CodegenCtx *ctx) {
    return ctx->label_count++;
}

/* Return the Win64 ABI 32-bit register for the Nth integer argument */
static const char *arg_reg32(int n) {
    static const char *regs[] = {"ecx", "edx", "r8d", "r9d"};
    if (n < 4)
        return regs[n];
    return NULL;
}

/* Return the Win64 ABI 64-bit register for the Nth integer argument */
static const char *arg_reg64(int n) {
    static const char *regs[] = {"rcx", "rdx", "r8", "r9"};
    if (n < 4)
        return regs[n];
    return NULL;
}

/* Return the rbp-relative offset for the Nth temp slot (0-based).
 * Temp slots are at [rbp-8], [rbp-16], ..., [rbp-72]. */
static int temp_offset(int slot) {
    return (slot + 1) * 8;
}

static void emit_console_color_hex(CodegenCtx *ctx, int value) {
    int r = (value >> 16) & 0xFF;
    int g = (value >> 8) & 0xFF;
    int b = value & 0xFF;
    ctx->needs_printf = 1;
    emit(ctx, "    lea rcx, [rel fmt_color]");
    emit(ctx, "    mov edx, %d", r);
    emit(ctx, "    mov r8d, %d", g);
    emit(ctx, "    mov r9d, %d", b);
    emit(ctx, "    xor eax, eax");
    emit(ctx, "    call printf");
}

/* ---- Variable offset tracking ---- */

typedef struct {
    char *name;
    int offset; /* rbp-relative offset (positive number: [rbp-offset]) */
    int is_string;
} LocalVar;

static LocalVar *locals = NULL;
static int local_count = 0;
static int local_cap = 0;

static void locals_reset(void) {
    int i;
    for (i = 0; i < local_count; i++)
        free(locals[i].name);
    free(locals);
    locals = NULL;
    local_count = 0;
    local_cap = 0;
}

/* Add a local variable at the next available stack offset.
 * Each slot is 8 bytes to accommodate both integers and 64-bit pointers.
 * Returns the rbp-relative offset (positive: [rbp-offset]). */
static int locals_add(const char *name, int *local_offset, int is_string) {
    *local_offset += 8;
    int offset = LOCAL_BASE_OFFSET + *local_offset - 8;
    if (local_count >= local_cap) {
        local_cap = local_cap ? local_cap * 2 : 16;
        locals = (LocalVar *)realloc(locals, sizeof(LocalVar) * local_cap);
    }
    locals[local_count].name = strdup(name);
    locals[local_count].offset = offset;
    locals[local_count].is_string = is_string;
    local_count++;
    return offset;
}

static int locals_lookup(const char *name) {
    int i;
    for (i = 0; i < local_count; i++) {
        if (strcmp(locals[i].name, name) == 0)
            return locals[i].offset;
    }
    return -1;
}

static int is_expr_string(ASTNode *expr) {
    if (!expr)
        return 0;
    switch (expr->type) {
    case AST_STRING:
        return 1;
    case AST_VAR: {
        for (int i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, expr->as.var.name) == 0) {
                return locals[i].is_string;
            }
        }
        return 0;
    }
    case AST_CALL: {
        const char *fname = expr->as.call.name;
        if (strcmp(fname, "input") == 0 || strcmp(fname, "console_input") == 0) {
            return 1;
        }
        return 0;
    }
    default:
        return 0;
    }
}

/* ---- Expression code generation ---- */

/*
 * Emit code to evaluate an expression, leaving the result in eax.
 * Uses temp slots [rbp-8]..[rbp-72] to save intermediate results
 * across sub-expression evaluations.
 */
static void emit_expr(CodegenCtx *ctx, ASTNode *node) {
    switch (node->type) {
    case AST_NUMBER:
        emit(ctx, "    mov eax, %d", node->as.number.value);
        break;

    case AST_STRING: {
        /* Load address of string literal into rax */
        const char *label = string_table_add(node->as.string.value);
        emit(ctx, "    lea rax, [rel %s]", label);
        break;
    }

    case AST_VAR: {
        int off = locals_lookup(node->as.var.name);
        if (off < 0) {
            fprintf(stderr, "Forge codegen error: line %d: internal: unknown variable '%s'\n",
                    node->line, node->as.var.name);
            emit(ctx, "    mov rax, 0");
            break;
        }
        /* Load full 64-bit slot: works for both integers and pointers. */
        emit(ctx, "    mov rax, [rbp-%d]", off);
        break;
    }

    case AST_BINARY: {
        ASTNode *left = node->as.binary.left;
        ASTNode *right = node->as.binary.right;
        BinaryOp op = node->as.binary.op;
        int temp = temp_offset(ctx->temp_slot);
        ctx->temp_slot = (ctx->temp_slot + 1) % NUM_TEMP_SLOTS;

        /* Evaluate RIGHT first, save in temp slot, then evaluate LEFT.
         * Result: eax = left, ecx = right. */
        emit_expr(ctx, right);
        emit(ctx, "    mov [rbp-%d], eax", temp);
        emit_expr(ctx, left);
        emit(ctx, "    mov ecx, [rbp-%d]", temp);

        switch (op) {
        case BIN_ADD:
            emit(ctx, "    add eax, ecx");
            break;
        case BIN_SUB:
            emit(ctx, "    sub eax, ecx");
            break;
        case BIN_MUL:
            emit(ctx, "    imul eax, ecx");
            break;
        case BIN_DIV:
            emit(ctx, "    cdq");
            emit(ctx, "    idiv ecx");
            break;
        case BIN_EQ:
            emit(ctx, "    cmp eax, ecx");
            emit(ctx, "    sete al");
            emit(ctx, "    movzx eax, al");
            break;
        case BIN_NEQ:
            emit(ctx, "    cmp eax, ecx");
            emit(ctx, "    setne al");
            emit(ctx, "    movzx eax, al");
            break;
        case BIN_LT:
            emit(ctx, "    cmp eax, ecx");
            emit(ctx, "    setl al");
            emit(ctx, "    movzx eax, al");
            break;
        case BIN_GT:
            emit(ctx, "    cmp eax, ecx");
            emit(ctx, "    setg al");
            emit(ctx, "    movzx eax, al");
            break;
        case BIN_LE:
            emit(ctx, "    cmp eax, ecx");
            emit(ctx, "    setle al");
            emit(ctx, "    movzx eax, al");
            break;
        case BIN_GE:
            emit(ctx, "    cmp eax, ecx");
            emit(ctx, "    setge al");
            emit(ctx, "    movzx eax, al");
            break;
        }
        break;
    }

    case AST_UNARY:
        emit_expr(ctx, node->as.unary.operand);
        if (node->as.unary.op == UNARY_NEG) {
            emit(ctx, "    neg eax");
        } else {
            emit(ctx, "    cmp eax, 0");
            emit(ctx, "    sete al");
            emit(ctx, "    movzx eax, al");
        }
        break;

    case AST_CALL: {
        int i;
        const char *fname = node->as.call.name;

        /* Built-in: print(expr)
         * Only handle as built-in if there is at least one argument.
         * A zero-arg call means the user has defined their own print(). */
        if ((strcmp(fname, "console_print") == 0 || strcmp(fname, "print") == 0) && node->as.call.arg_count > 0) {
            ctx->needs_printf = 1;
            emit_expr(ctx, node->as.call.args[0]);
            /* Print based on whether the expression is string-typed or integer-typed */
            if (is_expr_string(node->as.call.args[0])) {
                emit(ctx, "    lea rcx, [rel fmt_str_out]");
                emit(ctx, "    mov rdx, rax");
            } else {
                emit(ctx, "    lea rcx, [rel fmt_int_out]");
                emit(ctx, "    mov rdx, rax");
            }
            emit(ctx, "    xor eax, eax");
            emit(ctx, "    call printf");
            break;
        }

        if (strcmp(fname, "console_clear") == 0 || strcmp(fname, "clear") == 0) {
            ctx->needs_printf = 1;
            emit(ctx, "    lea rcx, [rel fmt_clear]");
            emit(ctx, "    xor eax, eax");
            emit(ctx, "    call printf");
            break;
        }

        if (strcmp(fname, "console_color") == 0 || strcmp(fname, "color") == 0) {
            if (node->as.call.arg_count > 0 && node->as.call.args[0]->type == AST_NUMBER) {
                emit_console_color_hex(ctx, node->as.call.args[0]->as.number.value);
                break;
            }
            ctx->needs_printf = 1;
            emit(ctx, "    lea rcx, [rel fmt_clear]");
            emit(ctx, "    xor eax, eax");
            emit(ctx, "    call printf");
            break;
        }

        /* Built-in: input() — reads a string line into a static buffer.
         * Returns the buffer address in rax so callers can store/print it. */
        if (strcmp(fname, "console_input") == 0 || strcmp(fname, "input") == 0) {
            ctx->needs_scanf = 1;
            emit(ctx, "    lea rcx, [rel fmt_str_in]"); /* format: " %255s" */
            emit(ctx, "    lea rdx, [rel input_buf]");  /* buffer address */
            emit(ctx, "    xor eax, eax");
            emit(ctx, "    call scanf");
            emit(ctx, "    lea rax, [rel input_buf]"); /* return buffer ptr */
            break;
        }

        /* User-defined / extern function call */
        /* Evaluate arguments left-to-right, but keep each result on the
         * runtime stack so nested calls cannot overwrite earlier values.
         * The stack is restored before the actual CALL instruction. */
        for (i = 0; i < node->as.call.arg_count; i++) {
            emit_expr(ctx, node->as.call.args[i]);
            emit(ctx, "    push rax");
        }

        /* Pop in reverse order so the first argument ends up in rcx. */
        if (node->as.call.arg_count > 3)
            emit(ctx, "    pop r9");
        if (node->as.call.arg_count > 2)
            emit(ctx, "    pop r8");
        if (node->as.call.arg_count > 1)
            emit(ctx, "    pop rdx");
        if (node->as.call.arg_count > 0)
            emit(ctx, "    pop rcx");

        emit(ctx, "    xor eax, eax");
        emit(ctx, "    call %s", fname);
        break;
    }

    default:
        emit(ctx, "    mov eax, 0");
        break;
    }
}

/* ---- Statement code generation ---- */

static void emit_block(CodegenCtx *ctx, ASTNode *node);

static void emit_stmt(CodegenCtx *ctx, ASTNode *node) {
    switch (node->type) {
    case AST_VAR_DECL: {
        emit_expr(ctx, node->as.var_decl.init);
        int is_str = is_expr_string(node->as.var_decl.init);
        int off = locals_add(node->as.var_decl.name, &ctx->stack_offset, is_str);
        emit(ctx, "    mov [rbp-%d], rax", off);
        break;
    }

    case AST_ASSIGN: {
        emit_expr(ctx, node->as.assign.value);
        int off = locals_lookup(node->as.assign.name);
        if (off < 0) {
            fprintf(stderr, "Forge codegen error: line %d: internal: unknown variable '%s'\n",
                    node->line, node->as.assign.name);
            break;
        }
        for (int i = 0; i < local_count; i++) {
            if (strcmp(locals[i].name, node->as.assign.name) == 0) {
                locals[i].is_string = is_expr_string(node->as.assign.value);
                break;
            }
        }
        emit(ctx, "    mov [rbp-%d], rax", off);
        break;
    }

    case AST_EXPR_STMT:
        emit_expr(ctx, node->as.expr_stmt.expr);
        break;

    case AST_RETURN:
        emit_expr(ctx, node->as.return_stmt.expr);
        emit(ctx, "    jmp .Lexit%d", ctx->exit_label);
        break;

    case AST_IF: {
        int else_lbl = new_label(ctx);
        int end_lbl = new_label(ctx);
        emit_expr(ctx, node->as.if_stmt.cond);
        emit(ctx, "    cmp eax, 0");
        if (node->as.if_stmt.else_block) {
            emit(ctx, "    je .L%d", else_lbl);
            emit_block(ctx, node->as.if_stmt.then_block);
            emit(ctx, "    jmp .L%d", end_lbl);
            emit(ctx, ".L%d:", else_lbl);
            emit_block(ctx, node->as.if_stmt.else_block);
            emit(ctx, ".L%d:", end_lbl);
        } else {
            emit(ctx, "    je .L%d", end_lbl);
            emit_block(ctx, node->as.if_stmt.then_block);
            emit(ctx, ".L%d:", end_lbl);
        }
        break;
    }

    case AST_WHILE: {
        int loop_lbl = new_label(ctx);
        int end_lbl = new_label(ctx);
        if (ctx->loop_depth < MAX_LOOP_DEPTH) {
            ctx->loop_stack[ctx->loop_depth].start_lbl = loop_lbl;
            ctx->loop_stack[ctx->loop_depth].end_lbl = end_lbl;
            ctx->loop_depth++;
        }
        emit(ctx, ".L%d:", loop_lbl);
        emit_expr(ctx, node->as.while_stmt.cond);
        emit(ctx, "    cmp eax, 0");
        emit(ctx, "    je .L%d", end_lbl);
        emit_block(ctx, node->as.while_stmt.body);
        emit(ctx, "    jmp .L%d", loop_lbl);
        emit(ctx, ".L%d:", end_lbl);
        if (ctx->loop_depth > 0)
            ctx->loop_depth--;
        break;
    }

    case AST_FOR: {
        int loop_lbl = new_label(ctx);
        int pass_lbl = new_label(ctx);
        int end_lbl = new_label(ctx);
        if (ctx->loop_depth < MAX_LOOP_DEPTH) {
            ctx->loop_stack[ctx->loop_depth].start_lbl = pass_lbl;
            ctx->loop_stack[ctx->loop_depth].end_lbl = end_lbl;
            ctx->loop_depth++;
        }
        if (node->as.for_stmt.init)
            emit_stmt(ctx, node->as.for_stmt.init);
        emit(ctx, ".L%d:", loop_lbl);
        if (node->as.for_stmt.cond) {
            emit_expr(ctx, node->as.for_stmt.cond);
            emit(ctx, "    cmp eax, 0");
            emit(ctx, "    je .L%d", end_lbl);
        }
        emit_block(ctx, node->as.for_stmt.body);
        emit(ctx, ".L%d:", pass_lbl);
        if (node->as.for_stmt.incr)
            emit_stmt(ctx, node->as.for_stmt.incr);
        emit(ctx, "    jmp .L%d", loop_lbl);
        emit(ctx, ".L%d:", end_lbl);
        if (ctx->loop_depth > 0)
            ctx->loop_depth--;
        break;
    }

    case AST_DO_WHILE: {
        int loop_lbl = new_label(ctx);
        int end_lbl = new_label(ctx);
        if (ctx->loop_depth < MAX_LOOP_DEPTH) {
            ctx->loop_stack[ctx->loop_depth].start_lbl = loop_lbl;
            ctx->loop_stack[ctx->loop_depth].end_lbl = end_lbl;
            ctx->loop_depth++;
        }
        emit(ctx, ".L%d:", loop_lbl);
        emit_block(ctx, node->as.while_stmt.body);
        emit_expr(ctx, node->as.while_stmt.cond);
        emit(ctx, "    cmp eax, 0");
        emit(ctx, "    jne .L%d", loop_lbl);
        emit(ctx, ".L%d:", end_lbl);
        if (ctx->loop_depth > 0)
            ctx->loop_depth--;
        break;
    }

    case AST_BREAK:
        if (ctx->loop_depth > 0)
            emit(ctx, "    jmp .L%d", ctx->loop_stack[ctx->loop_depth - 1].end_lbl);
        else
            fprintf(stderr, "Forge codegen error: line %d: break() outside loop\n", node->line);
        break;

    case AST_PASS:
        if (ctx->loop_depth > 0)
            emit(ctx, "    jmp .L%d", ctx->loop_stack[ctx->loop_depth - 1].start_lbl);
        else
            fprintf(stderr, "Forge codegen error: line %d: pass() outside loop\n", node->line);
        break;

    case AST_BLOCK:
        emit_block(ctx, node);
        break;

    default:
        break;
    }
}

static void emit_block(CodegenCtx *ctx, ASTNode *node) {
    int i;
    for (i = 0; i < node->as.block.count; i++)
        emit_stmt(ctx, node->as.block.stmts[i]);
}

/* ---- Function code generation ---- */

/* Count the number of local variable declarations in a function body.
 * Used to pre-calculate stack frame size before emitting code. */
static int count_var_decls(ASTNode *node) {
    int i, count = 0;
    if (!node)
        return 0;
    switch (node->type) {
    case AST_VAR_DECL:
        return 1;
    case AST_BLOCK:
        for (i = 0; i < node->as.block.count; i++)
            count += count_var_decls(node->as.block.stmts[i]);
        return count;
    case AST_IF:
        count += count_var_decls(node->as.if_stmt.then_block);
        count += count_var_decls(node->as.if_stmt.else_block);
        return count;
    case AST_WHILE:
        return count_var_decls(node->as.while_stmt.body);
    case AST_FOR:
        if (node->as.for_stmt.init && node->as.for_stmt.init->type == AST_VAR_DECL)
            count++;
        count += count_var_decls(node->as.for_stmt.body);
        return count;
    case AST_DO_WHILE:
        return count_var_decls(node->as.while_stmt.body);
    default:
        return 0;
    }
}

static void emit_function(CodegenCtx *ctx, ASTNode *node) {
    int i;
    const char *fname = node->as.function.name;

    /* Reset local variable tracking */
    locals_reset();
    ctx->stack_offset = 0;
    ctx->exit_label = new_label(ctx);
    ctx->temp_slot = 0;
    ctx->loop_depth = 0;

    /*
     * Calculate frame size:
     *   - Temp area: NUM_TEMP_SLOTS * 8 = 64 bytes
     *   - Local variables: (param_count + var_decl_count) * 4 bytes
     *   - Shadow space: 32 bytes
     *   - Total must be 16-byte aligned
     */
    {
        int num_locals = node->as.function.param_count +
                         count_var_decls(node->as.function.body);
        int locals_size = num_locals * 8; /* 8 bytes per slot for 64-bit compat */
        int total = SHADOW_SIZE + LOCAL_BASE_OFFSET + locals_size;
        ctx->frame_size = (total + 15) & ~15;
        if (ctx->frame_size < SHADOW_SIZE + LOCAL_BASE_OFFSET + 16)
            ctx->frame_size = SHADOW_SIZE + LOCAL_BASE_OFFSET + 16;
    }

    /* Function label and prologue */
    emit(ctx, "");
    emit(ctx, "global %s", fname);
    emit(ctx, "%s:", fname);
    emit(ctx, "    push rbp");
    emit(ctx, "    mov rbp, rsp");
    emit(ctx, "    sub rsp, %d", ctx->frame_size);

    /* Store incoming parameters from registers into local variable slots */
    for (i = 0; i < node->as.function.param_count; i++) {
        int off = locals_add(node->as.function.params[i], &ctx->stack_offset, 0);
        if (i < 4) {
            /* Use 64-bit registers to store params into 8-byte slots */
            emit(ctx, "    mov [rbp-%d], %s", off, arg_reg64(i));
        }
    }

    /* Emit the function body */
    emit_block(ctx, node->as.function.body);

    /* Epilogue: single exit point */
    emit(ctx, ".Lexit%d:", ctx->exit_label);
    emit(ctx, "    mov rsp, rbp");
    emit(ctx, "    pop rbp");
    emit(ctx, "    ret");
}

/* ---- Two-pass scanning for builtins and strings ---- */

static void scan_expr(CodegenCtx *ctx, ASTNode *node) {
    int i;
    if (!node)
        return;
    switch (node->type) {
    case AST_CALL:
        /* Bare "print" is builtin printf only when called with args;
         * zero-arg means user-defined function, not the builtin. */
        if ((strcmp(node->as.call.name, "print") == 0 && node->as.call.arg_count > 0) ||
            strcmp(node->as.call.name, "console_print") == 0 ||
            strcmp(node->as.call.name, "console_color") == 0 ||
            strcmp(node->as.call.name, "console_clear") == 0)
            ctx->needs_printf = 1;
        if (strcmp(node->as.call.name, "input") == 0 ||
            strcmp(node->as.call.name, "console_input") == 0)
            ctx->needs_scanf = 1;
        for (i = 0; i < node->as.call.arg_count; i++)
            scan_expr(ctx, node->as.call.args[i]);
        break;
    case AST_STRING:
        /* Pre-register string literal */
        string_table_add(node->as.string.value);
        break;
    case AST_BINARY:
        scan_expr(ctx, node->as.binary.left);
        scan_expr(ctx, node->as.binary.right);
        break;
    case AST_UNARY:
        scan_expr(ctx, node->as.unary.operand);
        break;
    case AST_VAR_DECL:
        scan_expr(ctx, node->as.var_decl.init);
        break;
    case AST_ASSIGN:
        scan_expr(ctx, node->as.assign.value);
        break;
    case AST_RETURN:
        scan_expr(ctx, node->as.return_stmt.expr);
        break;
    case AST_EXPR_STMT:
        scan_expr(ctx, node->as.expr_stmt.expr);
        break;
    case AST_IF:
        scan_expr(ctx, node->as.if_stmt.cond);
        scan_expr(ctx, node->as.if_stmt.then_block);
        scan_expr(ctx, node->as.if_stmt.else_block);
        break;
    case AST_WHILE:
        scan_expr(ctx, node->as.while_stmt.cond);
        scan_expr(ctx, node->as.while_stmt.body);
        break;
    case AST_FOR:
        if (node->as.for_stmt.init)
            scan_expr(ctx, node->as.for_stmt.init);
        scan_expr(ctx, node->as.for_stmt.cond);
        scan_expr(ctx, node->as.for_stmt.incr);
        scan_expr(ctx, node->as.for_stmt.body);
        break;
    case AST_DO_WHILE:
        scan_expr(ctx, node->as.while_stmt.body);
        scan_expr(ctx, node->as.while_stmt.cond);
        break;
    case AST_BREAK:
    case AST_PASS:
        break;
    case AST_BLOCK:
        for (i = 0; i < node->as.block.count; i++)
            scan_expr(ctx, node->as.block.stmts[i]);
        break;
    default:
        break;
    }
}

static void scan_program(CodegenCtx *ctx, ASTNode *program) {
    int i;
    for (i = 0; i < program->as.program.count; i++) {
        ASTNode *node = program->as.program.functions[i];
        if (node->type == AST_FUNCTION)
            scan_expr(ctx, node->as.function.body);
    }
}

/* ---- Public API ---- */

int codegen_emit(ASTNode *program, FILE *out) {
    CodegenCtx ctx;
    int i;

    ctx.out = out;
    ctx.label_count = 0;
    ctx.needs_printf = 0;
    ctx.needs_scanf = 0;
    ctx.stack_offset = 0;
    ctx.exit_label = 0;
    ctx.frame_size = 0;
    ctx.loop_depth = 0;

    /* Reset string table */
    string_table_reset();

    /* Scan for builtins and string literals */
    scan_program(&ctx, program);

    /* Emit header */
    fprintf(out, "; Forge-generated x86-64 assembly (Windows x64 ABI)\n");
    fprintf(out, "; Assemble with: nasm -f win64 <file>.asm\n");
    fprintf(out, "; Link with:     ld <file>.o -o <file>.exe\n\n");

    /* Emit data section with format strings and string literals */
    fprintf(out, "section .data\n");
    if (ctx.needs_printf) {
        fprintf(out, "    fmt_int_out: db \"%%d\", 10, 0\n");
        fprintf(out, "    fmt_str_out: db \"%%s\", 10, 0\n");
        fprintf(out, "    fmt_clear: db 27, '[2J', 27, '[H', 0\n");
        fprintf(out, "    fmt_color: db 27, '[38;2;%%d;%%d;%%dm', 0\n");
    }
    if (ctx.needs_scanf) {
        fprintf(out, "    fmt_str_in:  db \" %%255s\", 0\n");
    }
    for (i = 0; i < string_count; i++) {
        fprintf(out, "    %s: db ", string_table[i].label);
        /* Emit each character as a numeric byte */
        const char *s = string_table[i].content;
        int first = 1;
        while (*s) {
            if (!first)
                fprintf(out, ", ");
            fprintf(out, "%d", (unsigned char)*s);
            first = 0;
            s++;
        }
        if (!first)
            fprintf(out, ", ");
        fprintf(out, "0\n"); /* null terminator */
    }

    /* Emit BSS section for input buffer (only when scanf is needed) */
    if (ctx.needs_scanf) {
        fprintf(out, "\nsection .bss\n");
        fprintf(out, "    input_buf: resb 256\n");
    }

    /* Emit text section with extern declarations */
    fprintf(out, "\nsection .text\n");
    if (ctx.needs_printf)
        fprintf(out, "    extern printf\n");
    if (ctx.needs_scanf)
        fprintf(out, "    extern scanf\n");

    for (i = 0; i < program->as.program.count; i++) {
        ASTNode *node = program->as.program.functions[i];
        if (node->type == AST_EXTERN_FUNC) {
            fprintf(out, "    extern %s\n", node->as.extern_func.name);
        }
    }

    fprintf(out, "\n");

    /* Emit each function */
    for (i = 0; i < program->as.program.count; i++) {
        ASTNode *node = program->as.program.functions[i];
        if (node->type == AST_FUNCTION)
            emit_function(&ctx, node);
    }

    /* Cleanup */
    locals_reset();
    string_table_reset();
    return 0;
}
