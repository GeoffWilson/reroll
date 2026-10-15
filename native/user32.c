/* USER32 for the native build: the game's window is one SDL3 window, and
 * SDL's events become the Windows messages its window procedure expects.
 *
 * Window size: REROLL_WINDOW=WxH for a window of that size, else the
 * display settings chosen in the game's Options > Display menu
 * (reroll_display.cfg in the game folder: fullscreen or windowed, and a
 * resolution, 0x0 = the desktop's), by default borderless fullscreen at the
 * desktop's size. */
#include "keys.h"
#include "nt.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef void *HWND;
typedef unsigned int UINT;
typedef uint32_t WPARAM;
typedef int32_t LPARAM;
typedef int32_t LRESULT;
/* as the game's window procedure is declared (src/game/orig_pf.c window_proc):
 * the HWND as a 32-bit value */
typedef LRESULT(WINAPI *WNDPROC)(uint32_t, UINT, WPARAM, LPARAM);

#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ACTIVATEAPP 0x001c
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105

struct win_class {
    char name[64];
    WNDPROC proc;
};

struct window {
    int used;
    char class_name[64];
    WNDPROC proc;
    DWORD style, ex_style;
    int x, y, w, h;
    int visible;
    struct window *parent;
};

/* MSG, in game memory: 32-bit layout */
struct msg {
    uint32_t window;
    UINT message;
    WPARAM wparam;
    LPARAM lparam;
    DWORD time;
    int32_t x, y;
};

#define MAX_CLASSES 16
#define MAX_WINDOWS 16
#define QUEUE 256
static struct win_class classes[MAX_CLASSES];
static struct window windows[MAX_WINDOWS];
static struct msg queue[QUEUE];
static int queue_head, queue_count;
static SDL_Window *sdl_window;
static struct window *top_window;
static int cursor_count;
static uint8_t key_down[256];

/* HWNDs are 0x20000 + index * 4 */
static HWND handle_of(struct window *w)
{
    return w ? (HWND)(uintptr_t)(0x20000 + (w - windows) * 4) : 0;
}

static struct window *window_of(HWND h)
{
    uintptr_t v = (uintptr_t)h;
    if (v < 0x20000 || v >= 0x20000 + MAX_WINDOWS * 4 || v % 4)
        return 0;
    return windows[(v - 0x20000) / 4].used ? &windows[(v - 0x20000) / 4] : 0;
}

/* for the renderer (src/render/vkwin.c): the SDL window behind a window */
void *nt_sdl_window(void *hwnd)
{
    (void)hwnd;
    return sdl_window;
}

static void post(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct msg *m;
    if (queue_count == QUEUE)
        return;
    m = &queue[(queue_head + queue_count++) % QUEUE];
    m->window = (uint32_t)(uintptr_t)window;
    m->message = message;
    m->wparam = wparam;
    m->lparam = lparam;
    m->time = (DWORD)SDL_GetTicks();
    m->x = m->y = 0;
}

static LRESULT send(struct window *w, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (!w || !w->proc)
        return 0;
    return w->proc((uint32_t)(uintptr_t)handle_of(w), message, wparam, lparam);
}

/* ---- SDL events to messages ---- */

static void key_event(const SDL_KeyboardEvent *e)
{
    const struct nt_key *k = nt_key_from_sdl(e->scancode);
    LPARAM lparam;
    if (!k || !top_window)
        return;
    lparam = 1 | (LPARAM)(k->dik & 0x7f) << 16 | (k->dik & 0x80 ? 1 << 24 : 0);
    if (e->down) {
        if (key_down[k->vk])
            lparam |= 1 << 30;
        key_down[k->vk] = 1;
        post(handle_of(top_window), (SDL_GetModState() & SDL_KMOD_ALT) ? WM_SYSKEYDOWN : WM_KEYDOWN, k->vk, lparam);
    } else {
        key_down[k->vk] = 0;
        post(handle_of(top_window), (SDL_GetModState() & SDL_KMOD_ALT) ? WM_SYSKEYUP : WM_KEYUP, k->vk,
             lparam | 3 << 30);
    }
}

