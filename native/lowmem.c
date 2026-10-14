/* Memory below 2 GB for the game (docs/native/plan.md): Windows never gives
 * a 32-bit program addresses with bit 31 set, and the game relies on it (its
 * ordering-table links use bit 31 as the "table entry" flag, as on the
 * PlayStation). On 32-bit Linux glibc's heap and mmap live near the top, so
 * everything the game allocates (Heap*, Global*, Virtual*, thread stacks,
 * the fake DirectDraw's objects) comes from here instead: a region reserved
 * at 0x20000000-0x7f000000, handed out in 64 KB granules, with power-of-two
 * size classes for small blocks. */
#include "nt.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

#define REGION_START 0x20000000u
#define REGION_END 0x7f000000u
#define GRANULE 0x10000u
#define GRANULES ((REGION_END - REGION_START) / GRANULE)
#define MIN_CLASS 4  /* 16 bytes */
#define MAX_CLASS 15 /* 32 KB */

/* per granule: 0 free, 1..15 small blocks of class n, 0x10 start of a large
 * run (count in run_length), 0x11 inside a large run, 0x20 reserved by
 * VirtualAlloc (start), 0x21 inside; | 0x40 when it belongs to the game's
 * arena (freed all at once when the game restarts in place, plan step 7) */
#define GAME 0x40
static uint8_t kind[GRANULES];
static uint32_t run_length[GRANULES];
static void *free_list[2][MAX_CLASS + 1];
static int arena = GAME; /* where new allocations go: GAME or 0 (ours) */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t search_from;
static int ready;

#if !defined(__i386__)
/* A 64-bit program's heap (brk) starts anywhere in the first GB above its
 * image, which would land in the region. So the region is part of the image:
 * a section with no file contents, linked at REGION_START (the Makefile's
 * --section-start=.lowmem); the heap then starts above it. */
__asm__(".section .lowmem,\"aw\",@nobits\n"
        ".p2align 16\n"
        "nt_lowmem_image:\n"
        ".zero 0x5f000000\n" /* REGION_END - REGION_START */
        ".previous\n");
#define RESERVE_FLAGS MAP_FIXED /* over our own section */
#else
#define RESERVE_FLAGS MAP_FIXED_NOREPLACE
#endif

void nt_lowmem_init(void)
{
    if (mmap((void *)REGION_START, REGION_END - REGION_START, PROT_NONE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | RESERVE_FLAGS, -1, 0) != (void *)REGION_START)
    {
        /* what is in the way */
        FILE *maps = fopen("/proc/self/maps", "r");
        char line[256];
        while (maps && fgets(line, sizeof line, maps)) {
            unsigned long lo, hi;
            if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && lo < REGION_END && hi > REGION_START)
                nt_log("lowmem: in the way: %s", line);
        }
        if (maps)
            fclose(maps);
        nt_fatal("cannot reserve the game's memory below 2 GB");
    }
    ready = 1;
}

static void *granule_address(uint32_t g)
{
    return (void *)(uintptr_t)(REGION_START + g * GRANULE);
}

/* Finds n free granules in a row; marks them with `start_kind` / `start_kind + 1`. */
static uint32_t take_granules(uint32_t n, uint8_t start_kind)
{
    uint32_t g, run = 0, pass;
    for (pass = 0; pass < 2; pass++) {
        for (g = pass ? 0 : search_from; g < GRANULES; g++) {
            run = kind[g] ? 0 : run + 1;
            if (run == n) {
                uint32_t first = g + 1 - n, i;
                kind[first] = start_kind;
                for (i = 1; i < n; i++)
                    kind[first + i] = (uint8_t)(start_kind + 1);
                run_length[first] = n;
                search_from = g + 1;
                return first;
            }
        }
        run = 0;
    }
    return 0xffffffffu;
}

static void release_granules(uint32_t first)
{
    uint32_t n = run_length[first], i;
    for (i = 0; i < n; i++)
        kind[first + i] = 0;
    if (first < search_from)
        search_from = first;
}

static int size_class(uint32_t size)
{
    int c = MIN_CLASS;
    while ((1u << c) < size)
        c++;
    return c;
}

/* which arena allocations go to next; returns the previous one */
int nt_lowmem_arena(int game)
{
    int old = arena == GAME;
    arena = game ? GAME : 0;
    return old;
}

