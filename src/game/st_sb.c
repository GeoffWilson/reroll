/* Startup, group SB: the parts of the startup tree (main 0x4351e0) that are
 * game logic rather than platform code.
 *
 *  - the startup movies (0x4359f1): whether to show them (0x444ce0), the
 *    movie file path (0x42d5b0), the credits and caption texts that go over
 *    the movie (0x42dcb0, 0x444d00, 0x42da80) and their release (0x42dc60);
 *  - the text width of the font map (0x488c30) and the font map itself
 *    (0x443a10, 0x443780): DirectDraw set-up (0x4885d0), the sound engine
 *    (0x46f720, 0x46f000-0x473fff), the registry helpers (0x46b2xx-0x46b7xx),
 *    the movie player (0x492b90-0x495958), the renderer and the loading
 *    screen picture (0x436c2c) stay original and are called by address;
 *  - back to the front end after a race (0x435607) and the race set-up it
 *    ends with (0x432ec0 and the player / car / option picking under it);
 *  - the archive file layer (0x43b550..0x43b7f0: files held in memory, else
 *    the OS file functions), the replay file loader (0x41c400), and the boot
 *    data copy to and from the registry (0x474030..0x4746b0). */
#include "../runtime.h"
#include "gamefn.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))

#define g_race GP(0x4b1960)
#define g_set GP(0x4b2198) /* the settings block */

/* Not ours. Game addresses may be passed as integers or pointers. */
#define mem_alloc(n) ((uint8_t *)GCALL(0x4982c2, msvc_malloc)(n))
#define mem_free(p) GCALL(0x49827a, msvc_free)(GMEM(p))
#define string_load(buf, size, name, index) /* -1 when missing */ \
    GCALL(0x42e6d0, text_line_get)((uint8_t *)GMEM(buf), size, (const char *)GMEM(name), index)
#define file_exists(path) GCALL(0x475070, path_access_ok)((const char *)GMEM(path))
#define registry_read(key, value, buf, size) \
    GCALL(0x46b650, reg_string_get)(key, value, (uint32_t)(uintptr_t)(buf), size)
#define path_fix(dst, src) GCALL(0x474ed0, path_dir_make)((uint8_t *)GMEM(dst), (const uint8_t *)GMEM(src))
#define sprintf_(buf, format, v) GCALL(0x4979f0, msvc_sprintf)((char *)GMEM(buf), (const char *)GMEM(format), v)
#define debug_nop GCALL(0x4291f0, dx_error_ignore)
#define movie_prepare GCALL(0x487830, dd_primary_clear)
#define movie_run(path, window, config) /* (path, window, config) */ \
    GCALL(0x493210, movie_player_run)((uint32_t)(uintptr_t)(path), window, config)
#define sound_direct GCALL(0x46f720, snd_dsound)
#define display_start GCALL(0x4885d0, dd_start_2d)
#define display_stop GCALL(0x487210, dd_shutdown)
#define screen_flip GCALL(0x4876f0, flip_body)
#define screen_fill GCALL(0x41e040, viewport_clear)
#define screen_ready GCALL(0x41e170, d3d_scene_begin)
#define screen_done GCALL(0x41e190, d3d_scene_end)
#define loading_picture GCALL(0x436c2c, loading_picture_draw)
#define screen_end GCALL(0x4205e0, texture_slots_release_thunk)
#define image_load(name, image, flags) \
    GCALL(0x477530, image_bmp_load)((const char *)GMEM(name), (uint8_t *)GMEM(image), flags)
#define image_free(image) GCALL(0x477810, image_release)((uint8_t *)GMEM(image))
#define arena_state GCALL(0x496510, archive_select)
#define arena_get GCALL(0x496680, archive_state)
#define archive_load_entry(index, buf) \
    ((uint32_t)(uintptr_t)GCALL(0x496590, archive_entry_load)((int32_t)(index), (uint8_t *)GMEM(buf)))
#define arena_release(block) GCALL(0x496660, archive_free)(GMEM(block))
#define archive_name_equal(a, b) GCALL(0x43b5a0, archive_name_equal)((const char *)GMEM(a), (const char *)GMEM(b))
#define file_cache_init() GCALL(0x43b730, file_cache_init)()
#define os_file_close GCALL(0x476e10, os_handle_close)
#define os_file_read(handle, buf, size) GCALL(0x476e20, os_handle_read)(handle, GMEM(buf), size)
#define os_file_open(name) GCALL(0x476c30, os_file_open_read)((const char *)GMEM(name))
#define os_file_size GCALL(0x476e80, os_handle_size)
#define fe_time() GCALL(0x436935, fe_time)()
#define world_number GCALL(0x433570, track_world_number)
#define world_place GCALL(0x4335c0, track_world_place)
#define world_track(world, place) GCALL(0x4335e0, world_track)(world, place)
#define world_track_count(world) GCALL(0x448670, world_track_count)(world)
#define world_locked_tracks(world) GCALL(0x4486b0, world_locked_tracks)(world)
#define track_title_text(out, world, track) GCALL(0x4485b0, track_title_text)((char *)GMEM(out), world, track)
#define sound_reset GCALL(0x472f70, sound_engine_restart)
#define screen_reset GCALL(0x496510, archive_select)
#define view_scale GCALL(0x426200, gte_screen_offset_set)
#define view_project GCALL(0x417ed0, frustum_body)
#define view_clip GCALL(0x41f2d0, viewport_body)

/* Defined below (or by another group's hook, called by address). */
void GAMEAPI movie_texts_free(void);
void GAMEAPI movie_text_add(int32_t side, uint32_t y, const char *text);
void GAMEAPI font_image_info(uint32_t *pixels, uint32_t *palette, int32_t *stride);
int32_t GAMEAPI boot_data_save(void);
int32_t GAMEAPI race_init(void);
void GAMEAPI flags_5d622c_clear(void);
int32_t GAMEAPI replay_load(void);
int32_t GAMEAPI replay_save(void);

#define STR_BACKSLASH ((const char *)0x4ac784) /* "\\" */
#define STR_UP ((const char *)0x4ac788)        /* "..\\" */
#define STR_MOVIES ((const char *)0x4ac78c)    /* "movies" */

/* --- movies --- */

/* strcat, as the original: the terminator is copied too. */
static void str_cat(char *dst, const char *src)
{
    while (*dst)
        dst++;
    while ((*dst++ = *src++) != 0)
        ;
}

/* 0x42d5b0: the path of a movie file: "..\<dir>\<name>" (or just the name
 * when dir is null) at 0x4be1b0; when that file isn't there, the same under
 * the install folder from the registry (Installer\Source + "Rollcage\IDXData\")
 * in the buffer 0x5ff0c0, and that one if it exists. Returns 0x4be1b0. */
char *GAMEAPI movie_path(const char *dir, const char *name)
{
    char *path = (char *)0x4be1b0, *install = (char *)0x5ff0c0;

    path[0] = 0;
    if (dir) {
        str_cat(path, STR_UP);
        str_cat(path, dir);
        str_cat(path, STR_BACKSLASH);
    }
    str_cat(path, name);
    if (file_exists(path) == 0) {
        if (registry_read(0x4ac754, 0x4ac77c, install, 0x104) != -1) {
            path_fix(install, install);
            str_cat(install, (const char *)0x4ac740);
            if (dir) {
                str_cat(install, STR_UP);
                str_cat(install, dir);
                str_cat(install, STR_BACKSLASH);
            }
            str_cat(install, name);
            if (file_exists(install) != 0) {
                char *d = path;
                const char *s = install;

                while ((*d++ = *s++) != 0)
                    ;
            }
        }
    }
    return path;
}

/* 0x42d7e0: play a movie file if it exists: the movie player (0x493210) gets
 * a 0x28-byte config on the stack: flags (0x18c04, or 0x18c0c when no texts
 * are queued; 0x10 for a screen 640 wide or more; screens 400 high or more
 * set 0x4be2bc and add 0x20, or 0x60 more when the setting +0x39 is set;
 * 0x1000 when the sound engine has a DirectSound), the two sound pointers
 * 0x4d5f40 / 0x4d5f48, the sound's DirectSound (0x46f720) and the movie
 * player's callback (0x42d8e0). Then the queued texts are released. */
