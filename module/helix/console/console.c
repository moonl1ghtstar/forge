#include <stdint.h>
#include <stdio.h>
#include <windows.h>

static int console_is_string(uintptr_t value) {
    MEMORY_BASIC_INFORMATION info;
    const char *s = (const char *)value;
    size_t i;

    if (value < 65536)
        return 0;
    if (!VirtualQuery(s, &info, sizeof(info)))
        return 0;
    if (info.State != MEM_COMMIT || (info.Protect & PAGE_NOACCESS))
        return 0;

    for (i = 0; i < 4096; i++) {
        if (s[i] == '\0')
            return 1;
    }
    return 0;
}

int console_print(intptr_t value) {
    if (console_is_string((uintptr_t)value))
        printf("%s\n", (const char *)value);
    else
        printf("%lld\n", (long long)value);
    return 0;
}

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
