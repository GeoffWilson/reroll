/* The reimplementation's runtime: the C types, the game's calling
 * conventions, and the Windows calls the code makes. */
#ifndef REROLL_RUNTIME_H
#define REROLL_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The native Linux build (REROLL_NATIVE, docs/native/plan.md) keeps the
 * Windows declarations below: they resolve to its own implementations
 * (native/). The emulator tests (make emu-test) build without them. */
#if defined(REROLL_NATIVE)
#define REROLL_WIN32_API 1
#endif

/* Calling convention of the original game functions (MSVC cdecl). A 64-bit
 * build has one calling convention: everything that calls the game's C is
 * our C. */
#if defined(REROLL_NATIVE) && defined(__i386__)
#define GAMEAPI __attribute__((cdecl))
#define WINAPI __attribute__((stdcall))
#else
#define GAMEAPI
#define WINAPI
#endif

/* A C symbol's name in inline assembly. */
#define ASM_SYM(name) name

#ifdef REROLL_WIN32_API
typedef void *HANDLE;
typedef int BOOL;
typedef uint32_t DWORD;

#define GENERIC_WRITE 0x40000000
#define GENERIC_READ 0x80000000
#define FILE_APPEND_DATA 0x4
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define CREATE_ALWAYS 2
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_READWRITE 0x04
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0

__declspec(dllimport) BOOL WINAPI VirtualProtect(void *addr, size_t size, DWORD prot, DWORD *old);
__declspec(dllimport) BOOL WINAPI FlushInstructionCache(HANDLE proc, const void *addr, size_t size);
__declspec(dllimport) HANDLE WINAPI GetCurrentProcess(void);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char *name, DWORD access, DWORD share, void *sec,
                                                DWORD disposition, DWORD flags, HANDLE tmpl);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE f, const void *buf, DWORD len, DWORD *written, void *ovl);
__declspec(dllimport) DWORD WINAPI GetTickCount(void);
__declspec(dllimport) void WINAPI Sleep(DWORD ms);
__declspec(dllimport) BOOL WINAPI QueryPerformanceCounter(uint64_t *count);
__declspec(dllimport) BOOL WINAPI QueryPerformanceFrequency(uint64_t *frequency);
__declspec(dllimport) void *WINAPI VirtualAlloc(void *addr, DWORD size, DWORD type, DWORD prot);
__declspec(dllimport) BOOL WINAPI VirtualFree(void *addr, DWORD size, DWORD type);
__declspec(dllimport) DWORD WINAPI GetEnvironmentVariableA(const char *name, char *buf, DWORD size);

/* MEMORY_BASIC_INFORMATION as the game knows it: 32-bit fields (native/
 * kernel32.c fills it so on both builds) */
struct memory_basic_information {
    uint32_t base_address;
    uint32_t allocation_base;
    DWORD allocation_protect;
    uint32_t region_size;
    DWORD state;
    DWORD protect;
    DWORD type;
};
__declspec(dllimport) DWORD WINAPI VirtualQuery(const void *addr, struct memory_basic_information *info, DWORD size);

#define MEM_COMMIT 0x1000
#define MEM_RELEASE 0x8000
#define MEM_RESERVE 0x2000
__declspec(dllimport) HANDLE WINAPI GetActiveWindow(void);
#endif

#endif