/* Scripted key presses for headless tests: REROLL_KEYS="ms:Key,ms:Key,..."
 * (SDL key names, e.g. 4000:Return,6000:Down); each is held for 150 ms, or
 * for the milliseconds of a third field (6000:Up:20000). */
struct scripted_key {
    uint32_t at, hold;
    SDL_Scancode key;
    int state; /* 0 waiting, 1 down, 2 done */
};
static struct scripted_key script[64];
static int script_count = -1;
uint8_t nt_injected_keys[SDL_SCANCODE_COUNT];

static void run_script(void)
{
    uint32_t now = (uint32_t)SDL_GetTicks();
    int i;
    if (script_count < 0) {
        const char *env = getenv("REROLL_KEYS");
        char buf[1024], *item;
        script_count = 0;
        if (env) {
            snprintf(buf, sizeof buf, "%s", env);
            for (item = strtok(buf, ","); item && script_count < 64; item = strtok(0, ",")) {
                char *colon = strchr(item, ':');
                if (!colon)
                    continue;
                char *hold;
                *colon = 0;
                hold = strchr(colon + 1, ':');
                if (hold)
                    *hold++ = 0;
                script[script_count].at = (uint32_t)atoi(item);
                script[script_count].hold = hold ? (uint32_t)atoi(hold) : 150u;
                script[script_count].key = SDL_GetScancodeFromName(colon + 1);
                script[script_count].state = 0;
                if (script[script_count].key != SDL_SCANCODE_UNKNOWN)
                    script_count++;
            }
        }
    }
    for (i = 0; i < script_count; i++) {
        struct scripted_key *k = &script[i];
        SDL_KeyboardEvent e;
        if (k->state == 2 || now < k->at + (k->state ? k->hold : 0u))
            continue;
        memset(&e, 0, sizeof e);
        e.scancode = k->key;
        e.down = k->state == 0;
        nt_injected_keys[k->key] = e.down;
        k->state++;
        nt_log("script: %s %s", SDL_GetScancodeName(k->key), e.down ? "down" : "up");
        key_event(&e);
    }
}

void nt_pads_open(void); /* dinput.c */

void nt_pump_events(void)
{
    SDL_Event e;
    if (script_count)
        run_script();
    if (*(volatile uint32_t *)(uintptr_t)0x4da284) /* a movie: pads for its skip */
        nt_pads_open();
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            key_event(&e.key);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            /* the movie player stops on a key (Enter, Escape, Space...): a
             * pad's A, B or Start does it too, only while a movie plays
             * (the player's busy flag 0x4da284; menus read the pad themselves) */
            if (top_window && *(volatile uint32_t *)(uintptr_t)0x4da284 &&
                (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST ||
                 e.gbutton.button == SDL_GAMEPAD_BUTTON_START))
                post(handle_of(top_window), WM_KEYDOWN, 0x1b, 1 | 1 << 16);
            break;
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (top_window)
                post(handle_of(top_window), WM_CLOSE, 0, 0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (top_window)
                post(handle_of(top_window), WM_ACTIVATEAPP, e.type == SDL_EVENT_WINDOW_FOCUS_GAINED, 0);
            break;
        default:
            break;
        }
    }
}

/* The game restarts in place: its windows and messages go; the SDL window stays. */
void nt_user_reset(void)
{
    memset(windows, 0, sizeof windows);
    memset(classes, 0, sizeof classes);
    queue_head = queue_count = 0;
    top_window = 0;
    memset(key_down, 0, sizeof key_down);
}

/* ---- classes and windows ---- */

static uint16_t add_class(const char *name, WNDPROC proc)
{
    int i;
    for (i = 0; i < MAX_CLASSES; i++)
        if (!classes[i].proc || !strcasecmp(classes[i].name, name)) {
            snprintf(classes[i].name, sizeof classes[i].name, "%s", name);
            classes[i].proc = proc;
            return (uint16_t)(0xc000 + i);
        }
    return 0;
}

static uint32_t field32(const uint8_t *c, int offset)
{
    uint32_t v;
    memcpy(&v, c + offset, 4);
    return v;
}

