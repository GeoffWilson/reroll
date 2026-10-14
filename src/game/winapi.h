/* The Windows functions the game's C calls (Rollcage.exe's imports), by
 * name and with their types.
 *
 * The native build calls its own implementations (native/) directly; the
 * import table is not used. The emulator build (make emu-test) calls through
 * the exe's import slot instead, which the emulator stubs, so the tests see
 * the same calls as the original made.
 *
 * Game memory addresses are passed as pointers: (void *)(uintptr_t)addr.
 * Generated from native/'s definitions; keep the two in step. */
#ifndef REROLL_GAME_WINAPI_H
#define REROLL_GAME_WINAPI_H

#include "runtime.h"

#define W32API __attribute__((stdcall))

#ifndef REROLL_WIN32_API
typedef void *HANDLE;
typedef int BOOL;
typedef uint32_t DWORD;
#endif
typedef int32_t LONG;
typedef unsigned int UINT;
typedef uint16_t WORD;
typedef uint16_t WCHAR;
typedef int32_t HRESULT;
typedef void *HWND;
typedef uint32_t WPARAM;
typedef int32_t LPARAM;
typedef int32_t LRESULT;

/* a game memory address as a pointer */
#define GMEM(a) ((void *)(uintptr_t)(a))

#ifdef REROLL_NATIVE
#define REROLL_IMPORT(slot, name) name
#else
#define REROLL_IMPORT(slot, name) (*(__typeof__(&name) *)(uintptr_t)(slot))
#endif

/* DDRAW.dll */
HRESULT W32API DirectDrawCreate(const void *guid, uint32_t *out, void *outer);
HRESULT W32API DirectDrawEnumerateA(void *callback, void *ctx);

