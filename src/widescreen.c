/* Widescreen culling for the new renderer (docs/renderer/plan.md, step 5).
 *
 * With REROLL_ASPECT set (a ratio such as 2.389, or a size such as
 * 3440x1440), the game culls for a wider screen than it draws: a "virtual"
 * screen of the real height and width height * aspect, centred on the real
 * one. The focal length H stays as it is, so the middle of every view is
 * exactly the original 4:3 picture (Hor+: the same vertical field of view,
 * more to the sides). Two things change, both after the game sets them for
 * a view:
 *   - the view's clip rectangle 0x6ccd64 / 0x6ccd68 (left, right), which
 *     every projected vertex is outcoded against, is widened at the sides
 *     of views that touch the screen's left or right edge (0x41f2d0). The
 *     projection 0x426b00 and the outcode helper 0x426b80 compare screen
 *     x / y with the bounds as integers (the float bits), which only orders
 *     non-negative floats; with the left bound negative they are replaced by
 *     copies that compare as floats (identical for the original's bounds);
 *   - the frustum side planes 0x4b7fa8.. (0x417ed0, symmetric about the
 *     view centre) are built for the widened width. The original's "view
 *     leaves the screen" flag 0x4b7fec is kept from the real view, so the
 *     Direct3D back end clips as before.
 * The new renderer then finds the extra geometry in the packets, and its
 * scissor follows the widened clip rectangle. The countdown (0x46a9e0)
 * uses the clip rectangle's left edge as its origin, so it is drawn with
 * the real bounds.
 *
 * The track's visibility lists are precomputed per camera cell for the
 * original view, so rows only seen further to the side are missing from
 * them (stepped holes at the screen edges). In widescreen mode the list
 * manager 0x47a030 first draws extra runs, then calls the original: the
 * sections of the cells at the view's edges (below) that the camera cell's
 * list lacks, each run through the game's row test 0x47a1d0. Only runs from
 * the game's own lists are used: rows no list ever reaches may have no
 * render records (filling those gaps crashed). The game's list-taking steps
 * have fixed tables sized for its own lists (0x47a500's stack arrays,
 * 0x5d91c0, 0x5d7540), so the extra runs go through them in small chunks.
 * The edge cells: the cells under points along the two edges of the
 * widened view (1/2 to 16 cells out, track_cell 0x419420); every section
 * their lists have that the camera cell's list lacks is added, with those
 * lists' own runs and detail levels.
 * Visibility list (cell + 4, a word offset into *0x6bfacc): entries of u16
 * (bits 0-10 first row, 11-15 byte count), u8 (bit 0 last entry, bits 1-7
 * section), then per byte a run of (byte >> 2) rows of detail level
 * (byte & 3) + 1, each level-n row being 2^(n-1) full rows (from the
 * disassembly of 0x47a0d0; Ghidra's decompilation swaps the two).
 *
 * Draw distance (REROLL_DRAW_DISTANCE, default the whole track with the
 * Vulkan renderer): the same extra-run path also draws the sections no
 * list has, out to that distance (docs/renderer/plan.md, step 8). */
#include "widescreen.h"
#include <stdlib.h>

#include "hook.h"
#include "log.h"
#include "runtime.h"

#define G_SCREEN_W (*(int32_t *)0x4acc38)
#define G_SCREEN_H (*(int32_t *)0x4acc3c)
#define G_CLIP_LEFT (*(float *)0x6ccd64)
#define G_CLIP_RIGHT (*(float *)0x6ccd68)
#define G_OFFSCREEN (*(uint32_t *)0x4b7fec)

static float aspect; /* 0: off */
static int follow_window; /* aspect follows the Vulkan window's */
static float real_left, real_right; /* the current view's clip bounds before widening */

/* The wrapped originals' bodies, ported to C in src/game/orig_wa.c (group
 * WA; prototypes in build/groups/wa_protos.txt, declared here until they are
 * in physics.h). The wrappers below stay hooked at the original addresses
 * and call these where they called the original through a trampoline. */
void GAMEAPI frustum_body(float x, float y, float w, float h, float focal); /* 0x417ed0 */
void GAMEAPI viewport_body(int32_t x, int32_t y, int32_t w, int32_t h); /* 0x41f2d0 */
void GAMEAPI objects_body(void); /* 0x459b20 */
void GAMEAPI countdown_body(void); /* 0x46a9e0 */
uint32_t *GAMEAPI visibility_body(uint8_t *cell); /* 0x47a030 */
void GAMEAPI flake_spawn_body(int32_t i); /* 0x485140 */
void GAMEAPI sparkle_body(uint8_t *camera); /* 0x490430 */

/* Pixels the virtual screen adds on each side of the real one. */
static float extra_width(void)
{
    float wide = (float)G_SCREEN_H * aspect;
    return wide > (float)G_SCREEN_W ? (wide - (float)G_SCREEN_W) / 2.0f : 0.0f;
}

/* 0x417ed0(x, y, w, h, H): the frustum side planes. */
static void frustum(float x, float y, float w, float h, float focal)
{
    float extra = extra_width();
    uint32_t offscreen;

    frustum_body(x, y, w, h, focal);
    if (extra <= 0.0f)
        return;
    offscreen = G_OFFSCREEN;
    frustum_body(x - extra, y, w + 2.0f * extra, h, focal);
    G_OFFSCREEN = offscreen;
}

/* 0x41f2d0(x, y, w, h): the D3D viewport, projection centre and clip
 * rectangle of a view. */
static void viewport(int32_t x, int32_t y, int32_t w, int32_t h)
{
    float extra;

    viewport_body(x, y, w, h);
    real_left = G_CLIP_LEFT;
    real_right = G_CLIP_RIGHT;
    extra = extra_width();
    if (extra <= 0.0f)
        return;
    if (x <= 0)
        G_CLIP_LEFT -= extra;
    if (x + w >= G_SCREEN_W)
        G_CLIP_RIGHT += extra;
}

/* ---- the track at the wide edges ---- */

#define MAX_RUNS 2048
#define MAX_SECTIONS 128
#define MAX_SPANS 64
#define CHUNK_RUNS 32   /* 0x47a500 keeps ~50 runs on its stack; 0x5d91c0 has 64 entries */
#define CHUNK_ROWS 400  /* 0x5d7540: 552 rows across a list */

typedef void (*run_fn)(uint32_t *list, uint32_t section, uint32_t first_row, uint32_t length, uint32_t lod);
typedef void (*list2_fn)(uint32_t *list, uint32_t *previous);
typedef void (*list1_fn)(uint32_t *list);
typedef void (*light_fn)(uint32_t *list, uint32_t second_view);
typedef void *(*cell_fn)(int32_t x, int32_t y, int32_t z, int32_t *off_track);
#define visible_runs ((run_fn)0x47a1d0)
#define rows_newly_visible ((list2_fn)0x47a500)
#define rows_light_marks ((list1_fn)0x47c420)
#define rows_light ((light_fn)0x47c730)
#define track_draw ((list1_fn)0x477940)
#define track_cell ((cell_fn)0x419420)
#define G_VIEW (*(uint32_t *)0x6ce60c)

/* Run lists: u32 count, then 8 bytes per run (u16 section, first row, rows,
 * detail level), as the game's. */
static uint32_t extra[1 + 2 * MAX_RUNS];
static uint32_t chunk[1 + 2 * CHUNK_RUNS];
static uint32_t empty[1 + 2];

struct span {
    uint16_t first, end; /* full rows [first, end) */
    uint8_t lod;
};

