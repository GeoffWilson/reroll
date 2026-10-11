/* The native build's Windows layer (docs/native/plan.md): the functions
 * Rollcage.exe imports, implemented on Linux, SDL3 and the Vulkan renderer.
 * Built with glibc; Win32 entry points are stdcall. */
#ifndef REROLL_NATIVE_NT_H
#define REROLL_NATIVE_NT_H

#include <stddef.h>
#include <stdint.h>

#define WINAPI __attribute__((stdcall))
#define CDECL __attribute__((cdecl))

typedef uint32_t DWORD;
typedef int32_t LONG;
typedef int BOOL;
typedef void *HANDLE;
typedef uint16_t WORD;
typedef uint16_t WCHAR;

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE ((HANDLE)(intptr_t)-1)

#define IMAGE_BASE 0x400000u

/* errors (GetLastError) */
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_NO_MORE_FILES 18
#define ERROR_FILE_EXISTS 80
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_ALREADY_EXISTS 183
#define ERROR_ENVVAR_NOT_FOUND 203
#define ERROR_NOT_SUPPORTED 50

/* One module's exports: what the exe's import table is filled from (and
 * what GetProcAddress finds). */
struct nt_export {
    const char *name;
    void *fn;
};
struct nt_module {
    const char *name; /* lower case, with ".dll" */
    const struct nt_export *exports;
};
extern const struct nt_module nt_modules[];
/* `name` null: whether the module exists at all */
void *nt_find_export(const char *dll, const char *name);
/* fake module handles for LoadLibraryA / GetProcAddress */
uint32_t nt_module_handle(const char *dll);
const char *nt_module_name(uint32_t handle);

/* logging: stderr and native.log in the game directory */
void nt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* An unimplemented call: logs and stops. */
_Noreturn void nt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* thread block (fs:) for the calling thread */
void nt_teb_init_thread(void);
uint32_t nt_teb_address(void);

void nt_set_last_error(DWORD error);
DWORD nt_last_error(void);

/* configuration (native/config.c) */
struct nt_config {
    char drive_c[512];   /* host directory of C: */
    char drive_d[512];   /* host directory of D: (the CD) */
    char exe_windows[512]; /* the exe's Windows path */
    char exe_host[512];
    char data_dir[512];  /* where the registry file and native.log live */
    char command_line[1024];
};
extern struct nt_config nt_config;

/* native/profile.c: REROLL_PROFILE=1 */
void nt_profile_start(void);
void nt_profile_flush(void);

/* paths (native/paths.c): Windows path -> host path, resolving each part
 * case-insensitively. Returns 0 if the drive is unknown. `must_exist`: the
 * last part too (else it may be new). */
int nt_host_path(const char *windows, char *host, size_t size, int must_exist);
/* current directory (Windows form, no trailing backslash except "C:\") */
const char *nt_current_directory(void);
int nt_set_current_directory(const char *windows);
/* makes a full Windows path from one relative to the current directory */
void nt_full_path(const char *windows, char *out, size_t size);

/* memory below 2 GB for everything the game sees (native/lowmem.c) */
void nt_lowmem_init(void);
void *nt_lowmem_alloc(uint32_t size);
void *nt_lowmem_realloc(void *p, uint32_t size);
void nt_lowmem_free(void *p);
int nt_lowmem_owns(const void *p);
uint32_t nt_lowmem_size(const void *p);
void *nt_lowmem_reserve(uint32_t size);
int nt_lowmem_unreserve(void *p);
int nt_lowmem_arena(int game); /* allocations go to the game's arena (1) or ours (0); returns the old */
void nt_lowmem_reset_game(void);
static inline void *nt_lowmem_calloc(uint32_t n, uint32_t size)
{
    void *p = nt_lowmem_alloc(n * size);
    if (p)
        __builtin_memset(p, 0, n * size);
    return p;
}

#include <pthread.h>
void nt_low_stack(pthread_attr_t *attr, uint32_t size);

/* restarting in place (native/loader.c) */
extern int nt_game_started, nt_restart_pending;
void nt_restart_request(const char *command_line);
_Noreturn void nt_restart(void);

/* registry (native/advapi32.c) */
void nt_registry_load(void);

/* handles (native/kernel32.c) */
enum nt_handle_kind { NT_FREE, NT_FILE, NT_EVENT, NT_MUTEX, NT_THREAD, NT_FIND, NT_STD, NT_PROCESS };
struct nt_object;
struct nt_object *nt_handle_object(HANDLE h, int kind);

#include "display.h"

#endif
