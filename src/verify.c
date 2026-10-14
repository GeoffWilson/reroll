/* Shadow verification (REROLL_VERIFY=1).
 *
 * A verified call runs twice from the same game state: first the original
 * code with every replacement hook switched off, then our code. Game memory
 * (the executable's data and bss, and the heap block with the bodies) is
 * snapshotted before, the original's results are saved, the snapshot is put
 * back for our run, and afterwards both results are compared byte for byte.
 * The original's results are then kept, so the game always plays exactly as
 * the original and one mismatch cannot snowball into the next steps.
 *
 * Sound (0x471c90) talks to QMixer, outside game memory, so it would see a
 * different mixer state the second time. The original run records each call
 * and its result; our run gets the same results back without playing
 * anything, and its calls must match. The sound engine state the original's
 * calls wrote is left out of the comparison (sound_state).
 *
 * The 16 ms multimedia timer (0x4365ed) runs on another thread and can tick
 * at any moment; its variables are neither compared nor rolled back
 * (timer_state). Nor is the stack below the verified call, where the two
 * versions' locals naturally differ, nor scratch memory (scratch_state). */
#if defined(__i386__) /* checks against the original code: 32-bit only */
#include "verify.h"
#include "log.h"
#include "coverage.h"

#define DATA_START 0x4a7000u
#define DATA_END 0x6d3000u

#define g_car_slots 0x6bfb80u
#define CAR_SLOT_SIZE 0x274u
#define CAR_OBJECT_SIZE 0x1600u
#define g_race_data (*(uint8_t **)0x4b1960)
#define g_bodies (*(uint32_t **)0x6b9b80)
#define g_body_count (*(uint32_t *)0x6b9b84 & 0xffff)
#define BODY_SIZE 0x170u
#define g_events (*(uint32_t *)0x6b9868) /* collision events, 0x68 bytes each */
#define g_event_capacity (*(uint32_t *)0x6b9864)
#define EVENT_SIZE 0x68u
#define g_grid_pool (*(uint32_t *)0x6b9b90) /* grid link pool */

/* Sound engine state written by sound_play (0x471c90) and 0x471da0: not compared. */
struct range {
    uint32_t start, end;
};
static const struct range sound_state[] = {
    {0x4b1c6c, 0x4b1c70}, /* last voice handed out */
    {0x5ff300, 0x5ff340}, /* per voice flag */
    {0x5ff4a0, 0x5ff4a0 + 0x30 * 0x40}, /* per voice game state */
    {0x6cb500, 0x6cb500 + 0x3c * 0x40}, /* per voice mixer state */
};

/* Scratch: temporaries of the fixed-point maths routines (0x428090 and
 * friends). Not compared; the collision event array is scratch too (the
 * queue is always emptied within a step) and is checked in is_ignored. Some
 * contacts leave event fields unset, so they hold leftover stack values that
 * depend on the caller's stack layout. */
static const struct range scratch_state[] = {
    {0x6ccbe0, 0x6ccce4},
};

/* Written by the timer thread (0x4365ed and what it calls): live, never
 * compared or restored. One dword each. */
static const struct range timer_state[] = {
    {0x4be320, 0x4be324}, {0x4be388, 0x4be38c}, {0x4be3e8, 0x4be3ec}, {0x4be468, 0x4be46c},
    {0x4be4ac, 0x4be4b0}, {0x4be4b0, 0x4be4b4},
    {0x5f72a8, 0x5f72ac}, {0x6c7ff8, 0x6c7ffc},
    {0x4ac990, 0x4ac994}, /* screen fade level (0x430190) */
    {0x6b8d68, 0x6b8d6c}, /* pause menu blink counter (0x463c70) */
};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static uint32_t stack_low, stack_high; /* below the verified call's frame */

static int enabled, survey_mode;
static const struct hook *replacements;
static int replacement_count;

static uint8_t *saved_before, *saved_original;
static uint32_t buffer_size;

struct region {
    uint8_t *start;
    uint32_t size;
};
#define MAX_REGIONS 16
static struct region regions[MAX_REGIONS];
static int region_count;

int verify_enabled(void)
{
    return enabled;
}

/* --- sound record and replay --- */

typedef uint32_t(GAMEAPI *sound_fn)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef uint32_t(GAMEAPI *sound_moving_fn)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                          uint32_t g);
enum phase { PHASE_OFF, PHASE_RECORD, PHASE_REPLAY };
/* Both ways of starting a sound hand out a voice (0x472920); the original's
 * voices are recorded and handed back to ours in the same order. */
