/* Group PN: the original's still-reachable functions in 0x463f90-0x46caa0.
 * Pieces of three original source files:
 *
 *  - the pause menu's sprites (0x463f90 draw, 0x464650 page set-up with its
 *    texts, 0x464a20 an item's sprite, 0x464c10 a slider's rotated sprite);
 *  - small Win32 file helpers (0x465340 load a whole file, 0x465390 open
 *    with retries on a sharing violation, read / close / size / seek /
 *    append / write 0x465460..0x465530) and the CD root name read from
 *    ROOTNAME.TXT (0x465550);
 *  - the camera shake (0x466f70), the pickups' glow and ring sprites
 *    (0x467ec0, 0x467f30, 0x467ff0), the four debug meters (0x469d60,
 *    0x469e80, 0x469f80), the car shadows' drawing (0x46c840, 0x46c890,
 *    0x46c9d0, and 0x46caa0 the 4x4 subdivision, all x87);
 *  - the platform file's registry, mutex, event and environment wrappers
 *    (0x46b200..0x46b9e0; 0x46b510 / 0x46b7b0 are gl_gd.c's).
 *
 * Windows calls go through the original's import table (the same slots
 * the original calls). Float steps are the original's x87 instructions
 * (inline asm, memory operands). Everything outside the group is called by
 * address. Leftovers: 0x465390 hands debug_printf an uninitialised 0x104-
 * byte stack buffer (its sprintf was compiled out): here an empty string.
 * The original also uses some of its own argument slots as scratch
 * (0x464a20, 0x469d60, 0x46b3f0, 0x46b960, 0x46caa0; 0x467f30 / 0x467ff0
 * set their first argument's top byte to 0xff for the alpha); those are
 * the callers' dead stack and are not reproduced. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define SAR(v, n) ((int32_t)(v) >> (n))

#define STDCALL __attribute__((stdcall))
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

typedef int32_t(GAMEAPI *pn_fn0)(void);
typedef int32_t(GAMEAPI *pn_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pn_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pn_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *pn_fn6)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f);
typedef int32_t(GAMEAPI *pn_fn7)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                 uint32_t g);
#define CALL0(a) (((pn_fn0)(a))())
#define CALL1(a, x) (((pn_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pn_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((pn_fn3)(a))(A(x), A(y), A(z)))
#define CALL6(a, x, y, z, w, v, u) (((pn_fn6)(a))(A(x), A(y), A(z), A(w), A(v), A(u)))
#define CALL7(a, x, y, z, w, v, u, t) (((pn_fn7)(a))(A(x), A(y), A(z), A(w), A(v), A(u), A(t)))

/* Imports, through the original's import table. */
typedef int32_t(STDCALL *pn_api0)(void);
typedef int32_t(STDCALL *pn_api1)(uint32_t a);
typedef int32_t(STDCALL *pn_api2)(uint32_t a, uint32_t b);
typedef int32_t(STDCALL *pn_api3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(STDCALL *pn_api4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(STDCALL *pn_api5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef int32_t(STDCALL *pn_api6)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f);
typedef int32_t(STDCALL *pn_api7)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                  uint32_t g);
typedef int32_t(STDCALL *pn_api9)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                  uint32_t g, uint32_t h, uint32_t i);
#define API(slot, type) (*(type *)(uintptr_t)(slot))
#define RegDeleteValueA_(k, n) API(0x4a3000, pn_api2)(A(k), A(n))
#define RegCreateKeyExA_(k, s, r, c, o, sam, sa, out, disp)                                                       \
    API(0x4a3008, pn_api9)(A(k), A(s), (r), A(c), (o), (sam), A(sa), A(out), A(disp))
#define RegCloseKey_(k) API(0x4a300c, pn_api1)(A(k))
#define RegOpenKeyExA_(k, s, o, sam, out) API(0x4a3010, pn_api5)(A(k), A(s), (o), (sam), A(out))
#define RegQueryValueExA_(k, n, r, t, d, s) API(0x4a3014, pn_api6)(A(k), A(n), A(r), A(t), A(d), A(s))
#define RegSetValueExA_(k, n, r, t, d, s) API(0x4a3018, pn_api6)(A(k), A(n), (r), (t), A(d), (s))
#define RegUnLoadKeyA_(k, n) API(0x4a301c, pn_api2)(A(k), A(n))
#define GetEnvironmentVariableA_(n, b, s) API(0x4a308c, pn_api3)(A(n), A(b), (s))
#define ResetEvent_(h) API(0x4a3090, pn_api1)(A(h))
#define SetEvent_(h) API(0x4a30a8, pn_api1)(A(h))
#define CreateEventA_(sa, m, i, n) API(0x4a30ac, pn_api4)(A(sa), (m), (i), A(n))
#define OpenEventA_(acc, inh, n) API(0x4a30b8, pn_api3)((acc), (inh), A(n))
#define CreateMutexA_(sa, own, n) API(0x4a30fc, pn_api3)(A(sa), (own), A(n))
#define CloseHandle_(h) API(0x4a3100, pn_api1)(A(h))
#define SetFilePointer_(h, d, hi, m) API(0x4a3104, pn_api4)(A(h), (d), A(hi), (m))
#define CreateFileA_(n, a, s, sa, c, f, t) API(0x4a3108, pn_api7)(A(n), (a), (s), A(sa), (c), (f), (t))
#define GetLastError_() ((uint32_t)API(0x4a310c, pn_api0)())
#define Sleep_(ms) API(0x4a3110, pn_api1)(ms)

/* Outside the group. */
#define draw_prim_at(p) CALL1(0x428460, p)
#define texts_menu_at(a, b) CALL2(0x4792a0, a, b)
#define text_create_at(s, a, x, y, f, id, m) CALL7(0x479060, s, a, x, y, f, id, m)
#define prim_flag2_at(p, on) CALL2(0x4270e0, p, on)
#define camera_target_at(v) CALL1(0x423ce0, v)
#define sprite_3d_at(v, s, spr, a, b, c) CALL6(0x47e5f0, v, s, spr, a, b, c)
#define file_read_any_at(h, b, n) CALL3(0x43b7f0, h, b, n)
#define file_close_any_at(h) CALL1(0x43b760, h)
#define file_write_any_at(h, b, n) CALL3(0x476e50, h, b, n)
#define kr_malloc_at(n) ((uint32_t)CALL1(0x4445e0, n))
#define debug_printf_at(s) CALL1(0x476ec0, s)
#define file_exists_at(s) CALL1(0x476f00, s)
#define isctype_at(c, m) CALL2(0x498051, c, m)
#define vertex_project_codes_at(r) CALL1(0x4288c0, r)

#define g_scale GI32(0x6cd410) /* the HUD's scale, 0x100 = 1 */

/* The pause menu's sprites: 0x78 bytes at 0x6b91c4, count 0x6b958e. */
#define PAUSE_PRIM(i) (0x6b91c4u + (uint32_t)(i) * 0x78u)
#define g_pause_count G16(0x6b958e)

/* Registry state. */
#define g_reg_key G32(0x4d3db4)
#define g_reg_open G32(0x4d3db8)
#define g_reg_type G32(0x4d3dbc)
#define g_reg_size G32(0x4d3dc0)
#define g_reg_error G32(0x4d3dc4)
#define g_reg_root G32(0x4b1978)
#define g_reg_kind G32(0x4b197c) /* 1 string, 2 dword, 3 binary, 0x7f other */