/* KERNEL32.dll */
BOOL W32API CreateProcessA(const char *app, char *command_line, void *process_security, void *thread_security, BOOL inherit, DWORD flags, void *environment, const char *directory, void *startup, DWORD *info);
DWORD W32API WaitForSingleObject(HANDLE h, DWORD ms);
void *W32API GlobalFree(void *p);
UINT W32API GetDriveTypeA(const char *root);
BOOL W32API ReadFile(HANDLE h, void *buf, DWORD len, DWORD *read_count, void *overlapped);
BOOL W32API WriteFile(HANDLE h, const void *buf, DWORD len, DWORD *written, void *overlapped);
DWORD W32API GetFileSize(HANDLE h, DWORD *high);
UINT W32API GetTempFileNameA(const char *path, const char *prefix, UINT unique, char *out);
BOOL W32API ReleaseMutex(HANDLE h);
HANDLE W32API OpenMutexA(DWORD access, BOOL inherit, const char *name);
DWORD W32API WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL all, DWORD ms);
DWORD W32API GetEnvironmentVariableA(const char *name, char *buf, DWORD size);
BOOL W32API ResetEvent(HANDLE h);
BOOL W32API SetEndOfFile(HANDLE h);
BOOL W32API FindClose(HANDLE h);
BOOL W32API FindNextFileA(HANDLE h, uint8_t *data);
HANDLE W32API FindFirstFileA(const char *pattern, uint8_t *data);
HANDLE W32API LoadLibraryA(const char *name);
BOOL W32API SetEvent(HANDLE h);
HANDLE W32API CreateEventA(void *security, BOOL manual, BOOL initial, const char *name);
BOOL W32API FlushFileBuffers(HANDLE h);
DWORD W32API GetFileType(HANDLE h);
HANDLE W32API OpenEventA(DWORD access, BOOL inherit, const char *name);
UINT W32API SetHandleCount(UINT n);
WCHAR *W32API GetEnvironmentStringsW(void);
char *W32API GetEnvironmentStrings(void);
BOOL W32API FreeEnvironmentStringsW(WCHAR *p);
BOOL W32API FreeEnvironmentStringsA(char *p);
DWORD W32API GetModuleFileNameA(HANDLE module, char *buf, DWORD size);
void W32API RtlUnwind(void *frame, void *ip, void *record, void *value);
LONG W32API UnhandledExceptionFilter(void *info);
UINT W32API GetOEMCP(void);
UINT W32API GetACP(void);
BOOL W32API GetCPInfo(UINT page, uint8_t *info);
BOOL W32API DeleteFileA(const char *name);
BOOL W32API SetEnvironmentVariableA(const char *name, const char *value);
BOOL W32API SetStdHandle(DWORD which, HANDLE h);
void W32API RaiseException(DWORD code, DWORD flags, DWORD count, const DWORD *args);
HANDLE W32API GetStdHandle(DWORD which);
HANDLE W32API CreateMutexA(void *security, BOOL owned, const char *name);
BOOL W32API CloseHandle(HANDLE h);
DWORD W32API SetFilePointer(HANDLE h, LONG distance, LONG *high, DWORD method);
HANDLE W32API CreateFileA(const char *name, DWORD access, DWORD share, void *security, DWORD disposition, DWORD flags, HANDLE template_file);
DWORD W32API GetLastError(void);
void W32API Sleep(DWORD ms);
void *W32API GlobalAlloc(UINT flags, DWORD size);
WORD W32API GetUserDefaultLangID(void);
void *W32API TlsGetValue(DWORD i);
void W32API SetLastError(DWORD error);
DWORD W32API TlsAlloc(void);
DWORD W32API GetCurrentThreadId(void);
int32_t W32API LCMapStringW(DWORD locale, DWORD flags, const WCHAR *in, int32_t in_len, WCHAR *out, int32_t out_len);
int32_t W32API LCMapStringA(DWORD locale, DWORD flags, const char *in, int32_t in_len, char *out, int32_t out_len);
int32_t W32API WideCharToMultiByte(UINT page, DWORD flags, const WCHAR *in, int32_t in_len, char *out, int32_t out_len, const char *def, BOOL *used_def);
void *W32API HeapReAlloc(HANDLE heap, DWORD flags, void *p, DWORD size);
void W32API GetSystemInfo(DWORD *info);
BOOL W32API CreateDirectoryA(const char *name, void *security);
void *W32API VirtualAlloc(void *address, DWORD size, DWORD type, DWORD prot);
HANDLE W32API CreateThread(void *security, DWORD stack, void *start, void *param, DWORD flags, DWORD *id);
LONG W32API InterlockedExchange(LONG *p, LONG v);
void W32API DeleteCriticalSection(uint32_t *cs);
void W32API InitializeCriticalSection(uint32_t *cs);
BOOL W32API VirtualFree(void *address, DWORD size, DWORD type);
HANDLE W32API HeapCreate(DWORD options, DWORD initial, DWORD maximum);
BOOL W32API HeapDestroy(HANDLE heap);
BOOL W32API GetStringTypeW(DWORD type, const WCHAR *in, int32_t n, WORD *out);
BOOL W32API GetStringTypeA(DWORD locale, DWORD type, const char *in, int32_t n, WORD *out);
int32_t W32API MultiByteToWideChar(UINT page, DWORD flags, const char *in, int32_t in_len, WCHAR *out, int32_t out_len);
void W32API LeaveCriticalSection(uint32_t *cs);
void W32API EnterCriticalSection(uint32_t *cs);
void *W32API GetProcAddress(HANDLE module, const char *name);
void W32API GetSystemTimeAsFileTime(uint64_t *ft);
DWORD W32API GetVersion(void);
char *W32API GetCommandLineA(void);
void W32API GetStartupInfoA(uint8_t *info);
HANDLE W32API GetModuleHandleA(const char *name);
BOOL W32API TlsSetValue(DWORD i, void *v);
DWORD W32API ResumeThread(HANDLE h);
HANDLE W32API GetCurrentProcess(void);
BOOL W32API TerminateProcess(HANDLE process, UINT code);
void W32API ExitProcess(UINT code);
DWORD W32API GetFileAttributesA(const char *name);
BOOL W32API SetCurrentDirectoryA(const char *path);
LONG W32API InterlockedIncrement(LONG *p);
void W32API ExitThread(DWORD code);
BOOL W32API GetExitCodeThread(HANDLE h, DWORD *code);
DWORD W32API GetCurrentDirectoryA(DWORD size, char *buf);
DWORD W32API GetFullPathNameA(const char *name, DWORD size, char *buf, uint32_t *file_part);
BOOL W32API HeapFree(HANDLE heap, DWORD flags, void *p);
void *W32API HeapAlloc(HANDLE heap, DWORD flags, DWORD size);
LONG W32API InterlockedDecrement(LONG *p);

