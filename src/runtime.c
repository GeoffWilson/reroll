/* For the emulator tests' freestanding build (make emu-test): the compiler may
 * emit calls to these even with -fno-builtin, and there is no C library.
 * memcpy and memcmp work a dword at a time: verify.c copies and compares
 * megabytes of game memory every physics step. */
#include "runtime.h"

void *memcpy(void *dst, const void *src, size_t n)
{
    void *d = dst;
    const void *s = src;
    size_t dwords = n >> 2, bytes = n & 3;

    __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(dwords) : : "memory");
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(bytes) : : "memory");
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;

    for (; n >= 4; n -= 4, x += 4, y += 4)
        if (*(const uint32_t *)x != *(const uint32_t *)y)
            break;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

/* Referenced by MSVC-target objects that use floating point (wheels.c). */
int _fltused;
