/* Vulkan output to the game's window: see vkwin.h. */
#include "vkwin.h"

#include "../log.h"

#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
void *nt_sdl_window(void *hwnd); /* native/user32.c */

static const char *mode_name(int mode)
{
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
    case VK_PRESENT_MODE_FIFO_KHR: return "fifo (vsync)";
    default: return "other";
    }
}

/* SDL3 has the Vulkan loader and makes the surface of the window's SDL
 * window. */
int vkwin_open(struct vkwin *v, HWND parent, int validation, int vsync)
{
    SDL_Window *window = nt_sdl_window(parent);
    PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    Uint32 count = 0;
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    struct win_rect rect;

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
        v->parent = parent;
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
    v->parent = parent;
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

/* headless: renders the frame and saves every REROLL_SHOT_EVERY-th as shot_<n>.ppm
 * (REROLL_SHOT_PREFIX in place of "shot_": two copies of the game in one directory) */
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
    if (!every || v->frames % (uint32_t)every)
        return vkr_end(v->r, 0) != 0;
    v->pixels = realloc(v->pixels, (size_t)v->w * v->h * 4);
    if (!vkr_end(v->r, v->pixels))
        return 0;
    snprintf(name, sizeof name, "%.40s%05u.ppm", getenv("REROLL_SHOT_PREFIX") ? getenv("REROLL_SHOT_PREFIX") : "shot_",
             v->frames);
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

int vkwin_sync(struct vkwin *v)
{
    struct win_rect rect;

    if (!GetClientRect(v->parent, &rect) || rect.right <= 0 || rect.bottom <= 0)
        return 0;
    if (rect.right != v->w || rect.bottom != v->h) {
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
    if (v->headless)
        return present_headless(v);
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

void vkwin_close(struct vkwin *v)
{
    if (v->r)
        vkr_destroy(v->r); /* also destroys the surface (vkr owns it after vkr_set_surface) */
    else
        v->surface = VK_NULL_HANDLE;
    memset(v, 0, sizeof *v);
}