/* USER32.dll */
void W32API PostQuitMessage(int32_t code);
BOOL W32API ClientToScreen(HWND h, int32_t *point);
int16_t W32API GetKeyState(int32_t vk);
HWND W32API GetDlgItem(HWND h, int32_t id);
LRESULT W32API DispatchMessageA(const void *m);
BOOL W32API TranslateMessage(const void *m);
int32_t W32API ShowCursor(BOOL show);
BOOL W32API SetRect(int32_t *rect, int32_t left, int32_t top, int32_t right, int32_t bottom);
BOOL W32API GetMessageA(void *out, HWND h, UINT first, UINT last);
BOOL W32API PeekMessageA(void *out, HWND h, UINT first, UINT last, UINT remove);
BOOL W32API DestroyWindow(HWND h);
int32_t W32API GetSystemMetrics(int32_t index);
BOOL W32API AdjustWindowRectEx(int32_t *rect, DWORD style, BOOL menu, DWORD ex_style);
HWND W32API CreateWindowExA(DWORD ex_style, const char *class_name, const char *title, DWORD style, int32_t x, int32_t y, int32_t w, int32_t h, HWND parent, void *menu, void *instance, void *param);
void *W32API LoadIconA(void *instance, const char *name);
DWORD W32API RegisterClassExA(const uint8_t *c);
void *W32API LoadCursorA(void *instance, const char *name);
HWND W32API SetFocus(HWND h);
int32_t W32API MessageBoxA(HWND h, const char *text, const char *caption, UINT type);
BOOL W32API EnableWindow(HWND h, BOOL enable);
BOOL W32API EndDialog(HWND h, int32_t result);
int32_t W32API DialogBoxParamA(void *instance, const char *name, HWND parent, void *proc, LPARAM param);
LRESULT W32API SendDlgItemMessageA(HWND h, int32_t id, UINT message, WPARAM wparam, LPARAM lparam);
HWND W32API GetActiveWindow(void);
HWND W32API SetActiveWindow(HWND h);
BOOL W32API ShowWindow(HWND h, int32_t command);
LRESULT W32API DefWindowProcA(HWND h, UINT message, WPARAM wparam, LPARAM lparam);
BOOL W32API GetClientRect(HWND h, int32_t *rect);
BOOL W32API SetForegroundWindow(HWND h);

/* GDI32.dll */
int32_t W32API GetDeviceCaps(HANDLE dc, int32_t index);
BOOL W32API DeleteDC(HANDLE dc);
HANDLE W32API CreatePalette(const void *palette);
HANDLE W32API GetStockObject(int32_t index);
HANDLE W32API CreateICA(const char *driver, const char *device, const char *port, const void *mode);

/* ADVAPI32.dll */
LONG W32API RegDeleteValueA(DWORD key, const char *name);
BOOL W32API GetUserNameA(char *buf, DWORD *size);
LONG W32API RegCreateKeyExA(DWORD parent, const char *sub, DWORD reserved, char *class_name, DWORD options, DWORD sam, void *security, DWORD *result, DWORD *disposition);
LONG W32API RegCloseKey(DWORD key);
LONG W32API RegOpenKeyExA(DWORD parent, const char *sub, DWORD options, DWORD sam, DWORD *result);
LONG W32API RegQueryValueExA(DWORD key, const char *name, DWORD *reserved, DWORD *type, uint8_t *data, DWORD *size);
LONG W32API RegSetValueExA(DWORD key, const char *name, DWORD reserved, DWORD type, const uint8_t *data, DWORD size);
LONG W32API RegUnLoadKeyA(DWORD key, const char *sub);

/* ole32.dll */
void W32API CoUninitialize(void);
LONG W32API CoCreateInstance(const void *clsid, void *outer, DWORD context, const void *iid, uint32_t *out);
LONG W32API CoInitialize(void *reserved);

/* WINMM.dll */
UINT W32API timeGetDevCaps(UINT *caps, UINT size);
DWORD W32API timeGetTime(void);
UINT W32API timeEndPeriod(UINT ms);
UINT W32API timeSetEvent(UINT delay, UINT resolution, void *callback, DWORD user, UINT flags);
UINT W32API timeBeginPeriod(UINT ms);
UINT W32API timeKillEvent(UINT id);
UINT W32API mixerSetControlDetails(void *mixer, void *details, DWORD flags);
UINT W32API mixerOpen(void *mixer, UINT id, DWORD callback, DWORD instance, DWORD flags);
DWORD W32API mciSendCommandA(UINT device, UINT message, DWORD flags, DWORD params);
UINT W32API auxSetVolume(UINT id, DWORD volume);
UINT W32API auxGetVolume(UINT id, DWORD *volume);
UINT W32API auxGetDevCapsA(UINT id, uint8_t *caps, UINT size);
UINT W32API auxGetNumDevs(void);
UINT W32API mixerClose(void *mixer);
UINT W32API mixerGetLineInfoA(void *mixer, void *line, DWORD flags);
UINT W32API mixerGetNumDevs(void);
UINT W32API mixerGetDevCapsA(UINT id, void *caps, UINT size);
UINT W32API mixerGetLineControlsA(void *mixer, void *controls, DWORD flags);
UINT W32API mixerGetControlDetailsA(void *mixer, void *details, DWORD flags);

