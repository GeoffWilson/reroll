/* Group WB: the bodies of the five original functions that src/subunit.c
 * wraps (the camera at its exact 16.16 position). Until now each wrapper
 * did its own work and then ran the original through a trampoline; the
 * wrappers stay installed at the original addresses and call these instead.
 *
 *  - 0x423d30 camera_matrix_body: the camera matrix (0x423d70) then loaded
 *    (0x423df0), after the time-warp row scale 0x6cd480;
 *  - 0x426a20 / 0x426be0 subunit_transform_body / subunit_transform_only_body:
 *    the float GTE transform of the vector 0x6ccc64 into a vertex record,
 *    called through the pointers 0x6ccd78 / 0x6ccd7c. They hand st(0) to the
 *    projection *0x6ccd80 and leave eax / ecx / edx as below, so they are the
 *    original's instruction sequences in assembly (checked instruction by
 *    instruction), matching under any x87 control word;
 *  - 0x438310 hud_body: a view's HUD (lock-on rings, weapon icons, the
 *    position / lap / messages), x87 steps as the original's instructions;
 *  - 0x479be0 draw_view_body: one view of the race: viewport (with the
 *    time-warp squeeze), camera, then every drawing pass in order.
 *
 * Everything they call goes by address, so the hooks route it (0x423d30 and
 * 0x438310 back into subunit.c's wrappers). */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define g_race G32(0x4b1960)
#define g_render_flags G8(0x6cce40)

/* This group's functions (also in build/groups/wb_protos.txt for physics.h). */
void GAMEAPI camera_matrix_body(void); /* 0x423d30 */
#if defined(__i386__)
void subunit_transform_body(void); /* 0x426a20: asm, (record) */
void subunit_transform_only_body(void); /* 0x426be0: asm, (record) */
#else
/* x86-64: the C bodies of those two asm entries */
float subunit_transform_c(uint8_t *rec); /* 0x426a20: returns what the original leaves in st(0) */
float subunit_transform_only_c(uint8_t *rec); /* 0x426be0: the same (0 where it leaves none) */
#endif
void GAMEAPI hud_body(uint8_t *view); /* 0x438310 */
void GAMEAPI draw_view_body(uint8_t *view); /* 0x479be0 */

#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ===== 0x423d30: the camera: the time-warp row scale word 0x6cd480
 * (0xe000 while 0x6cce94 is set, else 0x2000), the camera matrix 0x6ce5d0
 * from the view matrix 0x6ce5a0 and translation 0x6ce5c0 (0x423d70), then
 * loaded into the GTE (0x423df0). */
void GAMEAPI camera_matrix_body(void)
{
    G16(0x6cd480) = G32(0x6cce94) ? 0xe000 : 0x2000;
    GCALL(0x423d70, view_camera_build)(PTR(0x6ce5d0), (const uint32_t *)GMEM(0x6ce5a0), PTR(0x6ce5c0));
    GCALL(0x423df0, view_camera_load)(PTR(0x6ce5d0));
}

/* ===== 0x426a20 / 0x426be0: the original's instructions.
 *
 * 0x426a20 (record): z = R2 . v + T2, view x / y the same with rows 0 / 1
 * (the GTE vector 0x6ccc64 as words, the float matrix 0x6ccd1c / 2c / 3c,
 * translation 0x6ccd0c); the record gets code (+0), z (+4), view x / y
 * (+0x10 / +0x14); code 0x10, | 0x20 when z (as an int) >= the far plane
 * 0x6ccd08, and from the near plane 0x6ccd04 on, 0x10 cleared and st(0) =
 * H / z (H 0x6ccd00), else st(0) = z: one value left on the x87 stack for
 * the projection *0x6ccd80. On return eax = z's bits, ecx = record, edx =
 * code; *0x6ccd74 = record.
 *
 * 0x426be0 (record): the same, but z first: when z is negative (sign bit)
 * the record gets only code 0x10 and z, and the x87 stack is left empty
 * (eax as on entry); otherwise as 0x426a20 with the codes built in memory
 * (eax = z's bits, ecx / edx as on entry). *0x6ccd74 = record either way. */
