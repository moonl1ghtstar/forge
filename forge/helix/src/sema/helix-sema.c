/*
 * sema.c - Semantic analysis for the Forge compiler
 *
 * Walks the AST to build symbol tables (per function scope), verify
 * that all variables are declared before use, check function call
 * argument counts, and assign resolved types to expression nodes.
 * Currently all types resolve to "int".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "helix-sema.h"

/* The current Windows x64 backend only passes up to 4 integer arguments
 * in registers. Reject wider call signatures instead of silently
 * truncating them in codegen. */
#define MAX_CALL_ARGS 4

/* ---- Symbol table (simple linear-scan arrays) ---- */

/* A variable entry: name and its stack offset (computed during codegen) */
typedef struct {
    char *name;
    int offset; /* Stack offset from rbp (set by codegen, 0 here) */
    char *type; /* Resolved type string, always "int" for now */
} VarEntry;

/* A function entry: name, parameter count, and line of definition */
typedef struct {
    char *name;
    int param_count;
    int line;
} FuncEntry;

/* Scope: holds variable entries for one function */
typedef struct {
    VarEntry *vars;
    int var_count;
    int var_cap;
} Scope;

/* Semantic analysis context (no global state) */
typedef struct {
    FuncEntry *functions;
    int func_count;
    int func_cap;
    Scope *current_scope;
    int had_error;
} SemaCtx;

/* Report a semantic error */
static void sema_error(SemaCtx *ctx, int line, const char *fmt, ...) {
    va_list args;
    fprintf(stderr, "Forge semantic error: line %d: ", line);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    ctx->had_error = 1;
}

/* Initialize a new scope */
static Scope *scope_new(void) {
    Scope *s = (Scope *)calloc(1, sizeof(Scope));
    s->vars = NULL;
    s->var_count = 0;
    s->var_cap = 0;
    return s;
}

/* Free a scope and its variable entries */
static void scope_free(Scope *s) {
    int i;
    if (!s)
        return;
    for (i = 0; i < s->var_count; i++)
        free(s->vars[i].name);
    free(s->vars);
    free(s);
}

/* Add a variable to the current scope */
static void scope_add_var(SemaCtx *ctx, Scope *s, const char *name, int line) {
    /* Check for redeclaration in the same scope */
    int i;
    for (i = 0; i < s->var_count; i++) {
        if (strcmp(s->vars[i].name, name) == 0) {
            sema_error(ctx, line, "variable '%s' is already declared in this scope. Rename it or remove the duplicate.", name);
            return;
        }
    }
    if (s->var_count >= s->var_cap) {
        s->var_cap = s->var_cap ? s->var_cap * 2 : 8;
        s->vars = (VarEntry *)realloc(s->vars, sizeof(VarEntry) * s->var_cap);
    }
    s->vars[s->var_count].name = strdup(name);
    s->vars[s->var_count].offset = 0; /* will be set by codegen */
    s->vars[s->var_count].type = "int";
    s->var_count++;
}

/* Look up a variable in the current scope */
static VarEntry *scope_lookup(Scope *s, const char *name) {
    int i;
    for (i = 0; i < s->var_count; i++) {
        if (strcmp(s->vars[i].name, name) == 0)
            return &s->vars[i];
    }
    return NULL;
}

/* Check if a name is a built-in function */
static int is_builtin(const char *name) {
    return (strcmp(name, "print") == 0 || strcmp(name, "input") == 0 ||
            strcmp(name, "clear") == 0 || strcmp(name, "color") == 0 ||
            strcmp(name, "console_print") == 0 || strcmp(name, "console_input") == 0 ||
            strcmp(name, "console_clear") == 0 || strcmp(name, "console_color") == 0);
}

/* Register a function in the global function table */
static void register_function(SemaCtx *ctx, const char *name, int param_count, int line) {
    int i;
    for (i = 0; i < ctx->func_count; i++) {
        if (strcmp(ctx->functions[i].name, name) == 0) {
            /* Allow user-defined functions to shadow built-in names.
             * The user's definition takes precedence; update the entry. */
            if (is_builtin(name)) {
                ctx->functions[i].param_count = param_count;
                ctx->functions[i].line = line;
                return;
            }
            sema_error(ctx, line, "function '%s' is already defined. Rename one of the definitions.", name);
            return;
        }
    }
    if (ctx->func_count >= ctx->func_cap) {
        ctx->func_cap = ctx->func_cap ? ctx->func_cap * 2 : 16;
        ctx->functions = (FuncEntry *)realloc(ctx->functions, sizeof(FuncEntry) * ctx->func_cap);
    }
    ctx->functions[ctx->func_count].name = strdup(name);
    ctx->functions[ctx->func_count].param_count = param_count;
    ctx->functions[ctx->func_count].line = line;
    ctx->func_count++;
}