struct sound_call {
    uint32_t kind, args[7];
    uint32_t result; /* all of eax: callers may keep the bits above the voice */
};
#define MAX_SOUNDS 128
static enum phase phase;
static struct sound_call sounds[MAX_SOUNDS];
static int sound_count, sound_next, sound_mismatch;
uint32_t GAMEAPI sound_play_body(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); /* 0x471c90, src/game/orig_wc.c */
uint32_t GAMEAPI sound_moving_body(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); /* 0x471da0 */

/* Replay: the recorded voice for this call, or 0x7f ("no channel"). */
static uint32_t sound_replay(uint32_t kind, const uint32_t *args)
{
    if (sound_next >= sound_count) {
        sound_mismatch = 1;
        return 0x7f;
    }
    if (sounds[sound_next].kind != kind || memcmp(sounds[sound_next].args, args, 7 * sizeof(*args)) != 0)
        sound_mismatch = 1;
    return sounds[sound_next++].result;
}

static void sound_record(uint32_t kind, const uint32_t *args, uint32_t result)
{
    if (phase == PHASE_RECORD && sound_count < MAX_SOUNDS) {
        sounds[sound_count].kind = kind;
        memcpy(sounds[sound_count].args, args, 7 * sizeof(*args));
        sounds[sound_count++].result = result;
    }
}

static uint32_t GAMEAPI sound_play_hook(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t args[7] = {a, b, c, d, e, 0, 0};
    uint32_t result;

    if (phase == PHASE_REPLAY)
        return sound_replay(0, args);
    result = sound_play_body(a, b, c, d, e);
    sound_record(0, args, result);
    return result;
}

/* 0x471da0: start a sound that follows a position (called by 0x471e60). */
static uint32_t GAMEAPI sound_moving_hook(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                         uint32_t g)
{
    /* a is never read (callers leave stack bits in it), so it is not compared. */
    uint32_t args[7] = {0, b, c, d, e, f, g};
    uint32_t result;

    if (phase == PHASE_REPLAY)
        return sound_replay(1, args);
    result = sound_moving_body(a, b, c, d, e, f, g);
    sound_record(1, args, result);
    return result;
}

static const struct hook sound_hook = {
    "sound_play (verify)", 0x471c90, sound_play_hook, {0xa1, 0x9c, 0x45, 0x4d, 0x00, 0x53, 0x8b, 0x5c}};
static const struct hook sound_moving_hook_def = {
    "sound_moving (verify)", 0x471da0, sound_moving_hook, {0xa1, 0x9c, 0x45, 0x4d, 0x00, 0x53, 0x55, 0x56}};

/* --- memory snapshots --- */

/* The game's data and bss, and the heap block holding the bodies and cars.
 * (0x4c2468.. describe the game allocator's limits, which span unrelated
 * Wine memory; the bodies sit in one committed block of about 10 MB.) */
static void add_region(uint32_t start, uint32_t end)
{
    int i;

    for (i = 0; i < region_count; i++)
        if ((uint32_t)regions[i].start == start)
            return;
    if (region_count < MAX_REGIONS && end > start) {
        regions[region_count].start = (uint8_t *)start;
        regions[region_count++].size = end - start;
    }
}

/* Every committed part of the allocation that holds `address`. */
static void add_allocation(uint32_t address)
{
    struct memory_basic_information info;
    uint32_t base, a;

    if (!VirtualQuery((void *)address, &info, sizeof(info)))
        return;
    base = (uint32_t)info.allocation_base;
    for (a = base; VirtualQuery((void *)a, &info, sizeof(info)) && (uint32_t)info.allocation_base == base;
         a = (uint32_t)info.base_address + info.region_size) {
        if (info.state == MEM_COMMIT)
            add_region((uint32_t)info.base_address, (uint32_t)info.base_address + info.region_size);
    }
}

static void find_regions(void)
{
    region_count = 0;
    add_region(DATA_START, DATA_END);
    if (*(uint32_t *)0x4c2474) /* the game heap: one GlobalAlloc block (0x444500) */
        add_allocation(*(uint32_t *)0x4c2474);
    if (g_bodies && g_body_count)
        add_allocation(g_bodies[0]);
    if (g_grid_pool)
        add_allocation(g_grid_pool);
}

static void save(uint8_t *buffer)
{
    int i;

    for (i = 0; i < region_count; i++) {
        memcpy(buffer, regions[i].start, regions[i].size);
        buffer += regions[i].size;
    }
}

