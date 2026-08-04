/*
 * coff-writer.c - Windows x64 COFF object file writer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coff-writer.h"
#include "helix-errors.h"

/* Structure packing to match file format exactly */
#pragma pack(push, 1)

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} CoffHeader;

typedef struct {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} CoffSecHeader;

typedef struct {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
} CoffRelocEntry;

typedef struct {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeros;
            uint32_t Offset;
        } LongName;
    } Name;
    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
} CoffSymEntry;

#pragma pack(pop)

/* Align value up to multiple */
static uint32_t align_up(uint32_t val, uint32_t mult) {
    if (mult == 0)
        return val;
    return (val + mult - 1) & ~(mult - 1);
}

int coff_write_object(const char *output_path,
                      const CoffSection *text_sec,
                      const X86Reloc *text_relocs,
                      int text_reloc_count,
                      const CoffSection *data_sec,
                      uint32_t bss_size,
                      const CoffSym *symbols,
                      int symbol_count) {
    FILE *f = fopen(output_path, "wb");
    if (!f) {
        anv_report_error(SEV_ERROR, "E902", 0, 0, NULL, NULL, "failed to write assembly output: cannot open '%s'", output_path);
        return 1;
    }

    int num_sections = 0;
    int text_idx = 0, data_idx = 0, bss_idx = 0;

    if (text_sec && text_sec->size > 0)
        text_idx = ++num_sections;
    if (data_sec && data_sec->size > 0)
        data_idx = ++num_sections;
    if (bss_size > 0)
        bss_idx = ++num_sections;

    /* Build headers */
    CoffHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.Machine = 0x8664; /* AMD64 */
    hdr.NumberOfSections = num_sections;
    hdr.TimeDateStamp = 0;
    hdr.SizeOfOptionalHeader = 0;
    hdr.Characteristics = 0;

    /* Section headers */
    CoffSecHeader *sec_hdrs = (CoffSecHeader *)calloc(num_sections, sizeof(CoffSecHeader));
    int curr_sec = 0;

    uint32_t file_offset = sizeof(CoffHeader) + num_sections * sizeof(CoffSecHeader);

    /* NASM only emits extern symbols that are actually referenced by a
     * relocation, so drop unreferenced ones to match its output. */
    int *extern_used = (int *)calloc(symbol_count > 0 ? (size_t)symbol_count : 1, sizeof(int));
    {
        int r;
        for (r = 0; r < text_reloc_count; r++) {
            int s;
            for (s = 0; s < symbol_count; s++) {
                if (symbols[s].section_num == 0 &&
                    strcmp(symbols[s].name, text_relocs[r].symbol) == 0) {
                    extern_used[s] = 1;
                    break;
                }
            }
        }
    }
    int kept = 0;
    {
        int k;
        for (k = 0; k < symbol_count; k++) {
            if (symbols[k].section_num != 0 || extern_used[k])
                kept++;
        }
    }
    int all_count = num_sections + kept;
    CoffSym *all_syms = (CoffSym *)calloc(all_count > 0 ? (size_t)all_count : 1, sizeof(CoffSym));
    int ai = 0;
    if (text_idx > 0) {
        all_syms[ai].name = ".text";
        all_syms[ai].value = 0;
        all_syms[ai].section_num = 1;
        all_syms[ai].is_global = 0;
        ai++;
    }
    if (data_idx > 0) {
        all_syms[ai].name = ".data";
        all_syms[ai].value = 0;
        all_syms[ai].section_num = 2;
        all_syms[ai].is_global = 0;
        ai++;
    }
    if (bss_idx > 0) {
        all_syms[ai].name = ".bss";
        all_syms[ai].value = 0;
        all_syms[ai].section_num = 3;
        all_syms[ai].is_global = 0;
        ai++;
    }
    {
        int k;
        for (k = 0; k < symbol_count; k++) {
            if (symbols[k].section_num != 0 || extern_used[k])
                all_syms[ai++] = symbols[k];
        }
    }
    free(extern_used);

    if (text_idx > 0) {
        CoffSecHeader *sh = &sec_hdrs[curr_sec++];
        memcpy(sh->Name, ".text\0\0\0", 8);
        sh->SizeOfRawData = text_sec->size;
        sh->PointerToRawData = file_offset;
        file_offset += align_up(text_sec->size, 4);

        if (text_reloc_count > 0) {
            sh->PointerToRelocations = file_offset;
            sh->NumberOfRelocations = text_reloc_count;
            file_offset += text_reloc_count * sizeof(CoffRelocEntry);
        }
        sh->Characteristics = 0x60500020; /* CNT_CODE | MEM_EXECUTE | MEM_READ | ALIGN_16 */
    }

    if (data_idx > 0) {
        CoffSecHeader *sh = &sec_hdrs[curr_sec++];
        memcpy(sh->Name, ".data\0\0\0", 8);
        sh->SizeOfRawData = data_sec->size;
        sh->PointerToRawData = file_offset;
        file_offset += align_up(data_sec->size, 4);
        sh->Characteristics = 0xC0300040; /* CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE | ALIGN_4 */
    }

    if (bss_idx > 0) {
        CoffSecHeader *sh = &sec_hdrs[curr_sec++];
        memcpy(sh->Name, ".bss\0\0\0\0", 8);
        sh->VirtualSize = bss_size;
        sh->SizeOfRawData = 0;
        sh->PointerToRawData = 0;
        sh->Characteristics = 0xC0300080; /* CNT_UNINITIALIZED_DATA | MEM_READ | MEM_WRITE | ALIGN_4 */
    }

    hdr.PointerToSymbolTable = file_offset;
    hdr.NumberOfSymbols = all_count;

    /* Write headers */
    fwrite(&hdr, 1, sizeof(hdr), f);
    fwrite(sec_hdrs, 1, num_sections * sizeof(CoffSecHeader), f);

    /* Write section data */
    if (text_idx > 0) {
        fwrite(text_sec->data, 1, text_sec->size, f);
        /* Pad to 4 bytes */
        uint32_t pad = align_up(text_sec->size, 4) - text_sec->size;
        if (pad > 0) {
            uint8_t zero[4] = {0};
            fwrite(zero, 1, pad, f);
        }

        /* Write relocations */
        if (text_reloc_count > 0) {
            CoffRelocEntry *re = (CoffRelocEntry *)calloc(text_reloc_count, sizeof(CoffRelocEntry));
            int i;
            for (i = 0; i < text_reloc_count; i++) {
                re[i].VirtualAddress = text_relocs[i].offset;

                /* Find symbol index */
                int sym_idx = -1;
                int s;
                for (s = 0; s < all_count; s++) {
                    if (strcmp(all_syms[s].name, text_relocs[i].symbol) == 0) {
                        sym_idx = s;
                        break;
                    }
                }
                if (sym_idx < 0) {
                    fprintf(stderr, "anv-coff: error: relocation to unresolved symbol '%s'\n",
                            text_relocs[i].symbol);
                    free(re);
                    free(sec_hdrs);
                    fclose(f);
                    return 1;
                }
                re[i].SymbolTableIndex = sym_idx;
                /* AMD64 REL32 = 4 */
                re[i].Type = text_relocs[i].is_rel32 ? 4 : 3;
            }
            fwrite(re, 1, text_reloc_count * sizeof(CoffRelocEntry), f);
            free(re);
        }
    }

    if (data_idx > 0) {
        fwrite(data_sec->data, 1, data_sec->size, f);
        uint32_t pad = align_up(data_sec->size, 4) - data_sec->size;
        if (pad > 0) {
            uint8_t zero[4] = {0};
            fwrite(zero, 1, pad, f);
        }
    }

    /* Build Symbol Table and String Table */
    /* String table starts after all symbols. First 4 bytes hold the size of string table. */
    uint32_t string_table_size = 4;
    int string_table_cap = 1024;
    char *string_table = (char *)malloc(string_table_cap);
    memset(string_table, 0, 4); /* size field placeholder */

    CoffSymEntry *sym_entries = (CoffSymEntry *)calloc(all_count > 0 ? (size_t)all_count : 1, sizeof(CoffSymEntry));
    int i;
    for (i = 0; i < all_count; i++) {
        const CoffSym *s = &all_syms[i];
        CoffSymEntry *se = &sym_entries[i];

        /* Symbol Name */
        size_t name_len = strlen(s->name);
        if (name_len <= 8) {
            strncpy(se->Name.ShortName, s->name, 8);
        } else {
            se->Name.LongName.Zeros = 0;
            se->Name.LongName.Offset = string_table_size;

            /* Add to string table */
            if (string_table_size + name_len + 1 > (size_t)string_table_cap) {
                string_table_cap *= 2;
                string_table = (char *)realloc(string_table, string_table_cap);
            }
            strcpy(string_table + string_table_size, s->name);
            string_table_size += (uint32_t)(name_len + 1);
        }

        se->Value = s->value;

        /* Section mapping */
        if (s->section_num == 1)
            se->SectionNumber = text_idx;
        else if (s->section_num == 2)
            se->SectionNumber = data_idx;
        else if (s->section_num == 3)
            se->SectionNumber = bss_idx;
        else
            se->SectionNumber = 0; /* UNDEF (extern) */

        se->Type = 0; /* Not a function type necessarily, simple scalar */
        /* Storage class: 2 = EXTERNAL, 3 = STATIC */
        se->StorageClass = s->is_global ? 2 : 3;
        se->NumberOfAuxSymbols = 0;
    }

    /* Update String Table size field */
    memcpy(string_table, &string_table_size, 4);

    /* Write Symbol and String Tables */
    fwrite(sym_entries, 1, all_count * sizeof(CoffSymEntry), f);
    fwrite(string_table, 1, string_table_size, f);

    free(sym_entries);
    free(string_table);
    free(sec_hdrs);
    free(all_syms);
    fclose(f);
    return 0;
}
