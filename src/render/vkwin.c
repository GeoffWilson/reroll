/* Vulkan output to a Win32 window: see vkwin.h. */
#include "vkwin.h"

#include "../log.h"

#ifdef REROLL_NATIVE
#include <SDL3/SDL_vulkan.h>
void *nt_sdl_window(void *hwnd); /* native/user32.c */
#endif

/* VK_KHR_win32_surface, declared here instead of through vulkan_win32.h
 * (which wants windows.h). */
typedef struct {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    HINSTANCE hinstance;
    HWND hwnd;
} win32_surface_info;
typedef VkResult(VKAPI_PTR *create_win32_surface_fn)(VkInstance instance, const win32_surface_info *info,
                                                       const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface);

static LRESULT WINAPI child_proc(HWND window, unsigned int message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcA(window, message, wparam, lparam);
}

static HWND make_child(HWND parent, int w, int h)
{
    static int registered;
    HINSTANCE instance = GetModuleHandleA(0);

    if (!registered) {
        struct win_class c;
        memset(&c, 0, sizeof c);
        c.proc = child_proc;
        c.instance = instance;
        c.class_name = "reroll_vulkan";
        if (!RegisterClassA(&c))
            return 0;
        registered = 1;
    }
    return CreateWindowExA(0, "reroll_vulkan", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, w, h, parent, 0,
                           instance, 0);
}

static const char *mode_name(int mode)
{
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
    case VK_PRESENT_MODE_FIFO_KHR: return "fifo (vsync)";
    default: return "other";
    }
}

/* A surface and swapchain for `window`; 0 (and nothing kept) if either fails. */
static int attach(struct vkwin *v, PFN_vkGetInstanceProcAddr get_proc, HWND window)
{
    create_win32_surface_fn create_surface =
        (create_win32_surface_fn)get_proc(vkr_instance(v->r), "vkCreateWin32SurfaceKHR");
    win32_surface_info info;
    VkResult result;

    memset(&info, 0, sizeof info);
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandleA(0);
    info.hwnd = window;
    if (!create_surface || (result = create_surface(vkr_instance(v->r), &info, 0, &v->surface)) != VK_SUCCESS) {
        log_printf("vulkan: vkCreateWin32SurfaceKHR failed");
        v->surface = VK_NULL_HANDLE;
        return 0;
    }
    if (!vkr_set_surface(v->r, v->surface, v->w, v->h)) {
        log_printf("vulkan: no swapchain for %s window (%dx%d)", window == v->parent ? "the game's" : "the child",
                   v->w, v->h);
        vkr_drop_surface(v->r);
        v->surface = VK_NULL_HANDLE;
        return 0;
    }
    v->window = window;
    return 1;
}

#ifdef REROLL_NATIVE
/* The native build: SDL3 has the Vulkan loader and makes the surface of the
 * window's SDL window. */
int vkwin_open(struct vkwin *v, HWND parent, int use_child, int validation, int vsync)
{
    SDL_Window *window = nt_sdl_window(parent);
    PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    Uint32 count = 0;
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    struct win_rect rect;

    (void)use_child;
    memset(v, 0, sizeof *v);
    const char *driver = SDL_GetCurrentVideoDriver();

    if (driver && !strcmp(driver, "offscreen")) {
        /* headless (tests): render offscreen; REROLL_SHOT_EVERY=n saves every n-th frame */
        memset(v, 0, sizeof *v);
        v->r = get_proc ? vkr_create(get_proc, validation, 0, 0) : 0;
        if (!v->r) {
            log_printf("vulkan: headless device creation failed");
            return 0;
        }
        GetClientRect(parent, &rect);
        v->parent = v->window = parent;
        v->w = rect.right > 0 ? rect.right : 1;
        v->h = rect.bottom > 0 ? rect.bottom : 1;
        v->headless = 1;
        log_printf("vulkan: headless at %dx%d", v->w, v->h);
        return 1;
    }
    if (!window || !get_proc || !extensions) {
        log_printf("vulkan: SDL has no Vulkan (%s)", SDL_GetError());
        return 0;
    }
    v->r = vkr_create(get_proc, validation, extensions, count);
    if (!v->r) {
        log_printf("vulkan: device creation failed");
        return 0;
    }
    vkr_set_vsync(v->r, vsync);
    GetClientRect(parent, &rect);
    v->parent = v->window = parent;
    v->w = rect.right > 0 ? rect.right : 1;
    v->h = rect.bottom > 0 ? rect.bottom : 1;
    if (!SDL_Vulkan_CreateSurface(window, vkr_instance(v->r), 0, &v->surface) ||
        !vkr_set_surface(v->r, v->surface, v->w, v->h)) {
        log_printf("vulkan: no surface or swapchain (%s)", SDL_GetError());
        vkwin_close(v);
        return 0;
    }
    log_printf("vulkan: presenting at %dx%d, %s", v->w, v->h, mode_name(vkr_present_mode(v->r)));
    return 1;
}
#else
int vkwin_open(struct vkwin *v, HWND parent, int use_child, int validation, int vsync)
{
    static const char *const extensions[2] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    HMODULE vulkan = LoadLibraryA("vulkan-1.dll");
    PFN_vkGetInstanceProcAddr get_proc;
    struct win_rect rect;
    HWND child;

    memset(v, 0, sizeof *v);
    if (!vulkan) {
        log_printf("vulkan: no vulkan-1.dll");
        return 0;
    }
    get_proc = (PFN_vkGetInstanceProcAddr)GetProcAddress(vulkan, "vkGetInstanceProcAddr");
    if (!get_proc) {
        log_printf("vulkan: no vkGetInstanceProcAddr");
        return 0;
    }
    v->r = vkr_create(get_proc, validation, extensions, 2);
    if (!v->r) {
        log_printf("vulkan: device creation failed");
        return 0;
    }
    vkr_set_vsync(v->r, vsync);
    GetClientRect(parent, &rect);
    v->parent = parent;
    v->w = rect.right > 0 ? rect.right : 1;
    v->h = rect.bottom > 0 ? rect.bottom : 1;
    /* the window itself (its own Direct3D never presents once we do), else a child over it */
    if (use_child <= 0 && attach(v, get_proc, parent)) {
    } else if (use_child != 0 && (child = make_child(parent, v->w, v->h)) != 0) {
        if (!attach(v, get_proc, child)) {
            DestroyWindow(child);
            vkwin_close(v);
            return 0;
        }
    } else {
        vkwin_close(v);
        return 0;
    }
    log_printf("vulkan: presenting at %dx%d%s, %s", v->w, v->h, v->window != parent ? " (child window)" : "",
               mode_name(vkr_present_mode(v->r)));
    return 1;
}
#endif

