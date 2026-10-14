/* Logs the first few exceptions (code, address, registers, stack) to
 * reroll.log, then lets them take their normal course. (The native layer's
 * AddVectoredExceptionHandler installs its own fault logger instead, which
 * writes to native.log.) */
#include "crash.h"

#include "log.h"
#include "runtime.h"

struct exception_record {
    DWORD code, flags;
    struct exception_record *next;
    void *address;
    DWORD parameter_count;
    uint32_t parameters[15];
};

struct exception_pointers {
    struct exception_record *record;
    uint8_t *context; /* x86 CONTEXT */
};

__declspec(dllimport) void *WINAPI AddVectoredExceptionHandler(DWORD first, long(WINAPI *handler)(struct exception_pointers *));

static long WINAPI handler(struct exception_pointers *info)
{
    static int logged;
    const struct exception_record *r = info->record;
    const uint8_t *c = info->context;
    const uint32_t *stack;
    int i;

    /* only real faults: access violations, illegal instructions, divides, stack overflow */
    if (r->code < 0xc0000000u || r->code == 0xc0000027u || logged >= 4)
        return 0;
    logged++;
    log_printf("crash: exception %x at %p (%x %x)", r->code, (uint32_t)(uintptr_t)r->address,
               r->parameter_count > 0 ? r->parameters[0] : 0, r->parameter_count > 1 ? r->parameters[1] : 0);
#if defined(__i386__) /* the registers: a 32-bit CONTEXT */
    log_printf("crash: eax %x ebx %x ecx %x edx %x esi %x edi %x ebp %x esp %x", *(const uint32_t *)(c + 0xb0),
               *(const uint32_t *)(c + 0xa4), *(const uint32_t *)(c + 0xac), *(const uint32_t *)(c + 0xa8),
               *(const uint32_t *)(c + 0xa0), *(const uint32_t *)(c + 0x9c), *(const uint32_t *)(c + 0xb4),
               *(const uint32_t *)(c + 0xc4));
    stack = (const uint32_t *)*(const uint32_t *)(c + 0xc4);
    for (i = 0; i < 24; i += 4)
        log_printf("crash: stack +%x: %x %x %x %x", i * 4, stack[i], stack[i + 1], stack[i + 2], stack[i + 3]);
#else
    (void)c;
    (void)stack;
    (void)i;
#endif
    return 0; /* EXCEPTION_CONTINUE_SEARCH */
}

void crash_install(void)
{
    AddVectoredExceptionHandler(1, handler);
}