#if defined(__i386__)
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl subunit_transform_body\n"
        ".type subunit_transform_body,@function\n"
        "subunit_transform_body:\n"
        "    push ebp\n" /* 426a20 */
        "    mov ebp, esp\n" /* 426a21 */
        "    push ecx\n" /* 426a23 */
        "    mov ecx, dword ptr [ebp + 0x8]\n" /* 426a24 */
        "    fild word ptr [0x6ccc64]\n" /* 426a27 */
        "    fild word ptr [0x6ccc66]\n" /* 426a2d */
        "    fild word ptr [0x6ccc68]\n" /* 426a33 */
        "    fld dword ptr [0x6ccd14]\n" /* 426a39 */
        "    fld dword ptr [0x6ccd3c]\n" /* 426a3f */
        "    fmul st, st(4)\n" /* 426a45 */
        "    fld dword ptr [0x6ccd40]\n" /* 426a47 */
        "    fmul st, st(4)\n" /* 426a4d */
        "    fxch st(1)\n" /* 426a4f */
        "    faddp st(2), st\n" /* 426a51 */
        "    fld dword ptr [0x6ccd44]\n" /* 426a53 */
        "    fmul st, st(3)\n" /* 426a59 */
        "    fxch st(1)\n" /* 426a5b */
        "    faddp st(2), st\n" /* 426a5d */
        "    mov edx, 0x10\n" /* 426a5f */
        "    faddp st(1), st\n" /* 426a64 */
        "    fld dword ptr [0x6ccd0c]\n" /* 426a66 */
        "    fld dword ptr [0x6ccd1c]\n" /* 426a6c */
        "    fmul st, st(5)\n" /* 426a72 */
        "    fld dword ptr [0x6ccd20]\n" /* 426a74 */
        "    fmul st, st(5)\n" /* 426a7a */
        "    fxch st(1)\n" /* 426a7c */
        "    faddp st(2), st\n" /* 426a7e */
        "    fld dword ptr [0x6ccd24]\n" /* 426a80 */
        "    fmul st, st(4)\n" /* 426a86 */
        "    fxch st(1)\n" /* 426a88 */
        "    faddp st(2), st\n" /* 426a8a */
        "    faddp st(1), st\n" /* 426a8c */
        "    fld dword ptr [0x6ccd10]\n" /* 426a8e */
        "    fld dword ptr [0x6ccd2c]\n" /* 426a94 */
        "    fmul st, st(6)\n" /* 426a9a */
        "    fld dword ptr [0x6ccd30]\n" /* 426a9c */
        "    fmul st, st(6)\n" /* 426aa2 */
        "    fxch st(1)\n" /* 426aa4 */
        "    faddp st(2), st\n" /* 426aa6 */
        "    fld dword ptr [0x6ccd34]\n" /* 426aa8 */
        "    fmul st, st(5)\n" /* 426aae */
        "    fxch st(1)\n" /* 426ab0 */
        "    faddp st(2), st\n" /* 426ab2 */
        "    faddp st(1), st\n" /* 426ab4 */
        "    fxch st(1)\n" /* 426ab6 */
        "    fstp dword ptr [ecx + 0x10]\n" /* 426ab8 */
        "    fstp dword ptr [ecx + 0x14]\n" /* 426abb */
        "    fst dword ptr [ecx + 0x4]\n" /* 426abe */
        "    fxch st(3)\n" /* 426ac1 */
        "    mov eax, dword ptr [ecx + 0x4]\n" /* 426ac3 */
        "    fcompp\n" /* 426ac6 */
        "    fstp dword ptr [ebp - 0x4]\n" /* 426ac8 */
        "    cmp eax, dword ptr [0x6ccd08]\n" /* 426acb */
        "    jl .Lsubunit_transform_body_426ad6\n" /* 426ad1 */
        "    or edx, 0x20\n" /* 426ad3 */
        ".Lsubunit_transform_body_426ad6:\n"
        "    cmp eax, dword ptr [0x6ccd04]\n" /* 426ad6 */
        "    jl .Lsubunit_transform_body_426ae7\n" /* 426adc */
        "    fdivr dword ptr [0x6ccd00]\n" /* 426ade */
        "    and edx, -0x11\n" /* 426ae4 */
        ".Lsubunit_transform_body_426ae7:\n"
        "    mov dword ptr [ecx], edx\n" /* 426ae7 */
        "    mov dword ptr [0x6ccd74], ecx\n" /* 426ae9 */
        "    mov esp, ebp\n" /* 426aef */
        "    pop ebp\n" /* 426af1 */
        "    ret\n" /* 426af2 */
        ".size subunit_transform_body, .-subunit_transform_body\n"
        "\n"
        ".globl subunit_transform_only_body\n"
        ".type subunit_transform_only_body,@function\n"
        "subunit_transform_only_body:\n"
        "    push ebp\n" /* 426be0 */
        "    mov ebp, esp\n" /* 426be1 */
        "    push ecx\n" /* 426be3 */
        "    push ebx\n" /* 426be4 */
        "    mov ebx, dword ptr [ebp + 0x8]\n" /* 426be5 */
        "    fild word ptr [0x6ccc64]\n" /* 426be8 */
        "    fild word ptr [0x6ccc66]\n" /* 426bee */
        "    fild word ptr [0x6ccc68]\n" /* 426bf4 */
        "    mov dword ptr [ebx], 0x10\n" /* 426bfa */
        "    fld dword ptr [0x6ccd14]\n" /* 426c00 */
        "    fld dword ptr [0x6ccd3c]\n" /* 426c06 */
        "    fmul st, st(4)\n" /* 426c0c */
        "    fld dword ptr [0x6ccd40]\n" /* 426c0e */
        "    fmul st, st(4)\n" /* 426c14 */
        "    fxch st(1)\n" /* 426c16 */
        "    faddp st(2), st\n" /* 426c18 */
        "    fld dword ptr [0x6ccd44]\n" /* 426c1a */
        "    fmul st, st(3)\n" /* 426c20 */
        "    fxch st(1)\n" /* 426c22 */
        "    faddp st(2), st\n" /* 426c24 */
        "    faddp st(1), st\n" /* 426c26 */
        "    fstp dword ptr [ebx + 0x4]\n" /* 426c28 */
        "    test dword ptr [ebx + 0x4], 0x80000000\n" /* 426c2b */
        "    je .Lsubunit_transform_only_body_426c3b\n" /* 426c32 */
        "    fcompp\n" /* 426c34 */
        "    fstp dword ptr [ebp - 0x4]\n" /* 426c36 */
        "    jmp .Lsubunit_transform_only_body_426cb8\n" /* 426c39 */
        ".Lsubunit_transform_only_body_426c3b:\n"
        "    fld dword ptr [0x6ccd0c]\n" /* 426c3b */
        "    fld dword ptr [0x6ccd1c]\n" /* 426c41 */
        "    fmul st, st(4)\n" /* 426c47 */
        "    fld dword ptr [0x6ccd20]\n" /* 426c49 */
        "    fmul st, st(4)\n" /* 426c4f */
        "    fxch st(1)\n" /* 426c51 */
        "    faddp st(2), st\n" /* 426c53 */
        "    fld dword ptr [0x6ccd24]\n" /* 426c55 */
        "    fmul st, st(3)\n" /* 426c5b */
        "    fxch st(1)\n" /* 426c5d */
        "    faddp st(2), st\n" /* 426c5f */
        "    faddp st(1), st\n" /* 426c61 */
        "    fstp dword ptr [ebx + 0x10]\n" /* 426c63 */
        "    fld dword ptr [0x6ccd10]\n" /* 426c66 */
        "    fld dword ptr [0x6ccd2c]\n" /* 426c6c */
        "    fmul st, st(4)\n" /* 426c72 */
        "    fld dword ptr [0x6ccd30]\n" /* 426c74 */
        "    fmul st, st(4)\n" /* 426c7a */
        "    fxch st(1)\n" /* 426c7c */
        "    faddp st(2), st\n" /* 426c7e */
        "    fld dword ptr [0x6ccd34]\n" /* 426c80 */
        "    fmul st, st(3)\n" /* 426c86 */
        "    fxch st(1)\n" /* 426c88 */
        "    faddp st(2), st\n" /* 426c8a */
        "    faddp st(1), st\n" /* 426c8c */
        "    fstp dword ptr [ebx + 0x14]\n" /* 426c8e */
        "    fcompp\n" /* 426c91 */
        "    fstp dword ptr [ebp - 0x4]\n" /* 426c93 */
        "    fld dword ptr [ebx + 0x4]\n" /* 426c96 */
        "    mov eax, dword ptr [ebx + 0x4]\n" /* 426c99 */
        "    cmp eax, dword ptr [0x6ccd08]\n" /* 426c9c */
        "    jl .Lsubunit_transform_only_body_426ca7\n" /* 426ca2 */
        "    or dword ptr [ebx], 0x20\n" /* 426ca4 */
        ".Lsubunit_transform_only_body_426ca7:\n"
        "    cmp eax, dword ptr [0x6ccd04]\n" /* 426ca7 */
        "    jl .Lsubunit_transform_only_body_426cb8\n" /* 426cad */
        "    fdivr dword ptr [0x6ccd00]\n" /* 426caf */
        "    and dword ptr [ebx], -0x11\n" /* 426cb5 */
        ".Lsubunit_transform_only_body_426cb8:\n"
        "    mov dword ptr [0x6ccd74], ebx\n" /* 426cb8 */
        "    pop ebx\n" /* 426cbe */
        "    mov esp, ebp\n" /* 426cbf */
        "    pop ebp\n" /* 426cc1 */
        "    ret\n" /* 426cc2 */
        ".size subunit_transform_only_body, .-subunit_transform_only_body\n"
        ".att_syntax prefix\n");
