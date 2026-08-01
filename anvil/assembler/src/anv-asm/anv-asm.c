/*
 * anv-asm.c - Assembler implementation for Anvil
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm-parser.h"
#include "x86-encode.h"
#include "coff-writer.h"
#include "anv-asm.h"

typedef struct {
    char    *name;
    uint32_t offset;
    int      section_num;
} AsmLabel;

typedef struct {
    AsmLabel *items;
    int       count;
    int       cap;
} LabelTable;

static void label_add(LabelTable *table, const char *name, uint32_t offset, int section_num) {
    /* Avoid duplicates in table */
    int i;
    for (i = 0; i < table->count; i++) {
        if (strcmp(table->items[i].name, name) == 0) {
            return;
        }
    }
    if (table->count >= table->cap) {
        table->cap = table->cap ? table->cap * 2 : 32;
        table->items = (AsmLabel *)realloc(table->items, sizeof(AsmLabel) * table->cap);
    }
    table->items[table->count].name = strdup(name);
    table->items[table->count].offset = offset;
    table->items[table->count].section_num = section_num;
    table->count++;
}

typedef struct {
    char **items;
    int    count;
    int    cap;
} SymList;

static void sym_list_add(SymList *list, const char *name) {
    int i;
    for (i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], name) == 0)
            return;
    }
    if (list->count >= list->cap) {
        list->cap = list->cap ? list->cap * 2 : 16;
        list->items = (char **)realloc(list->items, sizeof(char *) * list->cap);
    }
    list->items[list->count++] = strdup(name);
}

static int sym_list_has(const SymList *list, const char *name) {
    int i;
    for (i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], name) == 0)
            return 1;
    }
    return 0;
}

