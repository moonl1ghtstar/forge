/*
 * parser.c - Recursive descent parser for the Forge compiler
 *
 * Implements a hand-written recursive descent parser that transforms
 * a Helix token stream into an AST. Follows operator precedence
 * via layered expression parsing functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// h
#include "helix-parser.h"

#ifdef _WIN32
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void *hModule, char *lpFilename, unsigned long nSize);
#define MAX_PATH 260
#endif

/* ---- Internal helpers ---- */

static void describe_token(const Token *tok, char *buf, size_t buf_size) {
    if (!tok || !buf || buf_size == 0)
        return;

    switch (tok->type) {
    case TOKEN_IDENT:
        snprintf(buf, buf_size, "identifier '%s'", tok->lexeme ? tok->lexeme : "");
        break;
    case TOKEN_STRING:
        snprintf(buf, buf_size, "string \"%s\"", tok->lexeme ? tok->lexeme : "");
        break;
    case TOKEN_NUMBER:
        snprintf(buf, buf_size, "number %d", tok->value);
        break;
    case TOKEN_ERROR:
        snprintf(buf, buf_size, "invalid token (%s)", tok->lexeme ? tok->lexeme : "unknown");
        break;
    case TOKEN_EOF:
        snprintf(buf, buf_size, "end of file");
        break;
    default:
        snprintf(buf, buf_size, "'%s'", token_type_to_string(tok->type));
        break;
    }
}

/* Report a parse error to stderr in the standard Forge format */
static void parse_error(Parser *p, const char *msg) {
    char tok[128];
    describe_token(&p->current, tok, sizeof(tok));
    fprintf(stderr, "Forge parse error: line %d: %s", p->current.line, msg);
    if (p->current.type != TOKEN_EOF)
        fprintf(stderr, " near %s", tok);
    fprintf(stderr, "\n");
    p->had_error = 1;
}

/* Advance to the next token.
 * We intentionally do NOT free the previous token's lexeme here because
 * callers (consume/match) may have already returned a reference to it.
 * The lexeme memory is small and short-lived; we accept the minor leak
 * for simplicity. A production compiler would use arena allocation.
 */
static void advance(Parser *p) {
    token_free(&p->previous);
    p->previous = p->current;
    p->current = lexer_next(p->lexer);
}

/* Check if the current token matches the expected type */
static int check(Parser *p, TokenType type) {
    return p->current.type == type;
}

/* Consume the current token if it matches; otherwise report an error.
 * The returned token's lexeme is a COPY that the caller must free.
 */
static Token consume(Parser *p, TokenType type, const char *msg) {
    if (check(p, type)) {
        advance(p);
        /* Return a copy with a separately owned lexeme string */
        Token copy = p->previous;
        if (p->previous.lexeme)
            copy.lexeme = strdup(p->previous.lexeme);
        return copy;
    }
    parse_error(p, msg);
    /* Return a copy of the current token as fallback */
    {
        Token copy = p->current;
        if (p->current.lexeme)
            copy.lexeme = strdup(p->current.lexeme);
        return copy;
    }
}

/* Check if the current token matches; if so, consume it and return 1 */
static int match(Parser *p, TokenType type) {
    if (check(p, type)) {
        advance(p);
        return 1;
    }
    return 0;
}

static int peek_token_type(Parser *p, int steps, TokenType *type_out, char **lexeme_out) {
    Lexer saved_lexer = *p->lexer;
    Token tok;
    int i;

    tok.lexeme = NULL;
    tok.type = TOKEN_EOF;
    tok.line = p->current.line;
    tok.value = 0;
    for (i = 0; i < steps; i++) {
        Token next = lexer_next(&saved_lexer);
        token_free(&tok);
        tok = next;
    }

    if (type_out)
        *type_out = tok.type;
    if (lexeme_out && tok.lexeme)
        *lexeme_out = strdup(tok.lexeme);
    else if (lexeme_out)
        *lexeme_out = NULL;

    token_free(&tok);
    return 0;
}