#else
/* x86-64: the same in C. The game runs the x87 at single precision, so
 * float arithmetic step by step gives the same values (range edge cases
 * aside); nothing passes values in registers or on the x87 stack here, so
 * the value the original leaves in st(0) for the projection is returned. */
#define GF(a) (*(float *)(uintptr_t)(a))

/* T + m0 * vx + m1 * vy + m2 * vz in the original's order. */
static float wb_row(uint32_t t, uint32_t m, float vx, float vy, float vz)
{
    float s = GF(m) * vx;
    float p;

    s = GF(t) + s;
    p = GF(m + 4) * vy;
    s = s + p;
    p = GF(m + 8) * vz;
    return s + p;
}

float subunit_transform_c(uint8_t *rec)
{
    float vx = (float)(int16_t)G16(0x6ccc64), vy = (float)(int16_t)G16(0x6ccc66), vz = (float)(int16_t)G16(0x6ccc68);
    float z = wb_row(0x6ccd14, 0x6ccd3c, vx, vy, vz);
    float x = wb_row(0x6ccd0c, 0x6ccd1c, vx, vy, vz);
    float y = wb_row(0x6ccd10, 0x6ccd2c, vx, vy, vz);
    uint32_t code = 0x10;
    float r = z;

    *(float *)(rec + 0x10) = x;
    *(float *)(rec + 0x14) = y;
    *(float *)(rec + 4) = z;
    if (I32(rec, 4) >= (int32_t)G32(0x6ccd08))
        code |= 0x20;
    if (I32(rec, 4) >= (int32_t)G32(0x6ccd04)) {
        r = GF(0x6ccd00) / r;
        code &= ~0x10u;
    }
    U32(rec, 0) = code;
    G32(0x6ccd74) = A(rec);
    return r;
}

