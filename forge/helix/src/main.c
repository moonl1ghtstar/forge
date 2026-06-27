/*
 * main.c - Command-line interface for the Forge compiler
 *
 * Reads a Helix (.hlx) source file, runs the compilation pipeline
 * (lex → parse → semantic analysis → codegen), and outputs x86-64 assembly.
 * Supports -o for output filename and -run for automatic nasm+ld assembly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <process.h>

// h
#include "lexer/helix-lexer.h"
#include "parser/helix-parser.h"
#include "sema/helix-sema.h"
#include "codegen/helix-codegen.h"

// define
#define MINGW_LIB_DIR "C:/msys64/mingw64/lib"
#define GCC_LIB_DIR "C:/msys64/mingw64/lib/gcc/x86_64-w64-mingw32/15.2.0"
#define CRT2_OBJ "C:/msys64/mingw64/lib/crt2.o"

/* Print usage information and exit */
static void usage(const char *progname, int exit_code) {
    fprintf(stderr, "Usage: %s <source.hlx> [-o <output>] [-asm] [-run]\n", progname);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o <file>  Output filename (default: source.exe or source.asm)\n");
    fprintf(stderr, "  -asm       Stop after assembly output\n");
    fprintf(stderr, "  -run       Build exe, then execute\n");
    fprintf(stderr, "\nPipeline: .hlx -> forge -> .asm -> nasm -f win64 + ld -> .exe\n");
    exit(exit_code);
}

static void cli_error(const char *progname, const char *msg) {
    fprintf(stderr, "Forge error: %s\n", msg);
    fprintf(stderr, "Try: %s <source.hlx> [-o <output>] [-asm] [-run]\n", progname);
}

/* Read an entire file into a dynamically allocated string */
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

/* Run the full compilation pipeline: source → AST → sema → assembly */
/* Run a process without invoking a shell */
static int run_process(const char *file, const char *const argv[]) {
    intptr_t rc = _spawnvp(_P_WAIT, file, argv);
    if (rc == -1) {
        fprintf(stderr, "Forge error: failed to launch '%s': %s\n", file, strerror(errno));
        return 1;
    }
    return (int)rc;
}

static char *replace_extension(const char *path, const char *new_ext) {
    char *copy = strdup(path);
    char *dot;

    if (!copy)
        return NULL;

    dot = strrchr(copy, '.');
    if (dot)
        *dot = '\0';

    {
        size_t need = strlen(copy) + strlen(new_ext) + 1;
        char *out = (char *)realloc(copy, need);
        if (!out) {
            free(copy);
            return NULL;
        }
        copy = out;
    }

    strcat(copy, new_ext);
    return copy;
}

static int compile(const char *source_path, const char *output_path) {
    char *source;
    Lexer lexer;
    Parser parse_ctx;
    ASTNode *program;
    FILE *out;
    int result;

    /* Read source file */
    source = read_file(source_path);
    if (!source)
        return 1;

    /* Lexing */
    lexer_init(&lexer, source);

    /* Parsing */
    parser_init(&parse_ctx, &lexer);
    program = parse_program(&parse_ctx);

    if (parse_ctx.had_error) {
        fprintf(stderr, "Forge error: parsing failed. Fix the errors above, then run again.\n");
        ast_free(program);
        free(source);
        return 1;
    }

    /* Semantic analysis */
    result = sema_analyze(program);
    if (result != 0) {
        fprintf(stderr, "Forge error: semantic checks failed. Fix the errors above, then run again.\n");
        ast_free(program);
        free(source);
        return 1;
    }

    /* Code generation */
    out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Forge error: cannot open output file '%s'\n", output_path);
        ast_free(program);
        free(source);
        return 1;
    }

    codegen_emit(program, out);
    fclose(out);

    printf("Forge: compiled '%s' -> '%s'\n", source_path, output_path);

    /* Cleanup */
    ast_free(program);
    free(source);
    return 0;
}

