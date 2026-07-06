#ifndef FORGE_ERRORS_H
#define FORGE_ERRORS_H

typedef enum {
    SEV_ERROR,
    SEV_WARN
} Severity;

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