/* The group's functions (also in physics.h once merged). */
void GAMEAPI pause_menu_draw(void); /* 0x463f90 */
void GAMEAPI pause_menu_open(uint32_t kind, uint32_t sel); /* 0x464650 */
int32_t GAMEAPI pause_menu_item_add(uint32_t kind, uint32_t sel); /* 0x464a20 */
void GAMEAPI pause_menu_slider_set(uint32_t item, uint32_t kind, uint32_t angle); /* 0x464c10 */
uint32_t GAMEAPI file_load_whole(uint32_t path); /* 0x465340 */
uint32_t GAMEAPI file_open_retry(uint32_t path, uint32_t access); /* 0x465390 */
int32_t GAMEAPI file_read_handle(uint32_t h, uint32_t buf, uint32_t n); /* 0x465460 */
int32_t GAMEAPI file_close_handle(uint32_t h); /* 0x465480 */
int32_t GAMEAPI file_size_get(uint32_t h); /* 0x465490 */
int32_t GAMEAPI file_seek_set(uint32_t h, uint32_t off, uint32_t whence); /* 0x4654d0 */
int32_t GAMEAPI file_append_data(uint32_t path, uint32_t buf, uint32_t n); /* 0x4654f0 */
int32_t GAMEAPI file_write_handle(uint32_t h, uint32_t buf, uint32_t n); /* 0x465530 */
uint32_t GAMEAPI root_name_get(void); /* 0x465550 */
void GAMEAPI camera_shake_apply(uint32_t vec, uint32_t car); /* 0x466f70 */
void GAMEAPI pickups_draw(void); /* 0x467ec0 */
void GAMEAPI pickup_glow_draw(uint32_t p, uint32_t colour); /* 0x467f30 */
void GAMEAPI pickup_rings_draw(uint32_t p); /* 0x467ff0 */
void GAMEAPI meters_colour_set(uint32_t rgb); /* 0x469d60 */
uint32_t GAMEAPI meter_level_get(uint32_t i); /* 0x469e80 */
void GAMEAPI meters_link(uint32_t ot, uint32_t pair); /* 0x469f80 */
int32_t GAMEAPI reg_key_close(void); /* 0x46b200 */
int32_t GAMEAPI reg_key_open(uint32_t sub); /* 0x46b240 */
int32_t GAMEAPI reg_key_create(uint32_t sub); /* 0x46b290 */
int32_t GAMEAPI reg_key_unload(uint32_t name); /* 0x46b2f0 */
int32_t GAMEAPI reg_value_type_query(uint32_t name); /* 0x46b330 */
int32_t GAMEAPI reg_value_read(uint32_t name, uint32_t buf, int32_t size); /* 0x46b3f0 */
int32_t GAMEAPI reg_value_write(uint32_t name, uint32_t data, int32_t size, uint32_t type); /* 0x46b490 */
int32_t GAMEAPI reg_value_delete(uint32_t name); /* 0x46b4e0 */
int32_t GAMEAPI reg_string_read(uint32_t name, uint32_t buf, int32_t size); /* 0x46b550 */
int32_t GAMEAPI reg_dword_read(uint32_t name, uint32_t out); /* 0x46b5a0 */
int32_t GAMEAPI reg_binary_read(uint32_t name, uint32_t buf, int32_t size); /* 0x46b600 */
int32_t GAMEAPI reg_string_get(uint32_t key, uint32_t name, uint32_t buf, int32_t size); /* 0x46b650 */
int32_t GAMEAPI reg_dword_get(uint32_t key, uint32_t name, uint32_t out); /* 0x46b690 */
int32_t GAMEAPI reg_binary_get(uint32_t key, uint32_t name, uint32_t buf, int32_t size); /* 0x46b6d0 */
int32_t GAMEAPI reg_binary_write(uint32_t name, uint32_t data, int32_t size); /* 0x46b710 */
int32_t GAMEAPI reg_binary_set(uint32_t key, uint32_t name, uint32_t data, int32_t size); /* 0x46b730 */
int32_t GAMEAPI reg_value_remove(uint32_t key, uint32_t name); /* 0x46b770 */
int32_t GAMEAPI win_handle_close(uint32_t h); /* 0x46b7d0 */
int32_t GAMEAPI win_mutex_create(uint32_t out, uint32_t name); /* 0x46b800 */
int32_t GAMEAPI app_mutex_close(void); /* 0x46b850 */
int32_t GAMEAPI app_mutex_create(uint32_t name); /* 0x46b870 */
int32_t GAMEAPI win_handle_close_quiet(uint32_t h); /* 0x46b890 */
int32_t GAMEAPI win_event_create(uint32_t out, uint32_t name); /* 0x46b8b0 */
int32_t GAMEAPI win_event_open(uint32_t out, uint32_t name); /* 0x46b900 */
int32_t GAMEAPI win_event_set(uint32_t h); /* 0x46b940 */
int32_t GAMEAPI win_event_reset(uint32_t h); /* 0x46b950 */
int32_t GAMEAPI env_var_get(uint32_t name, uint32_t buf, int32_t size); /* 0x46b960 */
int32_t GAMEAPI env_var_length(uint32_t name); /* 0x46b9c0 */
int32_t GAMEAPI env_var_exists(uint32_t name); /* 0x46b9e0 */
void GAMEAPI shadows_draw(void); /* 0x46c840 */
int32_t GAMEAPI shadow_project(uint32_t s, uint32_t view); /* 0x46c890 */
void GAMEAPI shadow_add(uint32_t car, uint32_t ot); /* 0x46c9d0 */
void GAMEAPI shadow_subdivide(uint32_t s, uint32_t tpage); /* 0x46caa0 */

/* ---- x87 helpers: the original's instructions ---- */

/* fild dword; fstp dword. */
static inline uint32_t pn_from_int(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild qword (high dword 0); fstp dword: an unsigned 32-bit value. */
static inline uint32_t pn_from_u32(uint32_t v)
{
    uint32_t q[2] = {v, 0}, r;
    __asm__ volatile("fildll %1\n\tfstps %0" : "=m"(r) : "m"(q) : "st");
    return r;
}

/* fld a; fadd b; fstp. */
static inline uint32_t pn_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fmul b; fstp. */
static inline uint32_t pn_mul(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fsub b; fmul k; fstp. */
static inline uint32_t pn_sub_mul(uint32_t a, uint32_t b, uint32_t k)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfmuls %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(k) : "st");
    return r;
}

