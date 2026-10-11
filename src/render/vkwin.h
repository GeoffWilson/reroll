#ifndef REROLL_RENDER_VKWIN_H
#define REROLL_RENDER_VKWIN_H

/* Vulkan output to a Win32 window (docs/renderer/plan.md, step 6): loads
 * vulkan-1.dll (Wine's winevulkan), creates the renderer and presents to a
 * child window that covers the given window's client area. The child takes
 * no input (WM_NCHITTEST says transparent, so clicks reach the parent, and it
 * never takes the focus), and sits on top of whatever the parent's own
 * Direct3D would show. */
#include "../win32.h"
#include "vkr.h"

struct vkwin {
    struct vkr *r;
    HWND parent, window; /* window: the child presented to, or the parent itself */
    VkSurfaceKHR surface;
    int w, h;            /* the swapchain's size */
    int failures;        /* presents failed in a row */
    int headless;        /* native build, SDL's offscreen driver: frames are rendered, not shown */
    uint8_t *pixels;
    uint32_t frames;
};

/* `use_child`: 0 presents to the parent itself, 1 to a child window over
 * it, -1 the parent if it can, else a child. `vsync` as vkr_set_vsync.
 * Returns 1 on success. */
int vkwin_open(struct vkwin *v, HWND parent, int use_child, int validation, int vsync);
/* Follows the parent's client size (child size and swapchain). Returns 0
 * while there is nothing to present to (minimised). */
int vkwin_sync(struct vkwin *v);
/* vkr_present, recreating the swapchain when it reports a problem. */
int vkwin_present(struct vkwin *v);
void vkwin_show(struct vkwin *v, int show);
void vkwin_close(struct vkwin *v);

#endif
