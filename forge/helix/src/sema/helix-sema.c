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
#include "../errors/forge-errors.h"

/* ---- Symbol table (simple linear-scan arrays) ---- */

/* A variable entry: name and its stack offset (computed during codegen) */
typedef struct {
    char *name;
    int offset; /* Stack offset from rbp (set by codegen, 0 here) */
    char *type_name; /* Resolved type string, "int", "str", or struct name */
} VarEntry;

/* A function entry: name, parameter count, and line of definition */
typedef struct {
    char *name;
    int param_count;
    int line;
} FuncEntry;

typedef struct {
    char *name;
    char *type_name;
} StructField;

typedef struct {
    char *name;
    StructField *fields;
    int field_count;
} StructEntry;

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
    StructEntry *structs;
    int struct_count;
    int struct_cap;
    Scope *current_scope;
    int had_error;
} SemaCtx;

/* Report a semantic error */
static void sema_error(SemaCtx *ctx, int line, const char *fmt, ...) {
    va_list args;
    char msg[512];
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    // Try to find the symbol name inside single quotes
    char symbol[128] = "";
    const char *p1 = strchr(msg, '\'');
    if (p1) {
        const char *p2 = strchr(p1 + 1, '\'');
        if (p2 && (p2 - p1 - 1) < (int)sizeof(symbol)) {
            memcpy(symbol, p1 + 1, p2 - p1 - 1);
            symbol[p2 - p1 - 1] = '\0';
        }
    }

    // Determine the column
    int col = 0;
    const char *source = forge_get_current_file_source();
    if (source && symbol[0] != '\0') {
        // Find line start
        const char *curr = source;
        int current_line = 1;
        while (current_line < line && *curr != '\0') {
            if (*curr == '\n') current_line++;
            curr++;
        }
        if (*curr != '\0') {
            const char *line_start = curr;
            while (*curr != '\0' && *curr != '\n' && *curr != '\r') curr++;
            int line_len = (int)(curr - line_start);
            // Search for symbol in this line
            const char *pos = strstr(line_start, symbol);
            if (pos && (pos - line_start) < line_len) {
                col = (int)(pos - line_start) + 1;
            }
        }
    }
    if (col == 0) col = 1;

    // Determine the error code based on message contents
    const char *err_code = "E301"; // default: undefined variable
    if (strstr(msg, "undefined variable") || strstr(msg, "undeclared variable")) err_code = "E301";
    else if (strstr(msg, "is already declared") || strstr(msg, "is already defined")) {
        if (strstr(msg, "struct")) err_code = "E510";
        else if (strstr(msg, "function")) err_code = "E304";
        else err_code = "E301"; // redeclared variable
    }
    else if (strstr(msg, "is not a function") || strstr(msg, "undefined function")) err_code = "E302";
    else if (strstr(msg, "expects") && strstr(msg, "argument")) err_code = "E303";
    else if (strstr(msg, "outside of function") || strstr(msg, "outside function")) err_code = "E306";
    else if (strstr(msg, "type mismatch") || (strstr(msg, "expects") && strstr(msg, "got"))) err_code = "E401";
    else if (strstr(msg, "cannot assign") || strstr(msg, "assignment to struct") || strstr(msg, "cannot be assigned")) err_code = "E402";
    else if (strstr(msg, "not supported in") || strstr(msg, "cannot print")) err_code = "E403";
    else if (strstr(msg, "unknown struct type")) err_code = "E501";
    else if (strstr(msg, "duplicate field")) err_code = "E502";
    else if (strstr(msg, "has no field")) err_code = "E503";
    else if (strstr(msg, "initialized more than once")) err_code = "E505";
    else if (strstr(msg, "field value(s)")) err_code = "E506";
    else if (strstr(msg, "member access")) err_code = "E508";
    else if (strstr(msg, "cannot convert")) err_code = "E404";
    else if (strstr(msg, "missing initializer")) err_code = "E504";
    else if (strstr(msg, "cannot be instantiated")) err_code = "E507";
    else if (strstr(msg, "recursive type")) err_code = "E509";

    forge_report_error(SEV_ERROR, err_code, line, col, NULL, NULL, "%s", msg);
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
    for (i = 0; i < s->var_count; i++)
        free(s->vars[i].type_name);
    free(s->vars);
    free(s);
}