/* Assemble .asm into .obj */
static int assemble_object(const char *asm_path, const char *obj_path) {
    const char *const nasm_argv[] = {"nasm", "-f", "win64", asm_path, "-o", obj_path, NULL};
    return run_process("nasm", nasm_argv);
}

/* Link .obj into .exe without gcc. Uses ld + MinGW import libs. */
static int link_executable(const char *obj_path, const char *out_path) {
    const char *const ld_argv[] = {
        "ld",
        "-o", out_path,
        CRT2_OBJ,
        obj_path,
        "-L", MINGW_LIB_DIR,
        "-L", GCC_LIB_DIR,
        "-lmingw32",
        "-lmsvcrt",
        "-lkernel32",
        "-lmingwex",
        "-lgcc",
        "-e", "mainCRTStartup",
        "-subsystem", "console",
        NULL};
    return run_process("ld", ld_argv);
}

int main(int argc, char *argv[]) {
    const char *source_path = NULL;
    const char *output_path = NULL;
    int asm_only = 0;
    int do_run = 0;
    int i;
    char *default_output = NULL;
    char *source_base = NULL;
    char *asm_path = NULL;
    char *obj_path = NULL;
    int result;

    if (argc < 2) {
        cli_error(argv[0], "no input file provided");
        return 1;
    }

    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
        usage(argv[0], 0);

    /* Parse command-line arguments */
    source_path = argv[1];
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-asm") == 0) {
            asm_only = 1;
        } else if (strcmp(argv[i], "-run") == 0) {
            do_run = 1;
        } else {
            fprintf(stderr, "Forge error: unexpected argument '%s'\n", argv[i]);
            fprintf(stderr, "Try: %s <source.hlx> [-o <output>] [-asm] [-run]\n", argv[0]);
            return 1;
        }
    }

    if (asm_only && do_run) {
        cli_error(argv[0], "cannot use '-asm' and '-run' together");
        return 1;
    }

    source_base = replace_extension(source_path, "");
    if (!source_base) {
        fprintf(stderr, "Forge error: out of memory\n");
        return 1;
    }

    if (!output_path) {
        default_output = replace_extension(source_path, asm_only ? ".asm" : ".exe");
        if (!default_output) {
            fprintf(stderr, "Forge error: out of memory\n");
            free(source_base);
            return 1;
        }
        output_path = default_output;
    }

    if (asm_only) {
        result = compile(source_path, output_path);
        free(default_output);
        free(source_base);
        return result;
    }

    asm_path = replace_extension(output_path, ".asm");
    obj_path = replace_extension(output_path, ".o");
    if (!asm_path || !obj_path) {
        fprintf(stderr, "Forge error: out of memory\n");
        free(default_output);
        free(source_base);
        free(asm_path);
        free(obj_path);
        return 1;
    }

    result = compile(source_path, asm_path);
    if (result != 0) {
        free(default_output);
        free(source_base);
        free(asm_path);
        free(obj_path);
        return 1;
    }

    result = assemble_object(asm_path, obj_path);
    if (result != 0) {
        fprintf(stderr, "Forge: nasm assembly failed\n");
        free(default_output);
        free(source_base);
        free(asm_path);
        free(obj_path);
        return 1;
    }

    result = link_executable(obj_path, output_path);
    if (result != 0) {
        fprintf(stderr, "Forge: linking failed\n");
        free(default_output);
        free(source_base);
        free(asm_path);
        free(obj_path);
        return 1;
    }

    printf("Forge: linked '%s'\n", output_path);

    if (do_run) {
        const char *const run_argv[] = {output_path, NULL};
        result = run_process(output_path, run_argv);
    }

    /* Cleanup temp files from exe build */
    remove(asm_path);
    remove(obj_path);

    free(default_output);
    free(source_base);
    free(asm_path);
    free(obj_path);

    if (do_run && result != 0)
        return 1;

    return 0;
}
