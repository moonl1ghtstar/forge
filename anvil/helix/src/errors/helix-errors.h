#ifndef ANVIL_ERRORS_H
#define ANVIL_ERRORS_H

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
void anv_set_current_file(const char *path, const char *source);
const char *anv_get_current_file_path(void);
const char *anv_get_current_file_source(void);
void anv_free_errors(void);

void anv_report_error(
    Severity severity,
    const char *err_code,
    int line,
    int col,
    const char *note_kind,
    const char *note_msg,
    const char *fmt,
    ...
);

#endif /* ANVIL_ERRORS_H */