float subunit_transform_only_c(uint8_t *rec)
{
    float vx = (float)(int16_t)G16(0x6ccc64), vy = (float)(int16_t)G16(0x6ccc66), vz = (float)(int16_t)G16(0x6ccc68);
    float r = 0;

    U32(rec, 0) = 0x10;
    *(float *)(rec + 4) = wb_row(0x6ccd14, 0x6ccd3c, vx, vy, vz);
    if (!(U32(rec, 4) & 0x80000000)) {
        *(float *)(rec + 0x10) = wb_row(0x6ccd0c, 0x6ccd1c, vx, vy, vz);
        *(float *)(rec + 0x14) = wb_row(0x6ccd10, 0x6ccd2c, vx, vy, vz);
        r = *(float *)(rec + 4);
        if (I32(rec, 4) >= (int32_t)G32(0x6ccd08))
            U32(rec, 0) |= 0x20;
        if (I32(rec, 4) >= (int32_t)G32(0x6ccd04)) {
            r = GF(0x6ccd00) / r;
            U32(rec, 0) &= ~0x10u;
        }
    }
    G32(0x6ccd74) = A(rec);
    return r;
}
#endif

/* fld a; fcomp b: 1 when a > b (C0 and C3 clear: not below, equal or unordered). */
static int x87_greater(const void *a, const void *b)
{
    uint16_t sw;
    __asm__ volatile("flds (%1)\n\tfcomps (%2)\n\tfnstsw %0" : "=a"(sw) : "r"(a), "r"(b) : "cc", X87);
    return !(sw & 0x4100);
}

