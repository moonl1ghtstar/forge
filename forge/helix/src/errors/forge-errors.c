#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "forge-errors.h"

#ifdef _WIN32
#include <windows.h>
static void enable_ansi_colors(void) {
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hErr, &mode))
            SetConsoleMode(hErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}
#else
static void enable_ansi_colors(void) { /* no-op */ }
#endif

#define COLOR_RED     "\033[1;31m"
#define COLOR_WARN    "\033[1;33m"
#define COLOR_BOLD    "\033[1;37m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_RESET   "\033[0m"

int g_debug = 0;

static char *current_path = NULL;
static char *current_source = NULL;

void forge_set_current_file(const char *path, const char *source) {
    if (current_path) {
        free(current_path);
        current_path = NULL;
    }
    if (current_source) {
        free(current_source);
        current_source = NULL;
    }
    if (path) current_path = strdup(path);
    if (source) current_source = strdup(source);
}

const char *forge_get_current_file_path(void) {
    return current_path;
}

const char *forge_get_current_file_source(void) {
    return current_source;
}

void forge_free_errors(void) {
    if (current_path) {
        free(current_path);
        current_path = NULL;
    }
    if (current_source) {
        free(current_source);
        current_source = NULL;
    }
}

static void get_source_line(const char *source, int line_num, const char **line_start_out, int *line_len_out) {
    if (!source || line_num < 1) {
        *line_start_out = NULL;
        *line_len_out = 0;
        return;
    }
    const char *curr = source;
    int current_line = 1;
    while (current_line < line_num && *curr != '\0') {
        if (*curr == '\n') {
            current_line++;
        }
        curr++;
    }
    if (*curr == '\0' && current_line < line_num) {
        *line_start_out = NULL;
        *line_len_out = 0;
        return;
    }
    const char *start = curr;
    while (*curr != '\0' && *curr != '\n' && *curr != '\r') {
        curr++;
    }
    *line_start_out = start;
    *line_len_out = (int)(curr - start);
}

void forge_report_error(
    Severity severity,
    const char *err_code,
    int line,
    int col,
    const char *note_kind,
    const char *note_msg,
    const char *fmt,
    ...
) {
    enable_ansi_colors();

    const char *sev_str = (severity == SEV_ERROR) ? "error" : "warn";
    const char *color_sev = (severity == SEV_ERROR) ? COLOR_RED : COLOR_WARN;

    // Print message
    fprintf(stderr, "%s%s[%s]%s: %s", color_sev, sev_str, err_code, COLOR_RESET, COLOR_BOLD);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "%s\n", COLOR_RESET);

    // Print location & context if path, source, and line are valid
    if (current_path && current_source && line > 0) {
        // Compute digit width of line number for proper gutter alignment
        int lw = snprintf(NULL, 0, "%d", line);

        fprintf(stderr, " %s%*s-->%s %s%s:%d:%d%s\n",
                COLOR_CYAN, lw, "", COLOR_RESET,
                COLOR_BOLD, current_path, line, col > 0 ? col : 1, COLOR_RESET);

        const char *line_start = NULL;
        int line_len = 0;
        get_source_line(current_source, line, &line_start, &line_len);

        if (line_start) {
            // Empty gutter line
            fprintf(stderr, " %s%*s |%s\n", COLOR_CYAN, lw, "", COLOR_RESET);
            // Code line with line number
            fprintf(stderr, " %s%d |%s %.*s\n", COLOR_CYAN, line, COLOR_RESET, line_len, line_start);
            // Pointer line
            fprintf(stderr, " %s%*s |%s ", COLOR_CYAN, lw, "", COLOR_RESET);
            if (col > 0) {
                for (int i = 0; i < col - 1; i++) {
                    if (line_start[i] == '\t') {
                        fprintf(stderr, "\t");
                    } else {
                        fprintf(stderr, " ");
                    }
                }
                fprintf(stderr, "%s^%s", color_sev, COLOR_RESET);
            }
            fprintf(stderr, "\n");
            // Closing gutter line
            fprintf(stderr, " %s%*s |%s\n", COLOR_CYAN, lw, "", COLOR_RESET);
        }
    }

    if (note_kind && note_msg) {
        fprintf(stderr, "= %s%s%s: %s\n", COLOR_BOLD, note_kind, COLOR_RESET, note_msg);
    }
    fprintf(stderr, "\n");
}
