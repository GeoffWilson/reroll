/* Minimal freestanding runtime: no C library, only the Win32 calls we use. */
#ifndef REROLL_RUNTIME_H
#define REROLL_RUNTIME_H

#ifdef _WIN32
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned int size_t;
typedef unsigned int uintptr_t;
#else
/* Native builds (tests) use the host C library. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

/* The native Linux build (REROLL_NATIVE, docs/native/plan.md) keeps the
 * Windows declarations below: they resolve to its own implementations
 * (native/). */
#if defined(_WIN32) || defined(REROLL_NATIVE)
#define REROLL_WIN32_API 1
#endif

/* Calling convention of the original game functions (MSVC cdecl). */
#if defined(_WIN32) && defined(__i386__)
#define GAMEAPI __cdecl
#define WINAPI __stdcall
#elif defined(REROLL_NATIVE)
#define GAMEAPI __attribute__((cdecl))
#define WINAPI __attribute__((stdcall))
#else
#define GAMEAPI
#define WINAPI
#endif

/* A C symbol's name in inline assembly (COFF prefixes an underscore). */
#ifdef _WIN32
#define ASM_SYM(name) "_" name
#else
#define ASM_SYM(name) name
#endif

#ifdef REROLL_WIN32_API
typedef void *HANDLE;
typedef int BOOL;
typedef unsigned long DWORD;

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
__declspec(dllimport) void *WINAPI VirtualAlloc(void *addr, size_t size, DWORD type, DWORD prot);
__declspec(dllimport) BOOL WINAPI VirtualFree(void *addr, size_t size, DWORD type);
__declspec(dllimport) DWORD WINAPI GetEnvironmentVariableA(const char *name, char *buf, DWORD size);

struct memory_basic_information {
    void *base_address;
    void *allocation_base;
    DWORD allocation_protect;
    size_t region_size;
    DWORD state;
    DWORD protect;
    DWORD type;
};
__declspec(dllimport) size_t WINAPI VirtualQuery(const void *addr, struct memory_basic_information *info,
                                                 size_t size);

#define MEM_COMMIT 0x1000
#define MEM_RELEASE 0x8000
#define MEM_RESERVE 0x2000
__declspec(dllimport) HANDLE WINAPI GetActiveWindow(void);
#endif

#ifdef _WIN32
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#endif

#endif
