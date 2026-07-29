#ifndef FORGE_ERRORS_H
#define FORGE_ERRORS_H

#include <stdio.h>

typedef enum {
    SEV_ERROR,
    SEV_WARN
} Severity;

extern int g_debug;

#define DEBUG_PRINT(...) \
    do { \
        if (g_debug) \
            fprintf(stderr, __VA_ARGS__); \
    } while (0)

/* Store global file context to print the line/pointer */
void forge_set_current_file(const char *path, const char *source);
const char *forge_get_current_file_path(void);
const char *forge_get_current_file_source(void);
void forge_free_errors(void);

void forge_report_error(
    Severity severity,
    const char *err_code,
    int line,
    int col,
    const char *note_kind,
    const char *note_msg,
    const char *fmt,
    ...
);

#endif /* FORGE_ERRORS_H */
