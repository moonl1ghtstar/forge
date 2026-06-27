/*
 * ast.c - AST node creation and destruction for the Forge compiler
 *
 * Provides factory functions for each AST node type and recursive
 * deallocation to prevent memory leaks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helix-ast.h"

/* Allocate and zero-initialize a new AST node */
ASTNode *ast_new(ASTNodeType type, int line) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Forge error: out of memory\n");
        exit(1);
    }
    node->type = type;
    node->line = line;
    node->resolved_type = NULL;
    return node;
}

/* Recursively free all memory associated with an AST node */
void ast_free(ASTNode *node) {
    if (!node)
        return;

    switch (node->type) {
    case AST_PROGRAM: {
        int i;
        for (i = 0; i < node->as.program.count; i++)
            ast_free(node->as.program.functions[i]);
        free(node->as.program.functions);
        break;
    }
    case AST_FUNCTION: {
        int i;
        free(node->as.function.name);
        for (i = 0; i < node->as.function.param_count; i++)
            free(node->as.function.params[i]);
        free(node->as.function.params);
        ast_free(node->as.function.body);
        break;
    }
    case AST_BLOCK: {
        int i;
        for (i = 0; i < node->as.block.count; i++)
            ast_free(node->as.block.stmts[i]);
        free(node->as.block.stmts);
        break;
    }
    case AST_VAR_DECL:
        free(node->as.var_decl.name);
        ast_free(node->as.var_decl.init);
        break;
    case AST_ASSIGN:
        free(node->as.assign.name);
        ast_free(node->as.assign.value);
        break;
    case AST_IF:
        ast_free(node->as.if_stmt.cond);
        ast_free(node->as.if_stmt.then_block);
        ast_free(node->as.if_stmt.else_block);
        break;
    case AST_WHILE:
        ast_free(node->as.while_stmt.cond);
        ast_free(node->as.while_stmt.body);
        break;
    case AST_RETURN:
        ast_free(node->as.return_stmt.expr);
        break;
    case AST_BINARY:
        ast_free(node->as.binary.left);
        ast_free(node->as.binary.right);
        break;
    case AST_UNARY:
        ast_free(node->as.unary.operand);
        break;
    case AST_NUMBER:
        break; /* no dynamic memory */
    case AST_STRING:
        free(node->as.string.value);
        break;
    case AST_VAR:
        free(node->as.var.name);
        break;
    case AST_CALL: {
        int i;
        free(node->as.call.name);
        for (i = 0; i < node->as.call.arg_count; i++)
            ast_free(node->as.call.args[i]);
        free(node->as.call.args);
        break;
    }
    case AST_EXTERN_FUNC: {
        int i;
        free(node->as.extern_func.name);
        for (i = 0; i < node->as.extern_func.param_count; i++)
            free(node->as.extern_func.param_types[i]);
        free(node->as.extern_func.param_types);
        free(node->as.extern_func.return_type);
        break;
    }
    case AST_EXPR_STMT:
        ast_free(node->as.expr_stmt.expr);
        break;
    case AST_FOR:
        ast_free(node->as.for_stmt.init);
        ast_free(node->as.for_stmt.cond);
        ast_free(node->as.for_stmt.incr);
        ast_free(node->as.for_stmt.body);
        break;
    case AST_DO_WHILE:
        ast_free(node->as.while_stmt.cond);
        ast_free(node->as.while_stmt.body);
        break;
    case AST_BREAK:
    case AST_PASS:
        break;
    }

    free(node->resolved_type);
    free(node);
}

/* Create an empty program node */
ASTNode *ast_program(void) {
    ASTNode *node = ast_new(AST_PROGRAM, 1);
    node->as.program.functions = NULL;
    node->as.program.count = 0;
    return node;
}

/* Create a function definition node */
ASTNode *ast_function(char *name, char **params, int param_count, ASTNode *body, int line) {
    ASTNode *node = ast_new(AST_FUNCTION, line);
    node->as.function.name = name;
    node->as.function.params = params;
    node->as.function.param_count = param_count;
    node->as.function.body = body;
    return node;
}

/* Create an empty block node */
ASTNode *ast_block(int line) {
    ASTNode *node = ast_new(AST_BLOCK, line);
    node->as.block.stmts = NULL;
    node->as.block.count = 0;
    return node;
}

/* Create a variable declaration node */
ASTNode *ast_var_decl(char *name, ASTNode *init, int line) {
    ASTNode *node = ast_new(AST_VAR_DECL, line);
    node->as.var_decl.name = name;
    node->as.var_decl.init = init;
    return node;
}

/* Create a variable assignment node */
ASTNode *ast_assign(char *name, ASTNode *value, int line) {
    ASTNode *node = ast_new(AST_ASSIGN, line);
    node->as.assign.name = name;
    node->as.assign.value = value;
    return node;
}

/* Create an if/else statement node */
ASTNode *ast_if(ASTNode *cond, ASTNode *then_block, ASTNode *else_block, int line) {
    ASTNode *node = ast_new(AST_IF, line);
    node->as.if_stmt.cond = cond;
    node->as.if_stmt.then_block = then_block;
    node->as.if_stmt.else_block = else_block;
    return node;
}