struct section_spans {
    uint32_t section;
    int count;
    struct span spans[MAX_SPANS];
};

static struct section_spans sections[MAX_SECTIONS];
static int section_count;
static uint32_t extra_lists, extra_runs_total;

static const uint8_t *visibility_entries(uint32_t offset)
{
    return *(const uint8_t **)0x6bfacc + offset * 2;
}

static const uint8_t *next_entry(const uint8_t *p)
{
    return (const uint8_t *)(((uintptr_t)p + (*(const uint16_t *)p >> 11) + 4) & ~(uintptr_t)1);
}

static struct section_spans *find_section(uint32_t section)
{
    int i;
    for (i = 0; i < section_count; i++)
        if (sections[i].section == section)
            return &sections[i];
    return 0;
}

/* The camera cell's sections and the rows its list covers. */
static void collect_cell(uint32_t offset)
{
    const uint8_t *p = visibility_entries(offset);

    section_count = 0;
    for (;;) {
        uint32_t row = *(const uint16_t *)p & 0x7ff, bytes = *(const uint16_t *)p >> 11;
        uint32_t section = (p[2] >> 1) & 0x7f;
        const uint8_t *b = p + 3;
        struct section_spans *ss = find_section(section);

        if (!ss && section_count < MAX_SECTIONS) {
            ss = &sections[section_count++];
            ss->section = section;
            ss->count = 0;
        }
        for (; bytes; bytes--, b++) {
            uint32_t lod = (*b & 3u) + 1, end = row + ((uint32_t)(*b >> 2) << (lod - 1));
            if (ss && ss->count < MAX_SPANS) {
                ss->spans[ss->count].first = (uint16_t)row;
                ss->spans[ss->count].end = (uint16_t)end;
                ss->spans[ss->count].lod = (uint8_t)lod;
                ss->count++;
            }
            row = end;
        }
        if (p[2] & 1)
            break;
        p = next_entry(p);
    }
}

/* Rows of the camera cell's own sections that its list leaves out (the
 * list can stop short of a section's last row), at the neighbouring run's
 * detail level. They may share render records with listed rows, which the
 * detach (below) makes safe. */
static void add_section_gaps(void)
{
    int i, j, k, count = section_count;

    for (i = 0; i < count; i++) {
        struct section_spans *ss = &sections[i];
        const uint8_t *section = (*(const uint8_t ***)0x6bfac0)[ss->section];
        uint32_t rows, at = 0;

        if (!section || !ss->count)
            continue;
        rows = *(const uint32_t *)(section + 0xc); /* rows (the lists end there) */
        if (rows > 0x7ff)
            continue;
        for (j = 1; j < ss->count; j++)
            for (k = j; k > 0 && ss->spans[k - 1].first > ss->spans[k].first; k--) {
                struct span t = ss->spans[k];
                ss->spans[k] = ss->spans[k - 1];
                ss->spans[k - 1] = t;
            }
        for (j = 0; j <= ss->count && extra[0] < MAX_RUNS - 64; j++) {
            uint32_t next = j < ss->count ? ss->spans[j].first : rows, lod, step, length;
            if (next > at) {
                lod = j > 0 ? ss->spans[j - 1].lod : ss->spans[j].lod;
                step = 1u << (lod - 1);
                length = (next - at) / step;
                if (length)
                    visible_runs(extra, ss->section, at, length, lod);
                if (at + length * step < next)
                    visible_runs(extra, ss->section, at + length * step, next - at - length * step, 1);
            }
            if (j < ss->count && ss->spans[j].end > at)
                at = ss->spans[j].end;
        }
    }
}

/* Sections the camera cell's list lacks, from the lists of the cells under
 * points along the two edges of the widened view. */
static void add_edge_sections(uint32_t camera_offset)
{
    static const int32_t distances[7] = {64, 128, 256, 512, 1024, 2048, 4096};
    static const float blend[3] = {0.4f, 0.7f, 1.0f}; /* from the 4:3 edge (0) to the wide edge (1) */
    uint32_t seen_offsets[32];
    const int16_t *m = (const int16_t *)0x6ce5a0; /* rows: right, up, forward (4.12) */
    const int32_t *t = (const int32_t *)0x6ce5c0; /* -camera position, world units */
    float h = *(const float *)0x6ccd00;
    int seen = 0, side, d, i, k;

    if (h <= 0.0f)
        return;
    seen_offsets[seen++] = camera_offset;
    for (side = -1; side <= 1; side += 2)
        for (d = 0; d < 21; d++) {
            float half = ((float)G_SCREEN_W / 2.0f + extra_width() * blend[d / 7]) / h;
            float dx = ((float)m[6] + (float)side * half * (float)m[0]) / 4096.0f;
            float dz = ((float)m[8] + (float)side * half * (float)m[2]) / 4096.0f;
            float len = dx * dx + dz * dz, inv = 1.0f;
            int32_t x, z, off_track;
            uint8_t *cell;
            uint32_t offset;
            const uint8_t *p;

            if (len < 1e-6f || len > 1e6f)
                continue;
            for (k = 0; k < 12; k++) /* 1 / sqrt(len) */
                inv = inv * (1.5f - 0.5f * len * inv * inv);
            x = -t[0] + (int32_t)(dx * inv * (float)distances[d % 7]);
            z = -t[2] + (int32_t)(dz * inv * (float)distances[d % 7]);
            cell = track_cell((int32_t)((uint32_t)x << 16), (int32_t)((uint32_t)-t[1] << 16),
                              (int32_t)((uint32_t)z << 16), &off_track);
            if (off_track || !cell)
                continue;
            offset = *(uint32_t *)(cell + 4) & 0xffff;
            for (i = 0; i < seen && seen_offsets[i] != offset; i++)
                ;
            if (i < seen || seen == 32)
                continue;
            seen_offsets[seen++] = offset;
            for (p = visibility_entries(offset);; p = next_entry(p)) {
                uint32_t row = *(const uint16_t *)p & 0x7ff, bytes = *(const uint16_t *)p >> 11;
                uint32_t section = (p[2] >> 1) & 0x7f;
                const uint8_t *b = p + 3;
                if (!find_section(section) && section_count < MAX_SECTIONS && extra[0] < MAX_RUNS - 64) {
                    for (; bytes; bytes--, b++) {
                        static const float lod_distance[5] = {0, 700, 1500, 2500, 7000};
                        uint32_t lod = (*b & 3u) + 1, length = (uint32_t)(*b >> 2);
                        if (lod_distance[lod] < *(const float *)0x6ccd08) /* levels beyond the far plane skipped, as 0x47a0d0 */
                            visible_runs(extra, section, row, length, lod);
                        row += length << (lod - 1);
                    }
                    sections[section_count].section = section;
                    sections[section_count].count = 0;
                    section_count++;
                }
                if (p[2] & 1)
                    break;
            }
        }
}

/* ---- the far track (longer draw distance) ---- */

#define G_SECTIONS (*(uint8_t ***)0x6bfac0)
#define G_SECTION_COUNT (*(uint32_t *)0x6bfad0)
#define G_TRACK_ROWS (*(uint8_t **)0x6bfad4)
#define G_TRACK_CELLS (*(uint8_t **)0x6bfaf8)
#define MAX_ROWS 0x800

static const void *scanned; /* the PVS data the tables below were built from */
static uint8_t reach[MAX_SECTIONS][MAX_ROWS]; /* bit n - 1: some list draws the row at detail level n */
static uint8_t list_seen[0x10000 / 8];
static uint8_t lod_ok[MAX_SECTIONS]; /* bit n - 1: the section's level n quads are drawable */
static uint8_t extra_off[MAX_SECTIONS]; /* looped even at full detail: no extra rows of it */
static float far_distance; /* world units; 0: the game's own lists */

