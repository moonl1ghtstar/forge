#include <stdio.h>
#include <string.h>
#include <windows.h>

char *console_input(void) {
    static char buf[256];
    if (scanf(" %255s", buf) != 1)
        buf[0] = '\0';
    return buf;
}

int console_clear(void) {
    printf("\x1b[2J\x1b[H");
    return 0;
}

int console_color(int code) {
    printf("\x1b[38;2;%d;%d;%dm", (code >> 16) & 255, (code >> 8) & 255, code & 255);
    return 0;
}