/* WNDCLASSEXA (32-bit layout): size, style, proc +8, ..., class name +0x28 */
/* an ATOM, zero-extended: the game keeps the whole of eax */
DWORD WINAPI RegisterClassExA(const uint8_t *c)
{
    return add_class((const char *)(uintptr_t)field32(c, 0x28), (WNDPROC)(uintptr_t)field32(c, 8));
}

/* WNDCLASSA: style, proc +4, ..., class name +0x24 */
uint16_t WINAPI RegisterClassA(const uint8_t *c)
{
    return add_class((const char *)(uintptr_t)field32(c, 0x24), (WNDPROC)(uintptr_t)field32(c, 4));
}

/* ---- display settings (Options > Display) ---- */

#define DISPLAY_FILE "reroll_display.cfg"
static struct nt_display display = {1, 0, 0};
static int anisotropy = 1; /* texture filtering samples, 1 = off */
static int display_loaded;

static void desktop_size(int *w, int *h)
{
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (mode) {
        *w = (int)((float)mode->w * mode->pixel_density + 0.5f);
        *h = (int)((float)mode->h * mode->pixel_density + 0.5f);
    } else {
        *w = 1920;
        *h = 1080;
    }
}

static void display_load(void)
{
    FILE *f;
    char line[64];
    const char *env = getenv("REROLL_WINDOW");
    int w, h;
    if (display_loaded)
        return;
    display_loaded = 1;
    if ((f = fopen(DISPLAY_FILE, "r")) != 0) {
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "fullscreen %d", &w) == 1)
                display.fullscreen = w != 0;
            else if (sscanf(line, "anisotropy %d", &w) == 1 && w >= 1 && w <= 16)
                anisotropy = w;
            else if (sscanf(line, "resolution %dx%d", &w, &h) == 2 && ((w >= 320 && h >= 240) || (!w && !h))) {
                display.w = w;
                display.h = h;
            }
        }
        fclose(f);
    }
    if (env && sscanf(env, "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240) { /* REROLL_WINDOW: a window, this once */
        display.fullscreen = 0;
        display.w = w;
        display.h = h;
    }
}

/* The window's size for a setting (pixels). */
static void display_size(const struct nt_display *d, int *w, int *h)
{
    int dw, dh;
    desktop_size(&dw, &dh);
    if (d->w && d->h) {
        *w = d->w;
        *h = d->h;
    } else if (d->fullscreen) {
        *w = dw;
        *h = dh;
    } else { /* a window at the desktop's resolution: most of the screen */
        *w = dw * 4 / 5;
        *h = dh * 4 / 5;
    }
}