static void scan_list(uint32_t offset)
{
    const uint8_t *p;
    for (p = visibility_entries(offset);; p = next_entry(p)) {
        uint32_t row = *(const uint16_t *)p & 0x7ff, bytes = *(const uint16_t *)p >> 11;
        uint32_t section = (p[2] >> 1) & 0x7f;
        const uint8_t *b = p + 3;
        for (; bytes; bytes--, b++) {
            uint32_t lod = (*b & 3u) + 1, end = row + ((uint32_t)(*b >> 2) << (lod - 1)), r;
            for (r = row; r < end && r < MAX_ROWS; r++)
                reach[section][r] |= (uint8_t)(1u << (lod - 1));
            row = end;
        }
        if (p[2] & 1)
            break;
    }
}

/* A row's centre from its LOD1 bound record (section +0x4c, 0x1c bytes:
 * +4 four (s16 x, s16 y), +0x14 four u16 z). */
static void row_centre(uint32_t section, uint32_t row, int32_t *c)
{
    const uint8_t *b = *(const uint8_t *const *)(G_SECTIONS[section] + 0x4c) + row * 0x1c;
    int k;
    c[0] = c[1] = c[2] = 0;
    for (k = 0; k < 4; k++) {
        c[0] += *(const int16_t *)(b + 4 + 4 * k);
        c[1] += *(const int16_t *)(b + 6 + 4 * k);
        c[2] += *(const int16_t *)(b + 0x14 + 2 * k);
    }
    c[0] /= 4;
    c[1] /= 4;
    c[2] /= 4;
}

static uint32_t quads_per_row(const uint8_t *section, uint32_t lod);

/* Which detail levels of a section can be drawn: level n's rows of quads
 * (2^(n-1) full rows each) whose render records are all different. Some
 * sections have level data that repeats one record across a row (levels
 * they don't really have); drawing those links a packet twice. */
static int readable(const void *p, uint32_t len)
{
    struct memory_basic_information info;
    uint32_t a = (uint32_t)(uintptr_t)p;
    if (a < 0x10000 || a + len < a || !VirtualQuery(p, &info, sizeof info) || info.state != MEM_COMMIT)
        return 0;
    return a + len <= (uint32_t)info.base_address + info.region_size;
}

static uint32_t check_levels(const uint8_t *section)
{
    static uint8_t seen[0x10000 / 8 + 1];
    uint32_t rows = *(const uint32_t *)(section + 0xc), lod, ok = 1, row, k;

    for (lod = 2; lod <= 4; lod++) {
        const uint8_t *quads = *(const uint8_t *const *)(section + 0x4c + 4 * lod);
        uint32_t q, good = 1;
        const uint32_t *steps = *(const uint32_t *const *)(section + 0x20 + 4 * lod);
        if (!readable(steps, 4) || !readable(*(const void *const *)(section + 0x34 + 4 * lod), 4))
            continue;
        q = quads_per_row(section, lod);
        if (!q || q > 80 || !readable(quads, (rows >> (lod - 1)) * q * 8))
            continue;
        for (row = 0; row < rows >> (lod - 1) && good; row++) {
            for (k = row * q; k < row * q + q; k++) {
                uint32_t r = *(const uint16_t *)(quads + k * 8);
                if (seen[r >> 3] & (1u << (r & 7)))
                    good = 0;
                seen[r >> 3] |= (uint8_t)(1u << (r & 7));
            }
            for (k = row * q; k < row * q + q; k++) {
                uint32_t r = *(const uint16_t *)(quads + k * 8);
                seen[r >> 3] &= (uint8_t)~(1u << (r & 7));
            }
        }
        if (good)
            ok |= 1u << (lod - 1);
    }
    return ok;
}

/* Every cell layer's list (the grid rows 0x6bfad4: 256 of 0x10 bytes, six
 * u16 column spans and the cell words; layers 0xc bytes, bit 31 of +4 =
 * another layer follows): which rows of each section any list reaches. */
static void scan_track(void)
{
    uint32_t z, lists = 0, rows = 0, quads = 0, reached = 0, i, r;

    memset(reach, 0, sizeof reach);
    memset(list_seen, 0, sizeof list_seen);
    for (z = 0; z < 256; z++) {
        const uint8_t *row = G_TRACK_ROWS + z * 0x10;
        const uint16_t *cells = *(const uint16_t *const *)(row + 0xc);
        uint32_t count = 0, n;
        if (!cells)
            continue;
        for (n = 0; n < 6; n++) {
            uint32_t span = ((const uint16_t *)row)[n];
            if (span == 0xff)
                break;
            count += (span >> 8) - (span & 0xff) + 1;
        }
        for (i = 0; i < count; i++) {
            const uint8_t *layer = G_TRACK_CELLS + cells[i];
            for (;;) {
                uint32_t offset = *(const uint32_t *)(layer + 4) & 0xffff;
                if (!(list_seen[offset >> 3] & (1u << (offset & 7)))) {
                    list_seen[offset >> 3] |= (uint8_t)(1u << (offset & 7));
                    scan_list(offset);
                    lists++;
                }
                if (!(*(const uint32_t *)(layer + 4) & 0x80000000u))
                    break;
                layer += 0xc;
            }
        }
    }
    for (i = 0; i < G_SECTION_COUNT && i < MAX_SECTIONS; i++) {
        const uint8_t *section = G_SECTIONS[i];
        uint32_t n = *(const uint32_t *)(section + 0xc);
        rows += n;
        quads += n * *(const uint32_t *)(section + 8);
        for (r = 0; r < n && r < MAX_ROWS; r++)
            reached += reach[i][r] != 0;
    }
    memset(extra_off, 0, sizeof extra_off);
    for (i = 0; i < G_SECTION_COUNT && i < MAX_SECTIONS; i++)
        if ((lod_ok[i] = (uint8_t)check_levels(G_SECTIONS[i])) != 0xf)
            log_printf("fartrack: section %u detail levels %x", i, lod_ok[i]);
    log_printf("fartrack: %u sections, %u rows (%u reached by %u lists), %u quads", G_SECTION_COUNT, rows, reached,
               lists, quads);
}

/* The detail level the game's lists use at a (squared) distance, measured
 * from them: level 1 to ~2000 units, 2 to ~6000, then 3. */
static uint32_t far_lod(float distance2)
{
    return distance2 < 2048.0f * 2048.0f ? 1 : distance2 < 6144.0f * 6144.0f ? 2 : 3;
}

/* Rows of the sections no list so far has (the camera cell's, the edge
 * cells'), out to far_distance: every row some list reaches, at the
 * detail level for its distance, in runs aligned to that level. Rows
 * behind the camera are dropped by the game's row test. */
