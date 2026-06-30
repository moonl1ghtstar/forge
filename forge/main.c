/*
 * main.c - Command-line interface for the Forge compiler
 *
 * Reads a Helix (.hlx) or C (.c) source file, runs the compilation pipeline
 * (lex → parse → semantic analysis → codegen), and outputs x86-64 assembly.
 *
 * Codegen output layer:
 *   Both Helix and C frontends funnel into compile_source_to_asm(), which
 *   dispatches to the appropriate parser and then calls codegen_emit().
 *   The codegen layer is frontend-agnostic: it only sees the shared AST/IR.
 *
 *   lib_mode controls whether a synthetic main() is injected for C files:
 *     lib_mode=0  → exe build (main() injected if absent)   used by default + -asm
 *     lib_mode=1  → library build (no main() injection)     used by -obj
 *   Helix always acts as lib_mode=1 (caller defines main() explicitly).
 *
 * Supported output modes:
 *   -asm         Stop after .asm output (debug/preview, always kept)
 *   -obj         Compile to .obj via nasm (stop before linking)
 *                C files compiled in library mode (no synthetic main())
 *   (default)    Compile + link → .exe (full build)
 *   -run         Build .exe, then execute it
 *
 * Cross-language linking:
 *   -link a.obj b.obj ...   Link pre-built .obj files together into .exe
 *                           Mix Forge Helix and Forge/gcc C objects freely.
 *                           All objects must be Windows x64 COFF (same ABI).
 *
 * ABI compatibility:
 *   - Forge codegen targets Windows x64 ABI (rcx/rdx/r8/r9, 32-byte shadow,
 *     16-byte stack alignment before CALL).
 *   - gcc -c with MinGW64 produces identical COFF format and ABI.
 *   - Symbol names match directly (no leading underscore on x86-64).
 *   - `global` in .asm → public COFF symbol; `extern` → external reference.
 *
 * Pipeline:
 *   source → compile_source_to_asm() → .asm → assemble_object() [nasm -f win64]
 *          → .obj → link_executable() [ld + MinGW CRT] → .exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <process.h>

/* Helix frontend headers */
#include "lexer/helix-lexer.h"
#include "parser/helix-parser.h"
#include "sema/helix-sema.h"
#include "ir/builder/ir-builder.h"
#include "ir/ir-opt.h"
#include "codegen/helix-codegen.h"

/* C frontend headers */
#include "c-frontend.h"
#include "c-lexer.h"

#ifdef _WIN32
__declspec(dllimport) unsigned long __stdcall GetShortPathNameA(const char *lpszLongPath, char *lpszShortPath, unsigned long cchBuffer);
__declspec(dllimport) unsigned long __stdcall GetCurrentDirectoryA(unsigned long nBufferLength, char *lpBuffer);
#endif

/* ---- Toolchain paths ---- */
#define MINGW_LIB_DIR "C:/msys64/mingw64/lib"
#define GCC_LIB_DIR   "C:/msys64/mingw64/lib/gcc/x86_64-w64-mingw32/15.2.0"
#define CRT2_OBJ      "C:/msys64/mingw64/lib/crt2.o"

/* Maximum number of extra .obj files that can be passed via -link */
#define MAX_LINK_OBJS 64

/* ---- Usage / error helpers ---- */

static void usage(const char *progname, int exit_code) {
    fprintf(stderr, "Usage: %s <source.hlx|source.c> [options]\n", progname);
    fprintf(stderr, "\nOutput mode (mutually exclusive):\n");
    fprintf(stderr, "  -asm          Output .asm only (debug/preview, default ext: .asm)\n");
    fprintf(stderr, "  -obj          Compile to .obj via nasm (stop before link)\n");
    fprintf(stderr, "  (default)     Compile + link to .exe\n");
    fprintf(stderr, "\nOther options:\n");
    fprintf(stderr, "  -o <file>     Output filename\n");
    fprintf(stderr, "  -run          Build .exe, then execute it\n");
    fprintf(stderr, "  -link a.obj [b.obj ...]  Link one or more .obj files into .exe\n");
    fprintf(stderr, "                            Mix Forge and C objects freely\n");
    fprintf(stderr, "  -dump-tokens  Print lexer tokens and exit\n");
    fprintf(stderr, "  -dump-ast     Print parsed AST and exit\n");
    fprintf(stderr, "  -dump-ir      Print lowered IR and exit\n");
    fprintf(stderr, "\nPipeline:  source -> forge -> .asm -> nasm -f win64 -> .obj -> ld -> .exe\n");
    fprintf(stderr, "Cross-obj: forge src.hlx -obj && gcc -c lib.c -o lib.obj && "
                    "forge -link src.obj lib.obj -o out.exe\n");
    exit(exit_code);
}