/* Create a while loop node */
ASTNode *ast_while(ASTNode *cond, ASTNode *body, int line) {
    ASTNode *node = ast_new(AST_WHILE, line);
    node->as.while_stmt.cond = cond;
    node->as.while_stmt.body = body;
    return node;
}

/* Create a return statement node */
ASTNode *ast_return(ASTNode *expr, int line) {
    ASTNode *node = ast_new(AST_RETURN, line);
    node->as.return_stmt.expr = expr;
    return node;
}

/* Create a binary expression node */
ASTNode *ast_binary(ASTNode *left, BinaryOp op, ASTNode *right, int line) {
    ASTNode *node = ast_new(AST_BINARY, line);
    node->as.binary.left = left;
    node->as.binary.op = op;
    node->as.binary.right = right;
    return node;
}

/* Create a unary expression node */
ASTNode *ast_unary(UnaryOp op, ASTNode *operand, int line) {
    ASTNode *node = ast_new(AST_UNARY, line);
    node->as.unary.op = op;
    node->as.unary.operand = operand;
    return node;
}

/* Create a number literal node */
ASTNode *ast_number(int value, int line) {
    ASTNode *node = ast_new(AST_NUMBER, line);
    node->as.number.value = value;
    return node;
}

/* Create a string literal node */
ASTNode *ast_string(char *value, int line) {
    ASTNode *node = ast_new(AST_STRING, line);
    node->as.string.value = value;
    return node;
}

/* Create a variable reference node */
ASTNode *ast_var(char *name, int line) {
    ASTNode *node = ast_new(AST_VAR, line);
    node->as.var.name = name;
    return node;
}

/* Create a function call node */
ASTNode *ast_call(char *name, ASTNode **args, int arg_count, int line) {
    ASTNode *node = ast_new(AST_CALL, line);
    node->as.call.name = name;
    node->as.call.args = args;
    node->as.call.arg_count = arg_count;
    return node;
}

/* Create an extern function declaration node (FFI) */
ASTNode *ast_extern_func(char *name, char **param_types, int param_count, char *return_type, int line) {
    ASTNode *node = ast_new(AST_EXTERN_FUNC, line);
    node->as.extern_func.name = name;
    node->as.extern_func.param_types = param_types;
    node->as.extern_func.param_count = param_count;
    node->as.extern_func.return_type = return_type;
    return node;
}

/* Create an expression statement node */
ASTNode *ast_expr_stmt(ASTNode *expr, int line) {
    ASTNode *node = ast_new(AST_EXPR_STMT, line);
    node->as.expr_stmt.expr = expr;
    return node;
}

ASTNode *ast_for(ASTNode *init, ASTNode *cond, ASTNode *incr, ASTNode *body, int line) {
    ASTNode *node = ast_new(AST_FOR, line);
    node->as.for_stmt.init = init;
    node->as.for_stmt.cond = cond;
    node->as.for_stmt.incr = incr;
    node->as.for_stmt.body = body;
    return node;
}

ASTNode *ast_do_while(ASTNode *cond, ASTNode *body, int line) {
    ASTNode *node = ast_new(AST_DO_WHILE, line);
    node->as.while_stmt.cond = cond;
    node->as.while_stmt.body = body;
    return node;
}

ASTNode *ast_break(int line) { return ast_new(AST_BREAK, line); }
ASTNode *ast_pass(int line) { return ast_new(AST_PASS, line); }

/* Append a function to a program node's function list */
void program_add_function(ASTNode *prog, ASTNode *func) {
    prog->as.program.count++;
    prog->as.program.functions = (ASTNode **)realloc(
        prog->as.program.functions,
        sizeof(ASTNode *) * prog->as.program.count);
    prog->as.program.functions[prog->as.program.count - 1] = func;
}

/* Append a statement to a block node's statement list */
void block_add_stmt(ASTNode *block, ASTNode *stmt) {
    block->as.block.count++;
    block->as.block.stmts = (ASTNode **)realloc(
        block->as.block.stmts,
        sizeof(ASTNode *) * block->as.block.count);
    block->as.block.stmts[block->as.block.count - 1] = stmt;
}

/* Prepend all statements from prefix block into block */
void block_prepend_block(ASTNode *block, ASTNode *prefix) {
    int prefix_count;
    ASTNode **new_stmts;
    if (!block || !prefix || prefix->as.block.count == 0)
        return;

    prefix_count = prefix->as.block.count;
    new_stmts = (ASTNode **)realloc(
        block->as.block.stmts,
        sizeof(ASTNode *) * (block->as.block.count + prefix_count));
    if (!new_stmts)
        return;

    memmove(
        new_stmts + prefix_count,
        new_stmts,
        sizeof(ASTNode *) * block->as.block.count);
    memcpy(new_stmts, prefix->as.block.stmts, sizeof(ASTNode *) * prefix_count);

    block->as.block.stmts = new_stmts;
    block->as.block.count += prefix_count;

    free(prefix->as.block.stmts);
    prefix->as.block.stmts = NULL;
    prefix->as.block.count = 0;
}