static void add_far_sections(void)
{
    const int32_t *t = (const int32_t *)0x6ce5c0; /* -camera position */
    uint32_t i, count = G_SECTION_COUNT < MAX_SECTIONS ? G_SECTION_COUNT : MAX_SECTIONS;
    float limit = far_distance * far_distance;

    for (i = 0; i < count && extra[0] < MAX_RUNS - 64; i++) {
        const uint8_t *section = G_SECTIONS[i];
        uint32_t rows, row = 0, run_lod = 0, run_first = 0;

        if (!section || find_section(i))
            continue;
        rows = *(const uint32_t *)(section + 0xc);
        if (rows > MAX_ROWS)
            continue;
        while (row <= rows) {
            uint32_t lod = 0, step;
            if (row < rows && reach[i][row]) {
                int32_t c[3];
                float dx, dy, dz, d;
                row_centre(i, row, c);
                dx = (float)(c[0] + t[0]);
                dy = (float)(c[1] + t[1]);
                dz = (float)(c[2] + t[2]);
                d = dx * dx + dy * dy + dz * dz;
                if (d <= limit) {
                    /* the nearest level some list draws this row at (not
                     * every section has every level's data), coarse ones
                     * only where whole rows of them fit */
                    uint32_t want = far_lod(d), best = 0, n;
                    for (n = 1; n <= 4; n++)
                        if ((n == 1 ? reach[i][row] : reach[i][row] & lod_ok[i] & (1u << (n - 1))) &&
                            !(row & ((1u << (n - 1)) - 1)) &&
                            row + (1u << (n - 1)) <= rows &&
                            (!best || (n <= want ? n > best || best > want : best > want && n < best)))
                            best = n;
                    lod = best;
                }
            }
            if (lod != run_lod || (run_lod && (row - run_first) >> (run_lod - 1) >= 63)) {
                if (run_lod)
                    visible_runs(extra, i, run_first, (row - run_first) >> (run_lod - 1), run_lod);
                run_lod = lod;
                run_first = row;
            }
            if (row == rows)
                break;
            step = lod ? 1u << (lod - 1) : 1;
            row += step;
        }
    }
}

/* ---- detaching drawn packets from the game's render records ---- */

/* The track's render records (the packets) are shared between sections that
 * no visibility list has together, so the extra sections' packets must not
 * stay in them: after each extra section is drawn, every packet it linked
 * into the view's ordering table is copied here and the copies linked in
 * its place. Two pools, one per view of a frame. */
#define POOL_SIZE 0x1000000 /* a frame's: every view's extra track and objects */
static uint8_t pool[POOL_SIZE];
void widescreen_log_pools(void);
void widescreen_log_pools(void)
{
    log_printf("leak: pool %x-%x; object buffers %x (+limit %x), %x (+limit %x)", (uint32_t)pool,
               (uint32_t)pool + POOL_SIZE,
               *(uint32_t *)0x4beb84, *(uint32_t *)0x6c1e34, *(uint32_t *)0x6c1e1c, *(uint32_t *)0x6c1e38);
}
/* One pool for the whole frame, emptied once it has been presented: the
 * copies stay linked until their view's lists are drawn, and the far
 * objects of one view can be drawn while the game works on another view
 * record (two pools, emptied per view, had copies overwritten while still
 * linked: the translucent list then ran into the world list). */
static uint32_t pool_used;

void widescreen_frame_end(void)
{
    pool_used = 0;
}
static uint32_t saved_ot[512], saved_ot2[512];
static uint32_t detach_overflow, detach_loops;

static uint32_t packet_bytes(const uint8_t *packet)
{
    switch (packet[7] >> 2) {
    case 0x23: return 0x84;
    case 0x27: return 0x64;
    case 0x26: return 0x78;
    case 0x25: return 0x60;
    case 0x2a: return 0x68;
    case 0x2b: return 0x3c;
    default: return 0x84;
    }
}

/* The world ordering table: the track drawer 0x4779f0 always inserts into
 * 0x6cd510 (512 entries); the view struct only picks which copy of a render
 * record (+0 / +0x84) is used. */
static uint32_t *view_ot(void)
{
    return (uint32_t *)0x6cd510;
}

/* Packets already copied in this walk: a packet linked twice (a shared
 * record used twice) makes a loop, and the walk stops at the first repeat
 * instead of copying the loop over and over. */
#define SEEN_SLOTS 0x8000
static uint32_t seen_address[SEEN_SLOTS], seen_stamp[SEEN_SLOTS], stamp;

static int seen_before(uint32_t address)
{
    uint32_t h = (address >> 2) * 2654435761u >> 17;
    while (seen_stamp[h] == stamp) {
        if (seen_address[h] == address)
            return 1;
        h = (h + 1) & (SEEN_SLOTS - 1);
    }
    seen_stamp[h] = stamp;
    seen_address[h] = address;
    return 0;
}

static void detach_table(uint32_t *ot, const uint32_t *saved)
{
    int i, looped = 0;

    if (++stamp == 0) {
        memset(seen_stamp, 0, sizeof seen_stamp);
        stamp = 1;
    }
    for (i = 0; i < 512; i++) {
        uint32_t word = ot[i], *link = &ot[i];
        int guard = SEEN_SLOTS / 2;
        if (word == saved[i])
            continue;
        while (word != saved[i] && !(word & 0x80000000u)) {
            const uint8_t *packet = (const uint8_t *)word;
            uint32_t size = packet_bytes(packet), next = *(const uint32_t *)packet;
            uint8_t *copy;
            if (seen_before(word) || --guard <= 0) {
                looped = 1;
                break;
            }
            if (pool_used + size > POOL_SIZE) {
                detach_overflow++;
                break;
            }
            copy = pool + pool_used;
            pool_used += (size + 3) & ~3u;
            memcpy(copy, packet, size);
            *link = (uint32_t)copy;
            link = (uint32_t *)copy;
            word = next;
        }
        *link = saved[i];
    }
    if (looped)
        detach_loops++;
}

/* Both lists: the track's opaque quads go to 0x6cd510 and its translucent
 * ones to the view's own list 0x6cdd80 (in the z-buffer mode the game
 * uses), and both share the records. */
static void detach(void)
{
    detach_table(view_ot(), saved_ot);
    detach_table((uint32_t *)0x6cdd80, saved_ot2);
}

/* The render records a chunk uses. The track's records are shared between
 * any quads that no one visibility list draws together (even rows of one
 * section at one detail level), so a chunk must not use one twice: that
 * links its packet twice and loops the ordering table. Quad lists: LOD n
 * at section +0x4c + 4n, 8 bytes a quad, the record index in the first
 * u16 (records of 0x120 bytes, 0x4779f0); a quad with an overlay layer
 * (LOD 1: bit 31 of +4; LOD n: bit 0 of its nibble in the flags at
 * section +0x34 + 4n) also uses the next record. */
static uint8_t record_used[0x10000 / 8 + 1];

static uint32_t quads_per_row(const uint8_t *section, uint32_t lod)
{
    if (lod == 1)
        return *(const uint32_t *)(section + 8);
    return (**(const uint32_t *const *)(section + 0x20 + 4 * lod) & 0xffff) - 1;
}

/* Whether lod row `row` (in that level's rows) uses a record already used;
 * with `mark`, marks its records instead. */
static int row_records(const uint8_t *section, uint32_t lod, uint32_t row, int mark)
{
    uint32_t q = quads_per_row(section, lod), k, first = row * q;
    const uint8_t *quads = *(const uint8_t *const *)(section + 0x4c + 4 * lod);
    const uint32_t *flags = lod > 1 ? *(const uint32_t *const *)(section + 0x34 + 4 * lod) : 0;

    for (k = first; k < first + q; k++) {
        uint32_t record = *(const uint16_t *)(quads + k * 8), overlay, r;
        overlay = lod == 1 ? *(const uint32_t *)(quads + k * 8 + 4) >> 31 : (flags[k >> 3] >> ((k & 7) * 4)) & 1;
        for (r = record; r <= record + overlay; r++) {
            if (mark)
                record_used[r >> 3] |= (uint8_t)(1u << (r & 7));
            else if (record_used[r >> 3] & (1u << (r & 7)))
                return 1;
        }
    }
    return 0;
}

