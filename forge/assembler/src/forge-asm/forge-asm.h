/*
 * forge-asm.h - Assembler interface for Forge
 *
 * Exposes the main assembly function to compile a .asm source file
 * directly into a COFF .obj file.
 */
#ifndef FORGE_ASM_H
#define FORGE_ASM_H

/* Compile .asm source file to COFF .obj file.
   Returns 0 on success, non-zero on error. */
int forge_assemble_file(const char *asm_path, const char *obj_path);

/* Compile ASM source text to COFF .obj file.
   Returns 0 on success, non-zero on error. */
int forge_assemble_text(const char *asm_text, const char *obj_path);

#endif /* FORGE_ASM_H */
