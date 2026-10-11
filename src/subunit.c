/* The camera at its exact 16.16 position (docs/renderer/plan.md, step 6).
 *
 * The original keeps only whole world units of the camera position:
 * 0x423ce0 stores -(pos >> 16) as the view translation, so with the
 * 120 fps interpolation's smooth positions the picture still moves in
 * whole-unit steps, which shows as judder at high resolution. The
 * dropped part is the same for everything drawn in a view's world: with
 * T = -pos / 65536 = T_int + f (0 <= f < 1 per axis), every world-to-camera
 * translation is R'(X + T_int) + R' f. So inside a view's world drawing
 * (draw_view 0x479be0, after its camera matrix 0x6ce5d0 is built by
 * 0x423d30, and not while its HUD is built by 0x438310) the two vertex
 * transforms (0x426a20, 0x426be0: camera = RT * V0 + TR, with the float
 * translation 0x6ccd0c, which many places write) run with TR + R' f, then
 * TR is put back exactly. Everything else (physics, the front end, the
 * HUD) sees no change.
 *
 * On with REROLL_VULKAN=1; REROLL_SUBUNIT=0 / 1 overrides. */
#include "subunit.h"

#include "hook.h"
#include "log.h"
#include "runtime.h"

#define CAMERA_TRANSLATION ((int32_t *)0x6ce5c0)
#define CAMERA_MATRIX ((const int16_t *)0x6ce5d0) /* 3 x 3, 4.12, rows of 3 */

/* read by the assembly below */
int subunit_active;
float subunit_offset[3];
uint32_t subunit_saved[3];
void *subunit_transform_original, *subunit_transform_only_original;

static float fraction[3];
static int in_view;

/* 0x423ce0(pos): the camera translation, as the original, keeping the fraction. */
static void GAMEAPI camera_translation(const int32_t *pos)
{
    int i;
    for (i = 0; i < 3; i++) {
        int32_t t = -pos[i];
        CAMERA_TRANSLATION[i] = t >> 16;
        fraction[i] = (float)(t & 0xffff) * (1.0f / 65536.0f);
    }
}

/* 0x423d30: the camera matrix (scale, rotation, translation), then loaded. */
static void(GAMEAPI *camera_matrix_original)(void);

static void GAMEAPI camera_matrix(void)
{
    int r;
    camera_matrix_original();
    if (!in_view)
        return;
    for (r = 0; r < 3; r++)
        subunit_offset[r] = ((float)CAMERA_MATRIX[r * 3] * fraction[0] + (float)CAMERA_MATRIX[r * 3 + 1] * fraction[1] +
                             (float)CAMERA_MATRIX[r * 3 + 2] * fraction[2]) *
                            (1.0f / 4096.0f);
    subunit_active = 1;
}

/* draw_view 0x479be0(slot) */
static void(GAMEAPI *draw_view_original)(uint32_t slot);

static void GAMEAPI draw_view(uint32_t slot)
{
    in_view = 1;
    subunit_active = 0;
    draw_view_original(slot);
    in_view = 0;
    subunit_active = 0;
}

/* 0x438310(slot): the view's HUD */
static void(GAMEAPI *hud_original)(uint32_t slot);

static void GAMEAPI hud(uint32_t slot)
{
    int active = subunit_active;
    subunit_active = 0;
    hud_original(slot);
    subunit_active = active;
}

/* The transforms (cdecl, one argument: the vertex record; the result stays
 * on the FPU stack and in eax / ecx / edx as the original leaves them). */