static void cli_error(const char *progname, const char *msg) {
    fprintf(stderr, "Forge error: %s\n", msg);
    fprintf(stderr, "Try: %s --help\n", progname);
}

/* ---- File I/O helpers ---- */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;

    if (!f) {
        fprintf(stderr, "Forge error: cannot open file '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = (char *)malloc(size + 1);
    if (!buf) {
        fprintf(stderr, "Forge error: out of memory reading '%s'\n", path);
        fclose(f);
        return NULL;
    }

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static int copy_file_bytes(const char *src_path, const char *dst_path) {
    FILE *src = fopen(src_path, "rb");
    FILE *dst;
    char buf[8192];
    size_t n;

    if (!src)
        return 1;

    dst = fopen(dst_path, "wb");
    if (!dst) {
        fclose(src);
        return 1;
    }

    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            return 1;
        }
    }

    fclose(src);
    fclose(dst);
    return 0;
}

/* Build a new string with the file extension replaced */
static char *replace_extension(const char *path, const char *new_ext) {
    char *copy = strdup(path);
    char *dot;
    size_t need;
    char *out;

    if (!copy)
        return NULL;

    dot = strrchr(copy, '.');
    if (dot)
        *dot = '\0';

    need = strlen(copy) + strlen(new_ext) + 1;
    out = (char *)realloc(copy, need);
    if (!out) {
        free(copy);
        return NULL;
    }
    copy = out;
    strcat(copy, new_ext);
    return copy;
}