/* Textures, lighting and drawing of one chunk, detached at once. */
static void draw_chunk(uint32_t second_view)
{
    if (!chunk[0])
        return;
    empty[0] = 0;
    rows_newly_visible(chunk, empty); /* "all newly visible": writes their texture words */
    rows_light_marks(chunk);
    rows_light(chunk, second_view);
    memcpy(saved_ot, view_ot(), sizeof saved_ot);
    memcpy(saved_ot2, (const void *)0x6cdd80, sizeof saved_ot2);
    track_draw(chunk);
    {
        uint32_t loops = detach_loops, k;
        detach();
        if (detach_loops != loops) /* still a shared record: those sections' coarse levels are not used again, */
            for (k = 0; k < chunk[0]; k++) { /* and at full detail, no extra rows of them at all */
                const uint16_t *run = (const uint16_t *)&chunk[1 + 2 * k];
                if (run[3] > 1 && lod_ok[run[0]] != 1) {
                    log_printf("fartrack: section %u row %u lod %u looped: full detail from now on", run[0], run[1], run[3]);
                    lod_ok[run[0]] = 1;
                } else if (run[3] == 1 && !extra_off[run[0]]) {
                    log_printf("fartrack: section %u row %u looped at full detail: no extra rows from now on", run[0],
                               run[1]);
                    extra_off[run[0]] = 1;
                }
            }
    }
    chunk[0] = 0;
    memset(record_used, 0, sizeof record_used);
}

static void chunk_add(uint32_t section, uint32_t first, uint32_t length, uint32_t lod)
{
    uint16_t *run = (uint16_t *)&chunk[1 + 2 * chunk[0]];
    run[0] = (uint16_t)section;
    run[1] = (uint16_t)first;
    run[2] = (uint16_t)length;
    run[3] = (uint16_t)lod;
    chunk[0]++;
}

/* The extra runs in chunks that fit the game's fixed tables (see CHUNK_*)
 * and use no render record twice: runs are split where a row would. */
static void draw_extra(void)
{
    uint32_t i, rows = 0, second_view = G_VIEW == 0x6cd4a0 ? 0 : 1;

    chunk[0] = 0;
    memset(record_used, 0, sizeof record_used);
    for (i = 0; i < extra[0]; i++) {
        uint16_t *run = (uint16_t *)&extra[1 + 2 * i];
        uint32_t lod, base, start = 0, r;
        const uint8_t *section = G_SECTIONS[run[0]];

        if (extra_off[run[0]])
            continue;
        if (!(lod_ok[run[0]] & (1u << (run[3] - 1)))) { /* a level the section lacks: full detail */
            run[2] = (uint16_t)(run[2] << (run[3] - 1));
            run[3] = 1;
        }
        lod = run[3];
        base = (uint32_t)run[1] >> (lod - 1);
        for (r = 0; r < run[2]; r++) {
            if (chunk[0] + 1 > CHUNK_RUNS || rows + (r - start) + 2 > CHUNK_ROWS ||
                row_records(section, lod, base + r, 0)) {
                if (r > start) {
                    chunk_add(run[0], run[1] + (start << (lod - 1)), r - start, lod);
                    start = r;
                }
                draw_chunk(second_view);
                rows = 0;
            }
            row_records(section, lod, base + r, 1);
        }
        if (run[2] > start) {
            chunk_add(run[0], run[1] + (start << (lod - 1)), run[2] - start, lod);
            rows += run[2] - start + 1;
        }
    }
    draw_chunk(second_view);
}

/* 0x47a030(cell): the extra track at the wide edges is drawn first, then
 * the original builds, lights and returns the view's own list, so the
 * tables the later scenery pass reads (0x47dd70) are the original's. */
static int extra_ready;
static uint32_t *visibility(void *cell)
{
    if ((extra_width() > 0.0f || far_distance > 0.0f) && cell) {
        uint32_t offset = *(uint32_t *)((uint8_t *)cell + 4) & 0xffff;
        if (scanned != *(const void **)0x6bfacc) {
            scanned = *(const void **)0x6bfacc;
            scan_track();
        }
        extra[0] = 0;
        collect_cell(offset);
        add_section_gaps();
        if (extra_width() > 0.0f)
            add_edge_sections(offset);
        if (far_distance > 0.0f)
            add_far_sections();
        extra_runs_total += extra[0];
        if (extra[0]) {
            uint32_t *previous = *(uint32_t **)(G_VIEW == 0x6cd4a0 ? 0x5d9454 : 0x5d9458);
            draw_extra();
            /* the extra sections overwrote shared records' texture words:
             * the original's list (next) must treat all its rows as newly
             * visible, so its "previous" list (after its swap) is emptied */
            if (previous)
                previous[0] = 0;
        }
        extra_ready = 1;
        if (++extra_lists % 20000 == 0)
            log_printf("widescreen: %u views, %u extra runs drawn, %u pool overflows, %u loops", extra_lists, extra_runs_total,
                       detach_overflow, detach_loops);
    }
    return visibility_body((uint8_t *)cell);
}

static const struct hook visibility_hook = {"wide_visibility", 0x47a030, visibility,
                                            {0xa1, 0x0c, 0xe6, 0x6c, 0x00, 0x56, 0x3d, 0xa0}};

/* ---- objects on the extra rows (off by default; REROLL_FAR_OBJECTS=1) ----
 *
 * Known problem (2026-10-12): with this second object pass, the game's own
 * pass afterwards sometimes links one of its packets into both the world
 * list 0x6cd510 and the translucent list 0x6cdd80, so the translucent list
 * runs on into the world and the effects flicker (REROLL_DEBUG_LEAKS=1
 * reports it). The list is still sound after the far call and its
 * detach; why the original pass then double-links (per-object packets,
 * not the shared buffer; not the hidden-object limit) is not found yet.
 * Planned instead: draw far objects in the renderer, not through 0x459b20. */

/* 0x459b20 draws the objects of the cells the view's list reaches (the
 * chains 0x47dd70 gathers into 0x5d7040, light words in 0x5d6ac0) and
 * collects the cars among them into 0x6ce640 (light words 0x6ce620). */
#define STATIC_LIST ((uint16_t *)0x5d7040)
#define STATIC_LIGHTS ((uint32_t *)0x5d6ac0)
#define STATIC_MAX 300 /* 0x5d6ac0 holds 352 */

static int far_objects;
static uint16_t saved_static[STATIC_MAX + 1];
static uint32_t saved_lights[STATIC_MAX + 1];
static uint8_t cell_seen[0x10000 / 8];
static uint8_t trigger_save[0x6b9cbc - 0x6b9ba0];
/* the GTE and projection state (matrices, translation, lights, colours) each object draw leaves behind */
#define GTE_START 0x6ccbe0u
#define GTE_END 0x6cce40u
static uint8_t gte_save[GTE_END - GTE_START];

static int mark_cell(uint32_t chain)
{
    if (cell_seen[chain >> 3] & (1u << (chain & 7)))
        return 0;
    cell_seen[chain >> 3] |= (uint8_t)(1u << (chain & 7));
    return 1;
}

/* Cars are left to their own pass (0x459b20 only collects them): their
 * bit 0x400 ("drawable") is cleared for the far call. */
#define HIDDEN_MAX 1024
static uint32_t *hidden[HIDDEN_MAX];
static uint32_t hidden_count;