/* DINPUT.dll */
HRESULT W32API DirectInputCreateA(void *instance, DWORD version, uint32_t *out, void *outer);

/* DPLAYX.dll */
LONG W32API DirectPlayLobbyCreateA(void *guid, uint32_t *out, void *outer, void *data, DWORD size);

/* QMIXER.dll */
DWORD W32API QSWaveMixStopProfile(DWORD session, DWORD *stats, DWORD size);
DWORD W32API QSWaveMixStartProfile(DWORD session);
DWORD W32API QSWaveMixSetSpeakerPlacement(DWORD session, DWORD placement);
DWORD W32API QSWaveMixEnableChannel(DWORD session, DWORD channel, DWORD flags, DWORD features);
DWORD W32API QSWaveMixSetOptions(DWORD session, DWORD options, DWORD mask);
DWORD W32API QSWaveMixSetDistanceMapping(DWORD session, DWORD channel, DWORD flags, const float *distances);
DWORD W32API QSWaveMixSetSourceVelocity(DWORD session, DWORD channel, DWORD flags, const float *velocity);
DWORD W32API QSWaveMixSetPolarPosition(DWORD session, DWORD channel, DWORD flags, const float *polar);
DWORD W32API QSWaveMixSetVolume(DWORD session, DWORD channel, DWORD flags, DWORD volume);
DWORD W32API QSWaveMixSetFrequency(DWORD session, DWORD channel, DWORD flags, DWORD frequency);
DWORD W32API QSWaveMixIsChannelDone(DWORD session, DWORD channel);
DWORD W32API QSWaveMixPauseChannel(DWORD session, DWORD channel, DWORD flags);
DWORD W32API QSWaveMixFlushChannel(DWORD session, DWORD channel, DWORD flags);
DWORD W32API QSWaveMixPlayEx(DWORD session, DWORD channel, DWORD flags, void *w, DWORD loops, DWORD params);
DWORD W32API QSWaveMixRestartChannel(DWORD session, DWORD channel, DWORD flags);
void *W32API QSWaveMixInitEx(void *config);
void *W32API QSWaveMixOpenWaveEx(DWORD session, const DWORD *params, DWORD flags);
DWORD W32API QSWaveMixOpenChannel(DWORD session, DWORD channel, DWORD flags);
DWORD W32API QSWaveMixGetDirectSoundBuffer(DWORD session, DWORD channel, uint32_t *out);
DWORD W32API QSWaveMixGetChannelParams(DWORD session, DWORD channel, DWORD *params);
DWORD W32API QSWaveMixSetListenerRolloff(DWORD session, DWORD value, DWORD flags);
DWORD W32API QSWaveMixGetDirectSound(DWORD session, uint32_t *out);
DWORD W32API QSWaveMixActivate(DWORD session, DWORD on);
DWORD W32API QSWaveMixCloseSession(DWORD session);
DWORD W32API QSWaveMixFreeWave(DWORD session, void *w);

/* DSOUND.dll */
LONG W32API DirectSoundCreate(const void *guid, uint32_t *out, void *outer);

#ifndef REROLL_NATIVE

/* DDRAW.dll */
#define DirectDrawCreate REROLL_IMPORT(0x4a3024, DirectDrawCreate)
#define DirectDrawEnumerateA REROLL_IMPORT(0x4a3028, DirectDrawEnumerateA)