/* fld a; fmul b; fadd c; fstp. */
static inline uint32_t pn_mul_add(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfadds %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fistp dword: rounded by the control word. */
static inline int32_t pn_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld dword a; call _ftol (0x497a80). */
static inline int32_t pn_ftol(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
}

/* The transform *0x6ccd78 (vertex), *colour_at = colour, then the
 * projection *0x6ccd80, which takes the transform's st(0). */
static inline void pn_transform_project(uint32_t vertex, uint32_t colour_at, uint32_t colour)
{
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tmovl %2, (%1)\n\tcall *0x6ccd80"
                     :
                     : "S"(vertex), "D"(colour_at), "b"(colour)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
}

/* ===================================================================== */
/* The pause menu. */

/* 0x463f90: draws the nine frame sprites (0x6b8d8c), then the items: the
 * selected one (item word 0x6b9590[i] == 0x6b8d66) white or dim by the
 * blink bit 0x6b8d68 & 0x10, the others grey; then the texts (0x4792a0). */
void GAMEAPI pause_menu_draw(void)
{
    uint32_t i;
    for (i = 0; i < 9; i++)
        draw_prim_at(0x6b8d8cu + i * 0x78u);
    if (g_pause_count != 0) {
        i = 0;
        do {
            uint32_t p = PAUSE_PRIM(i) + 5;
            uint8_t c = 0x80;
            if (G16(0x6b9590 + 2 * i) == G16(0x6b8d66))
                c = (G8(0x6b8d68) & 0x10) ? 0xff : 0x40;
            G8(p - 1) = c;
            G8(p) = c;
            G8(p + 1) = c;
            draw_prim_at(p - 5);
            i++;
        } while (i < (uint32_t)(int32_t)GS16(0x6b958e)); /* sic: unsigned against the sign-extended count */
    }
    texts_menu_at(0, 1);
}

/* 0x464650: sets up pause menu page `kind` with `sel` selected (word): the
 * texts (pointers 0x4d10f4..0x4d112c, y scaled by 0x6cd410) and sprites;
 * kinds 10 / 11 confirmations, 20 the car choice, others the main page. */
void GAMEAPI pause_menu_open(uint32_t kind, uint32_t sel)
{
    uint16_t old = G16(0x6b8d64);
    uint32_t i;
    G32(0x6b9586) = 0;
    G16(0x6b95a2) = old;
    G16(0x6b8d66) = (uint16_t)sel;
    G16(0x6b8d64) = (uint16_t)kind;
    G32(0x6b958a) = 0;
    for (i = 0; i < 8; i++)
        G8(0x6b95ac + i * 0x50) = 0;
    g_pause_count = 0;
    G32(0x6b9824) = 0;
    if (kind == 10) {
        G8(0x6b9586) = 1;
        G8(0x6b9587) = 1;
        text_create_at(G32(G16(0x6b95a2) == 2 ? 0x4d10fc : 0x4d10f8), 0, 0, SAR(-(g_scale * 79), 8), 6, -1, 1);
        text_create_at(G32(0x4d110c), 0, 0, SAR(-(g_scale * 11), 8), 6, 0, 1);
        text_create_at(G32(0x4d1110), 0, 0, SAR(g_scale * 11, 8), 6, 1, 1);
        G16(0x6b9584) = 2;
        return;
    }
    if (kind == 11) {
        G8(0x6b9586) = 1;
        G8(0x6b9587) = 1;
        G8(0x6b9588) = 1;
        text_create_at(G32(0x4d1100), 0, 0, SAR(-(g_scale * 79), 8), 6, -1, 1);
        text_create_at(G32(0x4d110c), 0, 0, SAR(-(g_scale * 11), 8), 6, 0, 1);
        text_create_at(G32(0x4d1110), 0, 0, SAR(g_scale * 11, 8), 6, 1, 1);
        G16(0x6b9584) = 2;
        return;
    }
    if (kind == 20) {
        int32_t y = -0x33;
        G8(0x6b9838) = U8(U32(G32(0x6bfb80), 0x2470), 0x1218);
        text_create_at(G32(0x4d1108), 0, 0, SAR(-(g_scale * 79), 8), 6, -1, 1);
        for (i = 0; i < 6; i++, y += 0x16) {
            text_create_at(G32(0x4d1114 + 4 * i), 0, 0, SAR(g_scale * y, 8), 6, i, 1);
            G8(0x6b9586 + i) = 1;
        }
        G16(0x6b9584) = 6;
        G16(0x6b8d66) = (uint16_t)(int16_t)(int8_t)U8(U32(G32(0x6bfb80), 0x2470), 0x1218);
        return;
    }
    for (i = 0; i < 5; i++)
        G8(0x6b9586 + i) = 1;
    text_create_at(G32(0x4d10f4), 0, 0, SAR(-(g_scale * 79), 8), 6, -1, 1);
    pause_menu_item_add(0, 0);
    G8(0x6b983a) = (uint8_t)pause_menu_item_add(2, 0);
    pause_menu_item_add(1, 1);
    G8(0x6b983b) = (uint8_t)pause_menu_item_add(3, 1);
    pause_menu_item_add(4, 0);
    pause_menu_item_add(6, 0);
    pause_menu_item_add(5, 1);
    pause_menu_item_add(7, 1);
    text_create_at(G32(kind == 2 ? 0x4d10fc : 0x4d10f8), 0, 0, SAR(g_scale * 14, 8), 6, 2, 1);
    if (kind == 0)
        text_create_at(G32(0x4d1100), 0, 0, SAR(g_scale * 36, 8), 6, 3, 1);
    else if (kind == 1)
        G8(0x6b9589) = 0;
    else if (kind == 2)
        text_create_at(G32(0x4d1108), 0, 0, SAR(g_scale * 36, 8), 6, 3, 1);
    text_create_at(G32(0x4d1104), 0, 0, SAR(g_scale * 58, 8), 6, 4, 1);
    G16(0x6b9584) = 5;
}

/* The texture record of pause sprite kind k (table 0x4b1638, 8 bytes per
 * kind: x, y words, +6 swap flag, +7 texture index). */
static uint32_t pause_texture(uint32_t kind)
{
    return ((uint32_t)U16(G32(0x6c1eb0), 0x2e) + G8(0x4b163f + 8 * kind)) * 16u + G32(0x6c1e14);
}

/* 0x464a20: appends pause sprite `kind` (a textured quad of floats at
 * 0x6b91c4 + n * 0x78, centred on the screen 0x6b8d6c / 0x6b8d70 plus the
 * kind's scaled offset) with item word `sel`; returns the old count n. */
int32_t GAMEAPI pause_menu_item_add(uint32_t kind, uint32_t sel)
{
    int32_t n = GS16(0x6b958e);
    uint32_t prim = PAUSE_PRIM(n), tex = pause_texture(kind), head, flags, fx, fy;
    int32_t ox, oy, w, h, t;
    head = U32(prim, 0);
    flags = U16(tex, 6) & 0xff3f;
    U32(prim, 0) = (head & 0xffffff) | 0xc000000;
    U8(prim, 7) = 0x98;
    if (!(G8(0x6ccd84) & 4))
        prim_flag2_at(prim, 1);
    U32(prim, 0x20) = U32(tex, 0);
    U32(prim, 0x3c) = U32(tex, 4);
    U16(prim, 0x58) = U16(tex, 8);
    U16(prim, 0x74) = U16(tex, 0xa);
    U16(prim, 0x3e) = (uint16_t)flags;
    ox = GS16(0x4b1638 + 8 * kind) * g_scale;
    oy = GS16(0x4b163a + 8 * kind) * g_scale;
    w = U8(tex, 0xa) - U8(tex, 0);
    h = U8(tex, 0xb) - U8(tex, 1);
    if (G8(0x4b163e + 8 * kind)) {
        t = w;
        w = h;
        h = t;
    }
    fx = pn_from_int((int32_t)((G32(0x6b8d6c) >> 1) + (uint32_t)SAR(ox, 8)));
    fy = pn_from_int((int32_t)((uint32_t)SAR(oy, 8) + (G32(0x6b8d70) >> 1)));
    U32(prim, 0x48) = fx;
    U32(prim, 0x10) = fx;
    U32(prim, 0x30) = fy;
    U32(prim, 0x14) = fy;
    U32(prim, 0x2c) = pn_add(pn_from_int(SAR(g_scale * w, 8)), U32(prim, 0x10));
    U32(prim, 0x64) = pn_add(pn_from_int(SAR(g_scale * w, 8)), U32(prim, 0x48));
    U32(prim, 0x4c) = pn_add(pn_from_int(SAR(g_scale * h, 8)), fy);
    U32(prim, 8) = 0x40;
    t = SAR(g_scale * h, 8);
    U32(prim, 0x60) = 0x40400000;
    U32(prim, 0x44) = 0x40400000;
    U32(prim, 0x28) = 0x40400000;
    U32(prim, 0xc) = 0x40400000;
    U32(prim, 0x68) = pn_add(pn_from_int(t), fy);
    G16(0x6b9590 + 2 * (uint32_t)n) = (uint16_t)sel;
    g_pause_count++;
    return n;
}

/* 0x464c10: pause sprite `item` (of kind `kind`) as a quad rotated by
 * angle - 0x40 (sine / cosine tables 0x4b24f8 / 0x4b2578, radius 19/16384),
 * corners as unsigned integers to float (fild qword). */
void GAMEAPI pause_menu_slider_set(uint32_t item, uint32_t kind, uint32_t angle)
{
    uint32_t a = (angle - 0x40) & 0xff, tex = pause_texture(kind), prim = PAUSE_PRIM(item);
    int32_t cx = SAR(U8(tex, 0xa) - U8(tex, 0), 1) + GS16(0x4b1638 + 8 * kind);
    int32_t cy = SAR(U8(tex, 0xb) - U8(tex, 1), 1) + GS16(0x4b163a + 8 * kind);
    int32_t sn = SAR(GS16(0x4b24f8 + 2 * a) * 19, 14);
    int32_t cs = SAR(-(GS16(0x4b2578 + 2 * a) * 19), 14);
#define PX(v) pn_from_u32((uint32_t)SAR((v) * g_scale, 8) + (G32(0x6b8d6c) >> 1))
#define PY(v) pn_from_u32((uint32_t)SAR((v) * g_scale, 8) + (G32(0x6b8d70) >> 1))
    U32(prim, 0x10) = PX(sn + cx);
    U32(prim, 0x14) = PY(cs + cy);
    U32(prim, 0x2c) = PX(cs + cx);
    U32(prim, 0x30) = PY(cy - sn);
    U32(prim, 0x64) = PX(cx - sn);
    U32(prim, 0x68) = PY(cy - cs);
    U32(prim, 0x48) = PX(cx - cs);
    U32(prim, 0x4c) = PY(sn + cy);
#undef PX
#undef PY
}

/* ===================================================================== */
/* Files. */

/* 0x465390: CreateFileA(path, access | GENERIC_READ, no sharing,
 * OPEN_EXISTING, normal), retried every 500 ms while it fails with a
 * sharing violation (0x21); other failures print (unless the path is
 * "DBUG") and return 0. */
uint32_t GAMEAPI file_open_retry(uint32_t path, uint32_t access)
{
    uint8_t msg[0x104];
    uint32_t h;
    access |= 0x80000000u;
    h = (uint32_t)CreateFileA_(path, access, 0, 0, 3, 0x80, 0);
    while (h == 0xffffffffu) {
        if (GetLastError_() != 0x21) {
            uint32_t s = path, t = 0x4a7ffc;
            while (G8(s) == G8(t) && G8(s) != 0) {
                s++;
                t++;
            }
            if (G8(s) != G8(t)) {
                msg[0] = 0; /* the original's buffer is never written */
                debug_printf_at(msg);
            }
            return 0;
        }
        Sleep_(500);
        h = (uint32_t)CreateFileA_(path, access, 0, 0, 3, 0x80, 0);
    }
    return h;
}

/* 0x465460: read n bytes (0x43b7f0). */
int32_t GAMEAPI file_read_handle(uint32_t h, uint32_t buf, uint32_t n)
{
    return file_read_any_at(h, buf, n);
}

/* 0x465480: close (0x43b760). */
int32_t GAMEAPI file_close_handle(uint32_t h)
{
    return file_close_any_at(h);
}

/* 0x4654d0: SetFilePointer(h, off, NULL, whence). */
int32_t GAMEAPI file_seek_set(uint32_t h, uint32_t off, uint32_t whence)
{
    return SetFilePointer_(h, off, 0, whence);
}

/* 0x465490: the file's size (seek to the end and back). */
int32_t GAMEAPI file_size_get(uint32_t h)
{
    int32_t at = file_seek_set(h, 0, 1), end = file_seek_set(h, 0, 2);
    file_seek_set(h, (uint32_t)at, 0);
    return end;
}

/* 0x465530: write n bytes (0x476e50). */
int32_t GAMEAPI file_write_handle(uint32_t h, uint32_t buf, uint32_t n)
{
    return file_write_any_at(h, buf, n);
}

/* 0x465340: a whole file into a new block (0x4445e0); 0 if it won't open. */
uint32_t GAMEAPI file_load_whole(uint32_t path)
{
    uint32_t h = file_open_retry(path, 0), size, buf;
    if (!h)
        return 0;
    size = (uint32_t)file_size_get(h);
    buf = kr_malloc_at(size);
    file_read_handle(h, buf, size);
    file_close_handle(h);
    return buf;
}

/* 0x4654f0: appends n bytes to an existing file. */
int32_t GAMEAPI file_append_data(uint32_t path, uint32_t buf, uint32_t n)
{
    uint32_t h = file_open_retry(path, 0x40000000);
    if (!h)
        return 0;
    file_seek_set(h, 0, 2);
    file_write_handle(h, buf, n);
    return file_close_handle(h);
}

/* 0x465550: the CD root name: 0x4be3f4 if set; else the first 0x50 bytes
 * of ROOTNAME.TXT (current directory, root, C:\) at 0x4d119c, cut at the
 * first byte that is not a letter, digit, punctuation or blank (_isctype
 * 0x157); 0x4d119c unchanged if none opens. */
uint32_t GAMEAPI root_name_get(void)
{
    uint32_t names[3] = {0x4b16ac, 0x4b169c, 0x4b168c}, h = 0, p;
    int i;
    if (G8(0x4be3f4) != 0)
        return 0x4be3f4;
    for (i = 0; i < 3; i++)
        if (file_exists_at(names[i]) && (h = file_open_retry(names[i], 0)) != 0)
            break;
    if (i == 3)
        return 0x4d119c;
    file_read_handle(h, 0x4d119c, 0x50);
    file_close_handle(h);
    for (p = 0x4d119c;; p++) { /* sic: no limit when all 0x50 bytes pass */
        int32_t ok;
        if (GI32(0x4b4d0c) > 1)
            ok = isctype_at(G8(p), 0x157);
        else
            ok = U16(G32(0x4b4b00), 2 * G8(p)) & 0x157;
        if (!ok)
            break;
    }
    G8(p) = 0;
    return 0x4d119c;
}

/* ===================================================================== */
/* Camera shake, pickups, meters. */

/* 0x466f70: the camera target (0x423ce0) moved by a random shake: car
 * +0x6a8 counts down (strength from +0x6aa), or 9 with the view's table
 * strength (0x6cf7e8 + view * 0x150) under render flag 0x10; three draws of
 * the x16807 generator 0x4b16bc. */
void GAMEAPI camera_shake_apply(uint32_t vec, uint32_t car)
{
    uint32_t view = G32(0x6cd408), seed, local[3];
    int32_t strength, amount, half, d[3], k;
    uint16_t count;
    if ((G8(0x6cce40) & 0x10) && G32(0x6cf7e8 + view * 0x150) != 0) {
        count = 9;
        strength = GI32(0x6cf7ec + view * 0x150);
    } else {
        count = U16(car, 0x6a8);
        if (count == 0) {
            camera_target_at(vec);
            return;
        }
        count--;
        U16(car, 0x6a8) = count;
        if (count == 0) {
            camera_target_at(vec);
            return;
        }
        strength = S16(car, 0x6aa);
    }
    strength -= 0xbb8;
    if (strength < 0)
        strength = 0;
    else if (strength > 0x4a38)
        strength = 0x4a38;
    amount = (int16_t)count * strength * 3;
    amount = SAR(amount + ((amount >> 31) & 0xf), 4);
    half = SAR(amount, 1);
    seed = G32(0x4b16bc);
    for (k = 0; k < 3; k++) {
        seed = (seed * 16807u) & 0x7fffffffu;
        d[k] = (int32_t)(((((seed >> 15) * (uint32_t)amount) >> 16) - (uint32_t)half) << 6);
    }
    G32(0x4b16bc) = seed;
    local[0] = U32(vec, 0) + (uint32_t)d[0];
    local[1] = U32(vec, 4) + (uint32_t)d[1];
    local[2] = U32(vec, 8) + (uint32_t)d[2];
    camera_target_at(local);
}

/* 0x467f30: a pickup's glow sprite (pickup +0x190, or +0x208 in the second
 * view) at its position (y - 10), colour 0xbbggrr, size from +0x180. */
void GAMEAPI pickup_glow_draw(uint32_t p, uint32_t colour)
{
    int16_t v[4];
    uint32_t sprite = p + 0x190;
    int32_t size;
    v[0] = (int16_t)SAR(I32(p, 0x34), 16);
    v[1] = (int16_t)(SAR(I32(p, 0x38), 16) - 10);
    v[2] = (int16_t)SAR(I32(p, 0x3c), 16);
    v[3] = 0; /* never written in the original */
    if (G32(0x6ce60c) != 0x6cd4a0)
        sprite += 0x78;
    G32(0x6ccc34) = (colour & 0xff) << 4;
    G32(0x6ccc38) = ((colour >> 8) & 0xff) << 4;
    G32(0x6ccc3c) = ((colour >> 16) & 0xff) << 4;
    G32(0x6ccc60) = 0xff0;
    size = S16(p, 0x180);
    size = size > 0x11 ? 0x1a - size : SAR(size, 1);
    sprite_3d_at(v, (uint32_t)size << 12, sprite, 3, 0x80000000u, 0);
}

/* 0x467ff0: a pickup's three spinning ring sprites (pickup +0x280, +0xf0
 * each; +0x78 in the second view), phases from +0x552 and the frame
 * 0x6c0a50 at speeds 16 / 20 / 24 (halved). */
void GAMEAPI pickup_rings_draw(uint32_t p)
{
    uint32_t phase = U16(p, 0x552), frame = G32(0x6c0a50), sprite = p + 0x280, i;
    int16_t v0, v1, v2, w[4];
    if (G32(0x6ce60c) != 0x6cd4a0)
        sprite += 0x78;
    v0 = (int16_t)SAR(I32(p, 0x34), 16);
    v1 = (int16_t)SAR(I32(p, 0x38), 16);
    v2 = (int16_t)SAR(I32(p, 0x3c), 16);
    G32(0x6ccc34) = 0x3f0;
    G32(0x6ccc38) = 0x3f0;
    G32(0x6ccc3c) = 0xff0;
    G32(0x6ccc60) = 0xff0;
    for (i = 0; i < 3; i++, sprite += 0xf0) {
        uint32_t t = i == 1 ? frame * 20 : i == 2 ? frame * 24 : frame << 4;
        uint32_t at = ((t >> 1) + phase) & 0xff;
        int16_t s = (int16_t)SAR(GS16(0x4b24f8 + 2 * at), 8), c = (int16_t)SAR(GS16(0x4b2578 + 2 * at), 8);
        int16_t dx, dy, dz;
        if (i == 1) {
            dx = 0;
            dy = s;
            dz = c;
        } else if (i == 2) {
            dx = s;
            dy = c;
            dz = 0;
        } else {
            dx = c;
            dy = 0;
            dz = s;
        }
        w[0] = (int16_t)(dx + v0);
        w[1] = (int16_t)(dy + v1);
        w[2] = (int16_t)(dz + v2);
        w[3] = 0; /* never written in the original */
        sprite_3d_at(w, 0x3000, sprite, 3, 0x80000000u, 0);
    }
}

/* 0x467ec0: the pickups' sprites (list 0x6b8ce0, count 0x6b8ce4) by state
 * +0x182 (bit 0x80 shown; 1 rings, 3 glow and rings, 2 / 4 none; anything
 * else ends the loop). */
void GAMEAPI pickups_draw(void)
{
    int32_t n = GI32(0x6b8ce4);
    uint32_t list = G32(0x6b8ce0);
    for (; n > 0; n--, list += 4) {
        uint32_t p = G32(list), k = (uint32_t)(int32_t)S16(p, 0x182);
        if (!(k & 0x80))
            continue;
        k = ((k & 0xffffff00u) | (k & 0x7f)) - 1;
        if (k > 3)
            return; /* sic */
        if (k == 0) {
            pickup_rings_draw(p);
        } else if (k == 2) {
            pickup_glow_draw(p, 0x7f0f7f);
            pickup_rings_draw(p);
        }
    }
}

/* The meters' records: 4 x 0x298 at *0x6b8cd8 (*0x6b8cdc in the second
 * view): +4 colour, 16 segments of 0x28 at +0x14 (colour +0x18), level
 * +0x294 (shown) / +0x296 (target). */
static uint32_t meters_base(void)
{
    return G32(0x6ce608) == 0x6ce5f0 ? G32(0x6b8cd8) : G32(0x6b8cdc);
}

/* 0x469e80: meter i's level 0..255 from the car *0x4b1948 (+0x227c,
 * +0x2274, +0x2280, +0x2278 over fixed ranges), eased by 16 per call;
 * returns the shown level. Always the first view's records (sic). */
uint32_t GAMEAPI meter_level_get(uint32_t i)
{
    int32_t v = 0, lo = 0, hi = 10, level;
    uint32_t car = G32(0x4b1948), rec;
    uint16_t cur;
    switch (i) {
    case 0:
        v = I32(car, 0x227c);
        lo = 0x40000;
        hi = 0x120000;
        break;
    case 1:
        v = I32(car, 0x2274);
        lo = -80000;
        hi = 80000;
        break;
    case 2:
        v = I32(car, 0x2280);
        lo = 0;
        hi = 3;
        break;
    case 3:
        v = I32(car, 0x2278);
        lo = -3500;
        hi = 5000;
        break;
    }
    rec = G32(0x6b8cd8) + i * 0x298;
    v -= lo;
    level = (int32_t)((uint32_t)v * 255u) / (hi - lo);
    cur = U16(rec, 0x294);
    U16(rec, 0x296) = (uint16_t)level;
    {
        int32_t d = (int32_t)cur - (int32_t)(uint16_t)level;
        if (d < 0)
            d = -d;
        if (d <= 16) {
            U16(rec, 0x294) = (uint16_t)level;
            return (uint16_t)level;
        }
    }
    if (cur < (uint16_t)level)
        U16(rec, 0x294) = (uint16_t)(cur + 16);
    if (U16(rec, 0x294) > (uint16_t)level)
        U16(rec, 0x294) = (uint16_t)(U16(rec, 0x294) - 16);
    return U16(rec, 0x294);
}

/* 0x469d60: the meters' colours (0xbbggrr) and segment shading up to each
 * meter's level (red rising, green falling along the 16 segments), unless there is no meter car (*0x4b1948 == -1). */
void GAMEAPI meters_colour_set(uint32_t rgb)
{
    uint32_t rec, i;
    int32_t r = rgb & 0xff, g = (rgb >> 8) & 0xff, b = (rgb >> 16) & 0xff;
    if (G32(0x4b1948) == 0xffffffffu)
        return;
    rec = meters_base();
    for (i = 0; i < 4; i++, rec += 0x298) {
        uint32_t level, seg, k, q = rec + 0x18;
        int32_t green = g * 0x3c0;
        U8(rec, 4) = (uint8_t)r;
        U8(rec, 5) = (uint8_t)g;
        U8(rec, 6) = (uint8_t)b;
        level = meter_level_get(i);
        for (seg = 0, k = 0; seg < 0x100; seg += 0x10, k++, q += 0x28, green -= g << 6) {
            if (seg > level) {
                U8(q, 0) = 0;
                U8(q, 1) = 0;
            } else {
                int32_t x = SAR((int32_t)(k << 6) * r, 8), y = SAR(green, 8);
                if (x > 0xff)
                    x = 0xff;
                if (y > 0xff)
                    y = 0xff;
                U8(q, 0) = (uint8_t)x;
                U8(q, 1) = (uint8_t)y;
            }
            U8(q, 2) = 0;
        }
    }
}

/* 0x469f80: links the meters into ordering table entry ot: the 4 records,
 * the view's 8-byte pair (copied from `pair` into 0x4d3ca0 / 0x4d3ca8,
 * pointer 0x4d3cb0), then the 64 segments. */
void GAMEAPI meters_link(uint32_t ot, uint32_t pair)
{
    uint32_t rec = meters_base(), o = ot & 0x7fffffffu, k, j, t;
    for (k = 0; k < 4; k++) {
        U32(rec + k * 0x298, 0) = G32(o);
        G32(o) = rec + k * 0x298;
    }
    t = G32(0x6ce608) == 0x6ce5f0 ? 0x4d3ca0 : 0x4d3ca8;
    G32(0x4d3cb0) = t;
    G32(t) = U32(pair, 0);
    U32(G32(0x4d3cb0), 4) = U32(pair, 4);
    U32(G32(0x4d3cb0), 0) = G32(o);
    G32(o) = G32(0x4d3cb0);
    for (k = 0; k < 4; k++)
        for (j = 0; j < 16; j++) {
            uint32_t s = rec + 0x14 + k * 0x298 + j * 0x28;
            U32(s, 0) = G32(o);
            G32(o) = s;
        }
}

/* ===================================================================== */
/* Registry, mutexes, events, environment. */

/* 0x46b200: closes the open key; -1 if RegCloseKey fails (error kept). */
int32_t GAMEAPI reg_key_close(void)
{
    int32_t r = 0, e;
    if (!g_reg_open)
        return 0;
    e = RegCloseKey_(g_reg_key);
    if (e) {
        g_reg_error = (uint32_t)e;
        r = -1;
    }
    g_reg_open = 0;
    return r;
}

/* 0x46b240: opens sub-key `sub` of the root 0x4b1978 (KEY_ALL_ACCESS). */
int32_t GAMEAPI reg_key_open(uint32_t sub)
{
    int32_t e;
    if (reg_key_close() == -1)
        return -1;
    e = RegOpenKeyExA_(g_reg_root, sub, 0, 0xf003f, 0x4d3db4);
    if (e) {
        g_reg_error = (uint32_t)e;
        return -1;
    }
    g_reg_open = 1;
    return 0;
}

/* 0x46b290: creates / opens sub-key `sub` of the root. */
int32_t GAMEAPI reg_key_create(uint32_t sub)
{
    uint32_t disposition;
    int32_t e;
    if (reg_key_close() == -1)
        return -1;
    e = RegCreateKeyExA_(g_reg_root, sub, 0, 0, 0, 0xf003f, 0, 0x4d3db4, &disposition);
    if (e) {
        g_reg_error = (uint32_t)e;
        return -1;
    }
    g_reg_open = 1;
    return 0;
}

/* 0x46b2f0: RegUnLoadKeyA(root, name) after closing the key. */
int32_t GAMEAPI reg_key_unload(uint32_t name)
{
    int32_t e;
    if (!name || G8(name) == 0 || reg_key_close() == -1)
        return -1;
    e = RegUnLoadKeyA_(g_reg_root, name);
    if (e) {
        g_reg_error = (uint32_t)e;
        return -1;
    }
    return 0;
}

/* 0x46b330: value `name`'s type and size (0x4d3dbc / 0x4d3dc0) and kind
 * 0x4b197c; -1 (type, size 0, kind 0x7f) if it can't be read. */
int32_t GAMEAPI reg_value_type_query(uint32_t name)
{
    int32_t e;
    if (g_reg_open) {
        e = RegQueryValueExA_(g_reg_key, name, 0, 0x4d3dbc, 0, 0x4d3dc0);
        if (!e) {
            switch (g_reg_type) {
            case 1: /* REG_SZ */
            case 2: /* REG_EXPAND_SZ */
            case 6: /* REG_LINK */
            case 7: /* REG_MULTI_SZ */
                g_reg_kind = 1;
                break;
            case 4: /* REG_DWORD */
                g_reg_kind = 2;
                break;
            case 3: /* REG_BINARY */
                g_reg_kind = 3;
                break;
            default:
                g_reg_kind = 0x7f;
                break;
            }
            return 0;
        }
        g_reg_error = (uint32_t)e;
    }
    g_reg_type = 0;
    g_reg_size = 0;
    g_reg_kind = 0x7f;
    return -1;
}

static void pn_zero(uint32_t buf, int32_t size)
{
    int32_t i;
    for (i = 0; i < size; i++)
        G8(buf + (uint32_t)i) = 0;
}

/* 0x46b3f0: reads value `name` into buf (cleared first; cleared again on
 * failure); returns the size read or -1. */
int32_t GAMEAPI reg_value_read(uint32_t name, uint32_t buf, int32_t size)
{
    int32_t r = -1;
    if (buf && size > 0)
        pn_zero(buf, size);
    if (g_reg_open && (!buf || size > 0)) {
        uint32_t got = (uint32_t)size;
        int32_t e = RegQueryValueExA_(g_reg_key, name, 0, 0, buf, &got);
        if (e) {
            g_reg_error = (uint32_t)e;
        } else {
            r = (int32_t)got;
            if (r != -1)
                return r;
        }
    }
    if (buf && size > 0)
        pn_zero(buf, size);
    return r;
}

/* 0x46b490: RegSetValueExA(key, name, 0, type, data, size); 0 or -1. */
int32_t GAMEAPI reg_value_write(uint32_t name, uint32_t data, int32_t size, uint32_t type)
{
    int32_t e;
    if (!g_reg_open || !data || size < 0)
        return -1;
    e = RegSetValueExA_(g_reg_key, name, 0, type, data, (uint32_t)size);
    if (e) {
        g_reg_error = (uint32_t)e;
        return -1;
    }
    return 0;
}

/* 0x46b4e0: RegDeleteValueA(key, name); 0 or -1. */
int32_t GAMEAPI reg_value_delete(uint32_t name)
{
    int32_t e;
    if (!g_reg_open)
        return -1;
    e = RegDeleteValueA_(g_reg_key, name);
    if (e) {
        g_reg_error = (uint32_t)e;
        return -1;
    }
    return 0;
}

/* 0x46b550 / 0x46b600: a string (kind 1) / binary (kind 3) value into buf
 * (size -1: the value's size). */
static int32_t reg_kind_read(uint32_t name, uint32_t buf, int32_t size, uint32_t kind)
{
    if (reg_value_type_query(name) == -1 || g_reg_kind != kind)
        return -1;
    if (size == -1)
        size = (int32_t)g_reg_size;
    return reg_value_read(name, buf, size);
}

int32_t GAMEAPI reg_string_read(uint32_t name, uint32_t buf, int32_t size)
{
    return reg_kind_read(name, buf, size, 1);
}

int32_t GAMEAPI reg_binary_read(uint32_t name, uint32_t buf, int32_t size)
{
    return reg_kind_read(name, buf, size, 3);
}

/* 0x46b5a0: a dword value (kind 2, size 4) into *out (0 if unread). */
int32_t GAMEAPI reg_dword_read(uint32_t name, uint32_t out)
{
    uint32_t v = 0;
    int32_t r = -1;
    if (reg_value_type_query(name) != -1 && g_reg_kind == 2 && g_reg_size == 4)
        r = reg_value_read(name, A(&v), 4);
    if (out)
        G32(out) = v;
    return r;
}

/* 0x46b650: string value `name` of key `key` (opened and closed). */
int32_t GAMEAPI reg_string_get(uint32_t key, uint32_t name, uint32_t buf, int32_t size)
{
    int32_t r;
    if (reg_key_open(key) == -1)
        return -1;
    r = reg_string_read(name, buf, size);
    reg_key_close();
    return r;
}

/* 0x46b690: dword value; 0 or -1. */
int32_t GAMEAPI reg_dword_get(uint32_t key, uint32_t name, uint32_t out)
{
    int32_t r = -1;
    if (reg_key_open(key) == -1)
        return -1;
    if (reg_dword_read(name, out) != -1)
        r = 0;
    reg_key_close();
    return r;
}

/* 0x46b6d0: binary value; the size read or -1. */
int32_t GAMEAPI reg_binary_get(uint32_t key, uint32_t name, uint32_t buf, int32_t size)
{
    int32_t r;
    if (reg_key_open(key) == -1)
        return -1;
    r = reg_binary_read(name, buf, size);
    reg_key_close();
    return r;
}

/* 0x46b710: a binary value (REG_BINARY). */
int32_t GAMEAPI reg_binary_write(uint32_t name, uint32_t data, int32_t size)
{
    return reg_value_write(name, data, size, 3);
}

/* 0x46b730: binary value `name` of key `key` (created); 0 or -1. */
int32_t GAMEAPI reg_binary_set(uint32_t key, uint32_t name, uint32_t data, int32_t size)
{
    int32_t r = -1;
    if (reg_key_create(key) == -1)
        return -1;
    if (reg_binary_write(name, data, size) != -1)
        r = 0;
    reg_key_close();
    return r;
}

/* 0x46b770: deletes value `name` of key `key`; 0 or -1. */
int32_t GAMEAPI reg_value_remove(uint32_t key, uint32_t name)
{
    int32_t r = -1;
    if (reg_key_open(key) == -1)
        return -1;
    if (reg_value_delete(name) != -1)
        r = 0;
    reg_key_close();
    return r;
}

/* 0x46b7d0: CloseHandle(h) (if h), error into 0x4d3dcc; 0 or -1. */
int32_t GAMEAPI win_handle_close(uint32_t h)
{
    if (h && !CloseHandle_(h)) {
        G32(0x4d3dcc) = GetLastError_();
        return -1;
    }
    G32(0x4d3dcc) = 0;
    return 0;
}

/* 0x46b800: *out = CreateMutexA(NULL, FALSE, name); -1 failed, 1 it
 * already existed (ERROR_ALREADY_EXISTS), else 0. */
int32_t GAMEAPI win_mutex_create(uint32_t out, uint32_t name)
{
    uint32_t h = (uint32_t)CreateMutexA_(0, 0, name);
    G32(0x4d3dcc) = GetLastError_();
    if (out)
        G32(out) = h;
    if (!h)
        return -1;
    return G32(0x4d3dcc) == 0xb7;
}

/* 0x46b850: closes the game's mutex 0x4d3dc8 (the handle is kept, sic). */
int32_t GAMEAPI app_mutex_close(void)
{
    if (G32(0x4d3dc8))
        return win_handle_close(G32(0x4d3dc8));
    return 0;
}

/* 0x46b870: the game's single-instance mutex. */
int32_t GAMEAPI app_mutex_create(uint32_t name)
{
    app_mutex_close();
    return win_mutex_create(0x4d3dc8, name);
}

/* 0x46b890: CloseHandle(h) (if h); 0 or -1. */
int32_t GAMEAPI win_handle_close_quiet(uint32_t h)
{
    if (h && !CloseHandle_(h))
        return -1;
    return 0;
}

/* 0x46b8b0: *out = CreateEventA(NULL, manual reset, not signalled, name),
 * error into 0x4d3dd0; -1 failed, 1 already existed, else 0. */
int32_t GAMEAPI win_event_create(uint32_t out, uint32_t name)
{
    uint32_t h = (uint32_t)CreateEventA_(0, 1, 0, name);
    G32(0x4d3dd0) = GetLastError_();
    if (out)
        G32(out) = h;
    if (!h)
        return -1;
    return G32(0x4d3dd0) == 0xb7;
}

/* 0x46b900: *out = OpenEventA(EVENT_ALL_ACCESS, FALSE, name); 0 or -1. */
int32_t GAMEAPI win_event_open(uint32_t out, uint32_t name)
{
    uint32_t h = (uint32_t)OpenEventA_(0x1f0003, 0, name);
    G32(0x4d3dd0) = GetLastError_();
    if (out)
        G32(out) = h;
    return h ? 0 : -1;
}

/* 0x46b940 / 0x46b950: SetEvent / ResetEvent. */
int32_t GAMEAPI win_event_set(uint32_t h)
{
    return SetEvent_(h);
}

int32_t GAMEAPI win_event_reset(uint32_t h)
{
    return ResetEvent_(h);
}

/* 0x46b960: environment variable `name` into buf (cleared first; at most
 * size bytes, -1 = all); returns the length the first probe reports
 * (at least 2), -1 if unset. */
int32_t GAMEAPI env_var_get(uint32_t name, uint32_t buf, int32_t size)
{
    uint32_t probe = 0; /* the original probes into its `buf` argument slot */
    int32_t n, len = -1;
    if (buf)
        G8(buf) = 0;
    if (!name)
        return -1;
    n = GetEnvironmentVariableA_(name, &probe, 1);
    if (!n)
        return -1;
    len = (uint32_t)n <= 1 ? n + 1 : n;
    if (!buf)
        return len;
    n = size;
    if (n == -1 || n > len)
        n = len;
    if (n)
        GetEnvironmentVariableA_(name, buf, (uint32_t)n);
    return len;
}

/* 0x46b9c0: the length env_var_get reports. */
int32_t GAMEAPI env_var_length(uint32_t name)
{
    return env_var_get(name, 0, -1);
}

/* 0x46b9e0: 1 if environment variable `name` is set. */
int32_t GAMEAPI env_var_exists(uint32_t name)
{
    return env_var_length(name) != -1;
}

/* ===================================================================== */
/* Car shadows: car +0x720, a quad (+0x20 + view * 0x84) projected from the
 * four corners at +0 (8 bytes each: x / z for the transform, +7 alpha), or
 * subdivided 4x4 (0x46caa0) into 16 quads at +0x12c. */

/* 0x46c890: projects shadow s's quad for `view`: corners through the
 * transform pair (*0x6ccd78 / *0x6ccd80), grey alpha colours (code 0x8e),
 * prim pointer +0x128, depth bucket +0x96c from the nearest z (1..0x1ff,
 * compressed above 0x100). Returns the bucket. */
int32_t GAMEAPI shadow_project(uint32_t s, uint32_t view)
{
    uint32_t prim = s + view * 0x84 + 0x20, k;
    int32_t nearest = 0xffffff, d;
    for (k = 0; k < 4; k++) {
        uint32_t a;
        int32_t z;
        G32(0x6ccc64) = U32(s, 8 * k);
        G32(0x6ccc68) = U32(s, 8 * k + 4);
        a = (uint32_t)U8(s, 8 * k + 7) << 2;
        if ((int32_t)a > 0xff)
            a = 0xff;
        pn_transform_project(prim + 8 + 0x20 * k, prim + 4 + 0x20 * k,
                             ((((a | 0xffff8e00u) << 8) | a) << 8) | a);
        z = pn_ftol(U32(prim + 8 + 0x20 * k, 4));
        if (z < nearest)
            nearest = z;
    }
    if (nearest == 0xffffff || nearest <= 0)
        nearest = 1;
    d = SAR(nearest, 5) - 4;
    if (d < 1) {
        d = 1;
    } else if (d > 0x100) {
        d = d / 2 + 0x80;
        if (d > 0x180) {
            d = d / 2 + 0xc0;
            if (d > 0x1ff)
                d = 0x1ff;
        }
    }
    U32(s, 0x128) = prim;
    I32(s, 0x96c) = d;
    return d;
}

/* 0x46c840: every listed shadow (0x4d3de4, count 0x4d3dfc): prim cleared,
 * projected when shown (+6). */
void GAMEAPI shadows_draw(void)
{
    uint32_t view = G32(0x6ce60c) != 0x6cd4a0, n = G32(0x4d3dfc), list = 0x4d3de4;
    for (; n != 0; n--, list += 4) {
        uint32_t s = G32(list);
        U32(s, 0x128) = 0;
        if (U8(s, 6))
            shadow_project(s, view);
    }
}

/* 0x46caa0: shadow s's projected quad (+0x128) cut into 4x4 quads at
 * +0x12c (0x84 each): a 5x5 grid of vertices interpolated in steps of a
 * quarter (0x4a33d4) and projected (0x4288c0), colours and texture
 * coordinates interpolated and rounded (fistp), texture page `tpage`. */
void GAMEAPI shadow_subdivide(uint32_t s, uint32_t tpage)
{
    uint32_t P = U32(s, 0x128), out = s + 0x12c, q = G32(0x4a33d4);
    uint32_t rec[25][6], U[25], V[25], R[25], G[25], B[25];
    uint32_t dxr, dyr, dzr, dur, dvr, dRr, dGr, dBr, dxc, dyc, dzc, duc, dvc, dRc, dGc, dBc;
    int r, c, k;
    /* The projection leaves a near vertex's screen x / y (+8 / +0xc)
     * unwritten: stack leftovers in the original, 0 here. */
    for (r = 0; r < 25; r++)
        for (k = 0; k < 6; k++)
            rec[r][k] = 0;
    dxr = pn_sub_mul(U32(P, 0x58), U32(P, 0x18), q);
    dyr = pn_sub_mul(U32(P, 0x5c), U32(P, 0x1c), q);
    dzr = pn_sub_mul(U32(P, 0x4c), U32(P, 0xc), q);
    dur = pn_mul(pn_from_int(U8(P, 0x60) - U8(P, 0x20)), q);
    dvr = pn_mul(pn_from_int(U8(P, 0x61) - U8(P, 0x21)), q);
    dRr = pn_mul(pn_from_int(U8(P, 0x44) - U8(P, 4)), q);
    dGr = pn_mul(pn_from_int(U8(P, 0x45) - U8(P, 5)), q);
    dBr = pn_mul(pn_from_int(U8(P, 0x46) - U8(P, 6)), q);
    dxc = pn_sub_mul(U32(P, 0x38), U32(P, 0x18), q);
    dyc = pn_sub_mul(U32(P, 0x3c), U32(P, 0x1c), q);
    dzc = pn_sub_mul(U32(P, 0x2c), U32(P, 0xc), q);
    duc = pn_mul(pn_from_int(U8(P, 0x40) - U8(P, 0x20)), q);
    dvc = pn_mul(pn_from_int(U8(P, 0x41) - U8(P, 0x21)), q);
    dRc = pn_mul(pn_from_int(U8(P, 0x24) - U8(P, 4)), q);
    dGc = pn_mul(pn_from_int(U8(P, 0x25) - U8(P, 5)), q);
    dBc = pn_mul(pn_from_int(U8(P, 0x26) - U8(P, 6)), q);
    for (r = 0; r < 5; r++) {
        uint32_t t = pn_from_int(r);
        int i = r * 5;
        rec[i][4] = pn_mul_add(t, dxr, U32(P, 0x18));
        rec[i][5] = pn_mul_add(t, dyr, U32(P, 0x1c));
        rec[i][1] = pn_mul_add(t, dzr, U32(P, 0xc));
        U[i] = pn_mul_add(t, dur, pn_from_int(U8(P, 0x20)));
        V[i] = pn_mul_add(t, dvr, pn_from_int(U8(P, 0x21)));
        R[i] = pn_mul_add(t, dRr, pn_from_int(U8(P, 4)));
        G[i] = pn_mul_add(t, dGr, pn_from_int(U8(P, 5)));
        B[i] = pn_mul_add(t, dBr, pn_from_int(U8(P, 6)));
        vertex_project_codes_at(rec[i]);
        for (c = 1; c < 5; c++) {
            i = r * 5 + c;
            rec[i][4] = pn_add(dxc, rec[i - 1][4]);
            rec[i][5] = pn_add(dyc, rec[i - 1][5]);
            rec[i][1] = pn_add(dzc, rec[i - 1][1]);
            U[i] = pn_add(duc, U[i - 1]);
            V[i] = pn_add(dvc, V[i - 1]);
            R[i] = pn_add(dRc, R[i - 1]);
            G[i] = pn_add(dGc, G[i - 1]);
            B[i] = pn_add(dBc, B[i - 1]);
            vertex_project_codes_at(rec[i]);
        }
    }
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++, out += 0x84) {
            int i = r * 5 + c, at[4] = {i, i + 1, i + 5, i + 6};
            for (k = 0; k < 6; k++) {
                U32(out, 8 + 4 * k) = rec[i][k];
                U32(out, 0x28 + 4 * k) = rec[i + 1][k];
                U32(out, 0x48 + 4 * k) = rec[i + 5][k];
                U32(out, 0x68 + 4 * k) = rec[i + 6][k];
            }
            for (k = 0; k < 4; k++)
                U8(out, 0x20 + 0x20 * k) = (uint8_t)pn_fist(U[at[k]]);
            for (k = 0; k < 4; k++)
                U8(out, 0x21 + 0x20 * k) = (uint8_t)pn_fist(V[at[k]]);
            for (k = 0; k < 4; k++)
                U8(out, 4 + 0x20 * k) = (uint8_t)pn_fist(R[at[k]]);
            for (k = 0; k < 4; k++)
                U8(out, 5 + 0x20 * k) = (uint8_t)pn_fist(G[at[k]]);
            for (k = 0; k < 4; k++)
                U8(out, 6 + 0x20 * k) = (uint8_t)pn_fist(B[at[k]]);
            U8(out, 7) = 0x8e;
            U16(out, 0x42) = (uint16_t)tpage;
            U32(out, 0) = (U32(out, 0) & 0xffffff) | 0xc000000;
        }
}