/* Cells' object chains can share their tails, so objects are marked one
 * by one (by record index): an object the original's pass draws, or an
 * earlier far chain already had, must not be drawn again (its packets
 * would be linked twice). */
static uint8_t object_seen[0x10000 / 8];

static void mark_objects(uint32_t chain)
{
    uint8_t *(*record)(uint32_t) = (uint8_t * (*)(uint32_t))0x4613e0;
    uint32_t index = chain, guard = 0;
    uint8_t *o;
    for (o = record(index); o && guard++ < 4096; o = record(index = *(const uint16_t *)(o + 4)))
        object_seen[index >> 3] |= (uint8_t)(1u << (index & 7));
}

/* clears 0x400 on the chain's objects that must not be drawn far */
static void hide_unwanted(uint32_t chain)
{
    uint8_t *(*record)(uint32_t) = (uint8_t * (*)(uint32_t))0x4613e0;
    uint32_t index = chain, guard = 0;
    uint8_t *o;
    for (o = record(index); o && hidden_count < HIDDEN_MAX && guard++ < 4096;
         o = record(index = *(const uint16_t *)(o + 4))) {
        uint32_t *kind = (uint32_t *)o;
        int seen = (object_seen[index >> 3] >> (index & 7)) & 1;
        object_seen[index >> 3] |= (uint8_t)(1u << (index & 7));
        if ((*kind & 0x400) && (seen || (*kind & 0x1ff) == 3)) {
            *kind &= ~0x400u;
            hidden[hidden_count++] = kind;
        }
    }
}

/* REROLL_DEBUG_FARDIFF=1: which game data the far call leaves changed
 * (after its own restores), by address, logged every 500 calls. */
#define DIFF_START 0x4a7000u
#define DIFF_END 0x6d3000u
static int far_diff = -1;
static uint8_t *diff_before;
static uint32_t diff_count[(DIFF_END - DIFF_START) / 4], diff_calls;

static void far_diff_before(void)
{
    if (!diff_before)
        diff_before = malloc(DIFF_END - DIFF_START);
    if (diff_before)
        memcpy(diff_before, (const void *)DIFF_START, DIFF_END - DIFF_START);
}

static void far_diff_after(void)
{
    const uint32_t *now = (const uint32_t *)DIFF_START, *was = (const uint32_t *)diff_before;
    uint32_t i, n = (DIFF_END - DIFF_START) / 4;
    if (!diff_before)
        return;
    for (i = 0; i < n; i++)
        if (now[i] != was[i])
            diff_count[i]++;
    if (++diff_calls % 500 == 0) {
        uint32_t start = 0, run = 0;
        log_printf("fardiff: %u calls; words changed (address, words, times):", diff_calls);
        for (i = 0; i <= n; i++) {
            if (i < n && diff_count[i]) {
                if (!run)
                    start = i;
                run++;
                continue;
            }
            if (run)
                log_printf("fardiff:   %x %u %u", DIFF_START + start * 4, run, diff_count[start]);
            run = 0;
        }
        memset(diff_count, 0, sizeof diff_count);
    }
}

/* REROLL_DEBUG_LEAKS: does the translucent list (0x6cdd80) run into the
 * world list's entries? (1 = yes) */
static int list_leaks(void)
{
    uint32_t word = 0x80000000u | 0x6ce57c, steps = 0;
    while (word && word != 0xffffffffu && steps++ < 200000) {
        uint32_t a = word & 0x7fffffffu;
        if (a >= 0x6cd510 && a < 0x6cdd10)
            return 1;
        if (a < 0x10000)
            return 0;
        word = *(const uint32_t *)a;
    }
    return 0;
}
static int leak_debug = -1;
static uint32_t far_buffer_end; /* where the far call's shared-buffer packets ended */

/* The first packet on the translucent list whose chain then reaches the
 * world list's entries: where its memory is. */
static void leak_report(void)
{
    static uint32_t reports;
    uint32_t word = 0x80000000u | 0x6ce57c, steps = 0, hist[64], n = 0;
    while (word && word != 0xffffffffu && steps++ < 200000) {
        uint32_t a = word & 0x7fffffffu;
        if (a >= 0x6cd510 && a < 0x6cdd10)
            break;
        if (a < 0x10000)
            return;
        hist[n++ & 63] = a;
        word = *(const uint32_t *)a;
    }
    if (reports++ >= 20 || !(word & 0x7fffffffu))
        return;
    {
        uint32_t base0 = *(uint32_t *)0x4beb84, lim0 = *(uint32_t *)0x6c1e34, base1 = *(uint32_t *)0x6c1e1c,
                 lim1 = *(uint32_t *)0x6c1e38, k;
        uint32_t first = n;
        while (first > 0 && n - first < 63 && hist[(first - 1) & 63] >= (uint32_t)pool &&
               hist[(first - 1) & 63] < (uint32_t)pool + POOL_SIZE)
            first--;
        log_printf("leak: %u far copies at the end", n - first);
        for (k = first > 4 ? first - 4 : 0; k < first + 1 && k < n; k++) {
            uint32_t a = hist[k & 63];
            const char *where = a >= (uint32_t)pool && a < (uint32_t)pool + POOL_SIZE ? "far copy"
                                : a >= base0 && a < lim0                               ? "shared buffer 0"
                                : a >= base1 && a < lim1                               ? "shared buffer 1"
                                : a >= 0x6cdd80 && a < 0x6ce580                        ? "list entry"
                                                                                       : "elsewhere";
            log_printf("leak:   %x code %x (%s%s)", a, a >= 0x10000 ? (uint32_t)((const uint8_t *)a)[7] : 0u, where,
                       (where[0] == 's' && a < far_buffer_end) ? ", below the far call's end" : "");
        }
        log_printf("leak: -> world entry %x; shared buffers %x-%x / %x-%x, far call ended at %x, view %x",
                   word & 0x7fffffffu, base0, lim0, base1, lim1, far_buffer_end, *(uint32_t *)0x6ce60c);
    }
}
static uint32_t leak_counts[4];