/* KERNEL32.dll */
#define CreateProcessA REROLL_IMPORT(0x4a3060, CreateProcessA)
#define WaitForSingleObject REROLL_IMPORT(0x4a3064, WaitForSingleObject)
#define GlobalFree REROLL_IMPORT(0x4a3068, GlobalFree)
#define GetDriveTypeA REROLL_IMPORT(0x4a306c, GetDriveTypeA)
#define ReadFile REROLL_IMPORT(0x4a3070, ReadFile)
#define WriteFile REROLL_IMPORT(0x4a3074, WriteFile)
#define GetFileSize REROLL_IMPORT(0x4a3078, GetFileSize)
#define GetTempFileNameA REROLL_IMPORT(0x4a307c, GetTempFileNameA)
#define ReleaseMutex REROLL_IMPORT(0x4a3080, ReleaseMutex)
#define OpenMutexA REROLL_IMPORT(0x4a3084, OpenMutexA)
#define WaitForMultipleObjects REROLL_IMPORT(0x4a3088, WaitForMultipleObjects)
#define GetEnvironmentVariableA REROLL_IMPORT(0x4a308c, GetEnvironmentVariableA)
#define ResetEvent REROLL_IMPORT(0x4a3090, ResetEvent)
#define SetEndOfFile REROLL_IMPORT(0x4a3094, SetEndOfFile)
#define FindClose REROLL_IMPORT(0x4a3098, FindClose)
#define FindNextFileA REROLL_IMPORT(0x4a309c, FindNextFileA)
#define FindFirstFileA REROLL_IMPORT(0x4a30a0, FindFirstFileA)
#define LoadLibraryA REROLL_IMPORT(0x4a30a4, LoadLibraryA)
#define SetEvent REROLL_IMPORT(0x4a30a8, SetEvent)
#define CreateEventA REROLL_IMPORT(0x4a30ac, CreateEventA)
#define FlushFileBuffers REROLL_IMPORT(0x4a30b0, FlushFileBuffers)
#define GetFileType REROLL_IMPORT(0x4a30b4, GetFileType)
#define OpenEventA REROLL_IMPORT(0x4a30b8, OpenEventA)
#define SetHandleCount REROLL_IMPORT(0x4a30bc, SetHandleCount)
#define GetEnvironmentStringsW REROLL_IMPORT(0x4a30c0, GetEnvironmentStringsW)
#define GetEnvironmentStrings REROLL_IMPORT(0x4a30c4, GetEnvironmentStrings)
#define FreeEnvironmentStringsW REROLL_IMPORT(0x4a30c8, FreeEnvironmentStringsW)
#define FreeEnvironmentStringsA REROLL_IMPORT(0x4a30cc, FreeEnvironmentStringsA)
#define GetModuleFileNameA REROLL_IMPORT(0x4a30d0, GetModuleFileNameA)
#define RtlUnwind REROLL_IMPORT(0x4a30d4, RtlUnwind)
#define UnhandledExceptionFilter REROLL_IMPORT(0x4a30d8, UnhandledExceptionFilter)
#define GetOEMCP REROLL_IMPORT(0x4a30dc, GetOEMCP)
#define GetACP REROLL_IMPORT(0x4a30e0, GetACP)
#define GetCPInfo REROLL_IMPORT(0x4a30e4, GetCPInfo)
#define DeleteFileA REROLL_IMPORT(0x4a30e8, DeleteFileA)
#define SetEnvironmentVariableA REROLL_IMPORT(0x4a30ec, SetEnvironmentVariableA)
#define SetStdHandle REROLL_IMPORT(0x4a30f0, SetStdHandle)
#define RaiseException REROLL_IMPORT(0x4a30f4, RaiseException)
#define GetStdHandle REROLL_IMPORT(0x4a30f8, GetStdHandle)
#define CreateMutexA REROLL_IMPORT(0x4a30fc, CreateMutexA)
#define CloseHandle REROLL_IMPORT(0x4a3100, CloseHandle)
#define SetFilePointer REROLL_IMPORT(0x4a3104, SetFilePointer)
#define CreateFileA REROLL_IMPORT(0x4a3108, CreateFileA)
#define GetLastError REROLL_IMPORT(0x4a310c, GetLastError)
#define Sleep REROLL_IMPORT(0x4a3110, Sleep)
#define GlobalAlloc REROLL_IMPORT(0x4a3114, GlobalAlloc)
#define GetUserDefaultLangID REROLL_IMPORT(0x4a3118, GetUserDefaultLangID)
#define TlsGetValue REROLL_IMPORT(0x4a311c, TlsGetValue)
#define SetLastError REROLL_IMPORT(0x4a3120, SetLastError)
#define TlsAlloc REROLL_IMPORT(0x4a3124, TlsAlloc)
#define GetCurrentThreadId REROLL_IMPORT(0x4a3128, GetCurrentThreadId)
#define LCMapStringW REROLL_IMPORT(0x4a312c, LCMapStringW)
#define LCMapStringA REROLL_IMPORT(0x4a3130, LCMapStringA)
#define WideCharToMultiByte REROLL_IMPORT(0x4a3134, WideCharToMultiByte)
#define HeapReAlloc REROLL_IMPORT(0x4a3138, HeapReAlloc)
#define GetSystemInfo REROLL_IMPORT(0x4a313c, GetSystemInfo)
#define CreateDirectoryA REROLL_IMPORT(0x4a3140, CreateDirectoryA)
#define VirtualAlloc REROLL_IMPORT(0x4a3144, VirtualAlloc)
#define CreateThread REROLL_IMPORT(0x4a3148, CreateThread)
#define InterlockedExchange REROLL_IMPORT(0x4a314c, InterlockedExchange)
#define DeleteCriticalSection REROLL_IMPORT(0x4a3150, DeleteCriticalSection)
#define InitializeCriticalSection REROLL_IMPORT(0x4a3154, InitializeCriticalSection)
#define VirtualFree REROLL_IMPORT(0x4a3158, VirtualFree)
#define HeapCreate REROLL_IMPORT(0x4a315c, HeapCreate)
#define HeapDestroy REROLL_IMPORT(0x4a3160, HeapDestroy)
#define GetStringTypeW REROLL_IMPORT(0x4a3164, GetStringTypeW)
#define GetStringTypeA REROLL_IMPORT(0x4a3168, GetStringTypeA)
#define MultiByteToWideChar REROLL_IMPORT(0x4a316c, MultiByteToWideChar)
#define LeaveCriticalSection REROLL_IMPORT(0x4a3170, LeaveCriticalSection)
#define EnterCriticalSection REROLL_IMPORT(0x4a3174, EnterCriticalSection)
#define GetProcAddress REROLL_IMPORT(0x4a3178, GetProcAddress)
#define GetSystemTimeAsFileTime REROLL_IMPORT(0x4a317c, GetSystemTimeAsFileTime)
#define GetVersion REROLL_IMPORT(0x4a3180, GetVersion)
#define GetCommandLineA REROLL_IMPORT(0x4a3184, GetCommandLineA)
#define GetStartupInfoA REROLL_IMPORT(0x4a3188, GetStartupInfoA)
#define GetModuleHandleA REROLL_IMPORT(0x4a318c, GetModuleHandleA)
#define TlsSetValue REROLL_IMPORT(0x4a3190, TlsSetValue)
#define ResumeThread REROLL_IMPORT(0x4a3194, ResumeThread)
#define GetCurrentProcess REROLL_IMPORT(0x4a3198, GetCurrentProcess)
#define TerminateProcess REROLL_IMPORT(0x4a319c, TerminateProcess)
#define ExitProcess REROLL_IMPORT(0x4a31a0, ExitProcess)
#define GetFileAttributesA REROLL_IMPORT(0x4a31a4, GetFileAttributesA)
#define SetCurrentDirectoryA REROLL_IMPORT(0x4a31a8, SetCurrentDirectoryA)
#define InterlockedIncrement REROLL_IMPORT(0x4a31ac, InterlockedIncrement)
#define ExitThread REROLL_IMPORT(0x4a31b0, ExitThread)
#define GetExitCodeThread REROLL_IMPORT(0x4a31b4, GetExitCodeThread)
#define GetCurrentDirectoryA REROLL_IMPORT(0x4a31b8, GetCurrentDirectoryA)
#define GetFullPathNameA REROLL_IMPORT(0x4a31bc, GetFullPathNameA)
#define HeapFree REROLL_IMPORT(0x4a31c0, HeapFree)
#define HeapAlloc REROLL_IMPORT(0x4a31c4, HeapAlloc)
#define InterlockedDecrement REROLL_IMPORT(0x4a31c8, InterlockedDecrement)

