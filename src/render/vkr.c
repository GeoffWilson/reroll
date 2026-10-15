/* Vulkan back end for decoded draw lists: see vkr.h.
 *
 * The original's per-primitive state (pipeline.md 3.3, 0x420770):
 *   blend pair: abr bit 1 (after or-ing 0x6cce60) picks (0x4bcd94, 0x4bcd98),
 *     otherwise (0x4bcd8c, 0x4bcd90), as D3DBLEND factors;
 *   tm = code & 3: 0 and 1 opaque, 2 and 3 alpha-blended with that pair;
 *   colour key: caps bit 0 set: on for tm 0/1, off for 2/3; caps bit 0
 *     clear: tm 0 abr bit 0, tm 1 on, tm 2/3 off;
 *   texture stage MODULATE; texel addressing from the 0x41cfd0 tables:
 *     t(i) = i * s + o, o = (1 / size) / 2.1, s = 1 / (size - o).
 * Primitives are drawn in draw-list order, clipped to each view's
 * rectangle. With a z-buffer (0x6cce7c, z-buffer mode 0x428270) the depth
 * test is LESSEQUAL on sz = z / 65536 (interpolated linearly across the
 * screen, as D3D does for transformed vertices), the depth is cleared when
 * a view starts (0x428770, at its first ordering table 0x6cdd0c), and only
 * opaque primitives write depth when 0x6cce80 is set. Without one, painter's
 * order alone, as the original. */
#include "vkr.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint32_t vert_spv[] =
#include "draw_vert.inc"
    ;
static const uint32_t frag_spv[] =
#include "draw_frag.inc"
    ;

#define INSTANCE_FUNCTIONS(X)                                                                                        \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceQueueFamilyProperties)                    \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)                                   \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR)                             \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkDestroySurfaceKHR)

#define DEVICE_FUNCTIONS(X)                                                                                          \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateCommandPool) X(vkDestroyCommandPool)                            \
    X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkQueueSubmit) X(vkQueueWaitIdle)    \
    X(vkResetCommandBuffer) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory)   \
    X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateBuffer)                \
    X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory)        \
    X(vkCreateSampler) X(vkDestroySampler) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout)            \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets)       \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateShaderModule) X(vkDestroyShaderModule)            \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage)              \
    X(vkCmdCopyImageToBuffer) X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdBindPipeline)                      \
    X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdPushConstants) X(vkCmdSetViewport)                  \
    X(vkCmdSetScissor) X(vkCmdDraw) X(vkCmdClearAttachments) X(vkCmdBlitImage) X(vkDeviceWaitIdle)                  \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences)                 \
    X(vkResetFences)

#define SWAPCHAIN_FUNCTIONS(X)                                                                                       \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR)            \
    X(vkQueuePresentKHR)

struct vertex {
    float pos[4];
    float uv[2];
    uint8_t colour[4];
    float depth;
};

struct command {
    int pipeline, slot, textured, colour_key, clear_depth;
    VkRect2D scissor;
    uint32_t first, count;
};

struct texture {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    int w, h, levels;
};

/* Texture pages get a short mip chain for anisotropic filtering: four
 * levels at most, as the pages are atlases (more would blend neighbouring
 * pictures). Off, the sampler uses the base level only, as the original. */
#define MAX_LEVELS 4

#define MAX_PIPELINES 16

#define FRAMES 2

struct frame {
    VkCommandBuffer cmd;
    VkFence done;          /* signalled when its last submit has finished */
    VkSemaphore acquired;  /* the swapchain image */
    VkBuffer vertex_buffer, staging;
    VkDeviceMemory vertex_memory, staging_memory;
    void *vertex_mapped, *staging_mapped;
    VkDeviceSize vertex_size, staging_size, staging_used;
    struct {
        int slot, w, h;
        VkDeviceSize offset;
    } pending[VKR_SLOTS * 2];
    int pending_count;
};

struct vkr {
    PFN_vkGetInstanceProcAddr get_proc;
    PFN_vkCreateInstance vkCreateInstance;
#define DECLARE(name) PFN_##name name;
    INSTANCE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
    SWAPCHAIN_FUNCTIONS(DECLARE)
#undef DECLARE
    int windowed;
    int vsync; /* -1 no preference or 1: FIFO; 0 off (mailbox, else immediate) */
    VkPresentModeKHR present_mode;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkImage swap_images[8];
    uint32_t swap_count;
    int swap_w, swap_h;
    VkSemaphore acquired, rendered[8]; /* rendered: one per swapchain image, released when it is re-acquired */
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkPhysicalDeviceMemoryProperties memory_props;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;
    VkCommandPool pool;
    VkSampler sampler;
    VkPhysicalDeviceFeatures features;
    float max_anisotropy;
    int anisotropy; /* samples; 1 = off */
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkShaderModule vert, frag;
    struct {
        int blend, depth_test, depth_write;
        uint32_t src, dst;
        VkPipeline pipeline;
    } pipelines[MAX_PIPELINES];
    int pipeline_count;
    struct texture textures[VKR_SLOTS];
    struct texture white;
    /* the frame being built */
    int width, height;
    float clear[4];
    VkImage target;
    VkDeviceMemory target_memory;
    VkImageView target_view;
    VkImage depth;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    int target_w, target_h;
    struct vertex *verts;
    uint32_t vert_count, vert_max;
    /* FRAMES frames in flight, each with its own commands, fence and
     * buffers (vertices, and texture uploads waiting for its submit); f is
     * the one being built, its previous use already finished */
    struct frame *f, frames[FRAMES];
    int frame_index;
    struct command *commands;
    uint32_t command_count, command_max;
};

static int vertex_room(struct vkr *r, uint32_t want);

