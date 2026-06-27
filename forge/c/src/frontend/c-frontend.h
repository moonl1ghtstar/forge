/*
 * c-frontend.h - Forge C frontend entry point
 */

#ifndef FORGE_C_FRONTEND_H
#define FORGE_C_FRONTEND_H

struct ASTNode;

/* Executable mode: synthesises main() if none present. */
int compile_c(const char *source_path, const char *output_path);
int c_parse_program_from_file(const char *source_path, struct ASTNode **program_out);

/*
 * Library mode: no implicit main() injection.
 * Use when compiling a .c file to .obj that will be linked with
 * another object providing main() (e.g. a Forge Helix .obj).
 */
int compile_c_lib(const char *source_path, const char *output_path);
int c_parse_program_from_file_lib(const char *source_path, struct ASTNode **program_out);

#endif