/* USER32.dll */
#define PostQuitMessage REROLL_IMPORT(0x4a3238, PostQuitMessage)
#define ClientToScreen REROLL_IMPORT(0x4a323c, ClientToScreen)
#define GetKeyState REROLL_IMPORT(0x4a3240, GetKeyState)
#define GetDlgItem REROLL_IMPORT(0x4a3244, GetDlgItem)
#define DispatchMessageA REROLL_IMPORT(0x4a3248, DispatchMessageA)
#define TranslateMessage REROLL_IMPORT(0x4a324c, TranslateMessage)
#define ShowCursor REROLL_IMPORT(0x4a3250, ShowCursor)
#define SetRect REROLL_IMPORT(0x4a3254, SetRect)
#define GetMessageA REROLL_IMPORT(0x4a3258, GetMessageA)
#define PeekMessageA REROLL_IMPORT(0x4a325c, PeekMessageA)
#define DestroyWindow REROLL_IMPORT(0x4a3260, DestroyWindow)
#define GetSystemMetrics REROLL_IMPORT(0x4a3264, GetSystemMetrics)
#define AdjustWindowRectEx REROLL_IMPORT(0x4a3268, AdjustWindowRectEx)
#define CreateWindowExA REROLL_IMPORT(0x4a326c, CreateWindowExA)
#define LoadIconA REROLL_IMPORT(0x4a3270, LoadIconA)
#define RegisterClassExA REROLL_IMPORT(0x4a3274, RegisterClassExA)
#define LoadCursorA REROLL_IMPORT(0x4a3278, LoadCursorA)
#define SetFocus REROLL_IMPORT(0x4a327c, SetFocus)
#define MessageBoxA REROLL_IMPORT(0x4a3280, MessageBoxA)
#define EnableWindow REROLL_IMPORT(0x4a3284, EnableWindow)
#define EndDialog REROLL_IMPORT(0x4a3288, EndDialog)
#define DialogBoxParamA REROLL_IMPORT(0x4a328c, DialogBoxParamA)
#define SendDlgItemMessageA REROLL_IMPORT(0x4a3290, SendDlgItemMessageA)
#define GetActiveWindow REROLL_IMPORT(0x4a3294, GetActiveWindow)
#define SetActiveWindow REROLL_IMPORT(0x4a3298, SetActiveWindow)
#define ShowWindow REROLL_IMPORT(0x4a329c, ShowWindow)
#define DefWindowProcA REROLL_IMPORT(0x4a32a0, DefWindowProcA)
#define GetClientRect REROLL_IMPORT(0x4a32a4, GetClientRect)
#define SetForegroundWindow REROLL_IMPORT(0x4a32a8, SetForegroundWindow)

