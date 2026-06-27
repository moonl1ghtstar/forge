/*
 * c-frontend.c - Frontend pipeline for Forge C subset
 */

#include <errno.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../helix/src/sema/helix-sema.h"
#include "../../../helix/src/codegen/helix-codegen.h"
#include "c-frontend.h"
#include "../parser/c-parser.h"

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

int c_parse_program_from_file(const char *source_path, ASTNode **program_out) {
    char *source;
    CLexer lexer;
    CParser parser;
    ASTNode *program;

    source = read_file(source_path);
    if (!source)
        return 1;

    c_lexer_init(&lexer, source);
    c_parser_init(&parser, &lexer); /* inject_main=1: exe mode */
    program = c_parse_program(&parser);

    if (parser.had_error) {
        ast_free(program);
        free(source);
        return 1;
    }

    *program_out = program;
    free(source);
    return 0;
}

/*
 * c_parse_program_from_file_lib:
 *   Parse a C file in library mode (inject_main=0).
 *   Use this when compiling a .c file to .obj for later linking;
 *   the file may not have a main() and should not have one synthesised.
 */
int c_parse_program_from_file_lib(const char *source_path, ASTNode **program_out) {
    char *source;
    CLexer lexer;
    CParser parser;
    ASTNode *program;

    source = read_file(source_path);
    if (!source)
        return 1;

    c_lexer_init(&lexer, source);
    c_parser_init_lib(&parser, &lexer); /* inject_main=0: library mode */
    program = c_parse_program(&parser);

    if (parser.had_error) {
        ast_free(program);
        free(source);
        return 1;
    }

    *program_out = program;
    free(source);
    return 0;
}

int compile_c(const char *source_path, const char *output_path) {
    ASTNode *program;
    FILE *out;
    int result;

    result = c_parse_program_from_file(source_path, &program);
    if (result != 0) {
        fprintf(stderr, "Forge error: C parsing failed. Fix the errors above, then run again.\n");
        return 1;
    }

    result = sema_analyze(program);
    if (result != 0) {
        fprintf(stderr, "Forge error: semantic checks failed. Fix the errors above, then run again.\n");
        ast_free(program);
        return 1;
    }

    out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Forge error: cannot open output file '%s'\n", output_path);
        ast_free(program);
        return 1;
    }

    codegen_emit(program, out);
    fclose(out);

    printf("Forge: compiled '%s' -> '%s'\n", source_path, output_path);

    ast_free(program);
    return 0;
}

/*
 * compile_c_lib:
 *   Compile a C file to .asm in library mode (no main() injected).
 *   Used when the -obj flag targets a file that provides functions
 *   but is not itself an entry point.
 */
int compile_c_lib(const char *source_path, const char *output_path) {
    ASTNode *program;
    FILE *out;
    int result;

    result = c_parse_program_from_file_lib(source_path, &program);
    if (result != 0) {
        fprintf(stderr, "Forge error: C parsing failed. Fix the errors above, then run again.\n");
        return 1;
    }

    result = sema_analyze(program);
    if (result != 0) {
        fprintf(stderr, "Forge error: semantic checks failed. Fix the errors above, then run again.\n");
        ast_free(program);
        return 1;
    }

    out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Forge error: cannot open output file '%s'\n", output_path);
        ast_free(program);
        return 1;
    }

    codegen_emit(program, out);
    fclose(out);

    printf("Forge: compiled '%s' -> '%s'\n", source_path, output_path);

    ast_free(program);
    return 0;
}
