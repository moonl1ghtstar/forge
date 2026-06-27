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
#include "parser/c-parser.h"

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

int compile_c(const char *source_path, const char *output_path) {
    char *source;
    CLexer lexer;
    CParser parser;
    ASTNode *program;
    FILE *out;
    int result;

    source = read_file(source_path);
    if (!source)
        return 1;

    c_lexer_init(&lexer, source);
    c_parser_init(&parser, &lexer);
    program = c_parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "Forge error: C parsing failed. Fix the errors above, then run again.\n");
        ast_free(program);
        free(source);
        return 1;
    }

    result = sema_analyze(program);
    if (result != 0) {
        fprintf(stderr, "Forge error: semantic checks failed. Fix the errors above, then run again.\n");
        ast_free(program);
        free(source);
        return 1;
    }

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

    ast_free(program);
    free(source);
    return 0;
}