/* GDI32.dll */
#define GetDeviceCaps REROLL_IMPORT(0x4a3048, GetDeviceCaps)
#define DeleteDC REROLL_IMPORT(0x4a304c, DeleteDC)
#define CreatePalette REROLL_IMPORT(0x4a3050, CreatePalette)
#define GetStockObject REROLL_IMPORT(0x4a3054, GetStockObject)
#define CreateICA REROLL_IMPORT(0x4a3058, CreateICA)

/* ADVAPI32.dll */
#define RegDeleteValueA REROLL_IMPORT(0x4a3000, RegDeleteValueA)
#define GetUserNameA REROLL_IMPORT(0x4a3004, GetUserNameA)
#define RegCreateKeyExA REROLL_IMPORT(0x4a3008, RegCreateKeyExA)
#define RegCloseKey REROLL_IMPORT(0x4a300c, RegCloseKey)
#define RegOpenKeyExA REROLL_IMPORT(0x4a3010, RegOpenKeyExA)
#define RegQueryValueExA REROLL_IMPORT(0x4a3014, RegQueryValueExA)
#define RegSetValueExA REROLL_IMPORT(0x4a3018, RegSetValueExA)
#define RegUnLoadKeyA REROLL_IMPORT(0x4a301c, RegUnLoadKeyA)

/* ole32.dll */
#define CoUninitialize REROLL_IMPORT(0x4a3300, CoUninitialize)
#define CoCreateInstance REROLL_IMPORT(0x4a3304, CoCreateInstance)
#define CoInitialize REROLL_IMPORT(0x4a3308, CoInitialize)

/* WINMM.dll */
#define timeGetDevCaps REROLL_IMPORT(0x4a32b0, timeGetDevCaps)
#define timeGetTime REROLL_IMPORT(0x4a32b4, timeGetTime)
#define timeEndPeriod REROLL_IMPORT(0x4a32b8, timeEndPeriod)
#define timeSetEvent REROLL_IMPORT(0x4a32bc, timeSetEvent)
#define timeBeginPeriod REROLL_IMPORT(0x4a32c0, timeBeginPeriod)
#define timeKillEvent REROLL_IMPORT(0x4a32c4, timeKillEvent)
#define mixerSetControlDetails REROLL_IMPORT(0x4a32c8, mixerSetControlDetails)
#define mixerOpen REROLL_IMPORT(0x4a32cc, mixerOpen)
#define mciSendCommandA REROLL_IMPORT(0x4a32d0, mciSendCommandA)
#define auxSetVolume REROLL_IMPORT(0x4a32d4, auxSetVolume)
#define auxGetVolume REROLL_IMPORT(0x4a32d8, auxGetVolume)
#define auxGetDevCapsA REROLL_IMPORT(0x4a32dc, auxGetDevCapsA)
#define auxGetNumDevs REROLL_IMPORT(0x4a32e0, auxGetNumDevs)
#define mixerClose REROLL_IMPORT(0x4a32e4, mixerClose)
#define mixerGetLineInfoA REROLL_IMPORT(0x4a32e8, mixerGetLineInfoA)
#define mixerGetNumDevs REROLL_IMPORT(0x4a32ec, mixerGetNumDevs)
#define mixerGetDevCapsA REROLL_IMPORT(0x4a32f0, mixerGetDevCapsA)
#define mixerGetLineControlsA REROLL_IMPORT(0x4a32f4, mixerGetLineControlsA)
#define mixerGetControlDetailsA REROLL_IMPORT(0x4a32f8, mixerGetControlDetailsA)