/* Add a variable to the current scope */
static void scope_add_var(SemaCtx *ctx, Scope *s, const char *name, const char *type_name, int line) {
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
    s->vars[s->var_count].type_name = strdup(type_name ? type_name : "int");
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

static StructEntry *lookup_struct(SemaCtx *ctx, const char *name) {
    int i;
    for (i = 0; i < ctx->struct_count; i++) {
        if (strcmp(ctx->structs[i].name, name) == 0)
            return &ctx->structs[i];
    }
    return NULL;
}

static StructField *lookup_struct_field(StructEntry *st, const char *field_name) {
    int i;
    if (!st)
        return NULL;
    for (i = 0; i < st->field_count; i++) {
        if (strcmp(st->fields[i].name, field_name) == 0)
            return &st->fields[i];
    }
    return NULL;
}

static int is_struct_type(SemaCtx *ctx, const char *type_name) {
    return type_name && lookup_struct(ctx, type_name) != NULL;
}

static void sema_register_struct(SemaCtx *ctx, ASTNode *node) {
    int i, j;
    StructEntry *st;
    if (!node || node->type != AST_STRUCT_DECL)
        return;
    for (i = 0; i < ctx->struct_count; i++) {
        if (strcmp(ctx->structs[i].name, node->as.struct_decl.name) == 0) {
            sema_error(ctx, node->line, "struct '%s' is already defined. Rename one of the definitions.", node->as.struct_decl.name);
            return;
        }
    }
    if (ctx->struct_count >= ctx->struct_cap) {
        ctx->struct_cap = ctx->struct_cap ? ctx->struct_cap * 2 : 8;
        ctx->structs = (StructEntry *)realloc(ctx->structs, sizeof(StructEntry) * ctx->struct_cap);
    }
    st = &ctx->structs[ctx->struct_count++];
    memset(st, 0, sizeof(*st));
    st->name = strdup(node->as.struct_decl.name);
    st->field_count = node->as.struct_decl.field_count;
    if (st->field_count > 0) {
        st->fields = (StructField *)calloc((size_t)st->field_count, sizeof(StructField));
        for (i = 0; i < st->field_count; i++) {
            st->fields[i].name = strdup(node->as.struct_decl.fields[i]);
            st->fields[i].type_name = NULL;
            for (j = 0; j < i; j++) {
                if (strcmp(st->fields[j].name, st->fields[i].name) == 0) {
                    sema_error(ctx, node->line, "struct '%s' has duplicate field '%s'.", st->name, st->fields[i].name);
                    break;
                }
            }
        }
    }
}

/* Register a function in the global function table */
static void register_function(SemaCtx *ctx, const char *name, int param_count, int line) {
    int i;
    for (i = 0; i < ctx->func_count; i++) {
        if (strcmp(ctx->functions[i].name, name) == 0) {
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
    case AST_INT_LITERAL:
        node->resolved_type = strdup("int");
        break;
    case AST_LONG_LITERAL:
        node->resolved_type = strdup("long");
        break;
    case AST_FLOAT_LITERAL:
        node->resolved_type = strdup("float");
        break;
    case AST_BOOL_LITERAL:
        node->resolved_type = strdup("bool");
        break;
    case AST_STRING_LITERAL:
        node->resolved_type = strdup("str");
        break;
    case AST_VAR:
        {
            VarEntry *v = scope_lookup(ctx->current_scope, node->as.var.name);
            if (!v) {
                sema_error(ctx, node->line, "undeclared variable '%s'. Declare it before use with 'let', 'var', 'const', or 'global'.", node->as.var.name);
                node->resolved_type = strdup("int");
            } else {
                node->resolved_type = strdup(v->type_name ? v->type_name : "int");
            }
        }
        break;
    case AST_FIELD_ACCESS: {
        StructEntry *st;
        StructField *field;
        analyze_expr(ctx, node->as.field_access.object);
        st = lookup_struct(ctx, node->as.field_access.object->resolved_type);
        if (!st) {
            sema_error(ctx, node->line, "field access on non-struct value.");
            node->resolved_type = strdup("int");
            break;
        }
        field = lookup_struct_field(st, node->as.field_access.field_name);
        if (!field) {
            sema_error(ctx, node->line, "struct '%s' has no field '%s'.", st->name, node->as.field_access.field_name);
            node->resolved_type = strdup("int");
            break;
        }
        node->resolved_type = strdup(field->type_name ? field->type_name : "int");
        break;
    }
    case AST_STRUCT_INIT:
        node->resolved_type = strdup(node->as.struct_init.type_name);
        break;
    case AST_BINARY:
        analyze_expr(ctx, node->as.binary.left);
        analyze_expr(ctx, node->as.binary.right);
        if (node->as.binary.left->resolved_type && is_struct_type(ctx, node->as.binary.left->resolved_type))
            sema_error(ctx, node->line, "struct values are not supported in binary expressions.");
        if (node->as.binary.right->resolved_type && is_struct_type(ctx, node->as.binary.right->resolved_type))
            sema_error(ctx, node->line, "struct values are not supported in binary expressions.");
        node->resolved_type = strdup("int");
        break;
    case AST_UNARY:
        analyze_expr(ctx, node->as.unary.operand);
        if (node->as.unary.operand->resolved_type && is_struct_type(ctx, node->as.unary.operand->resolved_type))
            sema_error(ctx, node->line, "struct values are not supported in unary expressions.");
        node->resolved_type = strdup("int");
        break;
    case AST_CALL: {
        int i;
        for (i = 0; i < node->as.call.arg_count; i++)
            analyze_expr(ctx, node->as.call.args[i]);
        if (strcmp(node->as.call.name, "console_print") == 0 || strcmp(node->as.call.name, "print") == 0) {
            if (node->as.call.arg_count != 1) {
                sema_error(ctx, node->line,
                           "function '%s' expects 1 argument, got %d.",
                           node->as.call.name, node->as.call.arg_count);
            } else {
                ASTNode *arg0 = node->as.call.args[0];
                const char *rtype = arg0 ? arg0->resolved_type : NULL;
                if (rtype) {
                    if (strcmp(rtype, "str") != 0 && strcmp(rtype, "int") != 0 &&
                        strcmp(rtype, "long") != 0 && strcmp(rtype, "float") != 0 &&
                        strcmp(rtype, "bool") != 0) {
                        sema_error(ctx, node->line,
                                   "cannot print value of type '%s'.", rtype);
                    }
                }
            }
        } else {
            FuncEntry *f = lookup_function(ctx, node->as.call.name);
            if (f) {
                if (f->param_count != node->as.call.arg_count) {
                    sema_error(ctx, node->line,
                               "function '%s' expects %d argument(s), got %d.",
                               node->as.call.name, f->param_count, node->as.call.arg_count);
                }
            } else if (strcmp(node->as.call.name, "strlen") == 0) {
                if (node->as.call.arg_count != 1) {
                    sema_error(ctx, node->line,
                               "function 'strlen' expects 1 argument, got %d.",
                               node->as.call.arg_count);
                }
            } else {
                sema_error(ctx, node->line, "undefined function '%s'. Add an extern declaration or define it first.", node->as.call.name);
            }
        }
        node->resolved_type = strdup("int");
        break;
    }
    default:
        node->resolved_type = strdup("int");
        break;
    }
}

static void analyze_struct_init(SemaCtx *ctx, ASTNode *node) {
    StructEntry *st;
    int i;
    if (!node || node->type != AST_STRUCT_INIT)
        return;

    st = lookup_struct(ctx, node->as.struct_init.type_name);
    if (!st) {
        sema_error(ctx, node->line, "unknown struct type '%s'.", node->as.struct_init.type_name);
        return;
    }

    if (scope_lookup(ctx->current_scope, node->as.struct_init.var_name)) {
        sema_error(ctx, node->line, "variable '%s' is already declared in this scope.", node->as.struct_init.var_name);
        return;
    }

    if (node->as.struct_init.named) {
        int *seen = NULL;
        if (st->field_count > 0)
            seen = (int *)calloc((size_t)st->field_count, sizeof(int));
        if (node->as.struct_init.value_count != st->field_count) {
            sema_error(ctx, node->line, "struct '%s' expects %d field value(s), got %d.", st->name, st->field_count, node->as.struct_init.value_count);
        }
        for (i = 0; i < node->as.struct_init.value_count; i++) {
            StructField *field = lookup_struct_field(st, node->as.struct_init.field_names[i]);
            int field_index = -1;
            int j;
            if (!field) {
                sema_error(ctx, node->line, "struct '%s' has no field '%s'.", st->name, node->as.struct_init.field_names[i]);
                continue;
            }
            for (j = 0; j < st->field_count; j++) {
                if (&st->fields[j] == field) {
                    field_index = j;
                    break;
                }
            }
            if (seen && field_index >= 0 && seen[field_index]) {
                sema_error(ctx, node->line, "field '%s' initialized more than once.", field->name);
                continue;
            }
            if (seen && field_index >= 0)
                seen[field_index] = 1;
            analyze_expr(ctx, node->as.struct_init.values[i]);
            if (field->type_name) {
                if (strcmp(field->type_name, node->as.struct_init.values[i]->resolved_type) != 0) {
                    sema_error(ctx, node->line, "field '%s' of struct '%s' expects '%s', got '%s'.",
                               field->name, st->name, field->type_name, node->as.struct_init.values[i]->resolved_type);
                }
            } else {
                free(field->type_name);
                field->type_name = strdup(node->as.struct_init.values[i]->resolved_type);
            }
        }
        free(seen);
    } else {
        if (node->as.struct_init.value_count != st->field_count) {
            sema_error(ctx, node->line, "struct '%s' expects %d field value(s), got %d.", st->name, st->field_count, node->as.struct_init.value_count);
        }
        for (i = 0; i < node->as.struct_init.value_count && i < st->field_count; i++) {
            StructField *field = &st->fields[i];
            analyze_expr(ctx, node->as.struct_init.values[i]);
            if (field->type_name) {
                if (strcmp(field->type_name, node->as.struct_init.values[i]->resolved_type) != 0) {
                    sema_error(ctx, node->line, "field '%s' of struct '%s' expects '%s', got '%s'.",
                               field->name, st->name, field->type_name, node->as.struct_init.values[i]->resolved_type);
                }
            } else {
                field->type_name = strdup(node->as.struct_init.values[i]->resolved_type);
            }
        }
    }

    scope_add_var(ctx, ctx->current_scope, node->as.struct_init.var_name, node->as.struct_init.type_name, node->line);
    node->resolved_type = strdup(node->as.struct_init.type_name);
}

/* Analyze a statement node */
static void analyze_node(SemaCtx *ctx, ASTNode *node) {
    if (!node)
        return;
    switch (node->type) {
    case AST_STRUCT_DECL:
        break;
    case AST_STRUCT_INIT:
        analyze_struct_init(ctx, node);
        break;
    case AST_VAR_DECL:
        analyze_expr(ctx, node->as.var_decl.init);
        scope_add_var(ctx, ctx->current_scope, node->as.var_decl.name, node->as.var_decl.init && node->as.var_decl.init->resolved_type ? node->as.var_decl.init->resolved_type : "int", node->line);
        break;
    case AST_ASSIGN:
        {
            VarEntry *v = scope_lookup(ctx->current_scope, node->as.assign.name);
            analyze_expr(ctx, node->as.assign.value);
            if (!v) {
                sema_error(ctx, node->line, "undeclared variable '%s'. Declare it before assignment.", node->as.assign.name);
            } else if (is_struct_type(ctx, v->type_name)) {
                sema_error(ctx, node->line, "assignment to struct variable '%s' is not supported yet.", node->as.assign.name);
            }
            if (node->as.assign.value && node->as.assign.value->resolved_type && is_struct_type(ctx, node->as.assign.value->resolved_type)) {
                sema_error(ctx, node->line, "struct values cannot be assigned directly yet.");
            }
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

static void register_structs_in_node(SemaCtx *ctx, ASTNode *node) {
    int i;
    if (!node)
        return;
    switch (node->type) {
    case AST_STRUCT_DECL:
        sema_register_struct(ctx, node);
        break;
    case AST_PROGRAM:
        for (i = 0; i < node->as.program.count; i++)
            register_structs_in_node(ctx, node->as.program.functions[i]);
        break;
    case AST_FUNCTION:
        register_structs_in_node(ctx, node->as.function.body);
        break;
    case AST_BLOCK:
        for (i = 0; i < node->as.block.count; i++)
            register_structs_in_node(ctx, node->as.block.stmts[i]);
        break;
    case AST_IF:
        register_structs_in_node(ctx, node->as.if_stmt.then_block);
        register_structs_in_node(ctx, node->as.if_stmt.else_block);
        break;
    case AST_WHILE:
        register_structs_in_node(ctx, node->as.while_stmt.body);
        break;
    case AST_FOR:
        register_structs_in_node(ctx, node->as.for_stmt.init);
        register_structs_in_node(ctx, node->as.for_stmt.body);
        break;
    case AST_DO_WHILE:
        register_structs_in_node(ctx, node->as.while_stmt.body);
        break;
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
        scope_add_var(ctx, ctx->current_scope, node->as.function.params[i], "int", node->line);

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
    for (i = 0; i < ctx->struct_count; i++) {
        int j;
        free(ctx->structs[i].name);
        for (j = 0; j < ctx->structs[i].field_count; j++) {
            free(ctx->structs[i].fields[j].name);
            free(ctx->structs[i].fields[j].type_name);
        }
        free(ctx->structs[i].fields);
    }
    free(ctx->structs);
    ctx->structs = NULL;
    ctx->struct_count = 0;
}

/* Public entry point: analyze the entire program */
int sema_analyze(ASTNode *program) {
    SemaCtx ctx;
    int i;

    /* Initialize context */
    ctx.functions = NULL;
    ctx.func_count = 0;
    ctx.func_cap = 0;
    ctx.structs = NULL;
    ctx.struct_count = 0;
    ctx.struct_cap = 0;
    ctx.current_scope = NULL;
    ctx.had_error = 0;

    /* First pass: register all structs, functions, and extern declarations */
    register_structs_in_node(&ctx, program);
    for (i = 0; i < program->as.program.count; i++) {
        ASTNode *node = program->as.program.functions[i];
        if (node->type == AST_FUNCTION) {
            register_function(&ctx, node->as.function.name,
                              node->as.function.param_count, node->line);
        } else if (node->type == AST_EXTERN_FUNC) {
            /* Register extern functions so they can be called */
            register_function(&ctx, node->as.extern_func.name,
                              node->as.extern_func.param_count, node->line);
        }
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