void *nt_lowmem_alloc(uint32_t size)
{
    void *p = 0;
    int a = arena ? 1 : 0;
    if (!ready)
        nt_lowmem_init();
    if (!size)
        size = 1;
    pthread_mutex_lock(&lock);
    if (size <= (1u << MAX_CLASS)) {
        int c = size_class(size);
        if (!free_list[a][c]) {
            uint32_t g = take_granules(1, (uint8_t)(c | arena)), off;
            uint8_t *base;
            if (g == 0xffffffffu)
                goto out;
            base = granule_address(g);
            mprotect(base, GRANULE, PROT_READ | PROT_WRITE);
            for (off = 0; off + (1u << c) <= GRANULE; off += 1u << c) {
                *(void **)(base + off) = free_list[a][c];
                free_list[a][c] = base + off;
            }
        }
        p = free_list[a][c];
        free_list[a][c] = *(void **)p;
    } else {
        uint32_t n = (size + GRANULE - 1) / GRANULE, g = take_granules(n, (uint8_t)(0x10 | arena));
        if (g == 0xffffffffu)
            goto out;
        p = granule_address(g);
        mprotect(p, n * GRANULE, PROT_READ | PROT_WRITE);
    }
out:
    pthread_mutex_unlock(&lock);
    if (!p)
        nt_log("lowmem: out of memory for %u bytes", size);
    return p;
}

int nt_lowmem_owns(const void *p)
{
    uint32_t a = (uint32_t)(uintptr_t)p;
    return a >= REGION_START && a < REGION_END;
}

uint32_t nt_lowmem_size(const void *p)
{
    uint32_t g = ((uint32_t)(uintptr_t)p - REGION_START) / GRANULE;
    uint8_t k = kind[g] & ~GAME;
    if (k && k <= MAX_CLASS)
        return 1u << k;
    if (k == 0x10)
        return run_length[g] * GRANULE;
    return 0;
}

void nt_lowmem_free(void *p)
{
    uint32_t g;
    if (!p || !nt_lowmem_owns(p))
        return;
    g = ((uint32_t)(uintptr_t)p - REGION_START) / GRANULE;
    pthread_mutex_lock(&lock);
    if ((kind[g] & ~GAME) && (kind[g] & ~GAME) <= MAX_CLASS) {
        int a = kind[g] & GAME ? 1 : 0, c = kind[g] & ~GAME;
        *(void **)p = free_list[a][c];
        free_list[a][c] = p;
    } else if ((kind[g] & ~GAME) == 0x10) {
        madvise(p, run_length[g] * GRANULE, MADV_DONTNEED);
        mprotect(p, run_length[g] * GRANULE, PROT_NONE);
        release_granules(g);
    }
    pthread_mutex_unlock(&lock);
}

void *nt_lowmem_realloc(void *p, uint32_t size)
{
    uint32_t old;
    void *q;
    if (!p)
        return nt_lowmem_alloc(size);
    old = nt_lowmem_size(p);
    if (size <= old && (old <= (1u << MAX_CLASS) || size > old / 2))
        return p;
    q = nt_lowmem_alloc(size);
    if (q) {
        memcpy(q, p, old < size ? old : size);
        nt_lowmem_free(p);
    }
    return q;
}

/* VirtualAlloc reservations: whole granules, left inaccessible until committed */
void *nt_lowmem_reserve(uint32_t size)
{
    uint32_t n = (size + GRANULE - 1) / GRANULE, g;
    if (!ready)
        nt_lowmem_init();
    pthread_mutex_lock(&lock);
    g = take_granules(n, (uint8_t)(0x20 | arena));
    pthread_mutex_unlock(&lock);
    return g == 0xffffffffu ? 0 : granule_address(g);
}

int nt_lowmem_unreserve(void *p)
{
    uint32_t g;
    if (!nt_lowmem_owns(p))
        return 0;
    g = ((uint32_t)(uintptr_t)p - REGION_START) / GRANULE;
    if ((kind[g] & ~GAME) != 0x20)
        return 0;
    pthread_mutex_lock(&lock);
    madvise(p, run_length[g] * GRANULE, MADV_DONTNEED);
    mprotect(p, run_length[g] * GRANULE, PROT_NONE);
    release_granules(g);
    pthread_mutex_unlock(&lock);
    return 1;
}

/* The game restarts in place: everything in its arena goes. */
void nt_lowmem_reset_game(void)
{
    uint32_t g, freed = 0;
    pthread_mutex_lock(&lock);
    for (g = 0; g < GRANULES; g++)
        if (kind[g] & GAME) {
            uint8_t k = kind[g] & ~GAME;
            if (k == 0x11 || k == 0x21)
                continue;
            {
                uint32_t n = (k == 0x10 || k == 0x20) ? run_length[g] : 1, i;
                madvise(granule_address(g), n * GRANULE, MADV_DONTNEED);
                mprotect(granule_address(g), n * GRANULE, PROT_NONE);
                for (i = 0; i < n; i++)
                    kind[g + i] = 0;
                freed += n;
            }
        }
    memset(free_list[1], 0, sizeof free_list[1]);
    search_from = 0;
    pthread_mutex_unlock(&lock);
    nt_log("lowmem: freed %u MB of the game's memory", freed * GRANULE >> 20);
}