#ifdef REROLL_NATIVE
#include <stdio.h>
#include <stdlib.h>

/* headless: renders the frame and saves every REROLL_SHOT_EVERY-th as shot_<n>.ppm */
static int present_headless(struct vkwin *v)
{
    static int every = -1;
    char name[64];
    FILE *f;
    int i;

    if (every < 0) {
        const char *e = getenv("REROLL_SHOT_EVERY");
        every = e ? atoi(e) : 0;
    }
    v->frames++;
    if (!every || v->frames % (uint32_t)every) {
        return vkr_end(v->r, v->pixels = realloc(v->pixels, (size_t)v->w * v->h * 4)) != 0;
    }
    v->pixels = realloc(v->pixels, (size_t)v->w * v->h * 4);
    if (!vkr_end(v->r, v->pixels))
        return 0;
    snprintf(name, sizeof name, "shot_%05u.ppm", v->frames);
    f = fopen(name, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", v->w, v->h);
        for (i = 0; i < v->w * v->h; i++)
            fwrite(v->pixels + i * 4, 1, 3, f);
        fclose(f);
        log_printf("vulkan: saved %s", name);
    }
    return 1;
}
#endif

int vkwin_sync(struct vkwin *v)
{
    struct win_rect rect;

    if (!GetClientRect(v->parent, &rect) || rect.right <= 0 || rect.bottom <= 0)
        return 0;
    if (rect.right != v->w || rect.bottom != v->h) {
        if (v->window != v->parent)
            MoveWindow(v->window, 0, 0, rect.right, rect.bottom, 0);
        v->w = rect.right;
        v->h = rect.bottom;
        if (v->headless)
            return 1;
        if (!vkr_set_surface(v->r, v->surface, v->w, v->h)) {
            log_printf("vulkan: swapchain recreation failed (%dx%d)", v->w, v->h);
            return 0;
        }
        log_printf("vulkan: resized to %dx%d", v->w, v->h);
    }
    return 1;
}

int vkwin_present(struct vkwin *v)
{
#ifdef REROLL_NATIVE
    if (v->headless)
        return present_headless(v);
#endif
    if (vkr_present(v->r)) {
        v->failures = 0;
        return 1;
    }
    if (v->failures < 5)
        log_printf("vulkan: present failed (%d in a row), recreating the swapchain", v->failures + 1);
    /* out of date or suboptimal: a new swapchain at the same size */
    if (++v->failures <= 3 || v->failures % 60 == 0)
        vkr_set_surface(v->r, v->surface, v->w, v->h);
    return 0;
}

void vkwin_show(struct vkwin *v, int show)
{
    if (v->window && v->window != v->parent && !IsWindowVisible(v->window) != !show)
        ShowWindow(v->window, show ? SW_SHOWNA : SW_HIDE);
}

void vkwin_close(struct vkwin *v)
{
    if (v->r)
        vkr_destroy(v->r); /* also destroys the surface (vkr owns it after vkr_set_surface) */
    else
        v->surface = VK_NULL_HANDLE;
    if (v->window && v->window != v->parent)
        DestroyWindow(v->window);
    memset(v, 0, sizeof *v);
}