void subunit_transform(void);
void subunit_transform_only(void);
#define TRANSFORM_WRAPPER(name, original)                                                                            \
    __asm__(".text\n"                                                                                                \
            ".globl " ASM_SYM(name) "\n"                                                                              \
            ASM_SYM(name) ":\n"                                                                                           \
            "    cmpl $0, " ASM_SYM("subunit_active") "\n"                                                                         \
            "    jne 1f\n"                                                                                           \
            "    jmp *" ASM_SYM(original) "\n"                                                                               \
            "1:  movl 0x6ccd0c, %eax\n"                                                                              \
            "    movl %eax, " ASM_SYM("subunit_saved") "\n"                                                                        \
            "    movl 0x6ccd10, %eax\n"                                                                              \
            "    movl %eax, " ASM_SYM("subunit_saved") "+4\n"                                                                      \
            "    movl 0x6ccd14, %eax\n"                                                                              \
            "    movl %eax, " ASM_SYM("subunit_saved") "+8\n"                                                                      \
            "    movss 0x6ccd0c, %xmm0\n"                                                                            \
            "    addss " ASM_SYM("subunit_offset") ", %xmm0\n"                                                                     \
            "    movss %xmm0, 0x6ccd0c\n"                                                                            \
            "    movss 0x6ccd10, %xmm0\n"                                                                            \
            "    addss " ASM_SYM("subunit_offset") "+4, %xmm0\n"                                                                   \
            "    movss %xmm0, 0x6ccd10\n"                                                                            \
            "    movss 0x6ccd14, %xmm0\n"                                                                            \
            "    addss " ASM_SYM("subunit_offset") "+8, %xmm0\n"                                                                   \
            "    movss %xmm0, 0x6ccd14\n"                                                                            \
            "    pushl 4(%esp)\n"                                                                                    \
            "    call *" ASM_SYM(original) "\n"                                                                              \
            "    addl $4, %esp\n"                                                                                    \
            "    pushl %eax\n"                                                                                       \
            "    movl " ASM_SYM("subunit_saved") ", %eax\n"                                                                        \
            "    movl %eax, 0x6ccd0c\n"                                                                              \
            "    movl " ASM_SYM("subunit_saved") "+4, %eax\n"                                                                      \
            "    movl %eax, 0x6ccd10\n"                                                                              \
            "    movl " ASM_SYM("subunit_saved") "+8, %eax\n"                                                                      \
            "    movl %eax, 0x6ccd14\n"                                                                              \
            "    popl %eax\n"                                                                                        \
            "    ret\n")
TRANSFORM_WRAPPER("subunit_transform", "subunit_transform_original");
TRANSFORM_WRAPPER("subunit_transform_only", "subunit_transform_only_original");

static const struct hook translation_hook = {"subunit_translation", 0x423ce0, camera_translation,
                                             {0x8b, 0x44, 0x24, 0x04, 0x8b, 0x08, 0xf7, 0xd9}};
static const struct hook matrix_hook = {"subunit_matrix", 0x423d30, camera_matrix,
                                        {0xa1, 0x94, 0xce, 0x6c, 0x00, 0x68, 0xc0, 0xe5}};
static const struct hook draw_view_hook = {"subunit_draw_view", 0x479be0, draw_view,
                                           {0x81, 0xec, 0xbc, 0x00, 0x00, 0x00, 0x8b, 0x0d}};
static const struct hook hud_hook = {"subunit_hud", 0x438310, hud, {0x8a, 0x0d, 0x40, 0xce, 0x6c, 0x00, 0x53, 0x55}};
static const struct hook transform_hook = {"subunit_transform", 0x426a20, subunit_transform,
                                           {0x55, 0x8b, 0xec, 0x51, 0x8b, 0x4d, 0x08, 0xdf}};
static const struct hook transform_only_hook = {"subunit_transform_only", 0x426be0, subunit_transform_only,
                                                {0x55, 0x8b, 0xec, 0x51, 0x53, 0x8b, 0x5d, 0x08}};

void subunit_install(int vulkan)
{
    char value[8];
    int on = vulkan;

    if (GetEnvironmentVariableA("REROLL_SUBUNIT", value, sizeof value))
        on = value[0] == '1';
    if (!on)
        return;
    /* the transforms first: nothing else is useful without them */
    subunit_transform_original = hook_install_trampoline(&transform_hook, 7);
    subunit_transform_only_original = hook_install_trampoline(&transform_only_hook, 8);
    if (!subunit_transform_original || !subunit_transform_only_original) {
        log_printf("subunit: transform hooks failed, off");
        return;
    }
    camera_matrix_original = (void(GAMEAPI *)(void))hook_install_trampoline(&matrix_hook, 5);
    draw_view_original = (void(GAMEAPI *)(uint32_t))hook_install_trampoline(&draw_view_hook, 6);
    hud_original = (void(GAMEAPI *)(uint32_t))hook_install_trampoline(&hud_hook, 6);
    if (!camera_matrix_original || !draw_view_original || !hud_original || !hook_install(&translation_hook)) {
        log_printf("subunit: camera hooks failed");
        return;
    }
    log_printf("subunit: the camera keeps its 16.16 position");
}