/* fld *0x6ccd54 or 58 (the screen offset); fadd *p; fstp *p */
static void x87_add_to(uint32_t from, uint8_t *p)
{
    __asm__ volatile("flds (%0)\n\tfadds (%1)\n\tfstps (%1)" : : "r"(PTR(from)), "r"(p) : "memory", X87);
}

/* ===== 0x438310 (view): a view's HUD. The view's car (*(view) +0x64e, a
 * player index) and a mode: 1 under render flag 2 (the replay?), under flag
 * 0x10 2 while race +0x58c is set, else 1 up to race mode 4 and 3 above;
 * otherwise 0, or 3 when the view's byte +0x2c is above race +0x586. With
 * 0x4d1198 set and mode 0: 0x45cfe0 (unless race +0x552), the position /
 * lap panel 0x43a4a0 and the messages 0x4792a0, done. From race mode 7 on,
 * nothing. Mode 0: panel 0x43a4a0, the lock-on 0x483d20, the incoming-
 * missile count 0x458760 (a nonzero count flashes 0x439d50 every 16 frames
 * and starts the warning beep 0x3d, its countdown at 0x6c7fe2 + 4 * car),
 * the weapon icons 0x439d70 (size 0x6c7fdc + 2 up to twice race +0xa84),
 * then the marker 0x6c7fa4 and the 0x43 lock-on rings (records of 0xf0 at
 * 0x6c3ec0: when +0x4c > +0x14 and 0x6c7f5a[i] is set, the first 0x78
 * bytes copied to +0x78, the four corners moved by the screen offset
 * 0x6ccd54 / 58, linked into the OT *(0x6ce608 + 4)) and in race kind 4
 * the OT entry 0x469f80. Modes 2 / 3 and mode 0 end with the messages
 * 0x4792a0 unless 0x4be454 is set; mode 1 draws nothing. */