/* Return a short (8.3) path on Windows to avoid NASM path issues */
static char *short_path_dup(const char *path) {
#ifdef _WIN32
    char buf[1024];
    unsigned long n = GetShortPathNameA(path, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        return strdup(buf);
#endif
    return strdup(path);
}

/* ---- Process execution ---- */

static int run_process(const char *file, const char *const argv[]) {
    intptr_t rc = _spawnvp(_P_WAIT, file, argv);
    if (rc == -1) {
        fprintf(stderr, "Forge error: failed to launch '%s': %s\n", file, strerror(errno));
        return 1;
    }
    return (int)rc;
}

/* ---- Drive mapping (for short-path linker invocation) ---- */

static int map_work_drive(const char *dir) {
    const char *const argv[] = {"cmd", "/c", "subst", "X:", dir, NULL};
    return run_process("cmd", argv);
}

static void unmap_work_drive(void) {
    const char *const argv[] = {"cmd", "/c", "subst", "X:", "/d", NULL};
    run_process("cmd", argv);
}

/* ---- AST dump helpers ---- */

static const char *ast_type_name(ASTNodeType type) {
    switch (type) {
    case AST_PROGRAM:    return "PROGRAM";
    case AST_FUNCTION:   return "FUNCTION";
    case AST_BLOCK:      return "BLOCK";
    case AST_VAR_DECL:   return "VAR_DECL";
    case AST_ASSIGN:     return "ASSIGN";
    case AST_IF:         return "IF";
    case AST_WHILE:      return "WHILE";
    case AST_RETURN:     return "RETURN";
    case AST_BINARY:     return "BINARY";
    case AST_UNARY:      return "UNARY";
    case AST_NUMBER:     return "NUMBER";
    case AST_STRING:     return "STRING";
    case AST_VAR:        return "VAR";
    case AST_CALL:       return "CALL";
    case AST_EXTERN_FUNC: return "EXTERN_FUNC";
    case AST_EXPR_STMT:  return "EXPR_STMT";
    case AST_FOR:        return "FOR";
    case AST_DO_WHILE:   return "DO_WHILE";
    case AST_BREAK:      return "BREAK";
    case AST_PASS:       return "PASS";
    case AST_STRUCT_DECL: return "STRUCT_DECL";
    case AST_STRUCT_INIT: return "STRUCT_INIT";
    case AST_FIELD_ACCESS: return "FIELD_ACCESS";
    }
    return "UNKNOWN";
}

static void ast_indent(int depth) {
    int i;
    for (i = 0; i < depth; i++)
        printf("  ");
}

static void dump_ast_node(ASTNode *node, int depth) {
    int i;
    if (!node)
        return;

    ast_indent(depth);
    printf("%s", ast_type_name(node->type));
    if (node->resolved_type)
        printf(" type=%s", node->resolved_type);
    printf(" line=%d", node->line);

    switch (node->type) {
    case AST_FUNCTION:
        printf(" name=%s", node->as.function.name);
        break;
    case AST_VAR_DECL:
        printf(" name=%s", node->as.var_decl.name);
        break;
    case AST_ASSIGN:
        printf(" name=%s", node->as.assign.name);
        break;
    case AST_NUMBER:
        printf(" value=%d", node->as.number.value);
        break;
    case AST_STRING:
        printf(" value=\"%s\"", node->as.string.value);
        break;
    case AST_VAR:
        printf(" name=%s", node->as.var.name);
        break;
    case AST_CALL:
        printf(" name=%s argc=%d", node->as.call.name, node->as.call.arg_count);
        break;
    case AST_EXTERN_FUNC:
        printf(" name=%s argc=%d", node->as.extern_func.name, node->as.extern_func.param_count);
        break;
    case AST_STRUCT_DECL:
        printf(" name=%s fields=%d", node->as.struct_decl.name, node->as.struct_decl.field_count);
        break;
    case AST_STRUCT_INIT:
        printf(" type=%s var=%s values=%d named=%d",
               node->as.struct_init.type_name,
               node->as.struct_init.var_name,
               node->as.struct_init.value_count,
               node->as.struct_init.named);
        break;
    case AST_FIELD_ACCESS:
        printf(" field=%s", node->as.field_access.field_name);
        break;
    default:
        break;
    }
    printf("\n");

    switch (node->type) {
    case AST_PROGRAM:
        for (i = 0; i < node->as.program.count; i++)
            dump_ast_node(node->as.program.functions[i], depth + 1);
        break;
    case AST_FUNCTION:
        dump_ast_node(node->as.function.body, depth + 1);
        break;
    case AST_BLOCK:
        for (i = 0; i < node->as.block.count; i++)
            dump_ast_node(node->as.block.stmts[i], depth + 1);
        break;
    case AST_VAR_DECL:
        dump_ast_node(node->as.var_decl.init, depth + 1);
        break;
    case AST_ASSIGN:
        dump_ast_node(node->as.assign.value, depth + 1);
        break;
    case AST_IF:
        dump_ast_node(node->as.if_stmt.cond, depth + 1);
        dump_ast_node(node->as.if_stmt.then_block, depth + 1);
        dump_ast_node(node->as.if_stmt.else_block, depth + 1);
        break;
    case AST_WHILE:
        dump_ast_node(node->as.while_stmt.cond, depth + 1);
        dump_ast_node(node->as.while_stmt.body, depth + 1);
        break;
    case AST_RETURN:
        dump_ast_node(node->as.return_stmt.expr, depth + 1);
        break;
    case AST_BINARY:
        dump_ast_node(node->as.binary.left, depth + 1);
        dump_ast_node(node->as.binary.right, depth + 1);
        break;
    case AST_UNARY:
        dump_ast_node(node->as.unary.operand, depth + 1);
        break;
    case AST_CALL:
        for (i = 0; i < node->as.call.arg_count; i++)
            dump_ast_node(node->as.call.args[i], depth + 1);
        break;
    case AST_EXTERN_FUNC:
        break;
    case AST_STRUCT_DECL:
        break;
    case AST_STRUCT_INIT:
        for (i = 0; i < node->as.struct_init.value_count; i++)
            dump_ast_node(node->as.struct_init.values[i], depth + 1);
        break;
    case AST_FIELD_ACCESS:
        dump_ast_node(node->as.field_access.object, depth + 1);
        break;
    case AST_EXPR_STMT:
        dump_ast_node(node->as.expr_stmt.expr, depth + 1);
        break;
    case AST_FOR:
        dump_ast_node(node->as.for_stmt.init, depth + 1);
        dump_ast_node(node->as.for_stmt.cond, depth + 1);
        dump_ast_node(node->as.for_stmt.incr, depth + 1);
        dump_ast_node(node->as.for_stmt.body, depth + 1);
        break;
    case AST_DO_WHILE:
        dump_ast_node(node->as.while_stmt.body, depth + 1);
        dump_ast_node(node->as.while_stmt.cond, depth + 1);
        break;
    case AST_NUMBER:
    case AST_STRING:
    case AST_VAR:
    case AST_BREAK:
    case AST_PASS:
        break;
    }
}

/* ---- Token dump helpers ---- */

static int dump_helix_tokens(const char *source_path) {
    char *source;
    Lexer lexer;
    Token tok;

    source = read_file(source_path);
    if (!source)
        return 1;

    lexer_init(&lexer, source);
    do {
        tok = lexer_next(&lexer);
        if (tok.type == TOKEN_NUMBER)
            printf("%d\t%s\t%s\t%d\n", tok.line, "HLX", token_type_to_string(tok.type), tok.value);
        else if (tok.lexeme)
            printf("%d\t%s\t%s\t%s\n", tok.line, "HLX", token_type_to_string(tok.type), tok.lexeme);
        else
            printf("%d\t%s\t%s\n", tok.line, "HLX", token_type_to_string(tok.type));
        token_free(&tok);
    } while (tok.type != TOKEN_EOF && tok.type != TOKEN_ERROR);

    free(source);
    return 0;
}

static int dump_c_tokens(const char *source_path) {
    char *source;
    CLexer lexer;
    CToken tok;

    source = read_file(source_path);
    if (!source)
        return 1;

    c_lexer_init(&lexer, source);
    do {
        tok = c_lexer_next(&lexer);
        if (tok.type == C_TOK_NUMBER)
            printf("%d\t%s\t%s\t%d\n", tok.line, "C", c_token_type_to_string(tok.type), tok.value);
        else if (tok.lexeme)
            printf("%d\t%s\t%s\t%s\n", tok.line, "C", c_token_type_to_string(tok.type), tok.lexeme);
        else
            printf("%d\t%s\t%s\n", tok.line, "C", c_token_type_to_string(tok.type));
        c_token_free(&tok);
    } while (tok.type != C_TOK_EOF && tok.type != C_TOK_ERROR);

    free(source);
    return 0;
}

static int dump_tokens_source(const char *source_path) {
    const char *dot = strrchr(source_path, '.');

    if (dot && strcmp(dot, ".c") == 0)
        return dump_c_tokens(source_path);
    if (dot && strcmp(dot, ".hlx") == 0)
        return dump_helix_tokens(source_path);

    fprintf(stderr, "Forge error: unsupported source file '%s'\n", source_path);
    fprintf(stderr, "Expected .hlx or .c\n");
    return 1;
}

/* ---- Helix parse+sema+codegen pipeline ---- */

static int parse_helix_program_from_file(const char *source_path, ASTNode **program_out) {
    char *source;
    Lexer lexer;
    Parser parse_ctx;
    ASTNode *program;

    source = read_file(source_path);
    if (!source)
        return 1;

    lexer_init(&lexer, source);
    parser_init(&parse_ctx, &lexer);
    program = parse_program(&parse_ctx);

    if (parse_ctx.had_error) {
        ast_free(program);
        free(source);
        return 1;
    }

    *program_out = program;
    free(source);
    return 0;
}

static int parse_source_program(const char *source_path, ASTNode **program_out);

static int build_ir_from_source(const char *source_path, IRProgram **ir_out, ASTNode **ast_out) {
    ASTNode *program;
    IRProgram *ir;
    int result;

    result = parse_source_program(source_path, &program);
    if (result != 0) {
        fprintf(stderr, "Forge error: parsing failed. Fix the errors above, then run again.\n");
        return 1;
    }

    result = sema_analyze(program);
    if (result != 0) {
        fprintf(stderr, "Forge error: semantic checks failed. Fix the errors above, then run again.\n");
        ast_free(program);
        return 1;
    }

    ir = ir_build_program(program);
    if (!ir) {
        fprintf(stderr, "Forge error: IR build failed.\n");
        ast_free(program);
        return 1;
    }

    if (ast_out)
        *ast_out = program;
    else
        ast_free(program);
    *ir_out = ir;
    return 0;
}

static int compile_helix(const char *source_path, const char *asm_path) {
    IRProgram *ir;
    ASTNode *program = NULL;
    FILE *out;
    int result;

    result = build_ir_from_source(source_path, &ir, &program);
    if (result != 0) {
        return 1;
    }

    ir_optimize(ir);

    out = fopen(asm_path, "w");
    if (!out) {
        fprintf(stderr, "Forge error: cannot open output file '%s'\n", asm_path);
        ir_program_free(ir);
        ast_free(program);
        return 1;
    }

    codegen_emit(ir, out);
    fclose(out);

    printf("Forge: compiled '%s' -> '%s'\n", source_path, asm_path);
    ir_program_free(ir);
    ast_free(program);
    return 0;
}

static int dump_ir_source(const char *source_path) {
    IRProgram *ir;
    ASTNode *program = NULL;
    int rc = build_ir_from_source(source_path, &ir, &program);
    if (rc != 0)
        return rc;
    ir_dump_program(ir, stdout);
    ir_program_free(ir);
    ast_free(program);
    return 0;
}

/*
 * compile_source_to_asm  –  unified frontend dispatch (codegen output layer)
 *
 * Entry point for all source-to-.asm compilation.
 * Both Helix and C frontends converge here before hitting the shared codegen.
 *
 * Design:
 *   source (.hlx or .c)
 *     └─► frontend (lex → parse → sema)    ← language-specific
 *           └─► ASTNode *program            ← shared IR
 *                 └─► codegen_emit()        ← language-agnostic, Windows x64 ABI
 *                       └─► .asm text
 *
 * lib_mode:
 *   0 – exe build: C parser injects synthetic main() if none is found.
 *       Use for default build and -asm (preview of the full program).
 *   1 – library build: no synthetic main(). Use for -obj on C library files.
 *       Helix ignores lib_mode (main() must be declared explicitly).
 *
 * Symbol ABI (both frontends, same rules):
 *   - Every defined function → `global <name>` in .asm (public COFF symbol).
 *   - Every `extern` declaration → `extern <name>` in .asm.
 *   - No name mangling; symbols link directly with gcc-produced COFF objects.
 */
static int compile_source_to_asm(const char *source_path, const char *asm_path, int lib_mode) {
    const char *dot = strrchr(source_path, '.');

    if (dot && strcmp(dot, ".c") == 0) {
        if (lib_mode)
            return compile_c_lib(source_path, asm_path);
        return compile_c(source_path, asm_path);
    }
    if (dot && strcmp(dot, ".hlx") == 0)
        return compile_helix(source_path, asm_path);

    fprintf(stderr, "Forge error: unsupported source file '%s'\n", source_path);
    fprintf(stderr, "Expected .hlx or .c\n");
    return 1;
}

/* ---- AST dump (parse only, no codegen) ---- */

static int parse_source_program(const char *source_path, ASTNode **program_out) {
    const char *dot = strrchr(source_path, '.');

    if (dot && strcmp(dot, ".c") == 0)
        return c_parse_program_from_file(source_path, program_out);
    if (dot && strcmp(dot, ".hlx") == 0)
        return parse_helix_program_from_file(source_path, program_out);

    fprintf(stderr, "Forge error: unsupported source file '%s'\n", source_path);
    fprintf(stderr, "Expected .hlx or .c\n");
    return 1;
}

static int dump_ast_source(const char *source_path) {
    ASTNode *program;
    int rc = parse_source_program(source_path, &program);
    if (rc != 0)
        return rc;
    dump_ast_node(program, 0);
    ast_free(program);
    return 0;
}

/* ---- Backend: assemble .asm → .obj ---- */

/*
 * assemble_object:
 *   Invoke nasm -f win64 to produce a COFF .obj from .asm text.
 *   The resulting object is Windows x64 ABI compatible and can be
 *   linked together with any other COFF .obj (Forge or C-produced).
 */
static int assemble_object(const char *asm_path, const char *obj_path) {
    char *asm_short = short_path_dup(asm_path);
    char *obj_short = short_path_dup(obj_path);
    const char *const nasm_argv[] = {"nasm", "-f", "win64", asm_short, "-o", obj_short, NULL};
    int rc = run_process("nasm", nasm_argv);
    free(asm_short);
    free(obj_short);
    if (rc != 0)
        fprintf(stderr, "Forge: nasm assembly failed for '%s'\n", asm_path);
    return rc;
}

/* ---- Backend: link one or more .obj → .exe ---- */

/*
 * link_executable:
 *   Link obj_paths[0..obj_count-1] into a single .exe using ld + MinGW CRT.
 *   All .obj files must be Windows x64 COFF (produced by nasm -f win64 or
 *   by gcc -c with the default win64 target).
 *
 *   Symbol visibility:
 *     - Every `global` symbol in Forge .asm (e.g. `global main`, `global foo`)
 *       is exported as a plain COFF public symbol.
 *     - Every `extern` in Forge .asm matches by name to symbols in other .obj.
 *     - gcc-compiled .obj uses the same naming convention (no leading underscore
 *       on x86-64), so symbols are directly compatible.
 *
 *   ABI:
 *     - Both Forge codegen and gcc target Windows x64 ABI (rcx/rdx/r8/r9,
 *       32-byte shadow space, 16-byte stack alignment before CALL).
 *     - Forge functions already respect this (see helix-codegen.c prologue).
 */
static int link_executable(const char **obj_paths, int obj_count, const char *out_path) {
    /*
     * Build ld argv dynamically.
     * Fixed slots: "ld", "-o", out_short, CRT2_OBJ, [objs...],
     *              "-L", MINGW_LIB_DIR, "-L", GCC_LIB_DIR,
     *              "-lmingw32", "-lmsvcrt", "-lkernel32", "-lmingwex", "-lgcc",
     *              "-e", "mainCRTStartup", "-subsystem", "console", NULL
     * = 3 + 1 + obj_count + 2 + 5 + 2 + 2 + 1 = 16 + obj_count
     */
    int i;
    int argc = 0;
    const char **ld_argv;
    char *out_short;
    char **obj_shorts;
    int rc;

    out_short = short_path_dup(out_path);
    obj_shorts = (char **)malloc(sizeof(char *) * obj_count);
    for (i = 0; i < obj_count; i++)
        obj_shorts[i] = short_path_dup(obj_paths[i]);

    ld_argv = (const char **)malloc(sizeof(const char *) * (20 + obj_count));

    ld_argv[argc++] = "ld";
    ld_argv[argc++] = "-o";
    ld_argv[argc++] = out_short;
    ld_argv[argc++] = CRT2_OBJ;
    for (i = 0; i < obj_count; i++)
        ld_argv[argc++] = obj_shorts[i];
    ld_argv[argc++] = "-L";
    ld_argv[argc++] = MINGW_LIB_DIR;
    ld_argv[argc++] = "-L";
    ld_argv[argc++] = GCC_LIB_DIR;
    ld_argv[argc++] = "-lmingw32";
    ld_argv[argc++] = "-lmsvcrt";
    ld_argv[argc++] = "-lkernel32";
    ld_argv[argc++] = "-lmingwex";
    ld_argv[argc++] = "-lgcc";
    ld_argv[argc++] = "-e";
    ld_argv[argc++] = "mainCRTStartup";
    ld_argv[argc++] = "-subsystem";
    ld_argv[argc++] = "console";
    ld_argv[argc++] = NULL;

    rc = run_process("ld", ld_argv);

    free(ld_argv);
    for (i = 0; i < obj_count; i++)
        free(obj_shorts[i]);
    free(obj_shorts);
    free(out_short);

    if (rc != 0)
        fprintf(stderr, "Forge: linking failed\n");
    return rc;
}

/* ---- main ---- */

int main(int argc, char *argv[]) {
    const char *source_path = NULL;
    const char *output_path = NULL;
    int asm_only  = 0;   /* -asm : output .asm text only */
    int obj_only  = 0;   /* -obj : output .obj (stop before link) */
    int do_run    = 0;   /* -run : execute after build */
    int do_link   = 0;   /* -link: link pre-built .obj files */
    int dump_tokens = 0;
    int dump_ast  = 0;
    int dump_ir   = 0;
    int i;

    /* Extra .obj files supplied via -link */
    const char *link_objs[MAX_LINK_OBJS];
    int link_obj_count = 0;

    char *default_output   = NULL;
    int   work_drive_mapped = 0;
    int   result = 0;

    /* Temporary work paths on X: (short, no spaces) */
    const char *work_asm_path = "X:\\forge-build.asm";
    const char *work_obj_path = "X:\\forge-build.o";
    const char *work_exe_path = "X:\\forge-build.exe";

    if (argc < 2) {
        cli_error(argv[0], "no input file provided");
        return 1;
    }

    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
        usage(argv[0], 0);

    /* ---- Argument parsing ---- */

    /*
     * Modes:
     *   forge src.hlx                   → compile + link → src.exe
     *   forge src.hlx -asm              → compile → src.asm
     *   forge src.hlx -obj              → compile → src.obj
     *   forge -link a.obj b.obj -o x.exe → link only (no source compile)
     */

    /* Check for -link mode first (no source file required) */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-link") == 0) {
            do_link = 1;
            break;
        }
    }

    if (do_link) {
        /* In -link mode: parse [-link obj...] [-o out] */
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-link") == 0) {
                /* Consume subsequent .obj arguments until next flag or end */
                while (i + 1 < argc && argv[i + 1][0] != '-') {
                    if (link_obj_count >= MAX_LINK_OBJS) {
                        fprintf(stderr, "Forge error: too many -link objects (max %d)\n", MAX_LINK_OBJS);
                        return 1;
                    }
                    link_objs[link_obj_count++] = argv[++i];
                }
            } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                output_path = argv[++i];
            } else if (strcmp(argv[i], "-run") == 0) {
                do_run = 1;
            } else {
                fprintf(stderr, "Forge error: unexpected argument '%s' in -link mode\n", argv[i]);
                return 1;
            }
        }

        if (link_obj_count == 0) {
            cli_error(argv[0], "-link requires at least one .obj file");
            return 1;
        }

        if (!output_path) {
            /* Default output: replace extension of first obj */
            default_output = replace_extension(link_objs[0], ".exe");
            output_path = default_output;
        }

        {
            char cwd[1024];
            if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) {
                fprintf(stderr, "Forge error: cannot read current directory\n");
                result = 1;
                goto cleanup;
            }
            if (map_work_drive(cwd) != 0) {
                fprintf(stderr, "Forge error: failed to map work drive for linker tools\n");
                result = 1;
                goto cleanup;
            }
            work_drive_mapped = 1;
        }

        result = link_executable(link_objs, link_obj_count, work_exe_path);
        if (result != 0)
            goto cleanup;

        if (copy_file_bytes(work_exe_path, output_path) != 0) {
            fprintf(stderr, "Forge: failed to write '%s'\n", output_path);
            result = 1;
            goto cleanup;
        }
        printf("Forge: linked %d object(s) -> '%s'\n", link_obj_count, output_path);

        if (do_run) {
            char *run_short = short_path_dup(output_path);
            const char *const run_argv[] = {run_short, NULL};
            result = run_process(run_short, run_argv);
            free(run_short);
        }
        goto cleanup;
    }

    /* ---- Normal compile mode ---- */
    source_path = argv[1];
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-asm") == 0) {
            asm_only = 1;
        } else if (strcmp(argv[i], "-obj") == 0) {
            obj_only = 1;
        } else if (strcmp(argv[i], "-run") == 0) {
            do_run = 1;
        } else if (strcmp(argv[i], "-dump-tokens") == 0) {
            dump_tokens = 1;
        } else if (strcmp(argv[i], "-dump-ast") == 0) {
            dump_ast = 1;
        } else if (strcmp(argv[i], "-dump-ir") == 0) {
            dump_ir = 1;
        } else {
            fprintf(stderr, "Forge error: unexpected argument '%s'\n", argv[i]);
            fprintf(stderr, "Try: %s --help\n", argv[0]);
            return 1;
        }
    }

    /* Validate flag combinations */
    if (asm_only && obj_only) {
        cli_error(argv[0], "-asm and -obj are mutually exclusive");
        return 1;
    }
    if ((asm_only || obj_only) && do_run) {
        cli_error(argv[0], "-run requires a full exe build (drop -asm/-obj)");
        return 1;
    }
    if (dump_tokens && dump_ast) {
        cli_error(argv[0], "-dump-tokens and -dump-ast are mutually exclusive");
        return 1;
    }
    if (dump_ir && (dump_tokens || dump_ast || asm_only || obj_only || do_run)) {
        cli_error(argv[0], "-dump-ir conflicts with output flags");
        return 1;
    }
    if (dump_tokens && (asm_only || obj_only || do_run)) {
        cli_error(argv[0], "-dump-tokens conflicts with other output flags");
        return 1;
    }
    if (dump_ast && (asm_only || obj_only || do_run)) {
        cli_error(argv[0], "-dump-ast conflicts with other output flags");
        return 1;
    }

    /* ---- Debug/diagnostic modes ---- */
    if (dump_tokens) {
        result = dump_tokens_source(source_path);
        goto cleanup;
    }

    if (dump_ast) {
        result = dump_ast_source(source_path);
        goto cleanup;
    }

    if (dump_ir) {
        result = dump_ir_source(source_path);
        goto cleanup;
    }

    /* ---- -asm mode: emit .asm text only ---- */
    if (asm_only) {
        if (!output_path) {
            default_output = replace_extension(source_path, ".asm");
            output_path = default_output;
        }
        result = compile_source_to_asm(source_path, output_path, 0);
        goto cleanup;
    }

    /* ---- -obj mode: .asm → nasm → .obj (stop before link) ---- */
    if (obj_only) {
        if (!output_path) {
            default_output = replace_extension(source_path, ".obj");
            output_path = default_output;
        }

        {
            char cwd[1024];
            if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) {
                fprintf(stderr, "Forge error: cannot read current directory\n");
                result = 1;
                goto cleanup;
            }
            if (map_work_drive(cwd) != 0) {
                fprintf(stderr, "Forge error: failed to map work drive for linker tools\n");
                result = 1;
                goto cleanup;
            }
            work_drive_mapped = 1;
        }

        result = compile_source_to_asm(source_path, work_asm_path, 1);
        if (result != 0)
            goto cleanup;

        result = assemble_object(work_asm_path, work_obj_path);
        if (result != 0)
            goto cleanup;

        if (copy_file_bytes(work_obj_path, output_path) != 0) {
            fprintf(stderr, "Forge: failed to write '%s'\n", output_path);
            result = 1;
            goto cleanup;
        }
        printf("Forge: assembled '%s' -> '%s'\n", source_path, output_path);
        goto cleanup;
    }

    /* ---- Default mode: compile + link → .exe ---- */
    if (!output_path) {
        default_output = replace_extension(source_path, ".exe");
        output_path = default_output;
    }

    {
        char cwd[1024];
        if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) {
            fprintf(stderr, "Forge error: cannot read current directory\n");
            result = 1;
            goto cleanup;
        }
        if (map_work_drive(cwd) != 0) {
            fprintf(stderr, "Forge error: failed to map work drive for linker tools\n");
            result = 1;
            goto cleanup;
        }
        work_drive_mapped = 1;
    }

    result = compile_source_to_asm(source_path, work_asm_path, 0);
    if (result != 0)
        goto cleanup;

    result = assemble_object(work_asm_path, work_obj_path);
    if (result != 0)
        goto cleanup;

    {
        const char *objs[] = {work_obj_path};
        result = link_executable(objs, 1, work_exe_path);
    }
    if (result != 0)
        goto cleanup;

    if (copy_file_bytes(work_exe_path, output_path) != 0) {
        fprintf(stderr, "Forge: failed to write '%s'\n", output_path);
        result = 1;
        goto cleanup;
    }

    printf("Forge: linked '%s'\n", output_path);

    if (do_run) {
        char *run_short = short_path_dup(output_path);
        const char *const run_argv[] = {run_short, NULL};
        result = run_process(run_short, run_argv);
        free(run_short);
    }

cleanup:
    remove(work_asm_path);
    remove(work_obj_path);
    remove(work_exe_path);
    if (work_drive_mapped)
        unmap_work_drive();

    free(default_output);

    if (do_run && result != 0)
        return 1;

    return result != 0 ? 1 : 0;
}
