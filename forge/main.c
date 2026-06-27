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
#include "c-frontend.h"

#ifdef _WIN32
__declspec(dllimport) unsigned long __stdcall GetShortPathNameA(const char *lpszLongPath, char *lpszShortPath, unsigned long cchBuffer);
__declspec(dllimport) unsigned long __stdcall GetCurrentDirectoryA(unsigned long nBufferLength, char *lpBuffer);
#endif

// define
#define MINGW_LIB_DIR "C:/msys64/mingw64/lib"
#define GCC_LIB_DIR "C:/msys64/mingw64/lib/gcc/x86_64-w64-mingw32/15.2.0"
#define CRT2_OBJ "C:/msys64/mingw64/lib/crt2.o"

/* Print usage information and exit */
static void usage(const char *progname, int exit_code) {
    fprintf(stderr, "Usage: %s <source.hlx|source.c> [-o <output>] [-asm] [-run]\n", progname);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o <file>  Output filename (default: source.exe or source.asm)\n");
    fprintf(stderr, "  -asm       Stop after assembly output\n");
    fprintf(stderr, "  -run       Build exe, then execute\n");
    fprintf(stderr, "\nPipeline: .hlx -> forge -> .asm -> nasm -f win64 + ld -> .exe\n");
    exit(exit_code);
}

static void cli_error(const char *progname, const char *msg) {
    fprintf(stderr, "Forge error: %s\n", msg);
    fprintf(stderr, "Try: %s <source.hlx|source.c> [-o <output>] [-asm] [-run]\n", progname);
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

static char *short_path_dup(const char *path) {
#ifdef _WIN32
    char buf[1024];
    unsigned long n = GetShortPathNameA(path, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        return strdup(buf);
#endif
    return strdup(path);
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

static int map_work_drive(const char *dir) {
    const char *const argv[] = {"cmd", "/c", "subst", "X:", dir, NULL};
    return run_process("cmd", argv);
}

static void unmap_work_drive(void) {
    const char *const argv[] = {"cmd", "/c", "subst", "X:", "/d", NULL};
    run_process("cmd", argv);
}

static int compile_helix(const char *source_path, const char *output_path) {
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

static int compile_source(const char *source_path, const char *output_path) {
    const char *dot = strrchr(source_path, '.');

    if (dot && strcmp(dot, ".c") == 0)
        return compile_c(source_path, output_path);
    if (dot && strcmp(dot, ".hlx") == 0)
        return compile_helix(source_path, output_path);

    fprintf(stderr, "Forge error: unsupported source file '%s'\n", source_path);
    fprintf(stderr, "Expected .hlx or .c\n");
    return 1;
}

/* Assemble .asm into .obj */
static int assemble_object(const char *asm_path, const char *obj_path) {
    char *asm_short = short_path_dup(asm_path);
    char *obj_short = short_path_dup(obj_path);
    const char *const nasm_argv[] = {"nasm", "-f", "win64", asm_short, "-o", obj_short, NULL};
    int rc = run_process("nasm", nasm_argv);
    free(asm_short);
    free(obj_short);
    return rc;
}

/* Link .obj into .exe without gcc. Uses ld + MinGW import libs. */
static int link_executable(const char *obj_path, const char *out_path) {
    char *obj_short = short_path_dup(obj_path);
    char *out_short = short_path_dup(out_path);
    const char *const ld_argv[] = {
        "ld",
        "-o", out_short,
        CRT2_OBJ,
        obj_short,
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
    int rc = run_process("ld", ld_argv);
    free(obj_short);
    free(out_short);
    return rc;
}

int main(int argc, char *argv[]) {
    const char *source_path = NULL;
    const char *output_path = NULL;
    int asm_only = 0;
    int do_run = 0;
    int i;
    char *default_output = NULL;
    char *source_base = NULL;
    int work_drive_mapped = 0;
    int result;
    const char *work_asm_path = "X:\\forge-build.asm";
    const char *work_obj_path = "X:\\forge-build.o";
    const char *work_exe_path = "X:\\forge-build.exe";

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
        result = compile_source(source_path, output_path);
        free(default_output);
        free(source_base);
        return result;
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

    result = compile_source(source_path, work_asm_path);
    if (result != 0) {
        goto cleanup;
    }

    result = assemble_object(work_asm_path, work_obj_path);
    if (result != 0) {
        fprintf(stderr, "Forge: nasm assembly failed\n");
        goto cleanup;
    }

    result = link_executable(work_obj_path, work_exe_path);
    if (result != 0) {
        fprintf(stderr, "Forge: linking failed\n");
        goto cleanup;
    }

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

    /* Cleanup temp files from exe build */
cleanup:
    remove(work_asm_path);
    remove(work_obj_path);
    remove(work_exe_path);
    if (work_drive_mapped)
        unmap_work_drive();

    free(default_output);
    free(source_base);

    if (do_run && result != 0)
        return 1;

    return 0;
}
