/*
 * ast.h - Abstract Syntax Tree node definitions for the Forge compiler
 *
 * Defines all AST node types used to represent Helix programs.
 * Uses a tagged union structure for type-safe node access.
 * Designed with future dynamic type support in mind.
 */

#ifndef FORGE_AST_H
#define FORGE_AST_H

/* Maximum number of children/elements per node (simplifies allocation) */
#define AST_MAX_CHILDREN 256

/* AST node type enumeration */
typedef enum {
    AST_PROGRAM,       /* Top-level program: list of functions */
    AST_FUNCTION,      /* Function definition */
    AST_BLOCK,         /* Block of statements { ... } */
    AST_VAR_DECL,      /* Variable declaration: var x = expr; */
    AST_ASSIGN,        /* Variable assignment: x = expr; */
    AST_IF,            /* If/else statement */
    AST_WHILE,         /* While loop */
    AST_RETURN,        /* Return statement */
    AST_BINARY,        /* Binary expression: lhs op rhs */
    AST_UNARY,         /* Unary expression: op operand */
    AST_NUMBER,        /* Integer literal */
    AST_STRING,        /* String literal (for FFI) */
    AST_VAR,           /* Variable reference */
    AST_CALL,          /* Function call */
    AST_EXTERN_FUNC,   /* Extern function declaration (FFI) */
    AST_EXPR_STMT,     /* Expression statement */
    AST_FOR,
    AST_DO_WHILE,
    AST_BREAK,
    AST_PASS
} ASTNodeType;

/* Binary operator types */
typedef enum {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV,
    BIN_EQ, BIN_NEQ, BIN_LT, BIN_GT, BIN_LE, BIN_GE
} BinaryOp;

/* Unary operator types */
typedef enum {
    UNARY_NEG,  /* -x */
    UNARY_NOT   /* !x */
} UnaryOp;

/* Forward declaration */
typedef struct ASTNode ASTNode;

/*
 * AST node structure using a tagged union.
 * The 'type' field determines which union member is valid.
 * 'resolved_type' stores the type determined by semantic analysis (currently always "int").
 */
struct ASTNode {
    ASTNodeType type;
    char *resolved_type;  /* Type name after semantic analysis (e.g., "int") */
    int line;             /* Source line for error reporting */

    union {
        /* AST_PROGRAM: top-level list of function definitions */
        struct {
            ASTNode **functions;
            int count;
        } program;

        /* AST_FUNCTION: function name, parameter names, body block */
        struct {
            char *name;
            char **params;
            int param_count;
            ASTNode *body;
        } function;

        /* AST_BLOCK: list of statements */
        struct {
            ASTNode **stmts;
            int count;
        } block;

        /* AST_VAR_DECL: variable name and initializer expression */
        struct {
            char *name;
            ASTNode *init;
        } var_decl;

        /* AST_ASSIGN: variable assignment (name = expr) */
        struct {
            char *name;
            ASTNode *value;
        } assign;

        /* AST_IF: condition, then-block, optional else-block */
        struct {
            ASTNode *cond;
            ASTNode *then_block;
            ASTNode *else_block;  /* NULL if no else clause */
        } if_stmt;

        /* AST_WHILE: condition and loop body */
        struct {
            ASTNode *cond;
            ASTNode *body;
        } while_stmt;

        /* AST_RETURN: return expression */
        struct {
            ASTNode *expr;
        } return_stmt;

        /* AST_BINARY: left operand, operator, right operand */
        struct {
            ASTNode *left;
            BinaryOp op;
            ASTNode *right;
        } binary;

        /* AST_UNARY: operator and operand */
        struct {
            UnaryOp op;
            ASTNode *operand;
        } unary;

        /* AST_NUMBER: integer literal value */
        struct {
            int value;
        } number;

        /* AST_STRING: string literal content */
        struct {
            char *value;
        } string;

        /* AST_VAR: variable name reference */
        struct {
            char *name;
        } var;

        /* AST_CALL: function name and argument list */
        struct {
            char *name;
            ASTNode **args;
            int arg_count;
        } call;

        /* AST_EXTERN_FUNC: extern function declaration with param types */
        struct {
            char *name;
            char **param_types;
            int param_count;
            char *return_type;
        } extern_func;

        /* AST_EXPR_STMT: expression statement */
        struct {
            ASTNode *expr;
        } expr_stmt;

        /* AST_FOR: for(init; cond; incr) body */
        struct {
            ASTNode *init;
            ASTNode *cond;
            ASTNode *incr;
            ASTNode *body;
        } for_stmt;
    } as;
};

/* Create a new AST node of the given type */
ASTNode *ast_new(ASTNodeType type, int line);

/* Recursively free an AST node and all its children */
void ast_free(ASTNode *node);

/* Utility: create specific node types */
ASTNode *ast_program(void);
ASTNode *ast_function(char *name, char **params, int param_count, ASTNode *body, int line);
ASTNode *ast_block(int line);
ASTNode *ast_var_decl(char *name, ASTNode *init, int line);
ASTNode *ast_assign(char *name, ASTNode *value, int line);
ASTNode *ast_if(ASTNode *cond, ASTNode *then_block, ASTNode *else_block, int line);
ASTNode *ast_while(ASTNode *cond, ASTNode *body, int line);
ASTNode *ast_return(ASTNode *expr, int line);
ASTNode *ast_binary(ASTNode *left, BinaryOp op, ASTNode *right, int line);
ASTNode *ast_unary(UnaryOp op, ASTNode *operand, int line);
ASTNode *ast_number(int value, int line);
ASTNode *ast_string(char *value, int line);
ASTNode *ast_var(char *name, int line);
ASTNode *ast_call(char *name, ASTNode **args, int arg_count, int line);
ASTNode *ast_extern_func(char *name, char **param_types, int param_count, char *return_type, int line);
ASTNode *ast_expr_stmt(ASTNode *expr, int line);
ASTNode *ast_for(ASTNode *init, ASTNode *cond, ASTNode *incr, ASTNode *body, int line);
ASTNode *ast_do_while(ASTNode *cond, ASTNode *body, int line);
ASTNode *ast_break(int line);
ASTNode *ast_pass(int line);

/* Add a function node to a program node */
void program_add_function(ASTNode *prog, ASTNode *func);

/* Add a statement to a block node */
void block_add_stmt(ASTNode *block, ASTNode *stmt);

/* Prepend all statements from prefix block into block */
void block_prepend_block(ASTNode *block, ASTNode *prefix);

#endif /* FORGE_AST_H */