static void objects(void)
{
    uint32_t n = 0, i, own;
    const uint8_t *cells = *(const uint8_t **)0x6bfaf8;
    const uint16_t *cell_list = *(const uint16_t **)0x6bfae4;

    if (far_diff < 0)
        far_diff = getenv("REROLL_DEBUG_FARDIFF") != 0;
    if (leak_debug < 0)
        leak_debug = getenv("REROLL_DEBUG_LEAKS") != 0;
    if (leak_debug)
        leak_counts[0] += list_leaks();
    if (!far_objects || !extra_ready || !extra[0]) {
        objects_body();
        if (leak_debug) {
            leak_counts[2] += list_leaks();
            if (++leak_counts[3] % 2000 == 0)
                log_printf("leak: of %u object passes (no far call this time or far objects off), after the original %u",
                           leak_counts[3], leak_counts[2]);
        }
        return;
    }
    extra_ready = 0;
    memset(cell_seen, 0, sizeof cell_seen);
    for (own = 0; own < STATIC_MAX && STATIC_LIST[own]; own++) {
        mark_cell(STATIC_LIST[own]);
        saved_static[own] = STATIC_LIST[own];
        saved_lights[own] = STATIC_LIGHTS[own];
    }
    saved_static[own] = 0;
    for (i = 0; i < extra[0] && n < STATIC_MAX; i++) {
        const uint16_t *run = (const uint16_t *)&extra[1 + 2 * i];
        uint32_t step = 1u << (run[3] - 1), row = run[1], end = run[1] + ((uint32_t)run[2] << (run[3] - 1));
        const uint16_t *row_cells = *(const uint16_t *const *)(G_SECTIONS[run[0]] + 0x48);
        for (; row < end && n < STATIC_MAX; row += step) {
            uint32_t c, c_end = row_cells[row + step];
            for (c = row_cells[row]; c < c_end && n < STATIC_MAX; c++) {
                uint32_t chain = *(const uint32_t *)(cells + cell_list[c] * 0xc + 8) & 0xffff;
                if (chain && mark_cell(chain)) {
                    STATIC_LIST[n] = (uint16_t)chain;
                    STATIC_LIGHTS[n] = 0;
                    n++;
                }
            }
        }
    }
    if (n) {
        uint32_t trigger_counter = *(uint32_t *)0x6b9b94;
        if (far_diff)
            far_diff_before();
        STATIC_LIST[n] = 0;
        memcpy(trigger_save, (const void *)0x6b9ba0, sizeof trigger_save);
        memcpy(gte_save, (const void *)GTE_START, sizeof gte_save);
        memcpy(saved_ot, (const void *)0x6cd510, sizeof saved_ot);
        memcpy(saved_ot2, (const void *)0x6cdd80, sizeof saved_ot2);
        if (far_diff) {
            static int shown;
            if (shown++ % 2000 == 0)
                log_printf("leak: at the far call 0x6cdd80[0..3] = %x %x %x %x, [511] = %x; view %x", saved_ot2[0],
                           saved_ot2[1], saved_ot2[2], saved_ot2[3], saved_ot2[511], *(uint32_t *)0x6ce60c);
        }
        hidden_count = 0;
        memset(object_seen, 0, sizeof object_seen);
        for (i = 0; i < own; i++)
            mark_objects(saved_static[i]);
        for (i = 0; i < n; i++)
            hide_unwanted(STATIC_LIST[i]);
        if (leak_debug) {
            static uint32_t most;
            if (hidden_count > most) {
                most = hidden_count;
                log_printf("leak: hidden %u objects (limit %u), %u far chains, %u own", most, HIDDEN_MAX, n, own);
            }
        }
        objects_body();
        far_buffer_end = *(uint32_t *)0x6c1e18;
        /* objects share packet buffers between objects never seen together
         * (as the track's records): the far ones' packets are copied out */
        detach_table((uint32_t *)0x6cd510, saved_ot);
        detach_table((uint32_t *)0x6cdd80, saved_ot2);
        for (i = 0; i < hidden_count; i++)
            *hidden[i] |= 0x400;
        memcpy((void *)0x6b9ba0, trigger_save, sizeof trigger_save);
        *(uint32_t *)0x6b9b94 = trigger_counter;
        memcpy((void *)GTE_START, gte_save, sizeof gte_save);
        for (i = 0; i <= own; i++) {
            STATIC_LIST[i] = saved_static[i];
            STATIC_LIGHTS[i] = saved_lights[i];
        }
        if (far_diff)
            far_diff_after();
        if (leak_debug)
            leak_counts[1] += list_leaks();
    }
    objects_body();
    if (leak_debug) {
        if (list_leaks()) {
            leak_counts[2]++;
            leak_report();
        }
        if (++leak_counts[3] % 2000 == 0)
            log_printf("leak: of %u object passes, the translucent list ran into the world list: before %u, after the far "
                       "call %u, after the original %u", leak_counts[3], leak_counts[0], leak_counts[1], leak_counts[2]);
    }
}

static const struct hook objects_hook = {"far_objects", 0x459b20, objects,
                                         {0x83, 0xec, 0x24, 0xa1, 0x0c, 0xe6, 0x6c, 0x00}};

/* ---- float outcodes ---- */

/* 0x426b00 (through the pointer 0x6ccd80): projects the vertex record at
 * *0x6ccd74 with st(0) = H / z from the transform, sets its outcodes and
 * pops st(0) into 0x4bdf00. The original's instructions, with each integer
 * compare of a coordinate with a clip bound made an x87 compare. */
void wide_project(void);
__asm__(".text\n"
        ".globl " ASM_SYM("wide_project") "\n"
        ASM_SYM("wide_project") ":\n"
        "    movl 0x6ccd74, %edx\n"
        "    testl $0x10, (%edx)\n"
        "    jne 9f\n"
        "    flds 0x10(%edx)\n"
        "    movl (%edx), %ecx\n"
        "    fmul %st(1), %st\n"
        "    flds 0x14(%edx)\n"
        "    fmul %st(2), %st\n"
        "    fxch %st(1)\n"
        "    fadds 0x6ccd5c\n"
        "    fxch %st(1)\n"
        "    fadds 0x6ccd60\n"
        "    fxch %st(1)\n"
        "    fstps 0x8(%edx)\n"
        "    fstps 0xc(%edx)\n"
        "    flds 0x8(%edx)\n" /* x < left: 1, else x >= right: 2 */
        "    fcomps 0x6ccd64\n"
        "    fnstsw %ax\n"
        "    testb $1, %ah\n"
        "    jz 1f\n"
        "    orl $1, %ecx\n"
        "    jmp 2f\n"
        "1:  flds 0x8(%edx)\n"
        "    fcomps 0x6ccd68\n"
        "    fnstsw %ax\n"
        "    testb $1, %ah\n"
        "    jnz 2f\n"
        "    orl $2, %ecx\n"
        "2:  flds 0xc(%edx)\n" /* y < top: 4, else y >= bottom: 8 */
        "    fcomps 0x6ccd6c\n"
        "    fnstsw %ax\n"
        "    testb $1, %ah\n"
        "    jz 3f\n"
        "    orl $4, %ecx\n"
        "    jmp 4f\n"
        "3:  flds 0xc(%edx)\n"
        "    fcomps 0x6ccd70\n"
        "    fnstsw %ax\n"
        "    testb $1, %ah\n"
        "    jnz 4f\n"
        "    orl $8, %ecx\n"
        "4:  movl %ecx, (%edx)\n"
        "9:  fstps 0x4bdf00\n"
        "    ret\n");

/* 0x426b80(record): the outcodes of a projected record (cdecl): far/near
 * on z (as integers, as the original: z is positive there), then x / y
 * against the bounds, made x87 compares. */
void wide_outcodes(void);
__asm__(".text\n"
        ".globl " ASM_SYM("wide_outcodes") "\n"
        ASM_SYM("wide_outcodes") ":\n"
        "    movl 4(%esp), %edx\n"
        "    movl 4(%edx), %eax\n"
        "    movl $0, (%edx)\n"
        "    cmpl 0x6ccd08, %eax\n"
        "    jl 1f\n"
        "    orl $0x20, (%edx)\n"
        "1:  cmpl 0x6ccd04, %eax\n"
        "    jge 2f\n"
        "    orl $0x10, (%edx)\n"
        "    ret\n"
        "2:  flds 0x8(%edx)\n"
        "    fcomps 0x6ccd64\n"
        "    fnstsw %ax\n"
        "    testb $1, %ah\n"
        "    jz 3f\n"
        "    orl $1, (%edx)\n"
        "    jmp 4f\n"
        "3:  flds 0x8(%edx)\n"
        "    fcomps 0x6ccd68\n"
        "    fnstsw %ax\n"
        "    testb $1, %ah\n"
        "    jnz 4f\n"
        "    orl $2, (%edx)\n"
        "4:  flds 0xc(%edx)\n"
        "    fcomps 0x6ccd6c\n"
        "    fnstsw %ax\n"
        "    testb $1, %ah\n"
        "    jz 5f\n"
        "    orl $4, (%edx)\n"
        "    ret\n"
        "5:  flds 0xc(%edx)\n"
        "    fcomps 0x6ccd70\n"
        "    fnstsw %ax\n"
        "    testb $1, %ah\n"
        "    jnz 6f\n"
        "    orl $8, (%edx)\n"
        "6:  ret\n");

