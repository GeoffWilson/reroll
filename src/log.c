#include <stdarg.h>
#include "runtime.h"
#include "log.h"

static HANDLE log_file = INVALID_HANDLE_VALUE;

/* Appends, because the game swaps processes between races and menus and each
 * copy loads us again; the launch script clears the log once per session. */
void log_init(void)
{
    log_file = CreateFileA("reroll.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
}

static char *put_uint(char *p, char *end, uint32_t v, uint32_t base, int min_digits)
{
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    while (n < min_digits)
        tmp[n++] = '0';
    while (n && p < end)
        *p++ = tmp[--n];
    return p;
}

void log_printf(const char *fmt, ...)
{
    char buf[512];
    char *p = buf, *end = buf + sizeof(buf) - 1;
    va_list ap;
    DWORD written;

    if (log_file == INVALID_HANDLE_VALUE)
        return;
    p = put_uint(p, end, GetTickCount(), 10, 1);
    *p++ = ' ';

    va_start(ap, fmt);
    for (; *fmt && p < end; fmt++) {
        if (*fmt != '%') {
            *p++ = *fmt;
            continue;
        }
        switch (*++fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            while (s && *s && p < end)
                *p++ = *s++;
            break;
        }
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            if (v < 0 && p < end) {
                *p++ = '-';
                v = -v;
            }
            p = put_uint(p, end, (uint32_t)v, 10, 1);
            break;
        }
        case 'u':
            p = put_uint(p, end, va_arg(ap, uint32_t), 10, 1);
            break;
        case 'x':
            p = put_uint(p, end, va_arg(ap, uint32_t), 16, 1);
            break;
        case 'p':
            p = put_uint(p, end, va_arg(ap, uint32_t), 16, 8);
            break;
        case '%':
            *p++ = '%';
            break;
        default:
            fmt--;
            break;
        }
    }
    va_end(ap);
    *p++ = '\n';
    WriteFile(log_file, buf, (DWORD)(p - buf), &written, 0);
}
