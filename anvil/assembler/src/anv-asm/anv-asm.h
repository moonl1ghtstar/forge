/*
 * anv-asm.h - Assembler interface for Anvil
 *
 * Exposes the main assembly function to compile a .asm source file
 * directly into a COFF .obj file.
 */
#ifndef ANVIL_ASM_H
#define ANVIL_ASM_H

/* Compile .asm source file to COFF .obj file.
   Returns 0 on success, non-zero on error. */
int anv_assemble_file(const char *asm_path, const char *obj_path);

/* Compile ASM source text to COFF .obj file.
   Returns 0 on success, non-zero on error. */
int anv_assemble_text(const char *asm_text, const char *obj_path);

#endif /* ANVIL_ASM_H */