static const struct hook project_hook = {"wide_project", 0x426b00, wide_project,
                                         {0x8b, 0x15, 0x74, 0xcd, 0x6c, 0x00, 0xf7, 0x02}};
static const struct hook outcodes_hook = {"wide_outcodes", 0x426b80, wide_outcodes,
                                          {0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08, 0x8b, 0x42}};

/* 0x46a9e0: the countdown numerals, which use the clip rectangle's left and
 * top as their screen origin: drawn with the view's real bounds. */
static void countdown(void)
{
    float left = G_CLIP_LEFT, right = G_CLIP_RIGHT;
    if (extra_width() <= 0.0f) { /* widescreen off: the bounds are the real ones */
        countdown_body();
        return;
    }
    G_CLIP_LEFT = real_left;
    G_CLIP_RIGHT = real_right;
    countdown_body();
    G_CLIP_LEFT = left;
    G_CLIP_RIGHT = right;
}

static const struct hook countdown_hook = {"wide_countdown", 0x46a9e0, countdown,
                                           {0x55, 0x8b, 0xec, 0x83, 0xec, 0x18, 0x53, 0x56}};

static const struct hook frustum_hook = {"wide_frustum", 0x417ed0, frustum,
                                         {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec}};
static const struct hook viewport_hook = {"wide_viewport", 0x41f2d0, viewport,
                                          {0x55, 0x8b, 0xec, 0x51, 0xdb, 0x45, 0x10, 0x53}};

/* ---- screen-space overlays (docs/renderer/plan.md, step 7) ---- */

#define G_RACE (*(uint8_t **)0x4b1960)
#define RACE_WIDTH(race) (*(uint16_t *)((race) + 0xa82)) /* the width overlays are laid out in */
#define FLAKE(i) (*(uint8_t **)0x4d5898 + (i) * 0xa0)  /* +0x7c x, +0x80 y (floats, screen pixels) */

/* 0x485140(i): (re)spawns screen flake / lens drop i inside the 4:3 width;
 * its x is spread over the wide screen (the mover only drifts it outwards
 * from the middle and respawns it when its life ends). */
static void GAMEAPI flake_spawn(int32_t i)
{
    float extra = extra_width(), w, *x;

    flake_spawn_body(i);
    if (extra <= 0.0f || !G_RACE || !*(uint8_t **)0x4d5898 || i < 0 || i >= 32)
        return;
    w = (float)RACE_WIDTH(G_RACE);
    if (w <= 0.0f)
        return;
    x = (float *)(FLAKE(i) + 0x7c);
    *x = (*x - w * 0.5f) * (w + 2.0f * extra) / w + w * 0.5f;
}

/* 0x490430(camera): the respawn / snatch sparkle, random squares on a grid
 * of the width from the view's left clip edge: drawn with the widened width
 * so they cover the wide view instead of bunching at its left. */
static void GAMEAPI sparkle(uint32_t camera)
{
    uint8_t *race = G_RACE;
    uint16_t width;
    float wide = G_CLIP_RIGHT - G_CLIP_LEFT;

    if (!race || extra_width() <= 0.0f || wide <= (float)RACE_WIDTH(race) || wide > 8192.0f) {
        sparkle_body((uint8_t *)camera);
        return;
    }
    width = RACE_WIDTH(race);
    RACE_WIDTH(race) = (uint16_t)wide;
    sparkle_body((uint8_t *)camera);
    RACE_WIDTH(race) = width;
}

static const struct hook flake_hook = {"wide_flake_spawn", 0x485140, flake_spawn,
                                       {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x8b, 0x4d}};
static const struct hook sparkle_hook = {"wide_sparkle", 0x490430, sparkle,
                                         {0x55, 0x8b, 0xec, 0x83, 0xec, 0x14, 0xa0, 0x40}};

/* Reads a ratio ("2.389") or a size ("3440x1440"). */
static float parse_aspect(const char *s)
{
    float a = 0.0f, b = 0.0f, scale = 0.0f;
    int second = 0;

    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            float *v = second ? &b : &a;
            if (scale > 0.0f) {
                *v += (float)(*s - '0') * scale;
                scale /= 10.0f;
            } else {
                *v = *v * 10.0f + (float)(*s - '0');
            }
        } else if (*s == '.') {
            scale = 0.1f;
        } else if (*s == 'x' || *s == 'X' || *s == ':') {
            second = 1;
            scale = 0.0f;
        }
    }
    if (second)
        return b > 0.0f ? a / b : 0.0f;
    return a;
}

/* REROLL_DRAW_DISTANCE: world units ("0" = the game's own lists); with
 * the Vulkan renderer the default is the whole track. */
static void draw_distance_install(int vulkan)
{
    char value[32];
    if (GetEnvironmentVariableA("REROLL_DRAW_DISTANCE", value, sizeof value))
        far_distance = parse_aspect(value);
    else if (vulkan)
        far_distance = 65536.0f;
    if (far_distance > 0.0f)
        log_printf("draw distance: %d units", (int32_t)far_distance);
}

/* The window's aspect changed (Options > Display, or a resized window):
 * culling follows it, unless REROLL_ASPECT fixed it. */
void widescreen_set_aspect(float output_aspect)
{
    if (follow_window && output_aspect > 0.0f && output_aspect != aspect) {
        aspect = output_aspect;
        log_printf("widescreen: culling for aspect %d/1000", (int32_t)(aspect * 1000.0f));
    }
}

void widescreen_install(float output_aspect)
{
    char value[32] = "the window's";
    int frustum_ok, viewport_ok;

    draw_distance_install(output_aspect > 0.0f);
    if (GetEnvironmentVariableA("REROLL_ASPECT", value, sizeof value)) {
        aspect = parse_aspect(value);
    } else if (output_aspect > 0.0f) {
        /* the Vulkan window: its aspect, even 4:3 or narrower (no extra
         * width then), as the window can change later */
        aspect = output_aspect;
        follow_window = 1;
    }
    if (aspect <= 1.34f && !follow_window) {
        if (aspect > 0.0f)
            log_printf("widescreen: aspect (%s) is not wider than 4:3, off", value);
        aspect = 0.0f;
    }
    /* The wrappers are installed whatever the settings: with widescreen and
     * the draw distance off they just run the original's body (orig_wa.c),
     * so no original code is left at these addresses (REROLL_NO_EXE). */
    far_objects = far_distance > 0.0f &&
                  GetEnvironmentVariableA("REROLL_FAR_OBJECTS", value, sizeof value) && value[0] == '1';
    hook_install(&visibility_hook);
    hook_install(&objects_hook);
    hook_install(&countdown_hook);
    hook_install(&flake_hook);
    hook_install(&sparkle_hook);
    frustum_ok = hook_install(&frustum_hook);
    viewport_ok = hook_install(&viewport_hook);
    if (aspect == 0.0f)
        return;
    if (!frustum_ok || !viewport_ok) {
        aspect = 0.0f;
        return;
    }
    hook_install(&project_hook);
    hook_install(&outcodes_hook);
    log_printf("widescreen: culling for aspect %d/1000", (int32_t)(aspect * 1000.0f));
}