static void display_apply(void)
{
    int w, h;
    display_size(&display, &w, &h);
    if (display.fullscreen) {
        SDL_DisplayMode mode;
        int dw, dh;
        desktop_size(&dw, &dh);
        if ((w == dw && h == dh) ||
            !SDL_GetClosestFullscreenDisplayMode(SDL_GetPrimaryDisplay(), w, h, 0.0f, true, &mode))
            SDL_SetWindowFullscreenMode(sdl_window, 0); /* borderless at the desktop's size */
        else
            SDL_SetWindowFullscreenMode(sdl_window, &mode);
        SDL_SetWindowFullscreen(sdl_window, true);
    } else {
        /* leave fullscreen first (finished), then decorations and size:
         * resizing while the compositor is still leaving fullscreen left an
         * undecorated, unmovable surface on Wayland */
        SDL_SetWindowFullscreen(sdl_window, false);
        SDL_SyncWindow(sdl_window);
        SDL_SetWindowBordered(sdl_window, true);
        SDL_SetWindowResizable(sdl_window, true);
        SDL_SetWindowSize(sdl_window, w, h);
        SDL_SetWindowPosition(sdl_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_SyncWindow(sdl_window);
    nt_log("display: %s %dx%d", display.fullscreen ? "fullscreen" : "windowed", w, h);
}

void nt_display_get(struct nt_display *d)
{
    display_load();
    *d = display;
}

static void display_save(void)
{
    FILE *f;
    if ((f = fopen(DISPLAY_FILE, "w")) != 0) {
        fprintf(f, "fullscreen %d\nresolution %dx%d\nanisotropy %d\n", display.fullscreen, display.w, display.h,
                anisotropy);
        fclose(f);
    }
}

/* Applies and saves a setting (Options > Display). */
void nt_display_set(const struct nt_display *d)
{
    display_load();
    display = *d;
    if (sdl_window)
        display_apply();
    display_save();
}

/* Anisotropic filtering (Options > Graphics): saved with the display. */
int nt_anisotropy_get(void)
{
    display_load();
    return anisotropy;
}

void nt_anisotropy_set(int samples)
{
    display_load();
    anisotropy = samples;
    display_save();
}

/* The display's resolutions (pixels), largest first, without repeats. */
int nt_display_modes(int *w, int *h, int max)
{
    int count = 0, n = 0, i, j;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &count);
    for (i = 0; modes && i < count && n < max; i++) {
        int mw = (int)((float)modes[i]->w * modes[i]->pixel_density + 0.5f);
        int mh = (int)((float)modes[i]->h * modes[i]->pixel_density + 0.5f);
        for (j = 0; j < n && (w[j] != mw || h[j] != mh); j++)
            ;
        if (j == n && mw >= 640 && mh >= 480) {
            w[n] = mw;
            h[n] = mh;
            n++;
        }
    }
    SDL_free(modes);
    return n;
}

static int window_size(int *w, int *h)
{
    display_load();
    display_size(&display, w, h);
    return !display.fullscreen;
}

static void ensure_sdl_window(const char *title)
{
    int w, h, windowed;
    if (sdl_window)
        return;
    windowed = window_size(&w, &h);
    sdl_window = SDL_CreateWindow(title, w, h, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                                   (windowed ? SDL_WINDOW_RESIZABLE : 0));
    if (!sdl_window)
        nt_fatal("SDL_CreateWindow: %s", SDL_GetError());
    display_apply();
}

HWND WINAPI CreateWindowExA(DWORD ex_style, const char *class_name, const char *title, DWORD style, int x, int y,
                            int w, int h, HWND parent, void *menu, void *instance, void *param)
{
    struct window *win = 0;
    int i, pw, ph;
    (void)menu;
    (void)instance;
    (void)param;
    for (i = 0; i < MAX_WINDOWS; i++)
        if (!windows[i].used) {
            win = &windows[i];
            break;
        }
    if (!win)
        return 0;
    memset(win, 0, sizeof *win);
    win->used = 1;
    snprintf(win->class_name, sizeof win->class_name, "%s", (uintptr_t)class_name > 0xffff ? class_name : "?");
    for (i = 0; i < MAX_CLASSES; i++)
        if (classes[i].proc && ((uintptr_t)class_name <= 0xffff ? (uintptr_t)class_name == (uintptr_t)(0xc000 + i)
                                                                 : !strcasecmp(classes[i].name, class_name)))
            win->proc = classes[i].proc;
    win->style = style;
    win->ex_style = ex_style;
    win->parent = window_of(parent);
    if (!win->parent) {
        ensure_sdl_window(title ? title : "Rollcage");
        SDL_GetWindowSizeInPixels(sdl_window, &pw, &ph);
        win->x = win->y = 0;
        win->w = pw;
        win->h = ph;
        top_window = win;
    } else {
        win->x = x;
        win->y = y;
        win->w = w;
        win->h = h;
    }
    nt_log("CreateWindowExA(%s): %p %dx%d", win->class_name, handle_of(win), win->w, win->h);
    return handle_of(win);
}

BOOL WINAPI DestroyWindow(HWND h)
{
    struct window *w = window_of(h);
    if (!w)
        return FALSE;
    nt_log("DestroyWindow(%p)", h);
    send(w, WM_DESTROY, 0, 0);
    if (w == top_window) {
        top_window = 0;
        /* kept for the next window; shown on as it is when the game restarts in place */
    }
    w->used = 0;
    return TRUE;
}

LRESULT WINAPI DefWindowProcA(HWND h, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)wparam;
    (void)lparam;
    if (message == WM_CLOSE)
        DestroyWindow(h);
    return 0;
}