/* DINPUT.dll */
#define DirectInputCreateA REROLL_IMPORT(0x4a3030, DirectInputCreateA)

/* DPLAYX.dll */
#define DirectPlayLobbyCreateA REROLL_IMPORT(0x4a3038, DirectPlayLobbyCreateA)

/* QMIXER.dll */
#define QSWaveMixStopProfile REROLL_IMPORT(0x4a31d0, QSWaveMixStopProfile)
#define QSWaveMixStartProfile REROLL_IMPORT(0x4a31d4, QSWaveMixStartProfile)
#define QSWaveMixSetSpeakerPlacement REROLL_IMPORT(0x4a31d8, QSWaveMixSetSpeakerPlacement)
#define QSWaveMixEnableChannel REROLL_IMPORT(0x4a31dc, QSWaveMixEnableChannel)
#define QSWaveMixSetOptions REROLL_IMPORT(0x4a31e0, QSWaveMixSetOptions)
#define QSWaveMixSetDistanceMapping REROLL_IMPORT(0x4a31e4, QSWaveMixSetDistanceMapping)
#define QSWaveMixSetSourceVelocity REROLL_IMPORT(0x4a31e8, QSWaveMixSetSourceVelocity)
#define QSWaveMixSetPolarPosition REROLL_IMPORT(0x4a31ec, QSWaveMixSetPolarPosition)
#define QSWaveMixSetVolume REROLL_IMPORT(0x4a31f0, QSWaveMixSetVolume)
#define QSWaveMixSetFrequency REROLL_IMPORT(0x4a31f4, QSWaveMixSetFrequency)
#define QSWaveMixIsChannelDone REROLL_IMPORT(0x4a31f8, QSWaveMixIsChannelDone)
#define QSWaveMixPauseChannel REROLL_IMPORT(0x4a31fc, QSWaveMixPauseChannel)
#define QSWaveMixFlushChannel REROLL_IMPORT(0x4a3200, QSWaveMixFlushChannel)
#define QSWaveMixPlayEx REROLL_IMPORT(0x4a3204, QSWaveMixPlayEx)
#define QSWaveMixRestartChannel REROLL_IMPORT(0x4a3208, QSWaveMixRestartChannel)
#define QSWaveMixInitEx REROLL_IMPORT(0x4a320c, QSWaveMixInitEx)
#define QSWaveMixOpenWaveEx REROLL_IMPORT(0x4a3210, QSWaveMixOpenWaveEx)
#define QSWaveMixOpenChannel REROLL_IMPORT(0x4a3214, QSWaveMixOpenChannel)
#define QSWaveMixGetDirectSoundBuffer REROLL_IMPORT(0x4a3218, QSWaveMixGetDirectSoundBuffer)
#define QSWaveMixGetChannelParams REROLL_IMPORT(0x4a321c, QSWaveMixGetChannelParams)
#define QSWaveMixSetListenerRolloff REROLL_IMPORT(0x4a3220, QSWaveMixSetListenerRolloff)
#define QSWaveMixGetDirectSound REROLL_IMPORT(0x4a3224, QSWaveMixGetDirectSound)
#define QSWaveMixActivate REROLL_IMPORT(0x4a3228, QSWaveMixActivate)
#define QSWaveMixCloseSession REROLL_IMPORT(0x4a322c, QSWaveMixCloseSession)
#define QSWaveMixFreeWave REROLL_IMPORT(0x4a3230, QSWaveMixFreeWave)

/* DSOUND.dll */
#define DirectSoundCreate REROLL_IMPORT(0x4a3040, DirectSoundCreate)
#endif

#endif
