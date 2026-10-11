#ifndef REROLL_HOOK_H
#define REROLL_HOOK_H

#include "runtime.h"

/* One original game function redirected to a reimplementation.
 * `expect` holds the function's first bytes in the known Rollcage.exe build
 * (Direct3D, sha256 0648503b...), so a different executable is never patched. */
struct hook {
    const char *name;
    uint32_t address;
    const void *replacement;
    uint8_t expect[8];
};

/* Make the game's code writable, once, before any hook is installed. */
int hook_init(void);

/* Overwrite the start of hook->address with `jmp replacement`.
 * Returns 1 on success, 0 if the original bytes did not match. */
int hook_install(const struct hook *hook);

/* Switch an installed hook off (original bytes back) or on again. */
void hook_set_active(const struct hook *hook, int active);

/* As hook_install, but first build a trampoline that still runs the original
 * function: its first `copy_len` bytes (whole instructions, at least 5, with
 * no relative jumps or calls) followed by a jump back to the rest.
 * Returns the trampoline, or 0 if the hook was not installed. */
void *hook_install_trampoline(const struct hook *hook, uint32_t copy_len);

#endif