BOOL WINAPI ShowWindow(HWND h, int command)
{
    struct window *w = window_of(h);
    int was;
    if (!w)
        return FALSE;
    was = w->visible;
    w->visible = command != 0;
    if (w == top_window && sdl_window) {
        /* Shown, it stays shown: the game hides each new window right after
         * creating it (0x4771c0), which on a restart in place would take the
         * fullscreen window off the screen for a moment (a display mode change
         * to the monitor). It is hidden only when the program ends. */
        if (w->visible) {
            SDL_ShowWindow(sdl_window);
            SDL_RaiseWindow(sdl_window);
        }
    }
    return was;
}

BOOL WINAPI IsWindowVisible(HWND h)
{
    struct window *w = window_of(h);
    return w && w->visible;
}

BOOL WINAPI UpdateWindow(HWND h)
{
    (void)h;
    return TRUE;
}

BOOL WINAPI GetClientRect(HWND h, int32_t *rect)
{
    struct window *w = window_of(h);
    int pw, ph;
    if (!w)
        return FALSE;
    if (w == top_window && sdl_window) {
        SDL_GetWindowSizeInPixels(sdl_window, &pw, &ph);
        w->w = pw;
        w->h = ph;
    }
    rect[0] = rect[1] = 0;
    rect[2] = w->w;
    rect[3] = w->h;
    return TRUE;
}

BOOL WINAPI ClientToScreen(HWND h, int32_t *point)
{
    struct window *w = window_of(h);
    if (w) {
        point[0] += w->x;
        point[1] += w->y;
    }
    return TRUE;
}

BOOL WINAPI AdjustWindowRectEx(int32_t *rect, DWORD style, BOOL menu, DWORD ex_style)
{
    (void)rect;
    (void)style;
    (void)menu;
    (void)ex_style;
    return TRUE;
}

BOOL WINAPI MoveWindow(HWND h, int x, int y, int width, int height, BOOL repaint)
{
    struct window *w = window_of(h);
    (void)repaint;
    if (w && w != top_window) {
        w->x = x;
        w->y = y;
        w->w = width;
        w->h = height;
    }
    return w != 0;
}

BOOL WINAPI SetWindowPos(HWND h, HWND after, int x, int y, int width, int height, UINT flags)
{
    (void)after;
    (void)flags;
    if (flags & 0x40 /* SWP_SHOWWINDOW */)
        ShowWindow(h, 5);
    return MoveWindow(h, x, y, width, height, 0);
}

LONG WINAPI SetWindowLongA(HWND h, int index, LONG value)
{
    struct window *w = window_of(h);
    LONG old = 0;
    if (!w)
        return 0;
    if (index == -16) {
        old = (LONG)w->style;
        w->style = (DWORD)value;
    } else if (index == -20) {
        old = (LONG)w->ex_style;
        w->ex_style = (DWORD)value;
    }
    return old;
}

/* focus: there is one window */
HWND WINAPI SetFocus(HWND h)
{
    (void)h;
    return handle_of(top_window);
}

BOOL WINAPI SetForegroundWindow(HWND h)
{
    (void)h;
    if (sdl_window)
        SDL_RaiseWindow(sdl_window);
    return TRUE;
}

HWND WINAPI SetActiveWindow(HWND h)
{
    (void)h;
    return handle_of(top_window);
}

HWND WINAPI GetActiveWindow(void)
{
    return handle_of(top_window);
}

HWND WINAPI GetForegroundWindow(void)
{
    return handle_of(top_window);
}

BOOL WINAPI EnableWindow(HWND h, BOOL enable)
{
    (void)h;
    (void)enable;
    return FALSE;
}

int WINAPI GetSystemMetrics(int index)
{
    int w, h;
    if (sdl_window)
        SDL_GetWindowSizeInPixels(sdl_window, &w, &h);
    else
        window_size(&w, &h);
    return index == 0 ? w : index == 1 ? h : 0;
}

