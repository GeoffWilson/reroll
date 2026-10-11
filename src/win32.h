/* The window and module calls the Vulkan presenter makes (src/render/vkwin.c,
 * src/vkgame.c, tools/vkwintest.c), declared here: there is no windows.h in
 * the freestanding build. */
#ifndef REROLL_WIN32_H
#define REROLL_WIN32_H

#include "runtime.h"

typedef void *HWND;
typedef void *HINSTANCE;
typedef void *HMODULE;
typedef long LRESULT;
typedef unsigned int WPARAM;
typedef long LPARAM;
typedef LRESULT(WINAPI *WNDPROC)(HWND, unsigned int, WPARAM, LPARAM);

struct win_rect {
    int32_t left, top, right, bottom;
};

struct win_class {
    uint32_t style;
    WNDPROC proc;
    int32_t class_extra, window_extra;
    HINSTANCE instance;
    void *icon, *cursor, *background;
    const char *menu_name, *class_name;
};

struct win_msg {
    HWND window;
    uint32_t message;
    WPARAM wparam;
    LPARAM lparam;
    DWORD time;
    int32_t x, y;
};

#define WS_POPUP 0x80000000u
#define WS_CHILD 0x40000000u
#define WS_VISIBLE 0x10000000u
#define WS_DISABLED 0x08000000u
#define WS_CLIPSIBLINGS 0x04000000u
#define WS_CLIPCHILDREN 0x02000000u
#define WS_OVERLAPPEDWINDOW 0x00cf0000u
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define SWP_NOSIZE 0x1
#define SWP_NOMOVE 0x2
#define SWP_NOZORDER 0x4
#define SWP_NOACTIVATE 0x10
#define SWP_FRAMECHANGED 0x20
#define SWP_SHOWWINDOW 0x40
#define SW_HIDE 0
#define SW_SHOW 5
#define SW_SHOWNA 8
#define WM_DESTROY 0x2
#define WM_CLOSE 0x10
#define WM_QUIT 0x12
#define WM_NCHITTEST 0x84
#define HTTRANSPARENT (-1)
#define PM_REMOVE 1
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1

__declspec(dllimport) HMODULE WINAPI LoadLibraryA(const char *name);
__declspec(dllimport) void *WINAPI GetProcAddress(HMODULE module, const char *name);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(const char *name);

__declspec(dllimport) uint16_t WINAPI RegisterClassA(const struct win_class *c);
__declspec(dllimport) HWND WINAPI CreateWindowExA(DWORD ex_style, const char *class_name, const char *title,
                                                  DWORD style, int32_t x, int32_t y, int32_t w, int32_t h,
                                                  HWND parent, void *menu, HINSTANCE instance, void *param);
__declspec(dllimport) BOOL WINAPI DestroyWindow(HWND window);
__declspec(dllimport) LRESULT WINAPI DefWindowProcA(HWND window, unsigned int message, WPARAM w, LPARAM l);
__declspec(dllimport) BOOL WINAPI GetClientRect(HWND window, struct win_rect *rect);
__declspec(dllimport) BOOL WINAPI MoveWindow(HWND window, int32_t x, int32_t y, int32_t w, int32_t h, BOOL repaint);
__declspec(dllimport) BOOL WINAPI ShowWindow(HWND window, int32_t show);
__declspec(dllimport) BOOL WINAPI IsWindowVisible(HWND window);
__declspec(dllimport) BOOL WINAPI SetWindowPos(HWND window, HWND after, int32_t x, int32_t y, int32_t w, int32_t h,
                                               uint32_t flags);
__declspec(dllimport) int32_t WINAPI SetWindowLongA(HWND window, int32_t index, int32_t value);
__declspec(dllimport) int32_t WINAPI GetSystemMetrics(int32_t index);
__declspec(dllimport) int32_t WINAPI ShowCursor(BOOL show);
__declspec(dllimport) HWND WINAPI GetForegroundWindow(void);
__declspec(dllimport) BOOL WINAPI SetForegroundWindow(HWND window);
__declspec(dllimport) BOOL WINAPI PeekMessageA(struct win_msg *msg, HWND window, uint32_t first, uint32_t last,
                                               uint32_t remove);
__declspec(dllimport) BOOL WINAPI TranslateMessage(const struct win_msg *msg);
__declspec(dllimport) LRESULT WINAPI DispatchMessageA(const struct win_msg *msg);

#endif
