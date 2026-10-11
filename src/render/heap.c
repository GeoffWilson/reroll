/* malloc and friends on the process heap, for the renderer in the
 * freestanding Windows builds (see shim/stdlib.h). */
#include <stdlib.h>

typedef void *HANDLE;
__declspec(dllimport) HANDLE __stdcall GetProcessHeap(void);
__declspec(dllimport) void *__stdcall HeapAlloc(HANDLE heap, unsigned long flags, size_t size);
__declspec(dllimport) void *__stdcall HeapReAlloc(HANDLE heap, unsigned long flags, void *p, size_t size);
__declspec(dllimport) int __stdcall HeapFree(HANDLE heap, unsigned long flags, void *p);

#define HEAP_ZERO_MEMORY 0x8

void *malloc(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *calloc(size_t count, size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * size ? count * size : 1);
}

void *realloc(void *p, size_t size)
{
    if (!p)
        return malloc(size);
    return HeapReAlloc(GetProcessHeap(), 0, p, size ? size : 1);
}

void free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}
