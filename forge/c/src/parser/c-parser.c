/*
 * c-parser.c - Recursive descent parser for Forge C frontend
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c-parser.h"

static void c_parse_error(CParser *p, const char *msg) {
    fprintf(stderr, "Forge C parse error: line %d: %s near %s\n",
            p->current.line, msg, c_token_type_to_string(p->current.type));
    p->had_error = 1;
}

static void advance(CParser *p) {
    c_token_free(&p->previous);
    p->previous = p->current;
    p->current = c_lexer_next(p->lexer);
}

static int check(CParser *p, CTokenType type) {
    return p->current.type == type;
}

static int match(CParser *p, CTokenType type) {
    if (check(p, type)) {
        advance(p);
        return 1;
    }
    return 0;
}

static CToken consume(CParser *p, CTokenType type, const char *msg) {
    if (check(p, type)) {
        advance(p);
        {
            CToken copy = p->previous;
            if (p->previous.lexeme)
                copy.lexeme = strdup(p->previous.lexeme);
            return copy;
        }
    }
    c_parse_error(p, msg);
    {
        CToken copy = p->current;
        if (p->current.lexeme)
            copy.lexeme = strdup(p->current.lexeme);
        return copy;
    }
}

static ASTNode *parse_stmt(CParser *p);
static ASTNode *parse_block(CParser *p);
static ASTNode *parse_expr(CParser *p);

static ASTNode *parse_primary(CParser *p) {
    if (match(p, C_TOK_NUMBER))
        return ast_number(p->previous.value, p->previous.line);

    if (match(p, C_TOK_IDENT)) {
        char *name = strdup(p->previous.lexeme);
        int line = p->previous.line;
        if (match(p, C_TOK_LPAREN)) {
            ASTNode **args = NULL;
            int count = 0;
            if (!check(p, C_TOK_RPAREN)) {
                do {
                    ASTNode *arg = parse_expr(p);
                    args = (ASTNode **)realloc(args, sizeof(ASTNode *) * (count + 1));
                    args[count++] = arg;
                } while (match(p, C_TOK_COMMA));
            }
            consume(p, C_TOK_RPAREN, "expected ')' after call arguments");
            return ast_call(name, args, count, line);
        }
        return ast_var(name, line);
    }

    if (match(p, C_TOK_LPAREN)) {
        ASTNode *expr = parse_expr(p);
        consume(p, C_TOK_RPAREN, "expected ')' after expression");
        return expr;
    }

    c_parse_error(p, "expected expression");
    return ast_number(0, p->current.line);
}

static ASTNode *parse_unary(CParser *p) {
    if (match(p, C_TOK_MINUS))
        return ast_unary(UNARY_NEG, parse_unary(p), p->previous.line);
    if (match(p, C_TOK_NOT))
        return ast_unary(UNARY_NOT, parse_unary(p), p->previous.line);
    return parse_primary(p);
}

static ASTNode *parse_mul(CParser *p) {
    ASTNode *expr = parse_unary(p);
    while (check(p, C_TOK_STAR) || check(p, C_TOK_SLASH)) {
        CTokenType op = p->current.type;
        int line = p->current.line;
        advance(p);
        if (op == C_TOK_STAR)
            expr = ast_binary(expr, BIN_MUL, parse_unary(p), line);
        else
            expr = ast_binary(expr, BIN_DIV, parse_unary(p), line);
    }
    return expr;
}

static ASTNode *parse_add(CParser *p) {
    ASTNode *expr = parse_mul(p);
    while (check(p, C_TOK_PLUS) || check(p, C_TOK_MINUS)) {
        CTokenType op = p->current.type;
        int line = p->current.line;
        advance(p);
        if (op == C_TOK_PLUS)
            expr = ast_binary(expr, BIN_ADD, parse_mul(p), line);
        else
            expr = ast_binary(expr, BIN_SUB, parse_mul(p), line);
    }
    return expr;
}

static ASTNode *parse_cmp(CParser *p) {
    ASTNode *expr = parse_add(p);
    while (check(p, C_TOK_LT) || check(p, C_TOK_GT) || check(p, C_TOK_LE) || check(p, C_TOK_GE)) {
        CTokenType op = p->current.type;
        int line = p->current.line;
        advance(p);
        if (op == C_TOK_LT)
            expr = ast_binary(expr, BIN_LT, parse_add(p), line);
        else if (op == C_TOK_GT)
            expr = ast_binary(expr, BIN_GT, parse_add(p), line);
        else if (op == C_TOK_LE)
            expr = ast_binary(expr, BIN_LE, parse_add(p), line);
        else
            expr = ast_binary(expr, BIN_GE, parse_add(p), line);
    }
    return expr;
}

static ASTNode *parse_eq(CParser *p) {
    ASTNode *expr = parse_cmp(p);
    while (check(p, C_TOK_EQ) || check(p, C_TOK_NEQ)) {
        CTokenType op = p->current.type;
        int line = p->current.line;
        advance(p);
        if (op == C_TOK_EQ)
            expr = ast_binary(expr, BIN_EQ, parse_cmp(p), line);
        else
            expr = ast_binary(expr, BIN_NEQ, parse_cmp(p), line);
    }
    return expr;
}

static ASTNode *parse_assign(CParser *p) {
    ASTNode *expr = parse_eq(p);
    if (match(p, C_TOK_ASSIGN)) {
        int line = p->previous.line;
        if (expr->type != AST_VAR) {
            c_parse_error(p, "left side of assignment must be variable");
            return expr;
        }
        return ast_assign(strdup(expr->as.var.name), parse_assign(p), line);
    }
    return expr;
}

static ASTNode *parse_expr(CParser *p) {
    return parse_assign(p);
}

static ASTNode *parse_var_decl(CParser *p, int line) {
    CToken name = consume(p, C_TOK_IDENT, "expected variable name");
    ASTNode *init = NULL;
    if (match(p, C_TOK_ASSIGN))
        init = parse_expr(p);
    else
        init = ast_number(0, line);
    consume(p, C_TOK_SEMICOLON, "expected ';' after variable declaration");
    return ast_var_decl(name.lexeme, init, line);
}

static ASTNode *parse_return_stmt(CParser *p) {
    int line = p->current.line;
    advance(p);
    if (check(p, C_TOK_SEMICOLON)) {
        advance(p);
        return ast_return(ast_number(0, line), line);
    }
    {
        ASTNode *expr = parse_expr(p);
        consume(p, C_TOK_SEMICOLON, "expected ';' after return value");
        return ast_return(expr, line);
    }
}

static ASTNode *parse_if_stmt(CParser *p) {
    int line = p->current.line;
    advance(p);
    consume(p, C_TOK_LPAREN, "expected '(' after if");
    {
        ASTNode *cond = parse_expr(p);
        consume(p, C_TOK_RPAREN, "expected ')' after if condition");
        {
            ASTNode *then_block = parse_stmt(p);
            ASTNode *else_block = NULL;
            if (match(p, C_TOK_ELSE))
                else_block = parse_stmt(p);
            return ast_if(cond, then_block, else_block, line);
        }
    }
}

static ASTNode *parse_while_stmt(CParser *p) {
    int line = p->current.line;
    advance(p);
    consume(p, C_TOK_LPAREN, "expected '(' after while");
    {
        ASTNode *cond = parse_expr(p);
        consume(p, C_TOK_RPAREN, "expected ')' after while condition");
        return ast_while(cond, parse_stmt(p), line);
    }
}

static ASTNode *parse_for_stmt(CParser *p) {
    int line = p->current.line;
    advance(p);
    consume(p, C_TOK_LPAREN, "expected '(' after for");
    {
        ASTNode *init = NULL;
        ASTNode *cond = NULL;
        ASTNode *incr = NULL;
        if (!check(p, C_TOK_SEMICOLON)) {
            if (match(p, C_TOK_INT)) {
                int decl_line = p->previous.line;
                CToken name = consume(p, C_TOK_IDENT, "expected variable name");
                ASTNode *init_expr = NULL;
                if (match(p, C_TOK_ASSIGN))
                    init_expr = parse_expr(p);
                else
                    init_expr = ast_number(0, decl_line);
                init = ast_var_decl(name.lexeme, init_expr, decl_line);
            } else {
                init = parse_expr(p);
            }
        }
        consume(p, C_TOK_SEMICOLON, "expected ';' after for initializer");
        if (!check(p, C_TOK_SEMICOLON))
            cond = parse_expr(p);
        consume(p, C_TOK_SEMICOLON, "expected ';' after for condition");
        if (!check(p, C_TOK_RPAREN))
            incr = parse_expr(p);
        consume(p, C_TOK_RPAREN, "expected ')' after for clauses");
        return ast_for(init, cond, incr, parse_stmt(p), line);
    }
}

static ASTNode *parse_expr_stmt(CParser *p) {
    ASTNode *expr = parse_expr(p);
    consume(p, C_TOK_SEMICOLON, "expected ';' after expression");
    return ast_expr_stmt(expr, expr ? expr->line : p->current.line);
}

static ASTNode *parse_block(CParser *p) {
    ASTNode *block = ast_block(p->current.line);
    consume(p, C_TOK_LBRACE, "expected '{'");
    while (!check(p, C_TOK_RBRACE) && !check(p, C_TOK_EOF)) {
        block_add_stmt(block, parse_stmt(p));
    }
    consume(p, C_TOK_RBRACE, "expected '}' after block");
    return block;
}

static ASTNode *parse_stmt(CParser *p) {
    if (check(p, C_TOK_LBRACE))
        return parse_block(p);
    if (match(p, C_TOK_INT))
        return parse_var_decl(p, p->previous.line);
    if (check(p, C_TOK_RETURN))
        return parse_return_stmt(p);
    if (check(p, C_TOK_IF))
        return parse_if_stmt(p);
    if (check(p, C_TOK_WHILE))
        return parse_while_stmt(p);
    if (check(p, C_TOK_FOR))
        return parse_for_stmt(p);
    if (match(p, C_TOK_BREAK)) {
        int line = p->previous.line;
        consume(p, C_TOK_SEMICOLON, "expected ';' after break");
        return ast_break(line);
    }
    if (match(p, C_TOK_CONTINUE)) {
        int line = p->previous.line;
        consume(p, C_TOK_SEMICOLON, "expected ';' after continue");
        return ast_pass(line);
    }
    return parse_expr_stmt(p);
}

static ASTNode *parse_function(CParser *p) {
    int line = p->current.line;
    ASTNode **params = NULL;
    int param_count = 0;
    CToken name;
    ASTNode *body;

    consume(p, C_TOK_INT, "expected return type 'int'");
    name = consume(p, C_TOK_IDENT, "expected function name");
    consume(p, C_TOK_LPAREN, "expected '(' after function name");
    if (!check(p, C_TOK_RPAREN)) {
        do {
            consume(p, C_TOK_INT, "expected parameter type 'int'");
            {
                CToken param = consume(p, C_TOK_IDENT, "expected parameter name");
                ASTNode *dummy = ast_var(param.lexeme, param.line);
                params = (ASTNode **)realloc(params, sizeof(ASTNode *) * (param_count + 1));
                params[param_count++] = dummy;
            }
        } while (match(p, C_TOK_COMMA));
    }
    consume(p, C_TOK_RPAREN, "expected ')' after parameters");
    body = parse_block(p);
    {
        char **names = NULL;
        int i;
        if (param_count > 0) {
            names = (char **)calloc((size_t)param_count, sizeof(char *));
            for (i = 0; i < param_count; i++) {
                names[i] = strdup(params[i]->as.var.name);
                ast_free(params[i]);
            }
        }
        free(params);
        return ast_function(name.lexeme, names, param_count, body, line);
    }
}

void c_parser_init(CParser *parser, CLexer *lexer) {
    parser->lexer = lexer;
    parser->had_error = 0;
    parser->current.lexeme = NULL;
    parser->previous.lexeme = NULL;
    advance(parser);
}

ASTNode *c_parse_program(CParser *p) {
    ASTNode *prog = ast_program();
    ASTNode *entry_block = NULL;
    int entry_line = 1;
    ASTNode *main_func = NULL;
    int i;

    while (!check(p, C_TOK_EOF)) {
        if (check(p, C_TOK_INT)) {
            ASTNode *func = parse_function(p);
            if (func)
                program_add_function(prog, func);
        } else {
            int stmt_line = p->current.line;
            ASTNode *stmt = parse_stmt(p);
            if (!entry_block) {
                entry_block = ast_block(stmt_line);
                entry_line = stmt_line;
            }
            block_add_stmt(entry_block, stmt);
        }
    }

    for (i = 0; i < prog->as.program.count; i++) {
        ASTNode *node = prog->as.program.functions[i];
        if (node->type == AST_FUNCTION && strcmp(node->as.function.name, "main") == 0) {
            main_func = node;
            break;
        }
    }

    if (entry_block && entry_block->as.block.count > 0) {
        if (main_func)
            block_prepend_block(main_func->as.function.body, entry_block);
        else {
            block_add_stmt(entry_block, ast_return(ast_number(0, entry_line), entry_line));
            program_add_function(prog, ast_function(strdup("main"), NULL, 0, entry_block, entry_line));
        }
    } else if (!main_func) {
        ASTNode *empty_body = ast_block(1);
        block_add_stmt(empty_body, ast_return(ast_number(0, 1), 1));
        program_add_function(prog, ast_function(strdup("main"), NULL, 0, empty_body, 1));
    }

    return prog;
}