void GAMEAPI hud_body(uint8_t *view)
{
    uint8_t flags = g_render_flags;
    uint32_t car = U16(PTR(U32(view, 0)), 0x64e);
    uint8_t *race = PTR(g_race);
    uint32_t mode;

    if (flags & 2) {
        mode = 1;
    } else if (flags & 0x10) {
        if (U8(race, 0x58c))
            mode = 2;
        else
            mode = I32(race, 0x57c) <= 4 ? 1 : 3;
    } else {
        mode = 0;
        if (U8(race, 0x552) && I32(race, 0x57c) > 4 && U8(view, 0x2c) > U8(race, 0x586))
            mode = 3;
        if (U8(view, 0x2c) > U8(race, 0x586)) /* sic: makes the test above redundant */
            mode = 3;
    }
    if (G32(0x4d1198) && mode == 0) {
        if (!U8(race, 0x552))
            GCALL(0x45cfe0, netplay_chat_show)(A(view), G32(0x6c7fdc) & 0xff);
        GCALL(0x43a4a0, hud_gap_bars)(G32(0x6c7fdc) & 0xff, view);
        GCALL(0x4792a0, text_messages_draw)(A(view), 0);
        return;
    }
    if (I32(race, 0x57c) >= 7)
        return;
    if (mode == 0)
        GCALL(0x43a4a0, hud_gap_bars)(G32(0x6c7fdc) & 0xff, view);
    if (mode == 1)
        return;
    if (mode == 0) {
        uint32_t count, size, limit, icon;
        uint32_t edx;
        int i;

        GCALL(0x483d20, lock_marker_draw)(view);
        count = (uint32_t)GCALL(0x458760, missile_lock_level)((const uint32_t *)view);
        if (count) {
            if (!(G8(0x6c0a50) & 0xf))
                GCALL(0x439d50, hud_fade_set)(car, 1, 9);
            if (!G8(0x6c7fe2 + 4 * car)) {
                GCALL(0x471c90, sound_play_body)(0x3d, 6, 0x400, 0x36b0, U32(view, 0) + 0x34);
                G8(0x6c7fe2 + 4 * car) = (uint8_t)count;
            }
            G8(0x6c7fe2 + 4 * car)--;
        }
        race = PTR(g_race);
        size = G32(0x6c7fdc) & 0xff;
        limit = (uint32_t)U8(race, 0xa84) * 2;
        if ((int32_t)limit > 0xff)
            limit = 0xff;
        size += 2;
        if (size > limit)
            size = limit;
        icon = G8(0x6c7fe0 + 4 * car);
        GCALL(0x439d70, hud_fade_apply)(car, size, icon ? icon - 1 : 0);
        if (!size)
            return;
        race = PTR(g_race);
        if (U8(race, 0x552) && U8(race, 0x551) != 3) {
            uint8_t *ot = PTR(U32(PTR(G32(0x6ce608)), 4) & 0x7fffffff);
            G32(0x6c7fa4) = U32(ot, 0);
            U32(ot, 0) = 0x6c7fa4;
        }
        for (i = 0, edx = 0x6c3f4c; i < 0x43; i++, edx += 0xf0) {
            uint8_t *e = PTR(edx);
            uint32_t ots;
            uint8_t *ot;
            int k;

            if (!x87_greater(e - 0x40, e - 0x78))
                continue;
            if (!G8(0x6c7f5a + i))
                continue;
            for (k = 0; k < 0x78; k += 4) /* rep movsd */
                U32(e - 0x14, k) = U32(e - 0x8c, k);
            x87_add_to(0x6ccd54, e - 4);
            x87_add_to(0x6ccd58, e);
            x87_add_to(0x6ccd54, e + 0x18);
            ots = G32(0x6ce608);
            x87_add_to(0x6ccd58, e + 0x1c);
            x87_add_to(0x6ccd54, e + 0x34);
            x87_add_to(0x6ccd58, e + 0x38);
            x87_add_to(0x6ccd54, e + 0x50);
            x87_add_to(0x6ccd58, e + 0x54);
            ot = PTR(U32(PTR(ots), 4) & 0x7fffffff);
            U32(e - 0x14, 0) = U32(ot, 0);
            U32(ot, 0) = edx - 0x14;
        }
        if (U8(PTR(g_race), 0x551) == 4)
            GCALL(0x469f80, meters_link)(U32(PTR(G32(0x6ce608)), 4), 0x6c7d90);
    }
    if (!G32(0x4be454))
        GCALL(0x4792a0, text_messages_draw)(A(view), 0);
}

/* 0x428bd0 (the far plane, in st(0)) stored as a float: fstp as the original. */
static uint32_t far_plane_get_bits(void)
{
#if defined(__i386__)
    uint32_t bits;
    __asm__ volatile("movl $0x428bd0, %%eax\n\tcall *%%eax\n\tfstps %0"
                     : "=m"(bits)
                     :
                     : "eax", "ecx", "edx", "memory", "cc", X87);
    return bits;
#else
    return G32(0x6ccd08); /* far_plane_get 0x428bd0 is fld dword [0x6ccd08]; ret */
#endif
}