/* ---- Forward declarations for recursive descent ---- */
static ASTNode *parse_function(Parser *p);
static ASTNode *parse_struct_decl(Parser *p);
static ASTNode *parse_block(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_var_decl(Parser *p);
static ASTNode *parse_if_stmt(Parser *p);
static ASTNode *parse_while_stmt(Parser *p);
static ASTNode *parse_return_stmt(Parser *p);
static void parse_import_stmt(Parser *p, ASTNode *prog);
static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_compare(Parser *p);
static ASTNode *parse_add_sub(Parser *p);
static ASTNode *parse_mul_div(Parser *p);
static ASTNode *parse_unary(Parser *p);
static ASTNode *parse_primary(Parser *p);
static ASTNode *parse_postfix(Parser *p, ASTNode *base, int line);

static ASTNode *parse_for_stmt(Parser *p);
static ASTNode *parse_do_stmt(Parser *p);
static ASTNode *parse_switch_stmt(Parser *p);
static ASTNode *parse_loop_ctrl_stmt(Parser *p);
static void parse_extern_block(Parser *p, ASTNode *prog);
static char *read_file(const char *path, int is_system);
static void append_program(ASTNode *dst, ASTNode *src);
static int load_module(Parser *p, ASTNode *prog, const char *module_name, int is_system, char **select_funcs, int select_count);
static char *dup_qualified_name(const char *module_name, const char *member_name);
static int peek_token_type(Parser *p, int steps, TokenType *type_out, char **lexeme_out);

/* ---- Public API ---- */

/* Initialize the parser and read the first token */
void parser_init(Parser *parser, Lexer *lexer) {
    parser->lexer = lexer;
    parser->had_error = 0;
    parser->current.lexeme = NULL;
    parser->previous.lexeme = NULL;
    advance(parser); /* prime the lookahead */
}

/* Parse a complete program (sequence of functions, extern blocks, and
 * optional top-level statements. Top-level statements are wrapped into an
 * implicit main() function so programs can run without defining main. */
ASTNode *parse_program(Parser *p) {
    ASTNode *prog = ast_program();
    ASTNode *entry_block = NULL;
    int entry_line = 1;
    ASTNode *main_func = NULL;
    int i;

    while (!check(p, TOKEN_EOF)) {
        if (match(p, TOKEN_IMPORT)) {
            parse_import_stmt(p, prog);
        } else if (match(p, TOKEN_EXTERN)) {
            parse_extern_block(p, prog);
        } else if (check(p, TOKEN_STRUCT)) {
            ASTNode *decl = parse_struct_decl(p);
            if (decl)
                program_add_function(prog, decl);
        } else if (check(p, TOKEN_FUNCTION)) {
            ASTNode *func = parse_function(p);
            if (func)
                program_add_function(prog, func);
        } else {
            int stmt_line = p->current.line;
            ASTNode *stmt = parse_statement(p);
            if (!entry_block) {
                entry_block = ast_block(stmt_line);
                entry_line = stmt_line;
            }
            if (stmt)
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
        if (main_func) {
            block_prepend_block(main_func->as.function.body, entry_block);
        } else {
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

static char *read_file(const char *path, int is_system) {
    FILE *f = NULL;
    long size;
    char *buf;
    char resolved_path[512];

    if (is_system) {
#ifdef _WIN32
        // Only search in the forge.exe directory for system modules
        char exe_dir[MAX_PATH];
        if (GetModuleFileNameA(NULL, exe_dir, MAX_PATH) != 0) {
            char *last_slash = strrchr(exe_dir, '\\');
            if (!last_slash)
                last_slash = strrchr(exe_dir, '/');
            if (last_slash) {
                *last_slash = '\0';
                snprintf(resolved_path, sizeof(resolved_path), "%s/%s", exe_dir, path);
                f = fopen(resolved_path, "rb");
            }
        }
#endif
        if (!f) {
            // Fallback to project directory if GetModuleFileNameA fails or not on Windows
            snprintf(resolved_path, sizeof(resolved_path), "%s", path);
            f = fopen(resolved_path, "rb");
        }
    } else {
        // Only search in the project directory (current working directory) for non-system modules
        snprintf(resolved_path, sizeof(resolved_path), "%s", path);
        f = fopen(resolved_path, "rb");
    }

    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static void append_program(ASTNode *dst, ASTNode *src) {
    int i;
    if (!dst || !src)
        return;
    for (i = 0; i < src->as.program.count; i++) {
        if (src->as.program.functions[i]) {
            program_add_function(dst, src->as.program.functions[i]);
            src->as.program.functions[i] = NULL;
        }
    }
    free(src->as.program.functions);
    src->as.program.functions = NULL;
    src->as.program.count = 0;
}

static char *extract_json_string(const char *json, const char *key) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    const char *pos = strstr(json, search_key);
    if (!pos)
        return NULL;
    pos += strlen(search_key);
    pos = strchr(pos, ':');
    if (!pos)
        return NULL;
    pos++;
    pos = strchr(pos, '"');
    if (!pos)
        return NULL;
    pos++;
    const char *end = strchr(pos, '"');
    if (!end)
        return NULL;
    size_t len = end - pos;
    char *val = (char *)malloc(len + 1);
    memcpy(val, pos, len);
    val[len] = '\0';
    return val;
}

static int extract_json_array(const char *json, const char *key, char ***out_arr) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    const char *pos = strstr(json, search_key);
    if (!pos)
        return 0;
    pos += strlen(search_key);
    pos = strchr(pos, ':');
    if (!pos)
        return 0;
    pos++;
    pos = strchr(pos, '[');
    if (!pos)
        return 0;
    pos++;

    char **arr = NULL;
    int count = 0;

    while (1) {
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',') {
            pos++;
        }
        if (*pos == ']') {
            break;
        }
        if (*pos == '\0') {
            break;
        }
        if (*pos == '"') {
            pos++;
            const char *end = strchr(pos, '"');
            if (!end)
                break;
            size_t len = end - pos;
            char *item = (char *)malloc(len + 1);
            memcpy(item, pos, len);
            item[len] = '\0';

            count++;
            arr = (char **)realloc(arr, sizeof(char *) * count);
            arr[count - 1] = item;
            pos = end + 1;
        } else {
            break;
        }
    }
    *out_arr = arr;
    return count;
}

static char *read_func_file(const char *module_name, const char *source_dir, const char *func_name, int is_system) {
    char rel_path[512];
    char *source = NULL;

    if (source_dir && strlen(source_dir) > 0) {
        if (source_dir[strlen(source_dir) - 1] == '/' || source_dir[strlen(source_dir) - 1] == '\\') {
            snprintf(rel_path, sizeof(rel_path), "module/%s/%s%s.hlx", module_name, source_dir, func_name);
        } else {
            snprintf(rel_path, sizeof(rel_path), "module/%s/%s/%s.hlx", module_name, source_dir, func_name);
        }
    } else {
        snprintf(rel_path, sizeof(rel_path), "module/%s/%s.hlx", module_name, func_name);
    }
    source = read_file(rel_path, is_system);

    if (!source) {
        if (source_dir && strlen(source_dir) > 0) {
            if (source_dir[strlen(source_dir) - 1] == '/' || source_dir[strlen(source_dir) - 1] == '\\') {
                snprintf(rel_path, sizeof(rel_path), "moudule/%s/%s%s.hlx", module_name, source_dir, func_name);
            } else {
                snprintf(rel_path, sizeof(rel_path), "moudule/%s/%s/%s.hlx", module_name, source_dir, func_name);
            }
        } else {
            snprintf(rel_path, sizeof(rel_path), "moudule/%s/%s.hlx", module_name, func_name);
        }
        source = read_file(rel_path, is_system);
    }
    return source;
}

static void rename_single_function(ASTNode *func, const char *module_name) {
    if (!func)
        return;

    const char *base = module_name;
    const char *last_slash = strrchr(module_name, '/');
    if (!last_slash)
        last_slash = strrchr(module_name, '\\');
    if (last_slash)
        base = last_slash + 1;

    char ns_name[256];
    strncpy(ns_name, base, sizeof(ns_name));
    ns_name[sizeof(ns_name) - 1] = '\0';
    char *dot_ptr = strrchr(ns_name, '.');
    if (dot_ptr)
        *dot_ptr = '\0';

    if (func->type == AST_FUNCTION) {
        char *old_name = func->as.function.name;
        func->as.function.name = dup_qualified_name(ns_name, old_name);
        free(old_name);
    } else if (func->type == AST_EXTERN_FUNC) {
        char *old_name = func->as.extern_func.name;
        func->as.extern_func.name = dup_qualified_name(ns_name, old_name);
        free(old_name);
    }
}

static void rename_module_functions(ASTNode *module_prog, const char *module_name) {
    if (!module_prog || module_prog->type != AST_PROGRAM)
        return;
    int i;
    for (i = 0; i < module_prog->as.program.count; i++) {
        rename_single_function(module_prog->as.program.functions[i], module_name);
    }
}

static int load_module(Parser *p, ASTNode *prog, const char *module_name, int is_system, char **select_funcs, int select_count) {
    Lexer lexer;
    Parser sub;
    ASTNode *module_prog;

    if (is_system) {
        char config_path[512];
        snprintf(config_path, sizeof(config_path), "module/%s/config.json", module_name);
        char *config_content = read_file(config_path, is_system);
        if (!config_content) {
            snprintf(config_path, sizeof(config_path), "moudule/%s/config.json", module_name);
            config_content = read_file(config_path, is_system);
        }
        if (!config_content) {
            fprintf(stderr, "Forge error: %d: cannot load module '%s' (config.json missing or unreadable)\n", p->current.line, module_name);
            return 1;
        }

        char *source_dir = extract_json_string(config_content, "source_dir");
        char **module_funcs = NULL;
        int module_func_count = extract_json_array(config_content, "funcs", &module_funcs);
        free(config_content);

        char **funcs_to_load = NULL;
        int load_count = 0;

        if (select_count > 0) {
            int i, j;
            for (i = 0; i < select_count; i++) {
                int found = 0;
                for (j = 0; j < module_func_count; j++) {
                    if (strcmp(select_funcs[i], module_funcs[j]) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    fprintf(stderr, "Forge error: %d: module '%s' does not export function '%s'\n", p->current.line, module_name, select_funcs[i]);
                    if (source_dir)
                        free(source_dir);
                    if (module_funcs) {
                        for (j = 0; j < module_func_count; j++)
                            free(module_funcs[j]);
                        free(module_funcs);
                    }
                    return 1;
                }
            }
            funcs_to_load = select_funcs;
            load_count = select_count;
        } else {
            funcs_to_load = module_funcs;
            load_count = module_func_count;
        }

        int i, j;
        for (i = 0; i < load_count; i++) {
            char *func_name = funcs_to_load[i];
            char *func_source = read_func_file(module_name, source_dir, func_name, is_system);
            if (!func_source) {
                fprintf(stderr, "Forge error: %d: cannot load function '%s' from module '%s'\n", p->current.line, func_name, module_name);
                if (source_dir)
                    free(source_dir);
                if (module_funcs) {
                    for (j = 0; j < module_func_count; j++)
                        free(module_funcs[j]);
                    free(module_funcs);
                }
                return 1;
            }

            lexer_init(&lexer, func_source);
            parser_init(&sub, &lexer);
            module_prog = parse_program(&sub);
            if (sub.had_error) {
                fprintf(stderr, "Forge error: %d: function '%s' in module '%s' has parse errors\n", p->current.line, func_name, module_name);
                ast_free(module_prog);
                free(func_source);
                if (source_dir)
                    free(source_dir);
                if (module_funcs) {
                    for (j = 0; j < module_func_count; j++)
                        free(module_funcs[j]);
                    free(module_funcs);
                }
                return 1;
            }

            rename_module_functions(module_prog, module_name);
            append_program(prog, module_prog);
            ast_free(module_prog);
            free(func_source);
        }

        if (source_dir)
            free(source_dir);
        if (module_funcs) {
            for (j = 0; j < module_func_count; j++)
                free(module_funcs[j]);
            free(module_funcs);
        }
        return 0;
    } else {
        char *project_source = read_file(module_name, is_system);
        if (!project_source) {
            fprintf(stderr, "Forge error: %d: cannot load module '%s'\n", p->current.line, module_name);
            return 1;
        }

        lexer_init(&lexer, project_source);
        parser_init(&sub, &lexer);
        module_prog = parse_program(&sub);
        if (sub.had_error) {
            fprintf(stderr, "Forge error: %d: module '%s' has parse errors\n", p->current.line, module_name);
            ast_free(module_prog);
            free(project_source);
            return 1;
        }

        if (select_count > 0) {
            int i, j;
            for (i = 0; i < select_count; i++) {
                int found = 0;
                for (j = 0; j < module_prog->as.program.count; j++) {
                    ASTNode *func = module_prog->as.program.functions[j];
                    if (func && func->type == AST_FUNCTION && strcmp(func->as.function.name, select_funcs[i]) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    fprintf(stderr, "Forge error: %d: module '%s' does not export function '%s'\n", p->current.line, module_name, select_funcs[i]);
                    ast_free(module_prog);
                    free(project_source);
                    return 1;
                }
            }

            for (i = 0; i < select_count; i++) {
                for (j = 0; j < module_prog->as.program.count; j++) {
                    ASTNode *func = module_prog->as.program.functions[j];
                    if (func && func->type == AST_FUNCTION && strcmp(func->as.function.name, select_funcs[i]) == 0) {
                        rename_single_function(func, module_name);
                        program_add_function(prog, func);
                        module_prog->as.program.functions[j] = NULL;
                    }
                }
            }
            ast_free(module_prog);
            free(project_source);
            return 0;
        } else {
            rename_module_functions(module_prog, module_name);
            append_program(prog, module_prog);
            ast_free(module_prog);
            free(project_source);
            return 0;
        }
    }
}

static void parse_import_stmt(Parser *p, ASTNode *prog) {
    Token name;
    int is_system = 1;
    if (check(p, TOKEN_STRING)) {
        name = consume(p, TOKEN_STRING, "expected module name");
        is_system = 0;
    } else {
        name = consume(p, TOKEN_IDENT, "expected module name after 'import'");
        is_system = 1;
    }
    if (!name.lexeme)
        return;

    char **select_funcs = NULL;
    int select_count = 0;

    if (match(p, TOKEN_LBRACE)) {
        if (!check(p, TOKEN_RBRACE)) {
            do {
                Token func_tok = consume(p, TOKEN_IDENT, "expected function name");
                if (func_tok.lexeme) {
                    select_count++;
                    select_funcs = (char **)realloc(select_funcs, sizeof(char *) * select_count);
                    select_funcs[select_count - 1] = strdup(func_tok.lexeme);
                    token_free(&func_tok);
                }
            } while (match(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RBRACE, "expected '}' after function list");
    }

    if (load_module(p, prog, name.lexeme, is_system, select_funcs, select_count) != 0)
        p->had_error = 1;

    if (select_funcs) {
        int i;
        for (i = 0; i < select_count; i++) {
            free(select_funcs[i]);
        }
        free(select_funcs);
    }

    token_free(&name);

    if (check(p, TOKEN_SEMICOLON)) {
        parse_error(p, "unexpected semicolon after import statement");
        advance(p);
    }
}

static char *dup_qualified_name(const char *module_name, const char *member_name) {
    size_t len = strlen(module_name) + strlen(member_name) + 2;
    char *out = (char *)malloc(len);
    if (!out)
        return NULL;
    snprintf(out, len, "%s_%s", module_name, member_name);
    return out;
}

/* ---- Parsing functions ---- */

/*
 * Parse an extern block: extern { fn name(params) -> type; ... }
 * Each declared function is added directly to the program node.
 */
static void parse_extern_block(Parser *p, ASTNode *prog) {
    consume(p, TOKEN_LBRACE, "expected '{' after 'extern'");
    /* Parse extern function declarations until closing brace */
    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        if (match(p, TOKEN_FUNCTION)) {
            Token name = consume(p, TOKEN_IDENT, "expected function name after 'function'");
            char *fname = strdup(name.lexeme);
            token_free(&name);
            consume(p, TOKEN_LPAREN, "expected '(' after extern function name");

            char **ptypes = NULL;
            int pcount = 0;
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    Token param_name = consume(p, TOKEN_IDENT, "expected parameter name");
                    token_free(&param_name);
                    char *param_type = strdup("int"); /* default type */
                    /* optional : type */
                    if (match(p, TOKEN_COLON)) {
                        Token t_type = consume(p, TOKEN_IDENT, "expected parameter type");
                        free(param_type);
                        param_type = strdup(t_type.lexeme);
                        token_free(&t_type);
                    }
                    pcount++;
                    ptypes = (char **)realloc(ptypes, sizeof(char *) * pcount);
                    ptypes[pcount - 1] = param_type;
                } while (match(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "expected ')' after parameters");

            char *ret_type = strdup("int");
            /* optional -> type (two tokens: - and >) */
            if (check(p, TOKEN_MINUS)) {
                advance(p); /* consume '-' */
                if (check(p, TOKEN_GT)) {
                    advance(p); /* consume '>' */
                    Token rt = consume(p, TOKEN_IDENT, "expected return type after '->'");
                    free(ret_type);
                    ret_type = strdup(rt.lexeme);
                    token_free(&rt);
                }
            }
            consume(p, TOKEN_SEMICOLON, "expected ';' after extern declaration");

            ASTNode *ext = ast_extern_func(fname, ptypes, pcount, ret_type, p->previous.line);
            program_add_function(prog, ext);
        } else {
            advance(p); /* skip unknown tokens */
        }
    }
    consume(p, TOKEN_RBRACE, "expected '}' to close extern block");
}

/* Parse a function definition: function name(params) { body } */
static ASTNode *parse_function(Parser *p) {
    int line = p->current.line;
    consume(p, TOKEN_FUNCTION, "expected 'function'");
    Token name = consume(p, TOKEN_IDENT, "expected function name");
    char *fname = strdup(name.lexeme);

    consume(p, TOKEN_LPAREN, "expected '(' after function name");

    /* Parse parameter list */
    char **params = NULL;
    int param_count = 0;
    if (!check(p, TOKEN_RPAREN)) {
        do {
            Token pname = consume(p, TOKEN_IDENT, "expected parameter name");
            param_count++;
            params = (char **)realloc(params, sizeof(char *) * param_count);
            params[param_count - 1] = strdup(pname.lexeme);
        } while (match(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RPAREN, "expected ')' after parameters");

    ASTNode *body = parse_block(p);
    return ast_function(fname, params, param_count, body, line);
}

static ASTNode *parse_struct_decl(Parser *p) {
    int line = p->current.line;
    Token name;
    char **fields = NULL;
    int field_count = 0;
    char *struct_name;

    consume(p, TOKEN_STRUCT, "expected 'struct'");
    name = consume(p, TOKEN_IDENT, "expected struct name");
    consume(p, TOKEN_LBRACE, "expected '{' after struct name");
    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        consume(p, TOKEN_LET, "expected 'let' in struct body");
        {
            Token field = consume(p, TOKEN_IDENT, "expected field name");
            field_count++;
            fields = (char **)realloc(fields, sizeof(char *) * field_count);
            fields[field_count - 1] = strdup(field.lexeme);
            token_free(&field);
        }
        consume(p, TOKEN_SEMICOLON, "expected ';' after struct field");
    }
    consume(p, TOKEN_RBRACE, "expected '}' after struct body");
    struct_name = strdup(name.lexeme);
    token_free(&name);
    return ast_struct_decl(struct_name, fields, field_count, line);
}

static ASTNode *parse_struct_init(Parser *p, Token type_tok, Token name_tok) {
    int line = p->current.line;
    ASTNode **values = NULL;
    char **field_names = NULL;
    int value_count = 0;
    int named = 0;
    char *type_name = strdup(type_tok.lexeme);
    char *var_name = strdup(name_tok.lexeme);

    consume(p, TOKEN_LBRACE, "expected '{' after struct variable name");
    if (!check(p, TOKEN_RBRACE)) {
        TokenType next_type;
        char *next_lexeme = NULL;
        peek_token_type(p, 1, &next_type, &next_lexeme);
        if (check(p, TOKEN_IDENT) && next_type == TOKEN_ASSIGN) {
            named = 1;
            while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
                Token field = consume(p, TOKEN_IDENT, "expected field name in struct initializer");
                consume(p, TOKEN_ASSIGN, "expected '=' after field name");
                {
                    ASTNode *value = parse_expr(p);
                    value_count++;
                    values = (ASTNode **)realloc(values, sizeof(ASTNode *) * value_count);
                    field_names = (char **)realloc(field_names, sizeof(char *) * value_count);
                    values[value_count - 1] = value;
                    field_names[value_count - 1] = strdup(field.lexeme);
                    token_free(&field);
                }
                if (check(p, TOKEN_SEMICOLON))
                    advance(p);
                else
                    break;
            }
        } else {
            do {
                ASTNode *value = parse_expr(p);
                value_count++;
                values = (ASTNode **)realloc(values, sizeof(ASTNode *) * value_count);
                values[value_count - 1] = value;
            } while (match(p, TOKEN_COMMA));
        }
    }
    consume(p, TOKEN_RBRACE, "expected '}' after struct initializer");
    token_free(&type_tok);
    token_free(&name_tok);
    return ast_struct_init(type_name, var_name, values, field_names, value_count, named, line);
}

/* Parse a block: { statements... } */
static ASTNode *parse_block(Parser *p) {
    int line = p->current.line;
    consume(p, TOKEN_LBRACE, "expected '{'");
    ASTNode *block = ast_block(line);
    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        ASTNode *stmt = parse_statement(p);
        if (stmt)
            block_add_stmt(block, stmt);
    }
    consume(p, TOKEN_RBRACE, "expected '}'");
    return block;
}

/* Dispatch to the appropriate statement parser based on the current token */
static ASTNode *parse_statement(Parser *p) {
    if (check(p, TOKEN_STRUCT))
        return parse_struct_decl(p);
    if (check(p, TOKEN_LET) || check(p, TOKEN_CONST) || check(p, TOKEN_GLOBAL))
        return parse_var_decl(p);
    if (check(p, TOKEN_FOR))
        return parse_for_stmt(p);
    if (check(p, TOKEN_UNTIL)) {
        int line = p->current.line;
        advance(p);
        consume(p, TOKEN_LPAREN, "expected '(' after 'until'");
        ASTNode *cond = parse_expr(p);
        consume(p, TOKEN_RPAREN, "expected ')' after condition");
        ASTNode *body = parse_block(p);
        ASTNode *not_cond = ast_unary(UNARY_NOT, cond, line);
        return ast_while(not_cond, body, line);
    }
    if (check(p, TOKEN_DO))
        return parse_do_stmt(p);
    if (check(p, TOKEN_SWITCH))
        return parse_switch_stmt(p);
    if (check(p, TOKEN_BREAK) || check(p, TOKEN_PASS))
        return parse_loop_ctrl_stmt(p);
    if (check(p, TOKEN_VAR))
        return parse_var_decl(p);
    if (check(p, TOKEN_IF))
        return parse_if_stmt(p);
    if (check(p, TOKEN_WHILE))
        return parse_while_stmt(p);
    if (check(p, TOKEN_RETURN))
        return parse_return_stmt(p);

    /* Assignment statement: IDENT = expr;
     * Check if current token is IDENT and next token is '='.
     * We need to peek ahead without consuming.
     */
    if (check(p, TOKEN_IDENT)) {
        TokenType next_type, third_type;
        char *next_lex = NULL;
        peek_token_type(p, 1, &next_type, &next_lex);
        if (next_type == TOKEN_IDENT) {
            peek_token_type(p, 2, &third_type, NULL);
            if (third_type == TOKEN_LBRACE) {
                Token type_tok = consume(p, TOKEN_IDENT, "expected struct type name");
                Token name_tok = consume(p, TOKEN_IDENT, "expected variable name");
                free(next_lex);
                {
                    ASTNode *si = parse_struct_init(p, type_tok, name_tok);
                    match(p, TOKEN_SEMICOLON); /* optional trailing ; after } */
                    return si;
                }
            }
        }
        free(next_lex);
        /* Save current lexer position for potential rollback */
        Lexer saved_lexer = *p->lexer;
        Token saved_current = p->current;
        Token saved_previous = p->previous;

        /* Read ahead: IDENT, then check for '=' */
        Token ident = lexer_next(p->lexer);
        if (ident.type == TOKEN_ASSIGN) {
            /* It's an assignment */
            char *name = strdup(saved_current.lexeme);
            int line = saved_current.line;
            /* Now we need to parse the expression after '='.
             * The lexer is already past the '=', so we can parse directly.
             * Update parser state to reflect what we've consumed.
             */
            token_free(&ident);
            p->current = lexer_next(p->lexer);
            ASTNode *value = parse_expr(p);
            consume(p, TOKEN_SEMICOLON, "expected ';' after assignment");
            return ast_assign(name, value, line);
        }

        /* Not an assignment; restore lexer state */
        token_free(&ident);
        *p->lexer = saved_lexer;
        p->current = saved_current;
        p->previous = saved_previous;
    }

    /* Expression statement */
    int line = p->current.line;
    ASTNode *expr = parse_expr(p);
    consume(p, TOKEN_SEMICOLON, "expected ';' after expression");
    return ast_expr_stmt(expr, line);
}

/* Parse variable declaration: var name = expr; */
static ASTNode *parse_var_decl(Parser *p) {
    int line = p->current.line;
    if (check(p, TOKEN_GLOBAL)) {
        advance(p);
        /* skip optional type keyword */
        if (check(p, TOKEN_IDENT)) {
            const char *lex = p->current.lexeme;
            if (strcmp(lex, "int") == 0 || strcmp(lex, "str") == 0 ||
                strcmp(lex, "bool") == 0 || strcmp(lex, "float") == 0 ||
                strcmp(lex, "long") == 0) {
                advance(p);
            }
        }
    } else if (check(p, TOKEN_CONST) || check(p, TOKEN_LET)) {
        advance(p);
    } else {
        consume(p, TOKEN_VAR, "expected 'var', 'let', 'const', or 'global'");
    }
    Token name = consume(p, TOKEN_IDENT, "expected variable name");
    consume(p, TOKEN_ASSIGN, "expected '=' after variable name");
    ASTNode *init = parse_expr(p);
    consume(p, TOKEN_SEMICOLON, "expected ';' after variable declaration");
    return ast_var_decl(strdup(name.lexeme), init, line);
}

/* Parse if statement: if (cond) block [else block] */
static ASTNode *parse_if_stmt(Parser *p) {
    int line = p->current.line;
    consume(p, TOKEN_IF, "expected 'if'");
    consume(p, TOKEN_LPAREN, "expected '(' after 'if'");
    ASTNode *cond = parse_expr(p);
    consume(p, TOKEN_RPAREN, "expected ')' after condition");
    ASTNode *then_b = parse_block(p);
    ASTNode *else_b = NULL;
    if (match(p, TOKEN_ELSE)) {
        if (check(p, TOKEN_IF)) {
            ASTNode *elif_block = ast_block(p->current.line);
            block_add_stmt(elif_block, parse_if_stmt(p));
            else_b = elif_block;
        } else {
            else_b = parse_block(p);
        }
    }
    return ast_if(cond, then_b, else_b, line);
}

/* Parse while loop: while (cond) block */
static ASTNode *parse_while_stmt(Parser *p) {
    int line = p->current.line;
    consume(p, TOKEN_WHILE, "expected 'while'");
    consume(p, TOKEN_LPAREN, "expected '(' after 'while'");
    ASTNode *cond = parse_expr(p);
    consume(p, TOKEN_RPAREN, "expected ')' after condition");
    ASTNode *body = parse_block(p);
    return ast_while(cond, body, line);
}

/* Parse return statement: return expr; */
static ASTNode *parse_return_stmt(Parser *p) {
    int line = p->current.line;
    consume(p, TOKEN_RETURN, "expected 'return'");
    if (check(p, TOKEN_SEMICOLON)) {
        advance(p);
        return ast_return(ast_number(0, line), line);
    }
    ASTNode *expr = parse_expr(p);
    consume(p, TOKEN_SEMICOLON, "expected ';' after return expression");
    return ast_return(expr, line);
}

/* ---- Expression parsing (precedence climbing) ---- */

/* Parse equality expressions: expr == compare, expr != compare */
static ASTNode *parse_expr(Parser *p) {
    ASTNode *left = parse_compare(p);
    while (match(p, TOKEN_EQ) || match(p, TOKEN_NEQ)) {
        BinaryOp op = (p->previous.type == TOKEN_EQ) ? BIN_EQ : BIN_NEQ;
        ASTNode *right = parse_compare(p);
        left = ast_binary(left, op, right, p->previous.line);
    }
    return left;
}

/* Parse comparison expressions: add_sub < add_sub, etc. */
static ASTNode *parse_compare(Parser *p) {
    ASTNode *left = parse_add_sub(p);
    while (match(p, TOKEN_LT) || match(p, TOKEN_GT) ||
           match(p, TOKEN_LE) || match(p, TOKEN_GE)) {
        BinaryOp op;
        switch (p->previous.type) {
        case TOKEN_LT:
            op = BIN_LT;
            break;
        case TOKEN_GT:
            op = BIN_GT;
            break;
        case TOKEN_LE:
            op = BIN_LE;
            break;
        case TOKEN_GE:
            op = BIN_GE;
            break;
        default:
            op = BIN_LT;
            break;
        }
        ASTNode *right = parse_add_sub(p);
        left = ast_binary(left, op, right, p->previous.line);
    }
    return left;
}

/* Parse addition and subtraction */
static ASTNode *parse_add_sub(Parser *p) {
    ASTNode *left = parse_mul_div(p);
    while (match(p, TOKEN_PLUS) || match(p, TOKEN_MINUS)) {
        BinaryOp op = (p->previous.type == TOKEN_PLUS) ? BIN_ADD : BIN_SUB;
        ASTNode *right = parse_mul_div(p);
        left = ast_binary(left, op, right, p->previous.line);
    }
    return left;
}

/* Parse multiplication and division */
static ASTNode *parse_mul_div(Parser *p) {
    ASTNode *left = parse_unary(p);
    while (match(p, TOKEN_STAR) || match(p, TOKEN_SLASH)) {
        BinaryOp op = (p->previous.type == TOKEN_STAR) ? BIN_MUL : BIN_DIV;
        ASTNode *right = parse_unary(p);
        left = ast_binary(left, op, right, p->previous.line);
    }
    return left;
}

/* Parse unary operators: -x, !x */
static ASTNode *parse_unary(Parser *p) {
    if (match(p, TOKEN_MINUS)) {
        ASTNode *operand = parse_unary(p);
        return ast_unary(UNARY_NEG, operand, p->previous.line);
    }
    if (match(p, TOKEN_NOT)) {
        ASTNode *operand = parse_unary(p);
        return ast_unary(UNARY_NOT, operand, p->previous.line);
    }
    return parse_primary(p);
}

/* Parse primary expressions: numbers, identifiers, parenthesized exprs, calls */
static ASTNode *parse_primary(Parser *p) {
    /* True / False literals */
    if (match(p, TOKEN_TRUE))
        return ast_number(1, p->previous.line);
    if (match(p, TOKEN_FALSE))
        return ast_number(0, p->previous.line);

    /* Number literal */
    if (match(p, TOKEN_NUMBER)) {
        return ast_number(p->previous.value, p->previous.line);
    }

    /* String literal */
    if (match(p, TOKEN_STRING)) {
        return ast_string(strdup(p->previous.lexeme), p->previous.line);
    }

    /* Parenthesized expression */
    if (match(p, TOKEN_LPAREN)) {
        ASTNode *expr = parse_expr(p);
        consume(p, TOKEN_RPAREN, "expected ')' after expression");
        return expr;
    }

    /* Identifier or function call */
    if (match(p, TOKEN_IDENT)) {
        char *name = strdup(p->previous.lexeme);
        int line = p->previous.line;
        return parse_postfix(p, ast_var(name, line), line);
    }

    parse_error(p, "expected expression");
    advance(p);                             /* skip the problematic token */
    return ast_number(0, p->previous.line); /* return a dummy node */
}

static ASTNode *parse_postfix(Parser *p, ASTNode *base, int line) {
    ASTNode *expr = base;

    while (check(p, TOKEN_DOT)) {
        Token member_name;
        advance(p);
        member_name = consume(p, TOKEN_IDENT, "expected member name after '.'");
        if (expr->type == AST_VAR && check(p, TOKEN_LPAREN)) {
            char *base_name = expr->as.var.name;
            char *resolved = dup_qualified_name(base_name, member_name.lexeme);
            token_free(&member_name);
            ast_free(expr);
            advance(p);
            ASTNode **args = NULL;
            int arg_count = 0;
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    ASTNode *arg = parse_expr(p);
                    arg_count++;
                    args = (ASTNode **)realloc(args, sizeof(ASTNode *) * arg_count);
                    args[arg_count - 1] = arg;
                } while (match(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "expected ')' after arguments");
            return ast_call(resolved, args, arg_count, line);
        }
        expr = ast_field_access(expr, strdup(member_name.lexeme), member_name.line);
        token_free(&member_name);
    }

    if (check(p, TOKEN_LPAREN)) {
        advance(p); /* consume '(' */
        if (expr->type == AST_VAR) {
            char *base_name = expr->as.var.name;
            expr->as.var.name = NULL;
            ast_free(expr);
            ASTNode **args = NULL;
            int arg_count = 0;
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    ASTNode *arg = parse_expr(p);
                    arg_count++;
                    args = (ASTNode **)realloc(args, sizeof(ASTNode *) * arg_count);
                    args[arg_count - 1] = arg;
                } while (match(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "expected ')' after arguments");
            return ast_call(base_name, args, arg_count, line);
        }
    }

    if (check(p, TOKEN_INCREMENT)) {
        advance(p);
        if (expr->type == AST_VAR) {
            char *base_name = expr->as.var.name;
            ASTNode *var_ref = ast_var(strdup(base_name), line);
            ASTNode *one = ast_number(1, line);
            ASTNode *inc_add = ast_binary(var_ref, BIN_ADD, one, line);
            char *inc_name = base_name;
            expr->as.var.name = NULL;
            ast_free(expr);
            return ast_assign(inc_name, inc_add, line);
        }
    }

    return expr;
}

static ASTNode *parse_for_stmt(Parser *p) {
    int line = p->current.line;
    consume(p, TOKEN_FOR, "expected 'for'");
    consume(p, TOKEN_LPAREN, "expected '(' after 'for'");

    ASTNode *init = NULL;
    if (check(p, TOKEN_VAR) || check(p, TOKEN_LET) || check(p, TOKEN_CONST)) {
        advance(p);
        Token iname = consume(p, TOKEN_IDENT, "expected variable name");
        consume(p, TOKEN_ASSIGN, "expected '='");
        ASTNode *ival = parse_expr(p);
        init = ast_var_decl(strdup(iname.lexeme), ival, line);
        token_free(&iname);
    } else if (check(p, TOKEN_IDENT)) {
        Lexer saved_lexer = *p->lexer;
        Token saved_cur = p->current;
        if (saved_cur.lexeme)
            saved_cur.lexeme = strdup(saved_cur.lexeme);
        Token saved_prev = p->previous;
        if (saved_prev.lexeme)
            saved_prev.lexeme = strdup(saved_prev.lexeme);

        char *first = p->current.lexeme ? strdup(p->current.lexeme) : NULL;
        advance(p);
        if (check(p, TOKEN_IDENT)) {
            /* type varname = expr */
            char *var_name = p->current.lexeme ? strdup(p->current.lexeme) : NULL;
            advance(p);
            consume(p, TOKEN_ASSIGN, "expected '='");
            ASTNode *ival = parse_expr(p);
            init = ast_var_decl(var_name, ival, line);

            token_free(&saved_cur);
            token_free(&saved_prev);
        } else if (check(p, TOKEN_ASSIGN)) {
            advance(p);
            ASTNode *ival = parse_expr(p);
            init = ast_assign(strdup(first), ival, line);

            token_free(&saved_cur);
            token_free(&saved_prev);
        } else {
            /* Rollback */
            token_free(&p->current);
            token_free(&p->previous);
            *p->lexer = saved_lexer;
            p->current = saved_cur;
            p->previous = saved_prev;
        }
        if (first)
            free(first);
    }
    consume(p, TOKEN_SEMICOLON, "expected ';' after for init");

    ASTNode *cond = parse_expr(p);
    consume(p, TOKEN_SEMICOLON, "expected ';' after for condition");

    ASTNode *incr = parse_expr(p);
    consume(p, TOKEN_RPAREN, "expected ')' after for increment");

    ASTNode *body = parse_block(p);
    return ast_for(init, cond, incr, body, line);
}

static ASTNode *parse_do_stmt(Parser *p) {
    int line = p->current.line;
    consume(p, TOKEN_DO, "expected 'do'");
    ASTNode *body = parse_block(p);
    consume(p, TOKEN_WHILE, "expected 'while' after do block");
    consume(p, TOKEN_LPAREN, "expected '('");
    ASTNode *cond = parse_expr(p);
    consume(p, TOKEN_RPAREN, "expected ')'");
    return ast_do_while(cond, body, line);
}

static ASTNode *parse_switch_stmt(Parser *p) {
    int line = p->current.line;
    consume(p, TOKEN_SWITCH, "expected 'switch'");
    consume(p, TOKEN_LBRACE, "expected '{' after 'switch'");

    ASTNode *root = NULL;
    ASTNode **tail = &root;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        if (!match(p, TOKEN_CASE)) {
            parse_error(p, "expected 'case' in switch");
            advance(p);
            continue;
        }
        if (check(p, TOKEN_LPAREN)) {
            advance(p);
            ASTNode *cc = parse_expr(p);
            consume(p, TOKEN_RPAREN, "expected ')'");
            ASTNode *cb = parse_block(p);
            ASTNode *ifn = ast_if(cc, cb, NULL, line);
            *tail = ifn;
            tail = &ifn->as.if_stmt.else_block;
        } else {
            /* default case */
            ASTNode *db = parse_block(p);
            *tail = db;
            tail = NULL; /* nothing can follow default */
            break;
        }
    }
    consume(p, TOKEN_RBRACE, "expected '}' to close switch");
    if (!root)
        return ast_block(line);
    return root;
}

static ASTNode *parse_loop_ctrl_stmt(Parser *p) {
    int line = p->current.line;
    int is_break = check(p, TOKEN_BREAK);
    advance(p);
    consume(p, TOKEN_LPAREN, "expected '('");
    consume(p, TOKEN_RPAREN, "expected ')'");
    consume(p, TOKEN_SEMICOLON, "expected ';'");
    return is_break ? ast_break(line) : ast_pass(line);
}
