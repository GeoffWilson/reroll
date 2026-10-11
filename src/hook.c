#include "hook.h"
#include "log.h"

#define TEXT_START 0x401000u
#define TEXT_SIZE 0xa2000u

int hook_init(void)
{
    DWORD old;

    /* Hooks are switched off and on around every verified physics step
     * (verify.c), so the code stays writable rather than being re-protected. */
    if (!VirtualProtect((void *)TEXT_START, TEXT_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        log_printf("hook: cannot make the game code writable");
        return 0;
    }
    return 1;
}

static void write_jmp(const struct hook *hook)
{
    uint8_t *target = (uint8_t *)hook->address;
    /* jmp rel32: E9 + displacement from the end of the 5-byte instruction */
    int32_t rel = (int32_t)((uintptr_t)hook->replacement - (hook->address + 5));

    target[0] = 0xE9;
    memcpy(target + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
}

int hook_install(const struct hook *hook)
{
    if (memcmp((const void *)hook->address, hook->expect, sizeof(hook->expect)) != 0) {
        log_printf("hook %s: unexpected bytes at %p, not patched", hook->name, hook->address);
        return 0;
    }
    write_jmp(hook);
    log_printf("hook %s: %p -> %p", hook->name, hook->address, (uint32_t)hook->replacement);
    return 1;
}

void hook_set_active(const struct hook *hook, int active)
{
    if (active) {
        write_jmp(hook);
    } else {
        memcpy((void *)hook->address, hook->expect, 5);
        FlushInstructionCache(GetCurrentProcess(), (void *)hook->address, 5);
    }
}

void *hook_install_trampoline(const struct hook *hook, uint32_t copy_len)
{
    uint8_t *target = (uint8_t *)hook->address;
    uint8_t *tramp;
    int32_t rel;

    if (memcmp(target, hook->expect, sizeof(hook->expect)) != 0) {
        log_printf("hook %s: unexpected bytes at %p, not patched", hook->name, hook->address);
        return 0;
    }
    tramp = VirtualAlloc(0, copy_len + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        log_printf("hook %s: no memory for trampoline", hook->name);
        return 0;
    }
    memcpy(tramp, target, copy_len);
    rel = (int32_t)((uintptr_t)(target + copy_len) - (uintptr_t)(tramp + copy_len + 5));
    tramp[copy_len] = 0xE9;
    memcpy(tramp + copy_len + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), tramp, copy_len + 5);
    if (!hook_install(hook))
        return 0;
    log_printf("hook %s: original callable at %p", hook->name, (uint32_t)tramp);
    return tramp;
}