void GAMEAPI movie_play(const char *path)
{
    uint32_t config[10], flags, i;

    G32(0x4be2bc) = 0;
    G32(0x4be2b4) = 0;
    G32(0x4ac738) = 0xffffffff;
#ifdef REROLL_NATIVE
    if (0) { /* no movie player yet in the native build (docs/native/plan.md step 8): as if the file were missing */
#else
    if (file_exists(path) != 0) {
#endif
        uint32_t direct;

        flags = 0x18c04;
        for (i = 0; i < 10; i++)
            config[i] = 0;
        if (G32(0x4be2b8) == 0)
            flags = 0x18c0c;
        if (GI32(0x4acc38) >= 0x280)
            flags |= 0x10;
        if (GI32(0x4acc3c) >= 400) {
            G32(0x4be2bc) = 1;
            flags |= (-(uint32_t)(g_set[0x39] != 0) & 0x60) + 0x20;
        }
        direct = (uint32_t)sound_direct();
        config[5] = direct;
        if (direct != 0)
            flags |= 0x1000;
        config[9] = 0x42d8e0;
        config[3] = G32(0x4d5f40);
        config[4] = G32(0x4d5f48);
        config[0] = flags;
        movie_prepare();
        debug_nop();
        movie_run(path, G32(0x6c8000), config);
        debug_nop();
    }
    movie_texts_free();
}

/* 0x42da20, 0x42da40, 0x42da60: the movies psyg.fmv, atd.fmv and intro.fmv. */
void GAMEAPI movie_psygnosis(void)
{
    movie_play(movie_path(STR_MOVIES, (const char *)0x4ac794));
}

void GAMEAPI movie_atd(void)
{
    movie_play(movie_path(STR_MOVIES, (const char *)0x4ac7a0));
}

void GAMEAPI movie_intro(void)
{
    movie_play(movie_path(STR_MOVIES, (const char *)0x4ac7a8));
}

/* 0x42da80: the credits: the 64 lines "CREDITS" 0..63 of the text file (0x42e6d0)
 * are copied into allocated buffers, queued in pairs (even lines on the left,
 * odd on the right) 2000 / (last line / 2 + 2) apart from 10 down, over the
 * credits movie; the buffers are freed after. */
void GAMEAPI credits_play(void)
{
    uint8_t used[0x42];
    char *lines[0x42];
    int32_t i, half = 0, y, step;

    for (i = 0; i < 0x42; i++) {
        used[i] = 0;
        lines[i] = 0;
    }
    for (i = 0; i < 0x40; i++) {
        char *p = (char *)mem_alloc(0x80);

        if (p) {
            if (string_load(p, 0x80, 0x4ac7c0, i) == -1)
                *p = 0;
            else {
                used[i] = 1;
                half = i / 2;
            }
        }
        lines[i] = p;
    }
    step = 2000 / (half + 2);
    y = 10;
    for (i = 0; i < 0x40; i += 2) {
        if (used[i])
            movie_text_add(0, (uint32_t)y, lines[i]);
        if (used[i + 1])
            movie_text_add(1, (uint32_t)y, lines[i + 1]);
        y += step;
    }
    movie_play(movie_path(STR_MOVIES, (const char *)0x4ac7b4));
    for (i = 0; i < 0x40; i++)
        if (lines[i])
            mem_free(lines[i]);
}

/* 0x42db90, 0x42dbb0, 0x42dbd0: coploses.fmv, copchal.fmv and expert.fmv. */
void GAMEAPI movie_cop_loses(void)
{
    movie_play(movie_path(STR_MOVIES, (const char *)0x4ac7c8));
}

void GAMEAPI movie_cop_challenge(void)
{
    movie_play(movie_path(STR_MOVIES, (const char *)0x4ac7d8));
}

void GAMEAPI movie_expert(void)
{
    movie_play(movie_path(STR_MOVIES, (const char *)0x4ac7e4));
}

/* 0x42dbf0: the winner's movie by language 0..6 (UK, Italian, German,
 * Japanese, American, French, Polish). */
void GAMEAPI movie_win(int32_t language)
{
    static const uint32_t names[7] = {0x4ac838, 0x4ac82c, 0x4ac820, 0x4ac814, 0x4ac808, 0x4ac7fc, 0x4ac7f0};

    if (language >= 0 && language <= 6)
        movie_play(movie_path(STR_MOVIES, (const char *)(uintptr_t)names[language]));
}

/* 0x42dc60: free the queued movie texts (16-byte records at 0x6cc400, count
 * 0x4be2b8) and empty the queue. */
void GAMEAPI movie_texts_free(void)
{
    int32_t i = 0;
    uint32_t *p = (uint32_t *)0x6cc40c;

    if (GI32(0x4be2b8) > 0) {
        do {
            if (*p)
                mem_free(*p);
            i++;
            p += 4;
        } while (i < GI32(0x4be2b8));
    }
    G32(0x4be2b8) = 0;
}

/* 0x488c30: the width of a string in the font map: 0x6c0a70 for a space,
 * else the cell width (0x6c0a80 + 0x10 * cell, via the character map
 * 0x6c1a7c) + the gap 0x6c0a74 for each known character. */
int32_t GAMEAPI text_width(const uint8_t *text)
{
    uint32_t pixels, image, stride;
    int32_t width = 0;

    font_image_info(&pixels, &image, (int32_t *)&stride);
    for (; *text; text++) {
        if (*text == 0x20)
            width += GI32(0x6c0a70);
        else if (G8(0x6c1a7c + *text) != 0xff)
            width += *(uint16_t *)(uintptr_t)(0x6c0a80 + G8(0x6c1a7c + *text) * 0x10) + GI32(0x6c0a74);
    }
    return width;
}

/* 0x42dcb0: queue a movie text (side 0 left, 1 right; y; the text is copied):
 * record 0x6cc400 + 0x10 * count: +0 x (centred in the screen width), +4 side
 * * 25, +8 y, +0xc the copy (null when there is none or no memory). */
void GAMEAPI movie_text_add(int32_t side, uint32_t y, const char *text)
{
    uint32_t *rec;
    char *copy = 0;
    int32_t n = GI32(0x4be2b8), width;

    if (n >= 0x40)
        return;
    if (text) {
        uint32_t len = 0;

        while (text[len])
            len++;
        len++;
        copy = (char *)mem_alloc(len);
        if (copy) {
            uint32_t i;

            for (i = 0; i < len; i++)
                copy[i] = text[i];
        }
    }
    n = GI32(0x4be2b8);
    rec = (uint32_t *)0x6cc400 + n * 4;
    rec[3] = (uint32_t)(uintptr_t)copy;
    rec[2] = y;
    width = text_width((const uint8_t *)text);
    rec[0] = (uint32_t)((GI32(0x4acc38) >> 1) - (width >> 1));
    rec[1] = (uint32_t)(side * 25);
    G32(0x4be2b8) = (uint32_t)GI32(0x4be2b8) + 1;
}

/* --- when the movies show --- */

/* 0x444c50: which intro movies to show: 1 (logos and intro) when nothing
 * stops them (0x4d4650 clear, settings +0xc and +0xe clear); 2 (intro only)
 * when the race says so (race +0x58c == 1, settings +0x3b low bits clear,
 * +0xc and +0xe clear); else 0. */
int32_t GAMEAPI movie_mode(void)
{
    if (G32(0x4d4650) == 0 && g_set[0xc] == 0 && g_set[0xe] == 0)
        return 1;
    if (g_race[0x58c] == 1 && (g_set[0x3b] & 3) == 0 && g_set[0xc] == 0 && g_set[0xe] == 0)
        return 2;
    return 0;
}

/* 0x444ca0: whether the end-credits texts show: 0x4d4658 set, 0x4cb570
 * clear, settings +0xe clear and +0x9c set. */
int32_t GAMEAPI credits_wanted(void)
{
    if (G32(0x4d4658) != 0 && G32(0x4cb570) == 0 && g_set[0xe] == 0 && g_set[0x9c] != 0)
        return 1;
    return 0;
}

/* 0x444ce0: any movies at all. */
int32_t GAMEAPI movies_wanted(void)
{
    if (movie_mode() != 0)
        return 1;
    return credits_wanted();
}

/* 0x444d00: queue the texts over the movies when settings +0x9c is set: with
 * 0x4cb560 the two title lines (+ the cop movies chosen by 0x4cb564 /
 * 0x4cb568 / 0x4cb56c), then the four caption lines, the winner's movie for
 * the race's language (race +0x1c), and the credits when 0x4cb560. */
void GAMEAPI movie_texts_build(void)
{
    if (g_set[0x9c] == 0)
        return;
    if (G32(0x4cb560) != 0) {
        movie_text_add(0, 10, (const char *)0x4ca7ac);
        movie_text_add(1, 10, (const char *)0x4ca72c);
        if (G32(0x4cb564) != 0)
            movie_cop_challenge();
        if (G32(0x4cb568) != 0)
            movie_cop_loses();
        if (G32(0x4cb56c) != 0)
            movie_expert();
    }
    movie_text_add(0, 10, (const char *)0x4ca5ac);
    movie_text_add(1, 10, (const char *)0x4ca52c);
    movie_text_add(0, 0x190, (const char *)0x4ca6ac);
    movie_text_add(1, 0x190, (const char *)0x4ca62c);
    movie_win(g_race[0x1c]);
    if (G32(0x4cb560) != 0)
        credits_play();
}

/* --- the font map (FONT.BMP, 16 cells wide) --- */

/* 0x443260: the first row (index) of count rows, `step` apart from start,
 * that has a non-zero byte among `width` bytes `across` apart; -1 for none. */
int32_t GAMEAPI bitmap_scan(const uint8_t *start, int32_t step, int32_t count, int32_t across, int32_t width)
{
    int32_t index = 0;

    for (;;) {
        const uint8_t *p = start;
        int32_t k = width;

        if (count < 1)
            return -1;
        count--;
        while (k > 0) {
            k--;
            if (*p != 0)
                return index;
            p += across;
        }
        start += step;
        index++;
    }
}

/* 0x443780: load a font bitmap (0x477530 into the image at 0x4c2408), make its
 * alpha table (0x4c2004: a palette entry is blank when its average is below
 * 15; blank pixels become 0), then measure `count` cells (`cell` wide, from
 * row `row`, `rows` high; count 0: the image width / cell): for each cell
 * the blank margins left / right / top / bottom give a 16-byte record in
 * `out` at +0x1c: x, y, width, height (16-bit each). out +8 gets the tallest
 * cell's height span, +0xc the count. Returns 0 when the image won't load. */
int32_t GAMEAPI font_cells_build(const char *name, int32_t cell, uint8_t *out, int32_t row, int32_t rows, int32_t count)
{
    uint8_t *palette, *pixels, *table = (uint8_t *)0x4c2004;
    int32_t i, n, high, min_top, max_bottom, index, x;
    int16_t *rec;

    G32(0x4c241c) = 0;
    if (image_load(name, 0x4c2408, 1) == 0)
        return 0;
    palette = GP(0x4c2418);
    for (i = 0; i < 0x100; i++) {
        uint32_t s0 = palette[i * 4], s1 = palette[i * 4 + 1], s2 = palette[i * 4 + 2];

        table[i * 4] = (uint8_t)s0;
        table[i * 4 + 1] = (uint8_t)s1;
        table[i * 4 + 2] = (uint8_t)s2;
        table[i * 4 + 3] = 0;
        if ((s1 + s2 + s0) >> 2 < 0xf)
            G32(0x4c2004 + i * 4) = 0;
    }
    n = (int32_t)(int16_t)G32(0x4c240e) * (int32_t)(int16_t)G32(0x4c240c);
    G32(0x4c2004) = 0;
    pixels = GP(0x4c241c);
    for (i = 0; i < n; i++)
        if (G32(0x4c2004 + pixels[i] * 4) == 0)
            pixels[i] = 0;
    high = (int16_t)G32(0x4c240e);
    if (high <= row)
        row = high - 1;
    if (high < rows + row)
        rows = high - row;
    if (count == 0)
        count = (int32_t)(int16_t)G32(0x4c240a) / cell;
    min_top = 0x8000;
    max_bottom = -0x8000;
    index = 0;
    x = 0;
    rec = (int16_t *)(out + 0x1e);
    for (; index < count; index++, x += cell, rec += 8) {
        int32_t stride = (int16_t)G32(0x4c240c);
        const uint8_t *p = GP(0x4c241c) + stride * row + x;
        int32_t a = bitmap_scan(p, 1, cell - 1, stride, rows - 1);
        int32_t b = bitmap_scan(p + (cell - 1), -1, cell - 1, stride, rows - 1);
        int32_t right = cell - b - 1;
        int32_t top = bitmap_scan(p, stride, rows - 1, 1, cell - 1);
        int32_t bottom = bitmap_scan(p + (rows - 1) * stride, -stride, rows - 1, 1, cell - 1);

        bottom = rows - bottom - 1;
        if (a == -1) {
            a = 0;
            bottom = 1;
            right = cell / 2;
            top = 0;
        }
        if (bottom > max_bottom)
            max_bottom = bottom;
        if (top < min_top)
            min_top = top;
        rec[-1] = (int16_t)(cell * index + a);
        rec[0] = (int16_t)(row + top);
        rec[1] = (int16_t)(right - a + 1);
        rec[2] = (int16_t)(bottom - top + 1);
    }
    I32(out, 8) = max_bottom - min_top;
    I32(out, 0xc) = count;
    return 1;
}

/* 0x443a10: the font map: FONT.BMP (16-wide cells from row 0x36, 100 rows,
 * 0x46 cells) into the record at 0x6c0a60, the palette converted (0x477460),
 * and the character map 0x6c1a7c (a byte per character, 0xff for none): A-Z
 * are cells 0-25, a-z 26-51, 0-9 52-61, then the nine characters at
 * 0x4af5a0 (62 on). The space is 5 wide, the gap 1. */
void GAMEAPI font_map_init(void)
{
    uint8_t extra[12];
    uint8_t *map = (uint8_t *)0x6c1a7c;
    uint32_t cell = 0, i;

    if (font_cells_build((const char *)0x4ac860, 0x10, (uint8_t *)0x6c0a60, 0x36, 100, 0x46) == 0)
        return;
    GCALL(0x477460, image_palette_to_16)((uint8_t *)GMEM(0x4c2408));
    *(uint32_t *)extra = G32(0x4af5a0);
    *(uint32_t *)(extra + 4) = G32(0x4af5a4);
    extra[8] = G8(0x4af5a8);
    for (i = 0; i < 0x100; i++)
        map[i] = 0xff;
    for (i = 0x41; i <= 0x5a; i++)
        map[i] = (uint8_t)cell++;
    for (i = 0x61; i <= 0x7a; i++)
        map[i] = (uint8_t)cell++;
    for (i = 0x30; i <= 0x39; i++)
        map[i] = (uint8_t)cell++;
    for (i = 0; i < 9; i++)
        map[extra[i]] = (uint8_t)cell++;
    G32(0x6c0a60) = 0;
    G32(0x6c0a64) = 0x100;
    G32(0x6c0a78) = 0;
    G32(0x6c0a70) = 5;
    G32(0x6c0a74) = 1;
}

/* 0x443b00: release the font image if one is loaded. */
void GAMEAPI font_image_release(void)
{
    if (G32(0x4c241c) != 0)
        image_free(0x4c2408);
}

/* 0x443b20: the font image's pixels (0x4c241c), palette (0x4c2420) and
 * stride (16-bit at 0x4c240c). */
void GAMEAPI font_image_info(uint32_t *pixels, uint32_t *palette, int32_t *stride)
{
    *pixels = G32(0x4c241c);
    *palette = G32(0x4c2420);
    *stride = (int16_t)G32(0x4c240c);
}

/* --- startup, return to the front end --- */

/* 0x4359f1: the startup movies: nothing (1) unless 0x444ce0; the display set-up
 * (0x4885d0: 640x480, 16 bit, windowed when settings +0x39 is clear) failing
 * returns 0; else the font map, the logos and intro by 0x444c50, the credits
 * texts, the display shut down (0x487210) and the screen size restored. */
int32_t GAMEAPI startup_movies(void)
{
    uint32_t width, height;
    int32_t mode;

    if (movies_wanted() == 0)
        return 1;
    width = G32(0x4acc38);
    height = G32(0x4acc3c);
    if (display_start(G32(0x4acc5c), G32(0x4acc60), 0x280, 0x1e0, 1, g_set[0x39] == 0, 0x10) == 0)
        return 0;
    font_map_init();
    mode = movie_mode();
    if (mode == 1) {
        movie_psygnosis();
        movie_atd();
        movie_intro();
    } else if (mode == 2)
        movie_intro();
    if (credits_wanted() != 0)
        movie_texts_build();
    font_image_release();
    display_stop();
    G32(0x4acc38) = width;
    G32(0x4acc3c) = height;
    return 1;
}

/* 0x436b9d: clear the screen: two buffers filled (0x41e040) and flipped
 * (0x4876f0), then two more drawn with the loading picture (0x436c2c) when
 * the screen is ready, the screen ended (0x4205e0), the loading image freed. */
void GAMEAPI loading_screen_show(uint32_t kind)
{
    int32_t i;

    for (i = 0; i < 2; i++) {
        screen_fill(0);
        screen_flip();
    }
    for (i = 0; i < 2; i++) {
        if (screen_ready() != 0) {
            loading_picture(kind);
            screen_done();
        }
        screen_flip();
    }
    screen_end();
    if (G32(0x4be490) != 0) {
        image_free(0x4be398);
        G32(0x4be490) = 0;
    }
}

/* 0x435607: back to the front end after a race: screen done, arena state 0,
 * the sound engine reset, settings +0x48 / +0x4c = -1, the boot data saved
 * (0x474260), the front end flags (0x4d46f4 = 0, 0x4be450 = 0, 0x4d4654 =
 * 0x4d4660 = 1, 0x4ac984 = 0), 0x5d622c low bits cleared (or 0x4be47c set when
 * settings +0x82), the race set up (0x432ec0); true unless that said -1. */
int32_t GAMEAPI front_end_return(void)
{
    int32_t r;

    screen_done();
    arena_state(0);
    sound_reset();
    I32(g_set, 0x48) = -1;
    I32(g_set, 0x4c) = -1;
    boot_data_save();
    G32(0x4d46f4) = 0;
    G32(0x4be450) = 0;
    G32(0x4d4654) = 1;
    G32(0x4d4660) = 1;
    G32(0x4ac984) = 0;
    if (g_set[0x82] == 0)
        flags_5d622c_clear();
    else
        G32(0x4be47c) = 1;
    r = race_init();
    if (r != -1)
        screen_end();
    return r != -1;
}

/* 0x48d370: the low two bits of 0x5d622c cleared. */
void GAMEAPI flags_5d622c_clear(void)
{
    G32(0x5d622c) = G32(0x5d622c) & 0xfffffffc;
}

/* 0x4483c0: settings +5 steps 0, 1, 2 round; 0x4cb58c = 1. */
void GAMEAPI setting5_next(void)
{
    g_set[5] = (uint8_t)(g_set[5] + 1);
    if (g_set[5] > 2)
        g_set[5] = 0;
    G32(0x4cb58c) = 1;
}

/* 0x43605a: debug keys: 0x7a sets 0x4be49c, 0x7b toggles 0x4be454; with
 * 0x4beb3c set and the front end on (0x4ac984): 0x42 steps setting +5, 0x4a
 * toggles setting +8. */
void GAMEAPI debug_key_event(int32_t key)
{
    if (key == 0x7a)
        G32(0x4be49c) = 1;
    else if (key == 0x7b)
        G32(0x4be454) = G32(0x4be454) ^ 1;
    if (G32(0x4beb3c) != 0) {
        if (key == 0x42) {
            if (G32(0x4ac984) != 0)
                setting5_next();
        } else if (key == 0x4a) {
            if (G32(0x4ac984) != 0)
                g_set[8] = (uint8_t)(g_set[8] == 0);
        }
    }
}

/* 0x428850: set the 2D view to a rectangle (x, y, w, h): the centre (w / 2,
 * h / 2) at 0x426200, the projection set-up (0x417ed0, with the focal length
 * 0x6ccd00), the viewport (0x41f2d0). */
void GAMEAPI view_rect_set(int32_t x, int32_t y, int32_t w, int32_t h)
{
    view_scale((uint32_t)(w >> 1), (uint32_t)(h >> 1));
    view_project((float)x, (float)y, (float)w, (float)h, *(const float *)GMEM(0x6ccd00));
    view_clip((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
}

/* --- the archive file layer (files held in memory: table 0x6c25c0 of
 * {data, position}; names in the table at 0x4ad000) --- */

/* 0x43b550: the entry of a name in the archive table ({name, s16 entry}, 8
 * bytes each, count 0x4adaa0), or 0xffff. Only the low 16 bits of the result
 * mean anything; the upper half is what the original leaves in eax. */
uint32_t GAMEAPI archive_find_by_name(uint32_t name)
{
    int32_t i;
    uint32_t r = (uint32_t)GI32(0x4adaa0);

    for (i = 0; i < GI32(0x4adaa0); i++) {
        r = (uint32_t)archive_name_equal(name, G32(0x4ad000 + i * 8));
        if (r != 0)
            return (r & 0xffff0000) | *(uint16_t *)(uintptr_t)(0x4ad004 + i * 8);
        r = (uint32_t)GI32(0x4adaa0);
    }
    return (r & 0xffff0000) | 0xffff;
}

/* 0x43b630: load an archive entry into a fresh block, the arena state saved
 * (0x496680) and set to 0 (0x496510) round it. */
uint32_t GAMEAPI archive_load_by_index(uint32_t index)
{
    uint32_t saved = (uint32_t)arena_get();
    uint32_t block;

    arena_state(0);
    block = (uint32_t)archive_load_entry(index, 0);
    arena_state(saved);
    return block;
}

/* 0x43b6c0: open a file by name: an archive entry (loaded once, position
 * reset) or else the disk file (0x476c30). Returns the handle (the memory
 * block for an archive entry). */
uint32_t GAMEAPI file_open_any(uint32_t name)
{
    uint32_t found = archive_find_by_name(name);
    int32_t slot = (int16_t)found;

    if ((int16_t)found >= 0 && slot < GI32(0x4adaa4)) {
        uint32_t *entry = (uint32_t *)(uintptr_t)(0x6c25c0 + slot * 8);

        file_cache_init();
        if (entry[0] == 0)
            entry[0] = archive_load_by_index(found);
        entry[1] = 0;
        return entry[0];
    }
    return (uint32_t)os_file_open(name);
}

/* 0x43b7b0: the slot of a memory file handle in the table, or 0xffff (16
 * bits; 0xffff for a null handle). */
uint32_t GAMEAPI file_slot_find(uint32_t handle)
{
    int32_t i = 0, count;

    if (handle == 0)
        return 0xffff;
    file_cache_init();
    count = GI32(0x4adaa4);
    if (count > 0) {
        for (; i < count; i++)
            if (G32(0x6c25c0 + i * 8) == handle)
                return (uint32_t)i;
    }
    return ((uint32_t)i & 0xffff0000) | 0xffff;
}

/* 0x43b760: close a file: a memory file's block is released (0x496660) and
 * the slot emptied, else the OS handle closed. */
void GAMEAPI file_close_any(uint32_t handle)
{
    uint32_t found = file_slot_find(handle);
    int32_t slot = (int16_t)found;

    if ((int16_t)found >= 0 && slot < GI32(0x4adaa4)) {
        arena_release(G32(0x6c25c0 + slot * 8));
        G32(0x6c25c0 + slot * 8) = 0;
        return;
    }
    os_file_close(handle);
}

/* 0x43b7f0: read from a file: a memory file copies from its position (and
 * advances it; a null destination only skips), else the OS read. Returns 1
 * for a memory file. */
uint32_t GAMEAPI file_read_any(uint32_t handle, uint8_t *dest, uint32_t size)
{
    uint32_t found = file_slot_find(handle);
    int32_t slot = (int16_t)found;

    if ((int16_t)found >= 0 && slot < GI32(0x4adaa4)) {
        if ((int32_t)size > 0) {
            if (dest) {
                const uint8_t *src = (const uint8_t *)(uintptr_t)(G32(0x6c25c4 + slot * 8) + G32(0x6c25c0 + slot * 8));
                uint32_t i;

                for (i = 0; i < size; i++)
                    dest[i] = src[i];
            }
            G32(0x6c25c4 + slot * 8) = G32(0x6c25c4 + slot * 8) + size;
        }
        return 1;
    }
    return (uint32_t)os_file_read(handle, dest, size);
}

/* --- the random numbers and picks --- */

/* 0x430ae0: the game's random generator (0x4be34c: (x + 0x4d2) * 0x41a7 &
 * 0x7fffffff): seeded from the clock (0x436935) the first time (0x4ac998
 * set). Returns the high bits (>> 16) modulo n, the whole value for n == -1,
 * or the high bits for n == 0. */
int32_t GAMEAPI rand_next(int32_t n)
{
    uint32_t seed, r;

    if (G32(0x4ac998) != 0) {
        G32(0x4ac998) = 0;
        seed = (uint32_t)fe_time() & 0x7fffffff;
    } else
        seed = G32(0x4be34c);
    seed = ((seed + 0x4d2) * 0x41a7) & 0x7fffffff;
    G32(0x4be34c) = seed;
    r = (uint32_t)((int32_t)seed >> 16);
    if (n != 0)
        r = (uint32_t)((int32_t)r % n);
    if (n == -1)
        return (int32_t)seed;
    return (int32_t)r;
}

/* 0x430b50: a coin: rand_next(64) >= 32. */
int32_t GAMEAPI rand_coin(void)
{
    return rand_next(0x40) >= 0x20;
}

/* 0x430b70: whether `mask` has all of the low n bits set. */
int32_t GAMEAPI mask_full(uint32_t mask, int32_t n)
{
    uint32_t all = 0;
    int32_t i;

    for (i = 0; i < n; i++)
        all |= 1u << (i & 31);
    return (all & mask) == all;
}

/* 0x430ba0: pick a random index below n that isn't set in mask, starting
 * from a random place and stepping on a random 5..36 times; `none` when the
 * mask is full. */
int32_t GAMEAPI mask_pick(uint32_t mask, int32_t n, int32_t none)
{
    int32_t pick = rand_next(n), skips = rand_next(0x20), i;

    if (mask_full(mask, n) != 0)
        return none;
    skips += 5;
    if (skips >= 0) {
        for (skips++; skips != 0; skips--) {
            for (i = 0;;) {
                pick++;
                if (pick >= n)
                    pick = 0;
                if ((mask & (1u << (pick & 31))) == 0)
                    break;
                i++;
                if (i >= 0x20)
                    break;
            }
        }
    }
    return pick;
}

/* --- race set-up --- */

/* 0x430130: the level file name at 0x4be3f4: "high\\lev%d" when settings +0x31
 * is 1, else "low\\lev%d", with race +0x579 + 1. */
void GAMEAPI level_name_set(void)
{
    if (g_set[0x31] == 1)
        sprintf_(0x4be3f4, 0x4acac0, (uint32_t)g_race[0x579] + 1);
    else
        sprintf_(0x4be3f4, 0x4acab4, (uint32_t)g_race[0x579] + 1);
}

/* 0x432b70: settings +0x26 (the start grid kind): settings +0x27 itself
 * (0-2), or for 3 random (1 in 7 is 1, 1 in 7 is 2, else 0) - but only when
 * race +0xe is set; else 0. */
void GAMEAPI start_kind_set(void)
{
    if (g_race[0xe] != 0) {
        uint8_t k = g_set[0x27];

        if (k < 3) {
            g_set[0x26] = k;
            return;
        }
        if (k == 3) {
            int32_t r = rand_next(7);

            if (r == 0)
                g_set[0x26] = 1;
            else if (r != 1)
                g_set[0x26] = 0;
            else
                g_set[0x26] = 2;
            return;
        }
    }
    g_set[0x26] = 0;
}

/* 0x432bd0: race +0x553 / +0x555 (the weapons on flag): off for `off`; else
 * by +0xa89 (1 on, 2 a coin); with +0xe 1 or 2, +0x555 is set; with track
 * kinds 1, 8, 10 and 0x10 the flag is 2. */
void GAMEAPI weapons_flags_set(int32_t kind, int32_t off)
{
    g_race[0x555] = 0;
    if (off != 0) {
        g_race[0x553] = 0;
        g_race[0x555] = 0;
        return;
    }
    if (g_race[0xa89] == 1)
        g_race[0x553] = 1;
    else if (g_race[0xa89] == 2) {
        int32_t coin = rand_coin();

        g_race[0x553] = coin != 0;
    } else
        g_race[0x553] = 0;
    if (g_race[0x553] == 1) {
        if (g_race[0xe] == 1 || g_race[0xe] == 2)
            g_race[0x555] = 1;
        if (kind == 1 || kind == 8 || kind == 10 || kind == 0x10)
            g_race[0x553] = 2;
    }
}

/* 0x432c80: race +0x554 (an option of race kind 3): settings +0x2d 1 on, 2 a
 * coin. */
void GAMEAPI race_option554_set(void)
{
    g_race[0x554] = 0;
    if (g_race[0xe] == 3) {
        if (g_set[0x2d] == 1)
            g_race[0x554] = 1;
        if (g_set[0x2d] == 2)
            g_race[0x554] = (uint8_t)rand_coin();
    }
}

/* 0x433ca0: 1 when no car slot (race +0x1c, six of them) holds `value`. */
int32_t GAMEAPI slot_value_free(uint32_t value)
{
    int32_t i;

    for (i = 0; i < 6; i++)
        if (g_race[i + 0x1c] == value)
            return 0;
    return 1;
}

/* 0x433bf0: a character for a car: 6 when asked for in the race (+0x585, or
 * +0x720 bit 4 and `fixed` clear) and not taken; else the first free (fixed)
 * or a random free one (mask_pick over the taken ones), 0xff for none. */
int32_t GAMEAPI character_pick(int32_t fixed, int32_t other)
{
    uint32_t taken = 0;
    int32_t i = 0;

    if ((g_race[0x585] != 0 || ((g_race[0x720] & 0x10) != 0 && other == 0)) && slot_value_free(6) != 0)
        return 6;
    for (;;) {
        if (fixed == 0) {
            int32_t c = mask_pick(taken, 6, -1);

            if (c == -1)
                return 0xff;
            if (slot_value_free((uint32_t)c) != 0)
                return c;
            taken |= 1u << (c & 31);
        } else if (slot_value_free((uint32_t)i) != 0)
            return i;
        i++;
        if (i > 5)
            return 0xff;
    }
}

/* 0x433980: pick a value 0..n-1 for the 4-byte record at p (+0 the value,
 * +2 the bits used so far), avoiding the bits in mask and, up to four
 * tries, the last value. When everything not in mask is used, the used bits
 * restart. Returns the value (the original leaves it in eax; 0x433670 uses
 * it). */
int32_t GAMEAPI pick_next_random(uint8_t *p, int32_t n, uint32_t mask)
{
    uint32_t all = 0, cur, bits;
    int32_t i, tries = 0, value;

    for (i = 0; i < n; i++)
        all |= 1u << (i & 31);
    all &= ~mask;
    cur = U16(p, 2);
    if ((uint16_t)(cur & all) == (uint16_t)all)
        U16(p, 2) = (uint16_t)(~all & cur);
    do {
        bits = (uint16_t)(U16(p, 2) | mask);
        value = mask_pick(bits, n, 0);
        if ((uint32_t)value != p[0])
            break;
        tries++;
    } while (tries < 4);
    p[0] = (uint8_t)value;
    U16(p, 2) = (uint16_t)(U16(p, 2) | (uint16_t)(1u << (value & 31)));
    return value;
}

/* 0x433a00: step the record's value (+1) round 0..n-1, past the bits in the
 * mask (at most 32 steps); sets 0x4be370. Returns the value. */
int32_t GAMEAPI pick_next_step(uint8_t *p, int32_t n, uint32_t mask)
{
    int32_t value, tries = 0;

    G32(0x4be370) = 1;
    value = p[1];
    for (;;) {
        value++;
        if (value < 0 || n <= value)
            value = 0;
        if ((mask & 0xffff & (1u << (value & 31))) == 0)
            break;
        tries++;
        if (tries >= 0x20)
            break;
    }
    p[1] = (uint8_t)value;
    return value;
}

/* 0x433a50: the track choice for record p in world `world`: random (100,
 * 0x433980) or next (101, 0x433a00) among the world's tracks (count 0x448670)
 * not locked (0x4486b0), else the given choice stored at +1. Returns the
 * value. */
int32_t GAMEAPI track_choice(uint8_t *p, uint32_t world, int32_t choice)
{
    int32_t count = world_track_count(world);
    uint32_t locked = (uint32_t)world_locked_tracks(world);

    if (choice == 100)
        return pick_next_random(p, count, locked);
    if (choice == 0x65)
        return pick_next_step(p, count, locked);
    p[1] = (uint8_t)choice;
    return choice;
}

/* 0x433ab0: the six car slots of the race: for `players` 1..4 the human
 * players' slots get their controllers from settings +0x18..+0x1b; each
 * slot starts as 8 (an AI car). With `set_grid` the grid places (+0x10) run
 * 5..0 (`reverse` set) or 0..5 and the +8 records clear; race kinds 5-8 take
 * the characters from the table 0x4af658, the others leave them open (0xff),
 * and every open character is picked (0x433bf0, with `fixed`, `other`). */
void GAMEAPI race_slots_init(int32_t players, int32_t set_grid, int32_t reverse, int32_t fixed, int32_t other)
{
    int32_t i;

    for (i = 0; i < 6; i++) {
        uint8_t kind;

        if (set_grid != 0) {
            g_race[i + 0x10] = (uint8_t)(reverse == 0 ? i : 5 - i);
            g_race[i + 8] = 0;
        }
        g_race[i] = 8;
        kind = g_race[0x551];
        if (kind == 5 || kind == 6 || kind == 7 || kind == 8)
            g_race[i + 0x1c] = G8(0x4af658 + i);
        else
            g_race[i + 0x1c] = 0xff;
    }
    if (players >= 1 && players <= 4) {
        if (players >= 4) {
            g_race[3] = 3;
            g_race[0x1f] = g_set[0x1b];
        }
        if (players >= 3) {
            g_race[2] = 2;
            g_race[0x1e] = g_set[0x1a];
        }
        if (players >= 2) {
            g_race[1] = 1;
            g_race[0x1d] = g_set[0x19];
        }
        g_race[0] = 0;
        g_race[0x1c] = g_set[0x18];
    }
    for (i = 0; i < 6; i++)
        if (g_race[i + 0x1c] == 0xff)
            g_race[i + 0x1c] = (uint8_t)character_pick(fixed, other);
}

/* 0x448580: the title of track `index` into buf (0x4485b0 with its world and
 * place). */
void GAMEAPI track_title(uint8_t *buf, int32_t index)
{
    uint32_t place = (uint32_t)world_place(index);
    uint32_t world = (uint32_t)world_number(index);

    track_title_text(buf, (int32_t)world, (int32_t)place);
}

/* 0x433670: the track for this race, once (cached in 0x4acab0; -1 = not yet
 * chosen) by game `kind` (1..13) and the setting `value`: 1-4 worlds 1-4
 * (the choice records at settings +0x54, +0x58, +0x5c, +0x60 by 0x433a50),
 * 5 / 6 the 15 tracks of all worlds (random / next, record +0x50, locks
 * from the four worlds shifted by the bytes 0x4ac978..b), 7-9 and 11-12 the
 * value (+3, +6 for 8 and 9), 10 a rotating or random world track (settings
 * +0x1f: 0 random but not the last, 100 next, else +0x1f - 1), 13 the first.
 * Unless the pick was a step (0x4be370), the choice records are reset
 * (+0x51, +0x55, ... = 0xff). The title is built (0x448580) and thrown away.
 * Out of 0..19 is -1. */
int32_t GAMEAPI race_track_choose(int32_t kind, uint32_t value)
{
    uint8_t title[0x200];
    int32_t track = GI32(0x4acab0), reset = 1;

    if (track != -1)
        return track;
    G32(0x4be370) = 0;
    switch (kind) {
    case 1:
    case 2:
    case 3:
    case 4: {
        int32_t choice = track_choice(g_set + (kind == 1 ? 0x54 : kind == 2 ? 0x58 : kind == 3 ? 0x5c : 0x60),
                                      (uint32_t)kind, (int32_t)value);

        G32(0x4acab0) = (uint32_t)world_track(kind, choice);
        reset = G32(0x4be370) == 0;
        break;
    }
    case 5:
    case 6: {
        uint32_t l4 = (uint32_t)world_locked_tracks(4);
        uint32_t l3 = (uint32_t)world_locked_tracks(3);
        uint32_t l2 = (uint32_t)world_locked_tracks(2);
        uint32_t l1 = (uint32_t)world_locked_tracks(1);
        uint32_t mask = l4 << (G8(0x4ac97b) & 31) | l3 << (G8(0x4ac97a) & 31) | l2 << (G8(0x4ac979) & 31) |
                        l1 << (G8(0x4ac978) & 31);

        if (kind == 5)
            G32(0x4acab0) = (uint32_t)pick_next_random(g_set + 0x50, 0xf, mask);
        else
            G32(0x4acab0) = (uint32_t)pick_next_step(g_set + 0x50, 0xf, mask);
        reset = G32(0x4be370) == 0;
        break;
    }
    case 7:
    case 11:
    case 12:
        G32(0x4acab0) = value;
        break;
    case 8:
        G32(0x4acab0) = value + 3;
        break;
    case 9:
        G32(0x4acab0) = value + 6;
        break;
    case 10: {
        uint8_t mode = g_set[0x1f];

        if (mode == 0) {
            int32_t tries = 0;
            uint32_t r;

            do {
                r = (uint32_t)rand_next(4);
                G32(0x4acab0) = r + 1;
                if (r + 1 != g_set[0x38])
                    break;
                tries++;
            } while (tries < 4);
            S16(g_set, (r & 3) * 2 + 0x78) = (int16_t)(S16(g_set, (r & 3) * 2 + 0x78) + 1);
            g_set[0x38] = (uint8_t)G32(0x4acab0);
        } else if (mode == 100) {
            G32(0x4acab0) = (uint32_t)g_set[0x38] + 1;
            if (GI32(0x4acab0) < 0x14)
                g_set[0x38] = (uint8_t)G32(0x4acab0);
            else {
                G32(0x4acab0) = 0;
                g_set[0x38] = 0;
            }
        } else {
            G32(0x4acab0) = (uint32_t)mode - 1;
            g_set[0x38] = (uint8_t)G32(0x4acab0);
        }
        reset = G32(0x4be370) == 0;
        break;
    }
    case 13:
        G32(0x4acab0) = 0;
        break;
    default:
        break;
    }
    if (reset) {
        g_set[0x51] = 0xff;
        g_set[0x55] = 0xff;
        g_set[0x59] = 0xff;
        g_set[0x5d] = 0xff;
        g_set[0x61] = 0xff;
    }
    track = GI32(0x4acab0);
    if (track < 0 || track > 0x13) {
        track = -1;
        G32(0x4acab0) = 0xffffffff;
    }
    track_title(title, track);
    return GI32(0x4acab0);
}

/* 0x41c400: load the replay files (settings +0xccc + 0x20 * i, up to six, one
 * per car in the race): the name is "..\TEMP\" (0x4a7f7c) + the path; each
 * must be a file of exactly 0x1e884 bytes, read into the race at +0x12a0 +
 * 0x1e884 * i. 1 for all loaded, 0 when one is missing or the wrong size. */
int32_t GAMEAPI replay_files_load(void)
{
    uint32_t paths[6];
    char name[0x100];
    int32_t failed = 0, i, offset = 0;

    for (i = 0; i < 6; i++)
        paths[i] = (uint32_t)(uintptr_t)(g_set + 0xccc + i * 0x20);
    if (g_race[0x54d] != 0) {
        i = 0;
        do {
            uint32_t file, k = 0;
            const char *prefix = (const char *)0x4a7f7c;
            const char *path = (const char *)(uintptr_t)paths[i];

            for (; k < 9; k++)
                name[k] = prefix[k];
            str_cat(name, path);
            file = file_open_any((uint32_t)(uintptr_t)name);
            if (file == 0)
                return 0;
            if (os_file_size(file) == 0x1e884)
                file_read_any(file, g_race + offset + 0x12a0, 0x1e884);
            else
                failed = 1;
            file_close_any(file);
            if (failed)
                return 0;
            i++;
            offset += 0x1e884;
        } while (i < g_race[0x54d]);
    }
    return 1;
}

/* 0x432ec0: set up the race from the settings (the front end's choices):
 * -1 when it can't be (a setting out of range, 'race kind' 1, no replay
 * file), else 0.
 *
 * A replay (settings +0x17 == 1): the replay file (0x474200) and the car
 * files (0x41c400) loaded, 0x4be31c cleared, race +0x58c = 2.
 * Otherwise the race is reset (+0xa85, +0xa88, +0x58c, ... ), the speed
 * class (settings +0x6c clamped to 24..112 floats) goes to +0xa84 and, by the
 * race kind (settings +0x14, 0-10), the race mode (+0x551), cars (+0x54d),
 * the player slots (0x433ab0) and the number of laps (+0x586: the asked
 * for number, or a default, kept between the limits). Then the level name,
 * start grid, track (0x433670), weapons and options, the rest of the race
 * block cleared (+0x24, 0x528 bytes), +0x57b = 1, +0x57c = 1 and, for a
 * race of its own, the replay file looked for (settings +0x21). */
int32_t GAMEAPI race_init(void)
{
    int32_t min = 3, max = 8, alt = 0, local_4, i;
    uint32_t req;
    uint8_t first;
    float speed;

    if (g_set[0x17] == 1) {
        G32(0x4be31c) = 1;
        if (g_set[0x21] == 0) {
            G32(0x4be31c) = 1;
            return -1;
        }
        if (replay_load() != -1 && replay_files_load() != 0) {
            level_name_set();
            g_race[0x58c] = 2;
            G32(0x4be31c) = 0;
            return 0;
        }
        if (G32(0x4be31c) != 0)
            return -1;
        return 0;
    }
    g_race[0xa85] = 0;
    g_race[0xa88] = (uint8_t)((g_set[0x76] != 0) + 1);
    g_race[0x58c] = 0;
    g_race[0xa8b] = 0;
    g_race[0x54f] = g_race[0xa8b];
    g_race[0x54c] = 0;
    g_race[0x552] = 0;
    g_race[0x54e] = 1;
    speed = *(float *)(g_set + 0x6c);
    if (!(speed >= *(float *)0x4a3390))
        U32(g_set, 0x6c) = 0x41c00000;
    speed = *(float *)(g_set + 0x6c);
    if (speed > *(float *)0x4a338c)
        U32(g_set, 0x6c) = 0x42e00000;
    speed = *(float *)(g_set + 0x6c);
    g_race[0xa84] = (uint8_t)(int32_t)speed;
    first = g_set[0x74];
    local_4 = first;
    if (first < 2 || first > 4) {
        local_4 = 1;
        req = 0xff;
    } else if (first == 2 && g_set[0x76] == 0)
        req = 1;
    else
        req = first;
    switch (g_set[0x14]) {
    case 0:
        g_race[0x551] = 0;
        g_race[0x58c] = 1;
        g_race[0x54d] = 6;
        race_slots_init(0, 1, 0, 0, 1);
        req = 3;
        goto clamp;
    case 2:
        g_race[0x551] = 0;
        g_race[0x54d] = 6;
        min = g_race[0x585] + 3;
        alt = min;
        req = g_set[0x2f];
        race_slots_init(1, g_set[0x16] == 0, 1, 1, 0);
        max = 0x32;
        goto laps;
    case 3:
        g_race[0x551] = 9;
        g_race[0x54d] = 6;
        alt = 3;
        req = g_set[0x35];
        min = 1;
        max = 0x32;
        race_slots_init(1, 1, 1, 0, 0);
        goto laps;
    case 4:
        g_race[0x551] = 1;
        g_race[0x54d] = 1;
        race_slots_init(1, 1, 0, 1, 0);
        req = 3;
        goto clamp;
    case 5: {
        uint8_t mode = g_set[0x88];
        uint32_t j, n;

        if (mode == 1 || mode == 2 || mode == 3) {
            g_race[0x551] = (uint8_t)(mode + 5);
            g_race[0x54d] = (uint8_t)local_4;
        } else {
            g_race[0x551] = 5;
            g_race[0x54d] = 6;
        }
        g_set[0x89] = (uint8_t)((G32(0x4cb48c) == 0) + 0x34);
        g_race[0x552] = (uint8_t)req;
        req = g_set[0x77];
        alt = 3;
        min = 1;
        max = 0x32;
        race_slots_init(local_4, g_set[0x75] == 0, 0, 0, 0);
        n = g_race[0x54d];
        for (j = (uint32_t)local_4; j < n;) {
            uint32_t k = 0;

            if (n != 0) {
                for (;;) {
                    uint32_t slot = 0;
                    int32_t found = 0;

                    if (j != 0)
                        for (; slot < j; slot++)
                            if (g_race[slot + 0x1c] == k) {
                                found = 1;
                                break;
                            }
                    if (!found) {
                        g_race[j + 0x1c] = (uint8_t)k;
                        break;
                    }
                    k++;
                    if (k >= n)
                        break;
                }
            }
            if (g_race[j + 0x1c] == 0xff)
                g_race[j + 0x1c] = (uint8_t)j;
            j++;
            n = g_race[0x54d];
        }
        goto laps;
    }
    case 6:
        g_race[0x551] = 4;
        g_race[0x54d] = 2;
        g_set[0x18] = 0;
        race_slots_init(1, 1, 0, 1, 1);
        req = 3;
        goto clamp;
    case 7:
    case 8: {
        uint8_t v;

        if (g_set[0x14] == 7)
            g_set[0x89] = (uint8_t)((G32(0x4cb48c) != 0 ? 0xfe : 0) + 0x45);
        v = g_set[0x8a];
        g_race[0x552] = v;
        alt = 3;
        min = 1;
        max = 0x32;
        req = g_set[0x85];
        goto laps;
    }
    case 9:
        g_race[0x551] = 9;
        g_race[0x54d] = 6;
        g_race[0x585] = 0;
        race_slots_init(1, 1, 1, 0, 1);
        g_race[0x586] = 3;
        goto after;
    case 10:
        g_race[0x551] = 10;
        g_race[0x54d] = 1;
        g_race[0x585] = 2;
        g_race[0x58d] = 0;
        g_set[0x92] = 0;
        g_set[0x18] = 3;
        race_slots_init(1, 1, 1, 0, 1);
        g_race[0x586] = 1;
        goto after;
    default:
        return -1;
    }
laps:
    if (req == 0)
        req = (uint32_t)alt;
clamp:
    if ((int32_t)req > max)
        req = (uint32_t)max;
    if ((int32_t)req < min)
        req = (uint32_t)min;
    g_race[0x586] = (uint8_t)req;
after:
    if (g_set[0xf] >= 1 && g_set[0xf] <= 4)
        g_race[0x552] = g_set[0xf];
    if (g_race[0x552] > 4)
        return -1;
    level_name_set();
    if (g_set[0x14] != 7 && g_set[0x14] != 8) {
        int32_t track;

        start_kind_set();
        track = race_track_choose(g_set[0x15], g_set[0x16]);
        weapons_flags_set(track, 0);
        race_option554_set();
    }
    for (i = 0; i < 0x14a; i++)
        U32(g_race, 0x24 + i * 4) = 0;
    g_race[0x57b] = 1;
    U32(g_race, 0x57c) = 1;
    if (g_race[0x58c] == 0) {
        U32(g_race, 0x16) = U32(g_race, 0x10);
        U16(g_race, 0x1a) = U16(g_race, 0x14);
        g_set[0x21] = replay_save() != -1;
    }
    return 0;
}

/* --- the boot data (registry key "Software\\Psygnosis\\Rollcage\\BootSys" at
 * 0x4b2284, values "DataPC" the settings block, "DataPSX" the race block,
 * "DataReplay" the race block of a replay) --- */

/* 0x474500: copy between the settings and a table of {kind, settings
 * field, other} records at `table` (0x4b21a0, ending at kind -1; a null
 * pointer ends it too): op 100 copies other -> field, 101 field -> other;
 * kinds 0 byte, 1 word, 2 and 3 dword, 4 eleven dwords. */
void GAMEAPI table_copy(int32_t op, const uint32_t *table)
{
    int32_t kind = (int32_t)table[0];
    uint8_t *a = (uint8_t *)(uintptr_t)table[1], *b = (uint8_t *)(uintptr_t)table[2];

    for (;;) {
        if (kind == -1 || a == 0 || b == 0)
            return;
        if (op == 100) {
            switch (kind) {
            case 0:
                *a = *b;
                break;
            case 1:
                *(uint16_t *)a = *(uint16_t *)b;
                break;
            case 2:
            case 3:
                *(uint32_t *)a = *(uint32_t *)b;
                break;
            case 4: {
                int32_t k;

                for (k = 0; k < 11; k++)
                    ((uint32_t *)a)[k] = ((uint32_t *)b)[k];
                break;
            }
            default:
                break;
            }
        } else if (op == 0x65) {
            switch (kind) {
            case 0:
                *b = *a;
                break;
            case 1:
                *(uint16_t *)b = *(uint16_t *)a;
                break;
            case 2:
            case 3:
                *(uint32_t *)b = *(uint32_t *)a;
                break;
            case 4: {
                int32_t k;

                for (k = 0; k < 11; k++)
                    ((uint32_t *)b)[k] = ((uint32_t *)a)[k];
                break;
            }
            default:
                break;
            }
        }
        table += 3;
        kind = (int32_t)table[0];
        a = (uint8_t *)(uintptr_t)table[1];
        b = (uint8_t *)(uintptr_t)table[2];
    }
}

/* 0x4745d0: the table 0x4b21a0 copied other -> field (op 100). */
void GAMEAPI settings_table_load(void)
{
    table_copy(100, (const uint32_t *)0x4b21a0);
}

/* 0x4746b0: the screen mode into the settings: +0x42c/+0x42e the width and
 * height (16-bit from 0x4acc38 / 0x4acc3c), +0x430 from 0x4acc40, +0x432 from
 * 0x4be448, bytes +0x419 .. +0x41f from 0x4acc44, 0x4be444, 0x4acc48,
 * 0x4acc58, 0x4acc4c and 0x4be44c. */
void GAMEAPI settings_screen_store(void)
{
    U16(g_set, 0x42c) = (uint16_t)G32(0x4acc38);
    U16(g_set, 0x42e) = (uint16_t)G32(0x4acc3c);
    U16(g_set, 0x430) = (uint16_t)G32(0x4acc40);
    U16(g_set, 0x432) = (uint16_t)G32(0x4be448);
    g_set[0x419] = G8(0x4acc44);
    g_set[0x41b] = G8(0x4acc48);
    g_set[0x41a] = G8(0x4be444);
    g_set[0x41c] = G8(0x4acc58);
    g_set[0x41e] = G8(0x4acc4c);
    g_set[0x41f] = G8(0x4be44c);
}

/* 0x474030: read a boot data value into buf (size bytes) from the registry
 * (0x46b6d0): the old contents are kept in a copy and put back (-1) when
 * fewer than `size` bytes came. 0 for success. */
int32_t GAMEAPI registry_load(uint8_t *buf, int32_t size, uint32_t name)
{
    uint8_t *backup = mem_alloc((uint32_t)size);
    int32_t got, i;

    if (backup)
        for (i = 0; i < size; i++)
            backup[i] = buf[i];
    got = GCALL(0x46b6d0, reg_binary_get)(0x4b2284, name, (uint32_t)(uintptr_t)buf, size);
    if (got < size) {
        if (backup) {
            for (i = 0; i < size; i++)
                buf[i] = backup[i];
            mem_free(backup);
        }
        return -1;
    }
    if (backup)
        mem_free(backup);
    return 0;
}

/* 0x474230: write a boot data value (0x46b730): 0 for success, -1 not. */
int32_t GAMEAPI registry_save(uint8_t *buf, uint32_t size, uint32_t name)
{
    int32_t r = GCALL(0x46b730, reg_binary_set)(0x4b2284, name, (uint32_t)(uintptr_t)buf, (int32_t)size);

    return (r != -1) - 1;
}

/* 0x474200: read the replay ("DataReplay") into the race block; sets
 * 0x4d46f8. 0 for success. */
int32_t GAMEAPI replay_load(void)
{
    int32_t r;

    G32(0x4d46f8) = 1;
    r = registry_load(g_race, 0xaa0, 0x4b22b8);
    return (r != -1) - 1;
}

/* 0x474330: write the replay ("DataReplay"). 0 for success. */
int32_t GAMEAPI replay_save(void)
{
    return registry_save(g_race, 0xaa0, 0x4b22b8);
}

/* 0x474260: save the boot data when it hasn't been yet (0x4d46f4, 0x4d46f8
 * clear): the screen mode and settings table into the settings, race +0x54c
 * = 0, settings +0x414 = 0x4db260, the version tags +0 / +0x1850 ("10cp",
 * "20cp"), then "DataPC" (settings, 0x1854 bytes) and "DataPSX" (race, 0xaa0);
 * 0x4d466c / 0x4d4670 = whether each was written, 0x4d46f4 = 1 if either. */
int32_t GAMEAPI boot_data_save(void)
{
    settings_screen_store();
    settings_table_load();
    if (G32(0x4d46f4) == 0 && G32(0x4d46f8) == 0) {
        int32_t r;

        g_race[0x54c] = 0;
        U32(g_set, 0x414) = G32(0x4db260);
        U32(g_set, 0) = 0x70633031;
        U32(g_set, 0x1850) = 0x70633032;
        r = registry_save(g_set, 0x1854, 0x4b22b0);
        G32(0x4d466c) = r != -1;
        r = registry_save(g_race, 0xaa0, 0x4b22a8);
        G32(0x4d4670) = r != -1;
        if (G32(0x4d466c) != 0 || G32(0x4d4670) != 0)
            G32(0x4d46f4) = 1;
    }
    return 0;
}