BOOL WINAPI SetRect(int32_t *rect, int left, int top, int right, int bottom)
{
    rect[0] = left;
    rect[1] = top;
    rect[2] = right;
    rect[3] = bottom;
    return TRUE;
}

void *WINAPI LoadIconA(void *instance, const char *name)
{
    (void)instance;
    (void)name;
    return (void *)0x30001;
}

void *WINAPI LoadCursorA(void *instance, const char *name)
{
    (void)instance;
    (void)name;
    return (void *)0x30002;
}

int WINAPI ShowCursor(BOOL show)
{
    cursor_count += show ? 1 : -1;
    if (cursor_count >= 0)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
    return cursor_count;
}

/* ---- messages ---- */

static int matches(const struct msg *m, HWND h, UINT first, UINT last)
{
    if (h && m->window != (uint32_t)(uintptr_t)h)
        return 0;
    return (!first && !last) || (m->message >= first && m->message <= last);
}

static int take(struct msg *out, HWND h, UINT first, UINT last, int remove)
{
    int i;
    for (i = 0; i < queue_count; i++) {
        struct msg *m = &queue[(queue_head + i) % QUEUE];
        if (!matches(m, h, first, last))
            continue;
        if (out)
            memcpy(out, m, sizeof *m);
        if (remove) {
            int j;
            for (j = i; j > 0; j--)
                queue[(queue_head + j) % QUEUE] = queue[(queue_head + j - 1) % QUEUE];
            queue_head = (queue_head + 1) % QUEUE;
            queue_count--;
        }
        return 1;
    }
    return 0;
}

BOOL WINAPI PeekMessageA(struct msg *out, HWND h, UINT first, UINT last, UINT remove)
{
    nt_pump_events();
    return take(out, h, first, last, remove & 1);
}

BOOL WINAPI GetMessageA(struct msg *out, HWND h, UINT first, UINT last)
{
    while (!take(out, h, first, last, 1)) {
        SDL_WaitEventTimeout(0, 10);
        nt_pump_events();
    }
    return out->message != WM_QUIT;
}

BOOL WINAPI TranslateMessage(const struct msg *m)
{
    (void)m;
    return FALSE;
}

LRESULT WINAPI DispatchMessageA(const struct msg *m)
{
    return send(window_of((HWND)(uintptr_t)m->window), m->message, m->wparam, m->lparam);
}

void WINAPI PostQuitMessage(int code)
{
    post(0, WM_QUIT, (WPARAM)code, 0);
}

short WINAPI GetKeyState(int vk)
{
    return (short)(key_down[vk & 0xff] ? 0x8000 : 0);
}

short WINAPI GetAsyncKeyState(int vk)
{
    const struct nt_key *k = nt_key_from_vk((uint8_t)vk);
    const bool *state;
    SDL_PumpEvents();
    state = SDL_GetKeyboardState(0);
    return (short)(k && (state[k->sdl] || nt_injected_keys[k->sdl]) ? 0x8000 : 0);
}

/* ---- dialogs: none in the native build ---- */

int WINAPI MessageBoxA(HWND h, const char *text, const char *caption, UINT type)
{
    (void)h;
    (void)type;
    nt_log("MessageBoxA(%s): %s", caption ? caption : "", text ? text : "");
    return 1; /* IDOK */
}

int32_t WINAPI DialogBoxParamA(void *instance, const char *name, HWND parent, void *proc, LPARAM param)
{
    (void)instance;
    (void)parent;
    (void)proc;
    (void)param;
    nt_log("DialogBoxParamA(%p): not shown", (const void *)name);
    return 1; /* IDOK */
}

BOOL WINAPI EndDialog(HWND h, int32_t result)
{
    (void)h;
    (void)result;
    return TRUE;
}

HWND WINAPI GetDlgItem(HWND h, int id)
{
    (void)h;
    (void)id;
    return 0;
}

LRESULT WINAPI SendDlgItemMessageA(HWND h, int id, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)h;
    (void)id;
    (void)message;
    (void)wparam;
    (void)lparam;
    return 0;
}
