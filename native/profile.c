/* A sampling profiler for the native build (REROLL_PROFILE=1). The kernel
 * samples the main thread's CPU time 4000 times a second (perf_event_open,
 * with a copy of the top of its stack), so the game itself is never
 * interrupted by a signal (SDL's Wayland code quits on those). A helper
 * thread turns each sample into: where it was, the first return address on
 * the stack inside Rollcage.exe (which original code it was working for),
 * the first inside our own code, and the frame number. Samples go to
 * profile.bin next to the logs, with /proc/self/maps in profile.maps;
 * tools/profile.py reads them. Time the thread spends waiting is not
 * sampled: frame times show that. */
#include "nt.h"

#include <linux/perf_event.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct sample {
    uint32_t eip, exe_caller, frame, own_caller;
};
extern char _init[], _fini[]; /* our own code */

#define STACK_COPY 2048
#define RING_PAGES 256 /* 1 MB */
#define MAX_FRAMES (1u << 21)

uint32_t nt_profile_stack_top; /* loader.c: the game's stack (unused here, kept for the loader) */
static FILE *out;
static struct perf_event_mmap_page *ring;
static size_t page_size;
static uint64_t *frame_times; /* CLOCK_MONOTONIC ns at each frame's end */
static volatile uint32_t frame_count;

static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void write_maps(void)
{
    char path[600];
    FILE *maps, *copy;
    int c;
    snprintf(path, sizeof path, "%s/profile.maps", nt_config.data_dir);
    maps = fopen("/proc/self/maps", "r");
    copy = fopen(path, "w");
    while (maps && copy && (c = fgetc(maps)) != EOF)
        fputc(c, copy);
    if (maps)
        fclose(maps);
    if (copy)
        fclose(copy);
}

static int is_call_return(uint32_t v)
{
    const uint8_t *c = (const uint8_t *)v;
    return c[-5] == 0xe8 || c[-6] == 0xff || c[-2] == 0xff || c[-3] == 0xff;
}

static uint32_t frame_at(uint64_t t)
{
    uint32_t lo = 0, hi = frame_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (frame_times[mid] < t)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* One PERF_RECORD_SAMPLE: ip, time, stack size, stack bytes, dyn size. */
static void take(const uint8_t *body)
{
    uint64_t ip, time, size;
    const uint32_t *stack;
    uint32_t own_lo = (uint32_t)_init, own_hi = (uint32_t)_fini, i, n;
    struct sample s = {0, 0, 0, 0};
    memcpy(&ip, body, 8);
    memcpy(&time, body + 8, 8);
    memcpy(&size, body + 16, 8);
    stack = (const uint32_t *)(body + 24);
    n = (uint32_t)size / 4;
    s.eip = (uint32_t)ip;
    for (i = 0; i < n && (!s.exe_caller || !s.own_caller); i++) {
        uint32_t v = stack[i];
        int in_exe = v >= 0x401000 && v < 0x4a0000, in_own = v >= own_lo && v < own_hi;
        if (!(in_exe || in_own) || !is_call_return(v))
            continue;
        if (in_exe && !s.exe_caller && (s.eip < 0x401000 || s.eip >= 0x4a0000))
            s.exe_caller = v;
        if (in_own && !s.own_caller && (s.eip < own_lo || s.eip >= own_hi))
            s.own_caller = v;
    }
    s.frame = frame_at(time);
    fwrite(&s, sizeof s, 1, out);
}

static void *reader(void *arg)
{
    uint8_t *data = (uint8_t *)ring + page_size;
    uint64_t data_size = (uint64_t)RING_PAGES * page_size;
    static uint8_t record[STACK_COPY + 256];
    (void)arg;
    for (;;) {
        /* the low halves are enough (1 MB ring; under 4 GB of samples), and
         * 64-bit atomics would need libatomic here */
        uint64_t head = __atomic_load_n((volatile uint32_t *)&ring->data_head, __ATOMIC_ACQUIRE), tail = ring->data_tail;
        while (tail < head) {
            struct perf_event_header h;
            uint64_t at = tail % data_size, i;
            for (i = 0; i < sizeof h; i++)
                ((uint8_t *)&h)[i] = data[(at + i) % data_size];
            if (h.size > sizeof record || h.size < sizeof h)
                break;
            for (i = 0; i < h.size; i++)
                record[i] = data[(at + i) % data_size];
            if (h.type == PERF_RECORD_SAMPLE)
                take(record + sizeof h);
            tail += h.size;
        }
        __atomic_thread_fence(__ATOMIC_RELEASE);
        ring->data_tail = tail;
        fflush(out);
        usleep(10000);
    }
    return 0;
}

/* At the end of every frame. */
void nt_profile_flush(void)
{
    static uint32_t calls;
    if (!out)
        return;
    if (frame_count < MAX_FRAMES) {
        frame_times[frame_count] = now_ns();
        __atomic_store_n(&frame_count, frame_count + 1, __ATOMIC_RELEASE);
    }
    if (calls++ % 512 == 0) /* libraries loaded since */
        write_maps();
}

void nt_profile_start(void)
{
    struct perf_event_attr attr;
    char path[600];
    pthread_t thread;
    int fd;

    if (!getenv("REROLL_PROFILE"))
        return;
    memset(&attr, 0, sizeof attr);
    attr.size = sizeof attr;
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_TASK_CLOCK;
    attr.sample_period = 250000; /* ns of the thread's CPU time */
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TIME | PERF_SAMPLE_STACK_USER;
    attr.sample_stack_user = STACK_COPY;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.use_clockid = 1;
    attr.clockid = CLOCK_MONOTONIC;
    fd = (int)syscall(SYS_perf_event_open, &attr, (pid_t)syscall(SYS_gettid), -1, -1, 0);
    if (fd < 0) {
        nt_log("profile: perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid)");
        return;
    }
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    ring = mmap(0, (RING_PAGES + 1) * page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    frame_times = calloc(MAX_FRAMES, sizeof *frame_times);
    snprintf(path, sizeof path, "%s/profile.bin", nt_config.data_dir);
    out = fopen(path, "wb");
    if (ring == MAP_FAILED || !frame_times || !out) {
        nt_log("profile: no ring buffer or output");
        out = 0;
        return;
    }
    write_maps();
    pthread_create(&thread, 0, reader, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    nt_log("profile: sampling the main thread's CPU time at 4 kHz into profile.bin");
}
