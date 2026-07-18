/*
 * coff-writer.h - Windows x64 COFF object file writer
 *
 * Assembles raw section bytes, symbols, and relocations into a
 * valid COFF (.obj) file format compatible with Windows x64 (PE/COFF).
 */
#ifndef FORGE_COFF_WRITER_H
#define FORGE_COFF_WRITER_H

#include <stdint.h>
#include <stddef.h>
#include "x86-encode.h"

/* Structure to hold section contents */
typedef struct {
    uint8_t  *data;
    uint32_t  size;
    uint32_t  cap;
} CoffSection;

/* Symbol structure for COFF output */
typedef struct {
    char    *name;
    uint32_t value;         /* offset in section or 0 for extern */
    int      section_num;   /* 1=.text, 2=.data, 3=.bss, 0=extern/undef */
    int      is_global;     /* 1=IMAGE_SYM_CLASS_EXTERNAL, 0=IMAGE_SYM_CLASS_STATIC */
} CoffSym;

/* Main COFF writer entry point */
int coff_write_object(const char *output_path,
                      const CoffSection *text_sec,
                      const X86Reloc *text_relocs,
                      int text_reloc_count,
                      const CoffSection *data_sec,
                      uint32_t bss_size,
                      const CoffSym *symbols,
                      int symbol_count);

#endif /* FORGE_COFF_WRITER_H */
