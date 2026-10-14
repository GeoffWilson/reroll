#include "hook.h"
#include "log.h"

#ifdef REROLL_NATIVE
extern int nt_no_exe; /* native/datafile.c: the code range holds only traps */
#else
static const int nt_no_exe = 0;
#endif

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
    uint8_t *target = (uint8_t *)(uintptr_t)hook->address;
    /* jmp rel32: E9 + displacement from the end of the 5-byte instruction */
    int64_t distance = (int64_t)(uintptr_t)hook->replacement - (int64_t)(hook->address + 5);
    int32_t rel = (int32_t)distance;

    /* our code is linked low (a 64-bit build too), within a jmp's reach */
    if (distance != rel)
        log_printf("hook %s: %p is out of a jmp's reach", hook->name, hook->address);

    target[0] = 0xE9;
    memcpy(target + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
}

/* Without the exe, the bytes a hook expects are written into the traps
 * first (byte by byte: neighbouring hooks' expected bytes overlap). */
static void no_exe_prepare(const struct hook *hook)
{
    uint32_t i;
    if (!nt_no_exe)
        return;
    for (i = 0; i < sizeof(hook->expect); i++)
        if (((uint8_t *)(uintptr_t)hook->address)[i] == 0xcc)
            ((uint8_t *)(uintptr_t)hook->address)[i] = hook->expect[i];
}

int hook_install(const struct hook *hook)
{
    no_exe_prepare(hook);
    if (memcmp((const void *)(uintptr_t)hook->address, hook->expect, sizeof(hook->expect)) != 0) {
        log_printf("hook %s: unexpected bytes at %p, not patched", hook->name, hook->address);
        return 0;
    }
    write_jmp(hook);
    log_printf("hook %s: %p -> %p", hook->name, hook->address, (uint32_t)(uintptr_t)hook->replacement);
    return 1;
}

void hook_set_active(const struct hook *hook, int active)
{
    if (active) {
        write_jmp(hook);
    } else {
        memcpy((void *)(uintptr_t)hook->address, hook->expect, 5);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)hook->address, 5);
    }
}

void *hook_install_trampoline(const struct hook *hook, uint32_t copy_len)
{
    uint8_t *target = (uint8_t *)(uintptr_t)hook->address;
    uint8_t *tramp;
    int32_t rel;

    if (nt_no_exe) { /* the original is gone: calling it traps (native/datafile.c) */
        tramp = VirtualAlloc(0, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!tramp || !hook_install(hook))
            return 0;
        memset(tramp, 0xcc, 16);
        log_printf("hook %s: no original (trap at %p)", hook->name, (uint32_t)(uintptr_t)tramp);
        return tramp;
    }
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
    log_printf("hook %s: original callable at %p", hook->name, (uint32_t)(uintptr_t)tramp);
    return tramp;
}