static uint32_t memory_type(struct vkr *r, uint32_t bits, VkMemoryPropertyFlags want)
{
    uint32_t i;
    for (i = 0; i < r->memory_props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (r->memory_props.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

static int make_buffer(struct vkr *r, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buffer,
                       VkDeviceMemory *memory)
{
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryRequirements req;
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

    info.size = size;
    info.usage = usage;
    if (r->vkCreateBuffer(r->device, &info, 0, buffer) != VK_SUCCESS)
        return 0;
    r->vkGetBufferMemoryRequirements(r->device, *buffer, &req);
    alloc.allocationSize = req.size;
    /* cached if there is such memory: vertices are written straight into it,
     * and uncached (write-combined) memory is slow for anything but a
     * single streaming copy */
    alloc.memoryTypeIndex = memory_type(r, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (!(r->memory_props.memoryTypes[alloc.memoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ||
        !(req.memoryTypeBits & (1u << alloc.memoryTypeIndex)))
        alloc.memoryTypeIndex = memory_type(r, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (r->vkAllocateMemory(r->device, &alloc, 0, memory) != VK_SUCCESS)
        return 0;
    r->vkBindBufferMemory(r->device, *buffer, *memory, 0);
    return 1;
}

static int make_image_levels(struct vkr *r, int w, int h, int levels, VkFormat format, VkImageUsageFlags usage,
                             VkImage *image, VkDeviceMemory *memory, VkImageView *view);

static int make_image(struct vkr *r, int w, int h, VkFormat format, VkImageUsageFlags usage, VkImage *image,
                      VkDeviceMemory *memory, VkImageView *view)
{
    return make_image_levels(r, w, h, 1, format, usage, image, memory, view);
}

static int make_image_levels(struct vkr *r, int w, int h, int levels, VkFormat format, VkImageUsageFlags usage,
                             VkImage *image, VkDeviceMemory *memory, VkImageView *view)
{
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryRequirements req;
    VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = (uint32_t)w;
    info.extent.height = (uint32_t)h;
    info.extent.depth = 1;
    info.mipLevels = (uint32_t)levels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (r->vkCreateImage(r->device, &info, 0, image) != VK_SUCCESS)
        return 0;
    r->vkGetImageMemoryRequirements(r->device, *image, &req);
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memory_type(r, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (r->vkAllocateMemory(r->device, &alloc, 0, memory) != VK_SUCCESS)
        return 0;
    r->vkBindImageMemory(r->device, *image, *memory, 0);
    view_info.image = *image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask =
        format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = (uint32_t)levels;
    view_info.subresourceRange.layerCount = 1;
    return r->vkCreateImageView(r->device, &view_info, 0, view) == VK_SUCCESS;
}

static void barrier_aspect(struct vkr *r, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                           VkImageLayout to, VkAccessFlags src_access, VkAccessFlags dst_access,
                           VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    b.subresourceRange.layerCount = 1;
    r->vkCmdPipelineBarrier(r->f->cmd, src_stage, dst_stage, 0, 0, 0, 0, 0, 1, &b);
}

static void barrier(struct vkr *r, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
                    VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    barrier_aspect(r, image, VK_IMAGE_ASPECT_COLOR_BIT, from, to, src_access, dst_access, src_stage, dst_stage);
}

/* After a frame's submit: on to the next frame's resources, once the GPU
 * has finished with them (the frame before last). */
/* microseconds spent blocked in the swapchain and fence calls, for the
 * frame-time log: acquiring an image, waiting for a frame's fence,
 * presenting */
uint64_t vkr_wait_us[3];

static uint64_t wait_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void next_frame(struct vkr *r)
{
    uint64_t t;
    static int in_flight;
    if (!in_flight) { /* REROLL_FRAMES=1: wait for each frame (for comparisons) */
        const char *e = getenv("REROLL_FRAMES");
        in_flight = e && atoi(e) >= 1 && atoi(e) <= FRAMES ? atoi(e) : FRAMES;
    }
    r->frame_index = (r->frame_index + 1) % in_flight;
    r->f = &r->frames[r->frame_index];
    t = wait_clock();
    r->vkWaitForFences(r->device, 1, &r->f->done, VK_TRUE, UINT64_MAX);
    vkr_wait_us[1] += wait_clock() - t;
}

static void begin_commands(struct vkr *r)
{
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r->vkResetCommandBuffer(r->f->cmd, 0);
    r->vkBeginCommandBuffer(r->f->cmd, &begin);
}

static void submit_and_wait(struct vkr *r)
{
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    r->vkEndCommandBuffer(r->f->cmd);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->f->cmd;
    r->vkQueueSubmit(r->queue, 1, &submit, VK_NULL_HANDLE);
    r->vkQueueWaitIdle(r->queue);
}

static void write_descriptor(struct vkr *r, int slot)
{
    VkDescriptorImageInfo image = {0};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    const struct texture *t = r->textures[slot].view ? &r->textures[slot] : &r->white;

    image.sampler = r->sampler;
    image.imageView = t->view;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    write.dstSet = r->set;
    write.dstBinding = 0;
    write.dstArrayElement = (uint32_t)slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    r->vkUpdateDescriptorSets(r->device, 1, &write, 0, 0);
}

static int upload(struct vkr *r, struct texture *t, int w, int h, const uint8_t *rgba)
{
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    void *mapped;
    VkBufferImageCopy copy = {0};
    VkDeviceSize size = (VkDeviceSize)w * (VkDeviceSize)h * 4;

    if (!make_image(r, w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    &t->image, &t->memory, &t->view))
        return 0;
    t->w = w;
    t->h = h;
    t->levels = 1;
    if (!make_buffer(r, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &staging_memory))
        return 0;
    r->vkMapMemory(r->device, staging_memory, 0, size, 0, &mapped);
    memcpy(mapped, rgba, (size_t)size);
    r->vkUnmapMemory(r->device, staging_memory);
    begin_commands(r);
    barrier(r, t->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = (uint32_t)w;
    copy.imageExtent.height = (uint32_t)h;
    copy.imageExtent.depth = 1;
    r->vkCmdCopyBufferToImage(r->f->cmd, staging, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(r, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    submit_and_wait(r);
    r->vkDestroyBuffer(r->device, staging, 0);
    r->vkFreeMemory(r->device, staging_memory, 0);
    return 1;
}

static void free_texture(struct vkr *r, struct texture *t)
{
    if (t->view)
        r->vkDestroyImageView(r->device, t->view, 0);
    if (t->image)
        r->vkDestroyImage(r->device, t->image, 0);
    if (t->memory)
        r->vkFreeMemory(r->device, t->memory, 0);
    memset(t, 0, sizeof *t);
}

/* Records the waiting texture copies into r->f->cmd (begun). */
static void record_uploads(struct vkr *r)
{
    int i;
    for (i = 0; i < r->f->pending_count; i++) {
        struct texture *t = &r->textures[r->f->pending[i].slot];
        VkBufferImageCopy copy = {0};
        VkDeviceSize offset = r->f->pending[i].offset;
        int level, lw, lh;
        if (!t->image || t->w != r->f->pending[i].w || t->h != r->f->pending[i].h)
            continue; /* replaced by a different size since */
        barrier(r, t->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        for (level = 0, lw = t->w, lh = t->h; level < t->levels; level++, lw /= 2, lh /= 2) {
            copy.bufferOffset = offset;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = (uint32_t)level;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent.width = (uint32_t)lw;
            copy.imageExtent.height = (uint32_t)lh;
            copy.imageExtent.depth = 1;
            r->vkCmdCopyBufferToImage(r->f->cmd, r->f->staging, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            offset += (VkDeviceSize)lw * (VkDeviceSize)lh * 4;
        }
        barrier(r, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    r->f->pending_count = 0;
    r->f->staging_used = 0; /* written again only once this frame's submit has finished */
}

static void flush_uploads(struct vkr *r)
{
    if (!r->f->pending_count)
        return;
    begin_commands(r);
    record_uploads(r);
    submit_and_wait(r);
}

/* A host-visible buffer of at least `need` bytes in *buffer (grown by
 * replacing it; the frame owning it has finished on the GPU). */
static int ensure_buffer(struct vkr *r, VkDeviceSize need, VkBufferUsageFlags usage, VkBuffer *buffer,
                         VkDeviceMemory *memory, void **mapped, VkDeviceSize *size)
{
    VkDeviceSize grow = *size ? *size : 0x100000;
    if (need <= *size)
        return 1;
    while (grow < need)
        grow *= 2;
    if (*buffer) {
        r->vkUnmapMemory(r->device, *memory);
        r->vkDestroyBuffer(r->device, *buffer, 0);
        r->vkFreeMemory(r->device, *memory, 0);
        *buffer = VK_NULL_HANDLE;
        *size = 0;
    }
    if (!make_buffer(r, grow, usage, buffer, memory))
        return 0;
    r->vkMapMemory(r->device, *memory, 0, grow, 0, mapped);
    *size = grow;
    return 1;
}

/* The next level down (w x h -> w/2 x h/2): colour averaged over the
 * texels that are there (alpha-weighted, so colour-key texels don't darken
 * edges), alpha averaged (the shader's colour-key test is alpha < 0.5). */
static void mip_down(const uint8_t *src, int w, int h, uint8_t *dst)
{
    int x, y, k, nw = w / 2, nh = h / 2;
    for (y = 0; y < nh; y++)
        for (x = 0; x < nw; x++) {
            const uint8_t *p[4];
            uint32_t a = 0, c[3] = {0, 0, 0};
            p[0] = src + ((y * 2) * w + x * 2) * 4;
            p[1] = p[0] + 4;
            p[2] = p[0] + w * 4;
            p[3] = p[2] + 4;
            for (k = 0; k < 4; k++) {
                a += p[k][3];
                c[0] += p[k][0] * p[k][3];
                c[1] += p[k][1] * p[k][3];
                c[2] += p[k][2] * p[k][3];
            }
            for (k = 0; k < 3; k++)
                dst[(y * nw + x) * 4 + k] = (uint8_t)(a ? c[k] / a : 0);
            dst[(y * nw + x) * 4 + 3] = (uint8_t)(a / 4);
        }
}

static int texture_levels(int w, int h)
{
    int levels = 1;
    while (levels < MAX_LEVELS && !(w & 1) && !(h & 1) && w >= 16 && h >= 16) {
        w /= 2;
        h /= 2;
        levels++;
    }
    return levels;
}

int vkr_set_texture(struct vkr *r, int slot, int w, int h, const uint8_t *rgba)
{
    struct texture *t;
    int levels = texture_levels(w, h), level, lw, lh;
    VkDeviceSize size = 0;

    if (slot < 0 || slot >= VKR_SLOTS || w <= 0 || h <= 0)
        return 0;
    for (level = 0, lw = w, lh = h; level < levels; level++, lw /= 2, lh /= 2)
        size += (VkDeviceSize)lw * (VkDeviceSize)lh * 4;
    t = &r->textures[slot];
    if (r->f->pending_count == (int)(sizeof r->f->pending / sizeof *r->f->pending) ||
        r->f->staging_used + size > r->f->staging_size)
        flush_uploads(r);
    if (!ensure_buffer(r, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &r->f->staging, &r->f->staging_memory, &r->f->staging_mapped,
                       &r->f->staging_size))
        return 0;
    if (!t->image || t->w != w || t->h != h) {
        r->vkDeviceWaitIdle(r->device); /* the image and descriptor may be in use by a frame in flight */
        free_texture(r, t);
        if (!make_image_levels(r, w, h, levels, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &t->image, &t->memory,
                               &t->view)) {
            free_texture(r, t);
            write_descriptor(r, slot);
            return 0;
        }
        t->w = w;
        t->h = h;
        t->levels = levels;
        write_descriptor(r, slot);
    }
    {
        /* the levels are made in ordinary memory: the staging buffer is
         * write-combined, and reading it back (as mip_down would) is
         * hundreds of times slower */
        static uint8_t *levels_buffer;
        static size_t levels_size;
        uint8_t *in = (uint8_t *)rgba, *out;
        if (levels > 1) {
            if (levels_size < size) {
                free(levels_buffer);
                levels_buffer = malloc((size_t)size);
                levels_size = levels_buffer ? (size_t)size : 0;
            }
            if (!levels_buffer)
                return 0;
            memcpy(levels_buffer, rgba, (size_t)w * (size_t)h * 4);
            for (level = 1, lw = w, lh = h, out = levels_buffer; level < levels; level++, lw /= 2, lh /= 2) {
                mip_down(out, lw, lh, out + (size_t)lw * (size_t)lh * 4);
                out += (size_t)lw * (size_t)lh * 4;
            }
            in = levels_buffer;
        }
        memcpy((uint8_t *)r->f->staging_mapped + r->f->staging_used, in, (size_t)size);
    }
    r->f->pending[r->f->pending_count].slot = slot;
    r->f->pending[r->f->pending_count].w = w;
    r->f->pending[r->f->pending_count].h = h;
    r->f->pending[r->f->pending_count].offset = r->f->staging_used;
    r->f->pending_count++;
    r->f->staging_used += (size + 15) & ~(VkDeviceSize)15;
    return 1;
}

static VkBlendFactor blend_factor(uint32_t d3d)
{
    switch (d3d) {
    case 1: return VK_BLEND_FACTOR_ZERO;
    case 2: return VK_BLEND_FACTOR_ONE;
    case 3: return VK_BLEND_FACTOR_SRC_COLOR;
    case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return VK_BLEND_FACTOR_DST_ALPHA;
    case 8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return VK_BLEND_FACTOR_DST_COLOR;
    case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

static int pipeline_for(struct vkr *r, int blend, uint32_t src, uint32_t dst, int depth_test, int depth_write)
{
    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    VkVertexInputBindingDescription binding = {0, sizeof(struct vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[4] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, 16},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 24},
        {3, 0, VK_FORMAT_R32_SFLOAT, 28},
    };
    VkPipelineDepthStencilStateCreateInfo depth = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineVertexInputStateCreateInfo input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState attachment = {0};
    VkPipelineColorBlendStateCreateInfo blending = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    int i;

    if (!blend)
        src = dst = 0;
    for (i = 0; i < r->pipeline_count; i++)
        if (r->pipelines[i].blend == blend && r->pipelines[i].src == src && r->pipelines[i].dst == dst &&
            r->pipelines[i].depth_test == depth_test && r->pipelines[i].depth_write == depth_write)
            return i;
    if (r->pipeline_count == MAX_PIPELINES)
        return 0;

    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = r->vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = r->frag;
    stages[1].pName = "main";
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 4;
    input.pVertexAttributeDescriptions = attributes;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE; /* the game culls in software */
    raster.lineWidth = 1.0f;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;
    if (blend) {
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = blend_factor(src);
        attachment.dstColorBlendFactor = blend_factor(dst);
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    blending.attachmentCount = 1;
    blending.pAttachments = &attachment;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    depth.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    rendering.depthAttachmentFormat = depth_format;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blending;
    info.pDepthStencilState = &depth;
    info.pDynamicState = &dynamic;
    info.layout = r->layout;
    if (r->vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &info, 0,
                                     &r->pipelines[r->pipeline_count].pipeline) != VK_SUCCESS)
        return 0;
    r->pipelines[r->pipeline_count].blend = blend;
    r->pipelines[r->pipeline_count].src = src;
    r->pipelines[r->pipeline_count].dst = dst;
    r->pipelines[r->pipeline_count].depth_test = depth_test;
    r->pipelines[r->pipeline_count].depth_write = depth_write;
    return r->pipeline_count++;
}

static VkShaderModule shader(struct vkr *r, const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule module = VK_NULL_HANDLE;
    info.codeSize = size;
    info.pCode = code;
    r->vkCreateShaderModule(r->device, &info, 0, &module);
    return module;
}

/* D3D TEXTUREMAG/MIN LINEAR, TEXTUREADDRESS CLAMP; with anisotropic
 * filtering on, the mip chain (trilinear) and that many samples. */
static void make_sampler(struct vkr *r)
{
    VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (r->anisotropy > 1 && r->features.samplerAnisotropy) {
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.maxLod = (float)(MAX_LEVELS - 1);
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = (float)r->anisotropy < r->max_anisotropy ? (float)r->anisotropy : r->max_anisotropy;
    } else {
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.maxLod = 0.0f; /* the base level only */
    }
    r->vkCreateSampler(r->device, &info, 0, &r->sampler);
}

void vkr_set_anisotropy(struct vkr *r, int samples)
{
    int i;
    if (samples < 1)
        samples = 1;
    if (!r || samples == r->anisotropy)
        return;
    r->anisotropy = samples;
    if (!r->sampler)
        return; /* made with this setting */
    r->vkDeviceWaitIdle(r->device);
    r->vkDestroySampler(r->device, r->sampler, 0);
    make_sampler(r);
    for (i = 0; i < VKR_SLOTS; i++)
        write_descriptor(r, i);
}

struct vkr *vkr_create(PFN_vkGetInstanceProcAddr get_proc, int validation, const char *const *extensions,
                       uint32_t extension_count)
{
    const char *swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    struct vkr *r = calloc(1, sizeof *r);
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    uint32_t count = 1, families = 0, i;
    VkQueueFamilyProperties family_props[16];
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    VkPhysicalDeviceVulkan13Features features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandBufferAllocateInfo cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    VkDescriptorSetLayoutBinding binding = {0};
    VkDescriptorSetLayoutCreateInfo set_layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VKR_SLOTS};
    VkDescriptorPoolCreateInfo descriptor_pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    VkDescriptorSetAllocateInfo set_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    VkPushConstantRange push = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 12};
    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    static const uint8_t white[4] = {255, 255, 255, 255};

    if (!r)
        return 0;
    r->vsync = -1;
    r->get_proc = get_proc;
    r->vkCreateInstance = (PFN_vkCreateInstance)get_proc(VK_NULL_HANDLE, "vkCreateInstance");
    app.pApplicationName = "reroll";
    app.apiVersion = VK_API_VERSION_1_3;
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames = extensions;
    if (validation) {
        instance_info.enabledLayerCount = 1;
        instance_info.ppEnabledLayerNames = layers;
    }
    if (!r->vkCreateInstance || r->vkCreateInstance(&instance_info, 0, &r->instance) != VK_SUCCESS) {
        instance_info.enabledLayerCount = 0; /* no validation layer installed */
        if (!r->vkCreateInstance || r->vkCreateInstance(&instance_info, 0, &r->instance) != VK_SUCCESS)
            goto fail;
    }
#define LOAD_INSTANCE(name) r->name = (PFN_##name)get_proc(r->instance, #name);
    INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    if (r->vkEnumeratePhysicalDevices(r->instance, &count, &r->gpu) < 0 || !count)
        goto fail;
    r->vkGetPhysicalDeviceMemoryProperties(r->gpu, &r->memory_props);
    families = 16;
    r->vkGetPhysicalDeviceQueueFamilyProperties(r->gpu, &families, family_props);
    for (i = 0; i < families; i++)
        if (family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            break;
    if (i == families)
        goto fail;
    r->queue_family = i;
    queue_info.queueFamilyIndex = i;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    features13.dynamicRendering = VK_TRUE;
    device_info.pNext = &features13;
    {
        VkPhysicalDeviceFeatures supported;
        VkPhysicalDeviceProperties props;
        r->vkGetPhysicalDeviceFeatures(r->gpu, &supported);
        r->vkGetPhysicalDeviceProperties(r->gpu, &props);
        if (supported.samplerAnisotropy) {
            r->features.samplerAnisotropy = VK_TRUE;
            r->max_anisotropy = props.limits.maxSamplerAnisotropy;
        }
        device_info.pEnabledFeatures = &r->features;
    }
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    if (extension_count) {
        r->windowed = 1;
        device_info.enabledExtensionCount = 1;
        device_info.ppEnabledExtensionNames = &swapchain_extension;
    }
    if (r->vkCreateDevice(r->gpu, &device_info, 0, &r->device) != VK_SUCCESS)
        goto fail;
#define LOAD_DEVICE(name) r->name = (PFN_##name)r->vkGetDeviceProcAddr(r->device, #name);
    DEVICE_FUNCTIONS(LOAD_DEVICE)
    if (r->windowed) {
        SWAPCHAIN_FUNCTIONS(LOAD_DEVICE)
    }
#undef LOAD_DEVICE
    for (i = 0; i < 8; i++)
        r->vkCreateSemaphore(r->device, &semaphore_info, 0, &r->rendered[i]);
    r->vkGetDeviceQueue(r->device, r->queue_family, 0, &r->queue);
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = r->queue_family;
    r->vkCreateCommandPool(r->device, &pool_info, 0, &r->pool);
    cmd_info.commandPool = r->pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (i = 0; i < FRAMES; i++) {
        r->vkCreateSemaphore(r->device, &semaphore_info, 0, &r->frames[i].acquired);
        r->vkCreateFence(r->device, &fence_info, 0, &r->frames[i].done);
        r->vkAllocateCommandBuffers(r->device, &cmd_info, &r->frames[i].cmd);
    }
    r->f = &r->frames[0];

    (void)sampler_info;
    make_sampler(r);

    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = VKR_SLOTS;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    set_layout_info.bindingCount = 1;
    set_layout_info.pBindings = &binding;
    r->vkCreateDescriptorSetLayout(r->device, &set_layout_info, 0, &r->set_layout);
    descriptor_pool_info.maxSets = 1;
    descriptor_pool_info.poolSizeCount = 1;
    descriptor_pool_info.pPoolSizes = &pool_size;
    r->vkCreateDescriptorPool(r->device, &descriptor_pool_info, 0, &r->descriptor_pool);
    set_info.descriptorPool = r->descriptor_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &r->set_layout;
    r->vkAllocateDescriptorSets(r->device, &set_info, &r->set);
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &r->set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    r->vkCreatePipelineLayout(r->device, &layout_info, 0, &r->layout);
    r->vert = shader(r, vert_spv, sizeof vert_spv);
    r->frag = shader(r, frag_spv, sizeof frag_spv);

    if (!upload(r, &r->white, 1, 1, white))
        goto fail;
    for (i = 0; i < VKR_SLOTS; i++)
        write_descriptor(r, (int)i);
    return r;
fail:
    vkr_destroy(r);
    return 0;
}

void vkr_destroy(struct vkr *r)
{
    int i;
    if (!r)
        return;
    if (r->device) {
        r->vkDeviceWaitIdle(r->device);
        if (r->swapchain)
            r->vkDestroySwapchainKHR(r->device, r->swapchain, 0);
        for (i = 0; i < 8; i++)
            if (r->rendered[i])
                r->vkDestroySemaphore(r->device, r->rendered[i], 0);
        for (i = 0; i < FRAMES; i++) {
            struct frame *f = &r->frames[i];
            if (f->acquired)
                r->vkDestroySemaphore(r->device, f->acquired, 0);
            if (f->done)
                r->vkDestroyFence(r->device, f->done, 0);
            if (f->vertex_buffer) {
                r->vkDestroyBuffer(r->device, f->vertex_buffer, 0);
                r->vkFreeMemory(r->device, f->vertex_memory, 0);
            }
            if (f->staging) {
                r->vkDestroyBuffer(r->device, f->staging, 0);
                r->vkFreeMemory(r->device, f->staging_memory, 0);
            }
        }
        for (i = 0; i < VKR_SLOTS; i++)
            free_texture(r, &r->textures[i]);
        free_texture(r, &r->white);
        free(r->verts);
        if (r->target_view)
            r->vkDestroyImageView(r->device, r->target_view, 0);
        if (r->target)
            r->vkDestroyImage(r->device, r->target, 0);
        if (r->target_memory)
            r->vkFreeMemory(r->device, r->target_memory, 0);
        if (r->depth_view)
            r->vkDestroyImageView(r->device, r->depth_view, 0);
        if (r->depth)
            r->vkDestroyImage(r->device, r->depth, 0);
        if (r->depth_memory)
            r->vkFreeMemory(r->device, r->depth_memory, 0);
        for (i = 0; i < r->pipeline_count; i++)
            r->vkDestroyPipeline(r->device, r->pipelines[i].pipeline, 0);
        r->vkDestroyShaderModule(r->device, r->vert, 0);
        r->vkDestroyShaderModule(r->device, r->frag, 0);
        r->vkDestroyPipelineLayout(r->device, r->layout, 0);
        r->vkDestroyDescriptorPool(r->device, r->descriptor_pool, 0);
        r->vkDestroyDescriptorSetLayout(r->device, r->set_layout, 0);
        r->vkDestroySampler(r->device, r->sampler, 0);
        r->vkDestroyCommandPool(r->device, r->pool, 0);
        r->vkDestroyDevice(r->device, 0);
    }
    if (r->surface)
        r->vkDestroySurfaceKHR(r->instance, r->surface, 0);
    if (r->instance)
        r->vkDestroyInstance(r->instance, 0);
    free(r->commands);
    free(r);
}

int vkr_begin(struct vkr *r, int w, int h, const float clear[4])
{
    if (r->target && (r->target_w != w || r->target_h != h)) {
        r->vkDeviceWaitIdle(r->device);
        r->vkDestroyImageView(r->device, r->target_view, 0);
        r->vkDestroyImage(r->device, r->target, 0);
        r->vkFreeMemory(r->device, r->target_memory, 0);
        r->vkDestroyImageView(r->device, r->depth_view, 0);
        r->vkDestroyImage(r->device, r->depth, 0);
        r->vkFreeMemory(r->device, r->depth_memory, 0);
        r->target = VK_NULL_HANDLE;
    }
    if (!r->target) {
        if (!make_image(r, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &r->target,
                        &r->target_memory, &r->target_view) ||
            !make_image(r, w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &r->depth,
                        &r->depth_memory, &r->depth_view))
            return 0;
        r->target_w = w;
        r->target_h = h;
    }
    r->width = w;
    r->height = h;
    memcpy(r->clear, clear, sizeof r->clear);
    r->vert_count = 0;
    r->command_count = 0;
    return vertex_room(r, 1);
}

/* The frame's vertices are built in ordinary memory (r->verts) and
 * copied into its vertex buffer at the submit (record_frame): writing them
 * straight into mapped memory was slower (the vertex code reads back what
 * it writes). */
static int vertex_room(struct vkr *r, uint32_t want)
{
    uint32_t max = r->vert_max ? r->vert_max : 0x10000;
    struct vertex *grown;
    if (want <= r->vert_max)
        return 1;
    while (max < want)
        max *= 2;
    grown = realloc(r->verts, (size_t)max * sizeof *r->verts);
    if (!grown)
        return 0;
    r->verts = grown;
    r->vert_max = max;
    return 1;
}

static struct vertex *add_vertices(struct vkr *r, uint32_t n)
{
    if (r->vert_count + n > r->vert_max && !vertex_room(r, r->vert_count + n))
        return &r->verts[r->vert_max - n]; /* out of memory: overwrites the last ones */
    r->vert_count += n;
    return &r->verts[r->vert_count - n];
}

/* Texel coordinate i (0..255) in a page of `size` texels, as the original's
 * tables (0x41cfd0). */
static float texel(int i, int size)
{
    float o = (1.0f / (float)size) / 2.1f, s = 1.0f / ((float)size - o);
    return (float)i * s + o;
}

/* Clip-space position of a draw-list vertex for the output. 3D vertices go
 * straight from camera space (no division, so vertices behind the camera
 * clip correctly against the near plane, as the original's camera-space
 * clipper does): screen x = cx + x * h / z becomes, in output pixels,
 * (cx + x * h / z) * scale + offset, and clip x = that * 2 / width - 1, times
 * w = z. 2D vertices are screen pixels with w = 1. */
static void put_position(struct vkr *r, struct vertex *out, const struct rd_view *view,
                         const struct vkr_mapping *map, const struct rd_vertex *v, int two_d)
{
    float kx = 2.0f / (float)r->width, ky = 2.0f / (float)r->height;
    float near_z = view->near_z > 0.0f ? view->near_z * 0.999f : 0.001f;

    if (two_d) {
        out->pos[0] = (v->x * map->scale + map->offset_x) * kx - 1.0f;
        out->pos[1] = (v->y * map->scale + map->offset_y) * ky - 1.0f;
        out->pos[2] = 0.0f;
        out->pos[3] = 1.0f;
        return;
    }
    out->pos[0] = kx * map->scale * view->h * v->x + (kx * (view->cx * map->scale + map->offset_x) - 1.0f) * v->z;
    out->pos[1] = ky * map->scale * view->h * v->y + (ky * (view->cy * map->scale + map->offset_y) - 1.0f) * v->z;
    out->pos[2] = v->z - near_z; /* clipped in front of the near plane; no far clip (the game culls) */
    out->pos[3] = v->z;
}

/* Screen position of a camera-space vertex with the walk's projection. */
static void project(const struct rd_view *view, const struct rd_vertex *v, float *sx, float *sy)
{
    if (v->z > 0.0f) {
        *sx = v->x * (view->h / v->z) + view->cx;
        *sy = v->y * (view->h / v->z) + view->cy;
    } else {
        *sx = view->cx;
        *sy = view->cy;
    }
}

static void emit(struct vkr *r, struct vertex *out, const struct rd_list *list, const struct rd_vertex *v,
                 const struct vkr_mapping *map, int two_d, int tex_w, int tex_h)
{
    put_position(r, out, &list->view, map, v, two_d);
    if (tex_w < 0) { /* RD_IMAGE: the whole texture */
        out->uv[0] = (float)v->u / 255.0f;
        out->uv[1] = (float)v->v / 255.0f;
    } else {
        out->uv[0] = texel(v->u, tex_w);
        out->uv[1] = texel(v->v, tex_h);
    }
    out->colour[0] = v->r;
    out->colour[1] = v->g;
    out->colour[2] = v->b;
    out->colour[3] = v->a;
    out->depth = two_d ? 0.0f : v->z * (1.0f / 65536.0f);
}

void vkr_draw(struct vkr *r, const struct rd_list *list, const struct vkr_state *state, const struct vkr_mapping *map)
{
    const struct rd_view *view = &list->view;
    VkRect2D scissor;
    float left = view->clip[0], right = view->clip[1], top = view->clip[2], bottom = view->clip[3];
    uint32_t i;

    if (right <= left || bottom <= top) {
        left = 0;
        top = 0;
        right = (float)view->screen_w;
        bottom = (float)view->screen_h;
    }
    scissor.offset.x = (int32_t)(left * map->scale + map->offset_x);
    scissor.offset.y = (int32_t)(top * map->scale + map->offset_y);
    scissor.extent.width = (uint32_t)((right - left) * map->scale + 0.5f);
    scissor.extent.height = (uint32_t)((bottom - top) * map->scale + 0.5f);
    if (scissor.offset.x < 0)
        scissor.offset.x = 0;
    if (scissor.offset.y < 0)
        scissor.offset.y = 0;

    if (state->zbuffer && view->ot_start == 0x6cdd0c) { /* a view starts: 0x428770 clears its z-buffer */
        struct command *c;
        if (r->command_count == r->command_max) {
            r->command_max = r->command_max ? r->command_max * 2 : 1024;
            r->commands = realloc(r->commands, r->command_max * sizeof *r->commands);
        }
        c = &r->commands[r->command_count++];
        memset(c, 0, sizeof *c);
        c->clear_depth = 1;
        c->pipeline = -1;
        c->scissor = scissor;
        c->first = r->vert_count;
    }

    for (i = 0; i < list->prim_count; i++) {
        const struct rd_prim *p = &list->prims[i];
        const struct rd_vertex *v = &list->verts[p->first];
        int two_d = (p->flags & RD_2D) != 0, textured = (p->flags & RD_TEXTURED) != 0;
        int abr = p->abr | (int)state->abr_or, blend = p->tm >= 2, key, slot = 0, tex_w = 256, tex_h = 256;
        uint32_t src = state->blend_pair[(abr & 2) ? 2 : 0], dst = state->blend_pair[(abr & 2) ? 3 : 1];
        struct vertex *out;
        struct command *c;
        int pipeline;

        if (state->caps & 1)
            key = p->tm < 2;
        else
            key = p->tm == 1 || (p->tm == 0 && (abr & 1));
        if (p->flags & RD_IMAGE) {
            slot = VKR_IMAGE_SLOT;
            if (r->textures[slot].view) {
                tex_w = r->textures[slot].w;
                tex_h = r->textures[slot].h;
            }
        } else if (textured) {
            slot = (int)state->page_slot[p->page & 0x3f];
            if (slot < 0 || slot >= 64)
                slot = 0;
            if (r->textures[slot].view) {
                tex_w = r->textures[slot].w;
                tex_h = r->textures[slot].h;
            }
        }
        if (p->flags & RD_IMAGE)
            tex_w = tex_h = -1;
        pipeline = pipeline_for(r, blend, src, dst, state->zbuffer != 0,
                                state->zbuffer && (!state->zwrite_by_tm || p->tm == 0));

        if (p->kind == RD_LINE) {
            /* a quad line_width wide in game pixels, widened along the minor axis */
            float x0, y0, x1, y1, dx, dy, w = view->line_width > 0.6f ? view->line_width : 0.6f;
            struct rd_vertex corners[4];
            int k;
            project(view, &v[0], &x0, &y0);
            project(view, &v[1], &x1, &y1);
            dx = x1 - x0;
            dy = y1 - y0;
            out = add_vertices(r, 6);
            for (k = 0; k < 4; k++) {
                const struct rd_vertex *end = &v[k >> 1];
                float side = (k & 1) ? 0.5f : -0.5f;
                float px = (k >> 1) ? x1 : x0, py = (k >> 1) ? y1 : y0;
                corners[k] = *end;
                if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy))
                    py += side * w * 1.4f;
                else
                    px += side * w * 1.4f;
                corners[k].x = (px - view->cx) * end->z / view->h;
                corners[k].y = (py - view->cy) * end->z / view->h;
            }
            emit(r, &out[0], list, &corners[0], map, 0, tex_w, tex_h);
            emit(r, &out[1], list, &corners[1], map, 0, tex_w, tex_h);
            emit(r, &out[2], list, &corners[2], map, 0, tex_w, tex_h);
            emit(r, &out[3], list, &corners[1], map, 0, tex_w, tex_h);
            emit(r, &out[4], list, &corners[3], map, 0, tex_w, tex_h);
            emit(r, &out[5], list, &corners[2], map, 0, tex_w, tex_h);
        } else if (p->kind == RD_QUAD) {
            static const int order[6] = {0, 1, 2, 1, 3, 2};
            int k;
            out = add_vertices(r, 6);
            for (k = 0; k < 6; k++)
                emit(r, &out[k], list, &v[order[k]], map, two_d, tex_w, tex_h);
        } else {
            int k;
            out = add_vertices(r, 3);
            for (k = 0; k < 3; k++)
                emit(r, &out[k], list, &v[k], map, two_d, tex_w, tex_h);
        }

        /* extend the previous command when the state is the same */
        c = r->command_count ? &r->commands[r->command_count - 1] : 0;
        if (c && !c->clear_depth && c->pipeline == pipeline && c->slot == slot && c->textured == textured && c->colour_key == key &&
            !memcmp(&c->scissor, &scissor, sizeof scissor) && c->first + c->count == (uint32_t)(out - r->verts)) {
            c->count = r->vert_count - c->first;
            continue;
        }
        if (r->command_count == r->command_max) {
            r->command_max = r->command_max ? r->command_max * 2 : 1024;
            r->commands = realloc(r->commands, r->command_max * sizeof *r->commands);
        }
        c = &r->commands[r->command_count++];
        c->pipeline = pipeline;
        c->clear_depth = 0;
        c->slot = slot;
        c->textured = textured;
        c->colour_key = key;
        c->scissor = scissor;
        c->first = (uint32_t)(out - r->verts);
        c->count = r->vert_count - c->first;
    }
}

void vkr_drop_surface(struct vkr *r)
{
    r->vkDeviceWaitIdle(r->device);
    if (r->swapchain)
        r->vkDestroySwapchainKHR(r->device, r->swapchain, 0);
    if (r->surface)
        r->vkDestroySurfaceKHR(r->instance, r->surface, 0);
    r->swapchain = VK_NULL_HANDLE;
    r->surface = VK_NULL_HANDLE;
}

void vkr_set_vsync(struct vkr *r, int vsync)
{
    r->vsync = vsync;
}

int vkr_present_mode(const struct vkr *r)
{
    return (int)r->present_mode;
}

VkInstance vkr_instance(struct vkr *r)
{
    return r->instance;
}

int vkr_set_surface(struct vkr *r, VkSurfaceKHR surface, int w, int h)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[32];
    VkPresentModeKHR modes[8];
    uint32_t format_count = 32, mode_count = 8, i;
    VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    VkSwapchainKHR old = r->swapchain;
    VkBool32 supported = VK_FALSE;

    r->surface = surface;
    r->vkDeviceWaitIdle(r->device);
    r->vkGetPhysicalDeviceSurfaceSupportKHR(r->gpu, r->queue_family, surface, &supported);
    if (!supported)
        return 0;
    r->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->gpu, surface, &caps);
    r->vkGetPhysicalDeviceSurfaceFormatsKHR(r->gpu, surface, &format_count, formats);
    r->vkGetPhysicalDeviceSurfacePresentModesKHR(r->gpu, surface, &mode_count, modes);
    info.surface = surface;
    info.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && info.minImageCount > caps.maxImageCount)
        info.minImageCount = caps.maxImageCount;
    info.imageFormat = formats[0].format;
    info.imageColorSpace = formats[0].colorSpace;
    for (i = 0; i < format_count; i++)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            info.imageFormat = formats[i].format;
            info.imageColorSpace = formats[i].colorSpace;
            break;
        }
    if (caps.currentExtent.width != 0xffffffff) {
        w = (int)caps.currentExtent.width;
        h = (int)caps.currentExtent.height;
    }
    info.imageExtent.width = (uint32_t)w;
    info.imageExtent.height = (uint32_t)h;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    /* default (-1): mailbox, else FIFO; off (0): mailbox, else immediate;
     * on (1): FIFO, one frame a refresh */
    if (r->vsync != 1) {
        for (i = 0; i < mode_count; i++)
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR && r->vsync == 0)
                info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        for (i = 0; i < mode_count; i++)
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
    r->present_mode = info.presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    if (r->vkCreateSwapchainKHR(r->device, &info, 0, &r->swapchain) != VK_SUCCESS)
        return 0;
    if (old)
        r->vkDestroySwapchainKHR(r->device, old, 0);
    r->swap_count = 8;
    r->vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swap_count, r->swap_images);
    r->swap_w = w;
    r->swap_h = h;
    return 1;
}

/* Records the frame's drawing into r->f->cmd (begun), leaving the target in
 * TRANSFER_SRC layout; returns the vertex buffer to free after the submit. */
static int record_frame(struct vkr *r)
{
    VkRenderingAttachmentInfo colour = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingAttachmentInfo depth = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingInfo rendering = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    VkViewport viewport = {0, 0, (float)r->width, (float)r->height, 0, 1};
    VkDeviceSize zero = 0;
    uint32_t i;

    if (!ensure_buffer(r, (VkDeviceSize)(r->vert_count ? r->vert_count : 1) * sizeof(struct vertex),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->f->vertex_buffer, &r->f->vertex_memory,
                       &r->f->vertex_mapped, &r->f->vertex_size))
        return 0;
    if (r->vert_count) /* into cached memory: a plain copy */
        memcpy(r->f->vertex_mapped, r->verts, (size_t)r->vert_count * sizeof(struct vertex));
    record_uploads(r);
    /* the target and depth are shared by the frames in flight: wait for the
     * previous frame's blit (or copy) out of the target and its depth tests */
    barrier(r, r->target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    barrier_aspect(r, r->depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    depth.imageView = r->depth_view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil.depth = 1.0f;
    colour.imageView = r->target_view;
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    memcpy(colour.clearValue.color.float32, r->clear, sizeof r->clear);
    rendering.renderArea.extent.width = (uint32_t)r->width;
    rendering.renderArea.extent.height = (uint32_t)r->height;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colour;
    rendering.pDepthAttachment = &depth;
    r->vkCmdBeginRendering(r->f->cmd, &rendering);
    r->vkCmdSetViewport(r->f->cmd, 0, 1, &viewport);
    r->vkCmdBindDescriptorSets(r->f->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1, &r->set, 0, 0);
    r->vkCmdBindVertexBuffers(r->f->cmd, 0, 1, &r->f->vertex_buffer, &zero);
    for (i = 0; i < r->command_count; i++) {
        const struct command *c = &r->commands[i];
        int32_t push[3] = {c->slot, c->textured, c->colour_key};
        VkRect2D scissor = c->scissor;
        if (scissor.offset.x + (int32_t)scissor.extent.width > r->width)
            scissor.extent.width = (uint32_t)(r->width - scissor.offset.x);
        if (scissor.offset.y + (int32_t)scissor.extent.height > r->height)
            scissor.extent.height = (uint32_t)(r->height - scissor.offset.y);
        if (c->clear_depth) {
            VkClearAttachment clear = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, {{{0}}}};
            VkClearRect rect = {scissor, 0, 1};
            clear.clearValue.depthStencil.depth = 1.0f;
            r->vkCmdClearAttachments(r->f->cmd, 1, &clear, 1, &rect);
            continue;
        }
        r->vkCmdBindPipeline(r->f->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[c->pipeline].pipeline);
        r->vkCmdPushConstants(r->f->cmd, r->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, push);
        r->vkCmdSetScissor(r->f->cmd, 0, 1, &scissor);
        r->vkCmdDraw(r->f->cmd, c->count, 1, c->first, 0);
    }
    r->vkCmdEndRendering(r->f->cmd);
    barrier(r, r->target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    return 1;
}

int vkr_end(struct vkr *r, uint8_t *rgba)
{
    VkBuffer readback;
    VkDeviceMemory readback_memory;
    VkDeviceSize image_size = (VkDeviceSize)r->width * (VkDeviceSize)r->height * 4;
    VkBufferImageCopy copy = {0};
    void *mapped;

    if (!rgba) { /* draw only */
        begin_commands(r);
        if (!record_frame(r))
            return 0;
        submit_and_wait(r);
        next_frame(r);
        return 1;
    }
    if (!make_buffer(r, image_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback, &readback_memory))
        return 0;
    begin_commands(r);
    if (!record_frame(r))
        return 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = (uint32_t)r->width;
    copy.imageExtent.height = (uint32_t)r->height;
    copy.imageExtent.depth = 1;
    r->vkCmdCopyImageToBuffer(r->f->cmd, r->target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
    submit_and_wait(r);
    r->vkMapMemory(r->device, readback_memory, 0, image_size, 0, &mapped);
    memcpy(rgba, mapped, (size_t)image_size);
    r->vkUnmapMemory(r->device, readback_memory);
    r->vkDestroyBuffer(r->device, readback, 0);
    r->vkFreeMemory(r->device, readback_memory, 0);
    next_frame(r);
    return 1;
}

int vkr_present(struct vkr *r)
{
    uint32_t index;
    VkImageBlit blit = {0};
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    VkResult result;

    uint64_t t;

    if (!r->swapchain)
        return 0;
    t = wait_clock();
    result = r->vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->f->acquired, VK_NULL_HANDLE, &index);
    vkr_wait_us[0] += wait_clock() - t;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return 0;
    begin_commands(r);
    if (!record_frame(r))
        return 0;
    barrier(r, r->swap_images[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1].x = r->width;
    blit.srcOffsets[1].y = r->height;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[1].x = r->swap_w;
    blit.dstOffsets[1].y = r->swap_h;
    blit.dstOffsets[1].z = 1;
    r->vkCmdBlitImage(r->f->cmd, r->target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r->swap_images[index],
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    barrier(r, r->swap_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    r->vkEndCommandBuffer(r->f->cmd);
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &r->f->acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->f->cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &r->rendered[index];
    r->vkResetFences(r->device, 1, &r->f->done);
    r->vkQueueSubmit(r->queue, 1, &submit, r->f->done);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &r->rendered[index];
    present.swapchainCount = 1;
    present.pSwapchains = &r->swapchain;
    present.pImageIndices = &index;
    t = wait_clock();
    result = r->vkQueuePresentKHR(r->queue, &present);
    vkr_wait_us[2] += wait_clock() - t;
    next_frame(r);
    return result == VK_SUCCESS;
}