/* 0x46c9d0: car's shadow into the frame: when shadows are on (0x6ccd84 &
 * 2) and projected, either subdivided (0x6cce84) into depth bucket
 * 0x6cdd80[car +0x14b4 >> 5, compressed to 1..0x1ff], or the quad itself
 * into ordering table entry ot. */
void GAMEAPI shadow_add(uint32_t car, uint32_t ot)
{
    uint32_t prim = U32(car, 0x848), s = car + 0x720, o, k;
    int32_t d;
    if (!(G8(0x6ccd84) & 2) || !prim)
        return;
    if (!G32(0x6cce84)) {
        o = ot & 0x7fffffffu;
        U32(prim, 0) = G32(o);
        G32(o) = prim;
        return;
    }
    d = SAR(I32(car, 0x14b4), 5);
    if (d > 0x100) {
        d = d / 2 + 0x80;
        if (d > 0x180) {
            d = d / 2 + 0xc0;
            if (d > 0x1ff)
                d = 0x1ff;
        }
    }
    if (d < 1)
        d = 1;
    shadow_subdivide(s, U16(prim, 0x42));
    for (k = 0; k < 16; k++) {
        uint32_t sub = s + 0x12c + k * 0x84;
        o = G32(0x6cdd80 + 4 * (uint32_t)d) & 0x7fffffffu;
        U32(sub, 0) = G32(o);
        G32(o) = sub;
    }
}
