/*
 * x86-encode.h - x86-64 instruction encoder for Anvil ASM
 *
 * Encodes AsmStatement (instruction form) into raw bytes.
 * Supports the subset of x86-64 instructions emitted by Anvil's codegen
 * and commonly found in hand-written ASM for the Windows x64 ABI.
 *
 * Two-pass usage:
 *   1. x86_measure()  - returns byte count for an instruction (uses near forms)
 *   2. x86_encode()   - encodes the instruction into a buffer, resolving
 *                       label references via a caller-supplied callback
 *
 * Relocations are described by X86Reloc structs and appended to a
 * caller-managed list.
 */
#ifndef ANVIL_X86_ENCODE_H
#define ANVIL_X86_ENCODE_H

#include <stdint.h>
#include <stddef.h>
#include "asm-parser.h"

/* A relocation entry: patch 4 bytes at (section_offset + reloc_offset)
 * to hold a REL32 reference to symbol. */
typedef struct {
    uint32_t offset;    /* byte offset within the .text section */
    char    *symbol;    /* target symbol name (heap-allocated copy) */
    int      is_rel32;  /* 1=IMAGE_REL_AMD64_REL32, 0=ADDR32NB */
} X86Reloc;

/* Context passed to x86_encode for the current section offset and reloc list */
typedef struct {
    uint32_t  section_offset;      /* current write position in .text */

    X86Reloc *relocs;
    int       reloc_count;
    int       reloc_cap;

    /* Callback: caller provides offset of a named local label, or -1 if unknown */
    long (*resolve_label)(const char *name, void *ctx_data);
    void *ctx_data;
} X86EncodeCtx;

/* Initialise an encode context */
void x86_ctx_init(X86EncodeCtx *ctx,
                  long (*resolve_label)(const char *name, void *data),
                  void *data);
void x86_ctx_free(X86EncodeCtx *ctx);

/* Add a relocation */
void x86_add_reloc(X86EncodeCtx *ctx, uint32_t offset, const char *sym);

/* Encode an instruction.
 * Returns the number of bytes written into `buf` (max 15), or -1 on error.
 * `stmt` must be an ASM_STMT_INSTR. */
int x86_encode(X86EncodeCtx *ctx, const AsmStatement *stmt,
               uint8_t *buf, int buf_size);

/* Measure the size of an instruction without encoding (uses near forms).
 * Returns byte count or -1 on error. */
int x86_measure(const AsmStatement *stmt);

#endif /* ANVIL_X86_ENCODE_H */