static void sym_list_free(SymList *list) {
    int i;
    for (i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
}

static long resolve_label_cb(const char *name, void *data) {
    LabelTable *table = (LabelTable *)data;
    int i;
    for (i = 0; i < table->count; i++) {
        if (strcmp(table->items[i].name, name) == 0) {
            /* Only resolve local jumps in .text directly (section 1) */
            if (table->items[i].section_num == 1) {
                return (long)table->items[i].offset;
            }
            break;
        }
    }
    return -1;
}

static void sec_append_byte(CoffSection *sec, uint8_t b) {
    if (sec->size >= sec->cap) {
        sec->cap = sec->cap ? sec->cap * 2 : 1024;
        sec->data = (uint8_t *)realloc(sec->data, sec->cap);
    }
    sec->data[sec->size++] = b;
}

int anv_assemble_text(const char *asm_text, const char *obj_path) {
    AsmProgram *prog = asm_parse(asm_text);
    if (!prog || prog->had_error) {
        fprintf(stderr, "anv-asm: assembly parsing failed.\n");
        asm_program_free(prog);
        return 1;
    }

    LabelTable label_table = {NULL, 0, 0};
    SymList global_syms = {NULL, 0, 0};
    SymList extern_syms = {NULL, 0, 0};

    uint32_t text_offset = 0;
    uint32_t data_offset = 0;
    uint32_t bss_offset = 0;
    int curr_section = 1; /* 1=.text, 2=.data, 3=.bss */

    /* Pass 1: Measure statements and register all labels */
    int i, j;
    for (i = 0; i < prog->count; i++) {
        AsmStatement *s = &prog->stmts[i];

        if (s->kind == ASM_STMT_SECTION) {
            if (strcmp(s->section_name, ".text") == 0) curr_section = 1;
            else if (strcmp(s->section_name, ".data") == 0) curr_section = 2;
            else if (strcmp(s->section_name, ".bss") == 0) curr_section = 3;
        }
        else if (s->kind == ASM_STMT_GLOBAL) {
            sym_list_add(&global_syms, s->sym_name);
        }
        else if (s->kind == ASM_STMT_EXTERN) {
            sym_list_add(&extern_syms, s->sym_name);
        }
        else if (s->kind == ASM_STMT_LABEL) {
            label_add(&label_table, s->sym_name,
                      curr_section == 1 ? text_offset :
                      (curr_section == 2 ? data_offset : bss_offset),
                      curr_section);
        }
        else if (s->kind == ASM_STMT_DB || s->kind == ASM_STMT_DW ||
                 s->kind == ASM_STMT_DD || s->kind == ASM_STMT_DQ) {
            int elem_size = (s->kind == ASM_STMT_DB) ? 1 :
                            (s->kind == ASM_STMT_DW) ? 2 :
                            (s->kind == ASM_STMT_DD) ? 4 : 8;
            /* Data always goes into the current section's data area */
            int sec = (curr_section == 2) ? 2 : curr_section;
            uint32_t *off_ptr = (sec == 1) ? &text_offset :
                                (sec == 2) ? &data_offset : &bss_offset;
            if (s->label)
                label_add(&label_table, s->label, *off_ptr, sec);
            uint32_t size = 0;
            for (j = 0; j < s->db_count; j++) {
                if (s->db_entries[j].is_string)
                    size += (uint32_t)s->db_entries[j].as.str.len;
                else
                    size += (uint32_t)elem_size;
            }
            *off_ptr += size;
        }
        else if (s->kind == ASM_STMT_RESB || s->kind == ASM_STMT_RESW ||
                 s->kind == ASM_STMT_RESD || s->kind == ASM_STMT_RESQ) {
            int esz = s->res_elem_size ? s->res_elem_size :
                      (s->kind == ASM_STMT_RESB) ? 1 :
                      (s->kind == ASM_STMT_RESW) ? 2 :
                      (s->kind == ASM_STMT_RESD) ? 4 : 8;
            if (s->label)
                label_add(&label_table, s->label, bss_offset, 3);
            bss_offset += (uint32_t)(s->resb_count * esz);
        }
        else if (s->kind == ASM_STMT_INSTR) {
            if (s->label) {
                label_add(&label_table, s->label, text_offset, 1);
            }
            int sz = x86_measure(s);
            if (sz < 0) {
                fprintf(stderr, "anv-asm: line %d: error measuring instruction '%s'\n",
                        s->line, s->mnemonic);
                goto error_cleanup;
            }
            text_offset += sz;
        }
    }

    /* Pass 2: Encode instructions and populate section data */
    CoffSection text_sec = {NULL, 0, 0};
    CoffSection data_sec = {NULL, 0, 0};
    X86EncodeCtx enc_ctx;
    x86_ctx_init(&enc_ctx, resolve_label_cb, &label_table);

    curr_section = 1;
    for (i = 0; i < prog->count; i++) {
        AsmStatement *s = &prog->stmts[i];

        if (s->kind == ASM_STMT_SECTION) {
            if (strcmp(s->section_name, ".text") == 0) curr_section = 1;
            else if (strcmp(s->section_name, ".data") == 0) curr_section = 2;
            else if (strcmp(s->section_name, ".bss") == 0) curr_section = 3;
        }
        else if (s->kind == ASM_STMT_DB || s->kind == ASM_STMT_DW ||
                 s->kind == ASM_STMT_DD || s->kind == ASM_STMT_DQ) {
            int elem_size = (s->kind == ASM_STMT_DB) ? 1 :
                            (s->kind == ASM_STMT_DW) ? 2 :
                            (s->kind == ASM_STMT_DD) ? 4 : 8;
            CoffSection *dsec = (curr_section == 1) ? &text_sec : &data_sec;
            for (j = 0; j < s->db_count; j++) {
                if (s->db_entries[j].is_string) {
                    int k;
                    for (k = 0; k < s->db_entries[j].as.str.len; k++)
                        sec_append_byte(dsec, (uint8_t)s->db_entries[j].as.str.data[k]);
                } else {
                    /* emit elem_size bytes, little-endian */
                    long v = s->db_entries[j].as.byte_val;
                    int b;
                    for (b = 0; b < elem_size; b++)
                        sec_append_byte(dsec, (uint8_t)((v >> (b * 8)) & 0xFF));
                }
            }
        }
        else if (s->kind == ASM_STMT_INSTR) {
            uint8_t ibuf[32];
            int sz = x86_encode(&enc_ctx, s, ibuf, sizeof(ibuf));
            if (sz < 0) {
                fprintf(stderr, "anv-asm: line %d: failed to encode instruction '%s'\n",
                        s->line, s->mnemonic);
                x86_ctx_free(&enc_ctx);
                free(text_sec.data);
                free(data_sec.data);
                goto error_cleanup;
            }
            int k;
            for (k = 0; k < sz; k++) {
                sec_append_byte(&text_sec, ibuf[k]);
            }
            enc_ctx.section_offset = text_sec.size;
        }
    }

    /* Build COFF Symbol Table.
       All labels, globals, and externs should be in this list. */
    int sym_cap = label_table.count + extern_syms.count;
    CoffSym *symbols = (CoffSym *)calloc(sym_cap, sizeof(CoffSym));
    int sym_count = 0;

    /* 1. Add all extern symbols */
    for (i = 0; i < extern_syms.count; i++) {
        CoffSym *sym = &symbols[sym_count++];
        sym->name = extern_syms.items[i];
        sym->value = 0;
        sym->section_num = 0;
        sym->is_global = 1;
    }

    /* 2. Add all defined labels */
    for (i = 0; i < label_table.count; i++) {
        const AsmLabel *lbl = &label_table.items[i];
        CoffSym *sym = &symbols[sym_count++];
        sym->name = lbl->name;
        sym->value = lbl->offset;
        sym->section_num = lbl->section_num;
        sym->is_global = sym_list_has(&global_syms, lbl->name);
    }

    /* Write out the COFF object file */
    int rc = coff_write_object(obj_path,
                               &text_sec,
                               enc_ctx.relocs,
                               enc_ctx.reloc_count,
                               &data_sec,
                               bss_offset,
                               symbols,
                               sym_count);

    /* Free all resources */
    x86_ctx_free(&enc_ctx);
    free(text_sec.data);
    free(data_sec.data);
    free(symbols);

    for (i = 0; i < label_table.count; i++) free(label_table.items[i].name);
    free(label_table.items);
    sym_list_free(&global_syms);
    sym_list_free(&extern_syms);
    asm_program_free(prog);

    return rc;

error_cleanup:
    for (i = 0; i < label_table.count; i++) free(label_table.items[i].name);
    free(label_table.items);
    sym_list_free(&global_syms);
    sym_list_free(&extern_syms);
    asm_program_free(prog);
    return 1;
}

static char *read_file_content(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

int anv_assemble_file(const char *asm_path, const char *obj_path) {
    char *content = read_file_content(asm_path);
    if (!content) {
        fprintf(stderr, "anv-asm: cannot read file '%s'\n", asm_path);
        return 1;
    }
    int rc = anv_assemble_text(content, obj_path);
    free(content);
    return rc;
}