/* Look up a function by name */
static FuncEntry *lookup_function(SemaCtx *ctx, const char *name) {
    int i;
    for (i = 0; i < ctx->func_count; i++) {
        if (strcmp(ctx->functions[i].name, name) == 0)
            return &ctx->functions[i];
    }
    return NULL;
}

/* Forward declaration for recursive analysis */
static void analyze_node(SemaCtx *ctx, ASTNode *node);

/* Analyze an expression node and set its resolved_type to "int" */
static void analyze_expr(SemaCtx *ctx, ASTNode *node) {
    if (!node)
        return;
    switch (node->type) {
    case AST_NUMBER:
        node->resolved_type = strdup("int");
        break;
    case AST_STRING:
        node->resolved_type = strdup("int"); /* strings are opaque for now */
        break;
    case AST_VAR:
        if (!scope_lookup(ctx->current_scope, node->as.var.name)) {
            sema_error(ctx, node->line, "undeclared variable '%s'. Declare it before use with 'let', 'var', or 'global'.", node->as.var.name);
        }
        node->resolved_type = strdup("int");
        break;
    case AST_BINARY:
        analyze_expr(ctx, node->as.binary.left);
        analyze_expr(ctx, node->as.binary.right);
        node->resolved_type = strdup("int");
        break;
    case AST_UNARY:
        analyze_expr(ctx, node->as.unary.operand);
        node->resolved_type = strdup("int");
        break;
    case AST_CALL: {
        int i;
        if (node->as.call.arg_count > MAX_CALL_ARGS) {
            sema_error(ctx, node->line,
                       "function call '%s' passes %d arguments, but this backend supports at most %d.",
                       node->as.call.name, node->as.call.arg_count, MAX_CALL_ARGS);
        }
        /* User-defined function takes priority over built-ins with the same name.
         * Only fall back to built-in check when no user definition exists. */
        {
            FuncEntry *f = lookup_function(ctx, node->as.call.name);
            if (f) {
                if (f->param_count != node->as.call.arg_count) {
                    sema_error(ctx, node->line,
                               "function '%s' expects %d argument(s), got %d.",
                               node->as.call.name, f->param_count, node->as.call.arg_count);
                }
            } else if (is_builtin(node->as.call.name)) {
                int expected;
                if (strcmp(node->as.call.name, "input") == 0 ||
                    strcmp(node->as.call.name, "console_input") == 0 ||
                    strcmp(node->as.call.name, "clear") == 0 ||
                    strcmp(node->as.call.name, "console_clear") == 0)
                    expected = 0;
                else
                    expected = 1;
                if (node->as.call.arg_count != expected) {
                    sema_error(ctx, node->line,
                               "built-in function '%s' expects %d argument(s), got %d.",
                               node->as.call.name, expected, node->as.call.arg_count);
                }
            } else {
                sema_error(ctx, node->line, "undefined function '%s'. Add an extern declaration or define it first.", node->as.call.name);
            }
        }
        for (i = 0; i < node->as.call.arg_count; i++)
            analyze_expr(ctx, node->as.call.args[i]);
        node->resolved_type = strdup("int");
        break;
    }
    default:
        node->resolved_type = strdup("int");
        break;
    }
}