static void restore(const uint8_t *buffer)
{
    uint32_t live[COUNT(timer_state)];
    unsigned i;

    for (i = 0; i < COUNT(timer_state); i++)
        live[i] = *(volatile uint32_t *)timer_state[i].start;
    for (i = 0; i < (unsigned)region_count; i++) {
        memcpy(regions[i].start, buffer, regions[i].size);
        buffer += regions[i].size;
    }
    for (i = 0; i < COUNT(timer_state); i++)
        *(volatile uint32_t *)timer_state[i].start = live[i];
}

void verify_init(const struct hook *hooks, int count)
{
    char buf[8];

    /* sound_play and the moving-sound start: always ours (their bodies are C);
     * the hooks also record / replay them for verification */
    int sound_ok = hook_install(&sound_hook) & hook_install(&sound_moving_hook_def);

    if (!GetEnvironmentVariableA("REROLL_VERIFY", buf, sizeof(buf)) || buf[0] == '0')
        return;
    survey_mode = buf[0] == 's';
    replacements = hooks;
    replacement_count = count;
    if (!sound_ok) {
        log_printf("verify: sound hook failed, verification off");
        return;
    }
    enabled = 1;
    log_printf("verify: on");
}

/* --- survey (REROLL_VERIFY=survey): where in memory does a step write? --- */

#define PAGE 4096u
#define MAX_PAGES 40000u
static uint32_t page_hash[MAX_PAGES];
static uint8_t page_readable[MAX_PAGES];
static int survey_steps;

