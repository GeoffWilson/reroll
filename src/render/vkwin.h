#ifndef REROLL_RENDER_VKWIN_H
#define REROLL_RENDER_VKWIN_H

/* Vulkan output to the game's window (docs/renderer/plan.md, step 6):
 * creates the renderer and presents to the SDL3 window behind the game's
 * window (native/user32.c), or renders offscreen under SDL's offscreen
 * driver (headless tests). */
#include "../win32.h"
#include "vkr.h"

struct vkwin {
    struct vkr *r;
    HWND parent;         /* the game's window */
    VkSurfaceKHR surface;
    int w, h;            /* the swapchain's size */
    int failures;        /* presents failed in a row */
    int headless;        /* SDL's offscreen driver: frames are rendered, not shown */
    uint8_t *pixels;
    uint32_t frames;
};

/* `vsync` as vkr_set_vsync. Returns 1 on success. */
int vkwin_open(struct vkwin *v, HWND parent, int validation, int vsync);
/* Follows the window's client size (the swapchain). Returns 0
 * while there is nothing to present to (minimised). */
int vkwin_sync(struct vkwin *v);
/* vkr_present, recreating the swapchain when it reports a problem. */
int vkwin_present(struct vkwin *v);
void vkwin_close(struct vkwin *v);

#endif