/* ===== 0x479be0 draw_view (view): one view of the race. The viewport is
 * the view's rectangle (words +8 / +0xc / +0x10 / +0x14); from race mode 7
 * (race +0x57c) it closes with race +0x580: under 0x30 narrowed (0x6cd414 =
 * height * 256 * that / 48), from 0x30 on empty. Under render
 * flag 0x20 the viewport with a 2-pixel border (0x428770), else the border
 * frame 0x4284b0 (the outer rectangle +0x18..+0x24) and the viewport. An
 * empty viewport draws nothing. Under render flag 4 the HUD belongs to the
 * view's slot remapped by 0x6c0a40 (views 0x6bfb80, 0x274 each). Then
 * 0x40cbe0 (begin); a camera object (*(view +0x28)) with no mode (+0xc & ~1
 * zero) only flags 0x6ce604 when race +0x54c is set; otherwise the camera
 * (0x466f70, view matrix 0x423d10, the time warp 0x479940, the camera
 * matrix 0x423d30), the back colour 0x6ccc34 / 38 / 3c from 0x4af4b8, and
 * the drawing passes in the original's order, the far plane set to 65536
 * for the sky (0x404f50 / 0x4049b0) and put back; 0x40cd00 (end). */
void GAMEAPI draw_view_body(uint8_t *view)
{
    uint8_t *hud_view = view;
    uint8_t *camera = PTR(U32(PTR(U32(view, 0x28)), 0));
    uint8_t *race = PTR(g_race);
    int32_t squeeze = I32(race, 0x580);
    int16_t r[4], border[4];
    uint32_t lod, colour, far;

    r[0] = S16(view, 8);
    r[1] = S16(view, 0xc);
    r[2] = S16(view, 0x10);
    r[3] = S16(view, 0x14);
    if (I32(race, 0x57c) >= 7) {
        if (squeeze >= 0x30) {
            r[3] = 0;
        } else {
            int32_t height = (int32_t)G32(0x4acc3c);
            int32_t h = (int32_t)(((uint32_t)height << 8) * (uint32_t)squeeze) / 48;
            uint32_t d;

            G32(0x6cd414) = (uint32_t)h;
            d = (uint32_t)h * (uint32_t)((int32_t)((uint32_t)I32(view, 0x14) << 8) / height + 1);
            r[1] = (int16_t)(r[1] + (uint16_t)(d >> 17));
            r[3] = (int16_t)(r[3] - (uint16_t)(d >> 16));
            if (r[3] < 0)
                r[3] = 0;
        }
    }
    if (g_render_flags & 0x20) {
        GCALL(0x428770, view_viewport_apply)((const uint8_t *)r, 2);
    } else {
        border[0] = S16(view, 0x18);
        border[1] = S16(view, 0x1c);
        border[2] = S16(view, 0x20);
        border[3] = S16(view, 0x24);
        GCALL(0x4284b0, view_border_draw)(border, r, 0);
        GCALL(0x428770, view_viewport_apply)((const uint8_t *)r, 0);
    }
    if (r[3] == 0)
        return;
    if (g_render_flags & 4) {
        uint32_t n = U8(PTR(g_race), 0x54d), i, slot;

        for (i = 0, slot = 0x6bfb80; (int32_t)i < (int32_t)n; i++, slot += 0x274) {
            if (A(view) == slot && slot < 0x6c0550) {
                hud_view = PTR(0x6bfb80 + G32(0x6c0a40 + 4 * i) * 0x274);
                break;
            }
        }
    }
    GCALL(0x40cbe0, camera_blend_begin)((uint8_t **)view);
    if ((U32(camera, 0xc) & ~1u) == 0) {
        if (U8(PTR(g_race), 0x54c)) {
            G32(0x6ce604) = 1;
            GCALL(0x40cd00, camera_blend_end)((uint8_t **)view);
            return;
        }
        GCALL(0x40cd00, camera_blend_end)((uint8_t **)view);
        return;
    }
    GCALL(0x466f70, camera_shake_apply)(A(camera + 0x34), U32(view, 0));
    GCALL(0x423d10, view_matrix_store)((const uint32_t *)(camera + 0xc4));
    GCALL(0x479940, time_warp_view_effect)((const uint32_t *)view);
    GCALL(0x423d30, camera_matrix_body)();
    colour = G32(0x4af4b8);
    lod = U32(camera, 0xc) & ~1u;
    G32(0x6ccc34) = (colour & 0xff) << 4 | 8;
    G32(0x6ccc38) = (colour >> 8 & 0xff) << 4 | 8;
    G32(0x6ccc3c) = (uint32_t)G8(0x4af4ba) << 4 | 8;
    GCALL(0x47bd20, cars_glow_texture_cycle)();
    GCALL(0x478df0, texture_anims_step)();
    GCALL(0x490430, sparkle_body)(PTR(U32(view, 0x28)));
    GCALL(0x42be20, shake_flash_tint)(A(camera));
    GCALL(0x47ea00, marker_proximity_tint)(camera);
    if (!(g_render_flags & 0x10))
        GCALL(0x479b30, view_surface_tint)(view);
    GCALL(0x42c0a0, screen_tint_draw)(A(view));
    GCALL(0x484980, weather_draw)(view);
    GCALL(0x438760, hud_update_extra)((uint32_t *)view);
    GCALL(0x433f00, trails_draw)();
    GCALL(0x42b9a0, smoke_clouds_draw)();
    GCALL(0x42c950, shock_waves_draw)();
    GCALL(0x42c420, blast_flare_draw)();
    GCALL(0x4345c0, beams_draw)();
    GCALL(0x467ec0, pickups_draw)();
    GCALL(0x483210, projectile_trails_draw)();
    GCALL(0x48fdd0, snatch_draw)();
    GCALL(0x46c840, shadows_draw)();
    GCALL(0x438310, hud_body)(hud_view);
    colour = G32(0x4af4b8);
    G32(0x6ccc34) = (colour & 0xff) << 4 | 8;
    G32(0x6ccc38) = (colour >> 8 & 0xff) << 4 | 8;
    G32(0x6ccc3c) = (uint32_t)G8(0x4af4ba) << 4 | 8;
    lod = A(GCALL(0x47a030, visibility_body)(PTR(lod)));
    GCALL(0x477940, track_sections_draw)((const uint32_t *)GMEM(lod));
    far = far_plane_get_bits();
    GCALL(0x479f60, view_depth_markers_link)();
    GCALL(0x428b90, far_plane_set)(0x47800000); /* 65536.0f */
    GCALL(0x404f50, sky_quads_draw)();
    GCALL(0x4291f0, dx_error_ignore)(); /* the original pushes the camera before these no-ops */
    GCALL(0x4291f0, dx_error_ignore)();
    GCALL(0x4049b0, sky_dome_draw)(camera);
    GCALL(0x4291f0, dx_error_ignore)();
    GCALL(0x4291f0, dx_error_ignore)();
    GCALL(0x428b90, far_plane_set)(far);
    GCALL(0x47dd70, sprites_draw)((const uint8_t *)GMEM(lod));
    GCALL(0x459b20, objects_body)();
    GCALL(0x46dc50, shields_draw)();
    GCALL(0x421ef0, cars_draw)();
    GCALL(0x423df0, view_camera_load)(PTR(0x6ce5d0));
    GCALL(0x4821d0, car_light_trails_draw)();
    GCALL(0x442060, flares_draw)(camera);
    GCALL(0x46f230, skid_marks_draw)();
    GCALL(0x47db20, warp_overlay_draw)();
    if (!(g_render_flags & 0x12))
        GCALL(0x46a9e0, countdown_body)();
    GCALL(0x41b2f0, screen_colour_apply)();
    GCALL(0x40cd00, camera_blend_end)((uint8_t **)view);
}