static uint32_t hash_page(const uint32_t *p)
{
    uint32_t h = 0, i;

    for (i = 0; i < PAGE / 4; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static void survey_map(uint32_t start, uint32_t end)
{
    struct memory_basic_information info;
    uint32_t a = start;

    while (a < end && VirtualQuery((void *)a, &info, sizeof(info))) {
        uint32_t base = (uint32_t)info.base_address, next = base + info.region_size, p;
        int readable = info.state == MEM_COMMIT && (info.protect & 0xee) && !(info.protect & 0x100);

        log_printf("survey: %p..%p state %x protect %x", base, next, info.state, info.protect);
        for (p = a; p < next && p < end; p += PAGE)
            if ((p - start) / PAGE < MAX_PAGES)
                page_readable[(p - start) / PAGE] = (uint8_t)readable;
        a = next;
    }
}

static void survey_hash(uint32_t start, uint32_t end, int compare)
{
    uint32_t p, run = 0, i;

    for (p = start, i = 0; p < end && i < MAX_PAGES; p += PAGE, i++) {
        uint32_t h = page_readable[i] ? hash_page((const uint32_t *)p) : 0;
        int changed = compare && h != page_hash[i];

        if (changed && !run)
            run = p;
        if (!changed && run) {
            log_printf("survey: step %u wrote %p..%p", survey_steps, run, p);
            run = 0;
        }
        page_hash[i] = h;
    }
    if (run)
        log_printf("survey: step %u wrote %p..%p", survey_steps, run, p);
}

static int32_t survey_call(verify_fn1 ours, int32_t arg)
{
    uint32_t i, cars = g_race_data ? g_race_data[0x54d] : 0, lo = ~0u, hi = 0;
    uint32_t start = DATA_START, end = 0x8500000; /* past the heap block seen */
    int32_t result;

    if (survey_steps == 0) {
        survey_map(start, end);
        for (i = 0; i < cars; i++)
            log_printf("survey: car %u at %p", i, *(uint32_t *)(0x6bfb80 + i * 0x274));
        for (i = 0; g_bodies && i < g_body_count; i++) {
            if (g_bodies[i] < lo)
                lo = g_bodies[i];
            if (g_bodies[i] > hi)
                hi = g_bodies[i];
        }
        log_printf("survey: %u bodies between %p and %p", g_body_count, lo, hi);
    }
    survey_hash(start, end, 0);
    result = ours(arg);
    survey_hash(start, end, 1);
    if (++survey_steps >= 5) {
        enabled = 0;
        log_printf("survey: done");
    }
    return result;
}

/* Buffers for the regions found at each verified call; they grow (and the
 * regions are logged again) when race setup or the heap adds memory. */
static int make_buffers(void)
{
    uint32_t need = 0;
    int i;

    find_regions();
    for (i = 0; i < region_count; i++)
        need += regions[i].size;
    if (saved_before && need <= buffer_size)
        return 1;
    if (saved_before) {
        VirtualFree(saved_before, 0, MEM_RELEASE);
        VirtualFree(saved_original, 0, MEM_RELEASE);
    }
    buffer_size = need;
    saved_before = VirtualAlloc(0, buffer_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    saved_original = VirtualAlloc(0, buffer_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!saved_before || !saved_original) {
        log_printf("verify: no memory for %u byte snapshots, verification off", buffer_size);
        enabled = 0;
        return 0;
    }
    for (i = 0; i < region_count; i++)
        log_printf("verify: snapshot %p, %x bytes", (uint32_t)regions[i].start, regions[i].size);
    log_printf("verify: grid pool %p, %u bytes per snapshot", g_grid_pool, buffer_size);
    return 1;
}

static void set_replacements(int active)
{
    int i;

    for (i = 0; i < replacement_count; i++)
        hook_set_active(&replacements[i], active);
}

/* --- reporting --- */


static char *put_text(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    return out;
}

static char *put_number(char *out, uint32_t value, uint32_t base)
{
    char digits[12];
    int n = 0;

    do {
        digits[n++] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value);
    while (n)
        *out++ = digits[--n];
    return out;
}

/* Describe an address: "car2+1a4", "body7+34", or "data"/"heap". */
static void describe(uint32_t address, char *out)
{
    uint32_t i, cars = g_race_data ? g_race_data[0x54d] : 0;

    for (i = 0; i < cars; i++) {
        uint32_t car = *(uint32_t *)(g_car_slots + i * CAR_SLOT_SIZE);
        if (car && address >= car && address < car + CAR_OBJECT_SIZE) {
            out = put_number(put_text(out, "car"), i, 10);
            *put_number(put_text(out, "+"), address - car, 16) = 0;
            return;
        }
    }
    for (i = 0; g_bodies && i < g_body_count; i++) {
        uint32_t body = g_bodies[i];
        if (body && address >= body && address < body + BODY_SIZE) {
            out = put_number(put_text(out, "body"), i, 10);
            *put_number(put_text(out, "+"), address - body, 16) = 0;
            return;
        }
    }
    if (g_events && address >= g_events && address < g_events + g_event_capacity * EVENT_SIZE) {
        out = put_number(put_text(out, "event"), (address - g_events) / EVENT_SIZE, 10);
        *put_number(put_text(out, "+"), (address - g_events) % EVENT_SIZE, 16) = 0;
        return;
    }
    *put_text(out, address >= DATA_START && address < DATA_END ? "data" : "heap") = 0;
}

static int in_ranges(const struct range *ranges, unsigned count, uint32_t address)
{
    unsigned i;

    for (i = 0; i < count; i++)
        if (address >= ranges[i].start && address < ranges[i].end)
            return 1;
    return 0;
}

/* Light records (0x441d00, 16 of 0x24 bytes at 0x5d7300): the launchers
 * pass a one-word body pointer as a three-word vector, so the last two words
 * hold the caller's leftover stack. */
#define LIGHTS 0x5d7300u
static int is_light_leftover(uint32_t address)
{
    return address >= LIGHTS && address < LIGHTS + 16 * 0x24 && (address - LIGHTS) % 0x24 >= 0x1c;
}

static int is_ignored(uint32_t address)
{
    return in_ranges(sound_state, COUNT(sound_state), address) || is_light_leftover(address) ||
           in_ranges(timer_state, COUNT(timer_state), address) ||
           in_ranges(scratch_state, COUNT(scratch_state), address) ||
           (g_events && address >= g_events && address < g_events + g_event_capacity * EVENT_SIZE) ||
           (address >= stack_low && address < stack_high);
}

#define MAX_REPORTED_CALLS 20
#define MAX_RANGES_PER_CALL 16

/* Compare game memory (our run) with the original's saved results. */
static int compare(const struct verify_point *point)
{
    const char *name = point->name;
    uint32_t calls = point->calls;
    const uint8_t *orig = saved_original;
    uint32_t ranges = 0;
    int i, report = point->mismatched < MAX_REPORTED_CALLS;

    for (i = 0; i < region_count; i++) {
        const uint8_t *ours = regions[i].start;
        uint32_t off = 0, size = regions[i].size;

        while (off < size) {
            uint32_t start, end, address;
            char where[32];

            if (size - off >= 4 && *(const uint32_t *)(ours + off) == *(const uint32_t *)(orig + off)) {
                off += 4;
                continue;
            }
            if (ours[off] == orig[off] || is_ignored((uint32_t)(ours + off))) {
                off++;
                continue;
            }
            /* a differing range: extend while differences are close together */
            start = end = off;
            while (end < size && end - start < 0x10000) {
                uint32_t gap = 0;
                while (end + gap < size && gap < 16 && ours[end + gap] == orig[end + gap])
                    gap++;
                if (gap >= 16 || end + gap >= size)
                    break;
                end += gap + 1;
            }
            address = (uint32_t)(ours + start);
            if (report && ranges < MAX_RANGES_PER_CALL) {
                describe(address, where);
                log_printf("verify %s: step %u: %p (%s) %u bytes differ, original %x ours %x", name,
                           calls, address, where, end - start, *(const uint32_t *)(orig + start),
                           *(const uint32_t *)(ours + start));
            }
            ranges++;
            off = end;
        }
        orig += size;
    }
    if (ranges && report && ranges > MAX_RANGES_PER_CALL)
        log_printf("verify %s: step %u: %u more ranges", name, calls, ranges - MAX_RANGES_PER_CALL);
    return ranges != 0;
}

/* Run original and ours from the same state (see the top of the file). */
#define COUNT_TOUCHED 512
static struct verify_point *touched[COUNT_TOUCHED];
static int touched_count;

static int32_t verify_run(struct verify_point *point, verify_fn2 original, verify_fn2 ours, int32_t a, int32_t b)
{
    int32_t result_original, result_ours;
    uint64_t start, end;
    int bad;

    stack_high = (uint32_t)&result_original;
    stack_low = stack_high - 0x20000;

    QueryPerformanceCounter(&start);
    save(saved_before);

    set_replacements(0);
    phase = PHASE_RECORD;
    sound_count = 0;
    result_original = original(a, b);
    set_replacements(1);
    save(saved_original);

    restore(saved_before);
    phase = PHASE_REPLAY;
    sound_next = sound_mismatch = 0;
    result_ours = ours(a, b);
    phase = PHASE_OFF;

    bad = compare(point);
    QueryPerformanceCounter(&end);
    {
        static uint32_t ticks_per_micro;

        if (!ticks_per_micro) {
            uint64_t frequency;

            QueryPerformanceFrequency(&frequency);
            ticks_per_micro = (uint32_t)frequency / 1000000; /* the counter runs below 4 GHz */
            if (!ticks_per_micro)
                ticks_per_micro = 1;
        }
        point->micros += (uint32_t)(end - start) / ticks_per_micro;
    }

    if ((result_ours != result_original && !point->void_result) || sound_mismatch || sound_next != sound_count) {
        if (point->mismatched < MAX_REPORTED_CALLS)
            log_printf("verify %s: step %u: result %d ours %d, sounds %d ours %d%s", point->name, point->calls,
                       result_original, result_ours, sound_count, sound_next,
                       sound_mismatch ? " (calls differ)" : "");
        bad = 1;
    }
    if (bad)
        point->mismatched++;
    if (point->calls++ == 0 && touched_count < COUNT_TOUCHED)
        touched[touched_count++] = point;
    if (point->calls % point->report_every == 0) {
        log_printf("verify %s: %u calls, %u mismatched, %u us per check", point->name, point->calls,
                   point->mismatched, point->micros / point->calls);
        if (point->log_coverage) {
            int i;
            for (i = 0; i < COVER_COUNT; i++)
                if (coverage[i])
                    log_printf("verify coverage: %s %u", coverage_names[i], coverage[i]);
        }
    }

    restore(saved_original);
    return result_original;
}

/* Every point checked at least once, for the summary at exit (the periodic
 * lines only come every report_every checks). */
void verify_summary(void)
{
    int i;
    if (!enabled)
        return;
    for (i = 0; i < touched_count; i++)
        log_printf("verify summary %s: %u calls, %u mismatched", touched[i]->name, touched[i]->calls,
                   touched[i]->mismatched);
}

int32_t verify_call2(struct verify_point *point, verify_fn2 original, verify_fn2 ours, int32_t a, int32_t b)
{
    static int depth;
    int32_t result;

    if (!enabled || survey_mode || depth || point->seen++ % point->sample || !make_buffers())
        return ours(a, b);
    depth++;
    result = verify_run(point, original, ours, a, b);
    depth--;
    return result;
}

int32_t verify_call1(const char *name, verify_fn1 original, verify_fn1 ours, int32_t arg)
{
    static struct verify_point world = {0, 1, 1000, 1, 0, 0, 0, 0, 0};

    world.name = name;
    if (enabled && survey_mode)
        return survey_call(ours, arg);
    return verify_call2(&world, (verify_fn2)(void *)original, (verify_fn2)(void *)ours, arg, 0);
}

#endif
