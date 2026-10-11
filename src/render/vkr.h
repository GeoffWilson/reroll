#ifndef REROLL_RENDER_VKR_H
#define REROLL_RENDER_VKR_H

/* Vulkan back end for the decoded draw lists (docs/renderer/plan.md).
 * It reproduces what the original Direct3D back end (0x420be0 and below)
 * does with each primitive, at any output size: painter's order, the
 * game's two blend pairs, colour-key transparency, MODULATE texturing and
 * the original texel addressing. Functions come through
 * vkGetInstanceProcAddr (SDL's loader in the game, libvulkan in the tools).
 * It renders to a window's swapchain, or offscreen (the tools, headless
 * tests). */
#include <stdint.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "draw.h"

#define VKR_SLOTS 65 /* the game's 64 texture slots, then the image slot (RD_IMAGE) */
#define VKR_IMAGE_SLOT 64

/* The back end state the game sets once (pipeline.md 3.3): read from game
 * memory or a capture. */
struct vkr_state {
    uint32_t blend_pair[4]; /* D3DBLEND src/dst for abr bit 1 clear (0x4bcd8c/90), set (0x4bcd94/98) */
    uint32_t caps;          /* 0x4b86bc: bit 0 colour key always on, bit 1 alpha blending available */
    uint32_t abr_or;        /* 0x6cce60, or'ed into every abr */
    uint32_t page_slot[64]; /* 0x6cd300: texture page -> slot */
    uint32_t zbuffer;       /* 0x6cce7c: a z-buffer exists: depth test LESSEQUAL, cleared per view */
    uint32_t zwrite_by_tm;  /* 0x6cce80: z writes only for opaque (tm 0) primitives */
};

/* How a frame captured at the game's screen size maps onto the output. */
struct vkr_mapping {
    float scale;            /* output pixels per game pixel */
    float offset_x, offset_y;
};

struct vkr;

/* `validation`: enable VK_LAYER_KHRONOS_validation if present.
 * `extensions`: instance extensions for presenting to a window (from the
 * windowing layer); with any, the device gets VK_KHR_swapchain. */
struct vkr *vkr_create(PFN_vkGetInstanceProcAddr get_proc, int validation, const char *const *extensions,
                       uint32_t extension_count);
void vkr_destroy(struct vkr *r);
VkInstance vkr_instance(struct vkr *r);

/* Present mode for the next vkr_set_surface: -1 (default) mailbox, else
 * FIFO; 0 mailbox, else immediate (no vsync); 1 FIFO (vsync). */
void vkr_set_vsync(struct vkr *r, int vsync);
/* The swapchain's VkPresentModeKHR. */
int vkr_present_mode(const struct vkr *r);

/* Window output: (re)creates the swapchain for `surface` (which vkr then
 * owns) at w x h. */
int vkr_set_surface(struct vkr *r, VkSurfaceKHR surface, int w, int h);

/* Destroys the swapchain and the surface (to try another window). */
void vkr_drop_surface(struct vkr *r);

/* Anisotropic filtering: 1 (off, the original's look) or 2..16 samples
 * (with the textures' mip chains). Call between frames. */
void vkr_set_anisotropy(struct vkr *r, int samples);

/* Sets texture slot `slot` (w x h RGBA8; alpha 0 = colour-key texel). */
int vkr_set_texture(struct vkr *r, int slot, int w, int h, const uint8_t *rgba);

/* Offscreen rendering: begin a frame of w x h cleared to `clear` (RGBA),
 * add draw lists in order, then end and read the pixels back (w * h * 4). */
int vkr_begin(struct vkr *r, int w, int h, const float clear[4]);
void vkr_draw(struct vkr *r, const struct rd_list *list, const struct vkr_state *state, const struct vkr_mapping *map);
int vkr_end(struct vkr *r, uint8_t *rgba);
/* Ends the frame by showing it in the window instead (begin with the
 * window's size). 0 if the swapchain needs recreating. */
int vkr_present(struct vkr *r);

#endif