/* Analyze a statement node */
static void analyze_node(SemaCtx *ctx, ASTNode *node) {
    if (!node)
        return;
    switch (node->type) {
    case AST_VAR_DECL:
        analyze_expr(ctx, node->as.var_decl.init);
        scope_add_var(ctx, ctx->current_scope, node->as.var_decl.name, node->line);
        break;
    case AST_ASSIGN:
        analyze_expr(ctx, node->as.assign.value);
        if (!scope_lookup(ctx->current_scope, node->as.assign.name)) {
            sema_error(ctx, node->line, "undeclared variable '%s'. Declare it before assignment.", node->as.assign.name);
        }
        break;
    case AST_IF:
        analyze_expr(ctx, node->as.if_stmt.cond);
        analyze_node(ctx, node->as.if_stmt.then_block);
        if (node->as.if_stmt.else_block)
            analyze_node(ctx, node->as.if_stmt.else_block);
        break;
    case AST_WHILE:
        analyze_expr(ctx, node->as.while_stmt.cond);
        analyze_node(ctx, node->as.while_stmt.body);
        break;
    case AST_FOR:
        if (node->as.for_stmt.init)
            analyze_node(ctx, node->as.for_stmt.init);
        if (node->as.for_stmt.cond)
            analyze_expr(ctx, node->as.for_stmt.cond);
        if (node->as.for_stmt.incr) {
            if (node->as.for_stmt.incr->type == AST_ASSIGN)
                analyze_expr(ctx, node->as.for_stmt.incr->as.assign.value);
            else
                analyze_expr(ctx, node->as.for_stmt.incr);
        }
        analyze_node(ctx, node->as.for_stmt.body);
        break;
    case AST_DO_WHILE:
        analyze_node(ctx, node->as.while_stmt.body);
        analyze_expr(ctx, node->as.while_stmt.cond);
        break;
    case AST_BREAK:
    case AST_PASS:
        break;
    case AST_RETURN:
        analyze_expr(ctx, node->as.return_stmt.expr);
        break;
    case AST_EXPR_STMT:
        analyze_expr(ctx, node->as.expr_stmt.expr);
        break;
    case AST_BLOCK: {
        int i;
        for (i = 0; i < node->as.block.count; i++)
            analyze_node(ctx, node->as.block.stmts[i]);
        break;
    }
    default:
        break;
    }
}

/* Analyze a function definition */
static void analyze_function(SemaCtx *ctx, ASTNode *node) {
    int i;
    Scope *prev_scope;

    /* Create a new scope for this function */
    prev_scope = ctx->current_scope;
    ctx->current_scope = scope_new();

    /* Add parameters to the scope */
    for (i = 0; i < node->as.function.param_count; i++)
        scope_add_var(ctx, ctx->current_scope, node->as.function.params[i], node->line);

    /* Analyze the function body */
    analyze_node(ctx, node->as.function.body);

    /* Restore previous scope */
    scope_free(ctx->current_scope);
    ctx->current_scope = prev_scope;
}

/* Clean up all resources in a SemaCtx */
static void sema_ctx_free(SemaCtx *ctx) {
    int i;
    for (i = 0; i < ctx->func_count; i++)
        free(ctx->functions[i].name);
    free(ctx->functions);
    ctx->functions = NULL;
    ctx->func_count = 0;
}

/* Public entry point: analyze the entire program */
int sema_analyze(ASTNode *program) {
    SemaCtx ctx;
    int i;
    int has_main = 0;

    /* Initialize context */
    ctx.functions = NULL;
    ctx.func_count = 0;
    ctx.func_cap = 0;
    ctx.current_scope = NULL;
    ctx.had_error = 0;

    /* First pass: register all functions and extern declarations */
    for (i = 0; i < program->as.program.count; i++) {
        ASTNode *node = program->as.program.functions[i];
        if (node->type == AST_FUNCTION) {
            if (node->as.function.param_count > MAX_CALL_ARGS) {
                sema_error(&ctx, node->line,
                           "function '%s' declares %d parameters, but this backend supports at most %d.",
                           node->as.function.name, node->as.function.param_count, MAX_CALL_ARGS);
            }
            register_function(&ctx, node->as.function.name,
                              node->as.function.param_count, node->line);
            if (strcmp(node->as.function.name, "main") == 0)
                has_main = 1;
        } else if (node->type == AST_EXTERN_FUNC) {
            /* Register extern functions so they can be called */
            if (node->as.extern_func.param_count > MAX_CALL_ARGS) {
                sema_error(&ctx, node->line,
                           "extern function '%s' declares %d parameters, but this backend supports at most %d.",
                           node->as.extern_func.name, node->as.extern_func.param_count, MAX_CALL_ARGS);
            }
            register_function(&ctx, node->as.extern_func.name,
                              node->as.extern_func.param_count, node->line);
        }
    }

    /* Check that main() exists */
    if (!has_main) {
        sema_error(&ctx, 1, "program must define a 'main' function or contain top-level statements.");
    }

    /* Second pass: analyze each function body */
    for (i = 0; i < program->as.program.count; i++) {
        ASTNode *node = program->as.program.functions[i];
        if (node->type == AST_FUNCTION)
            analyze_function(&ctx, node);
    }

    /* Clean up */
    sema_ctx_free(&ctx);

    return ctx.had_error;
}
