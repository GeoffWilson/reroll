/* Which original functions still run (docs/native/plan64.md, phase 2):
 * REROLL_EXE_COVERAGE=<file> (tools/remaining.py --list output: an address
 * at the start of each line) puts a one-shot breakpoint (int3) on the entry
 * of every function listed, except trampoline-wrapped hooks. The first time
 * one runs, its address is logged to native.log and appended to
 * exe_coverage.txt next to the logs, the byte is put back and the function
 * carries on. The breakpoints survive in-place restarts (the code section
 * is not reloaded). */
#include "nt.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#define MAX_TRAPS 4096
static uint32_t trap_at[MAX_TRAPS];
static uint8_t trap_byte[MAX_TRAPS];
static int trap_count;
static FILE *hits;

static int find(uint32_t address)
{
    int lo = 0, hi = trap_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (trap_at[mid] == address)
            return mid;
        if (trap_at[mid] < address)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static void on_trap(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;
    uint32_t at = (uint32_t)uc->uc_mcontext.gregs[REG_EIP] - 1;
    int i = find(at);
    (void)sig;
    (void)info;
    if (i < 0 || *(uint8_t *)(uintptr_t)at != 0xcc) {
        nt_log("exe coverage: unexpected trap at %x", at);
        abort();
    }
    *(uint8_t *)(uintptr_t)at = trap_byte[i];
    uc->uc_mcontext.gregs[REG_EIP] = (greg_t)at;
    nt_log("exe coverage: ran %06x", at);
    if (hits) {
        fprintf(hits, "%06x\n", at);
        fflush(hits);
    }
}

static int compare(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void nt_exe_coverage_start(void)
{
    const char *list = getenv("REROLL_EXE_COVERAGE");
    char line[256], path[600];
    struct sigaction sa;
    FILE *f;
    int i;

    if (!list || !(f = fopen(list, "r")))
        return;
    while (fgets(line, sizeof line, f) && trap_count < MAX_TRAPS) {
        char *end;
        unsigned long a = strtoul(line, &end, 16);
        if (end == line || *end != ' ' || a < 0x401000 || a >= 0x4a2000 || strstr(line, "(wrapped)"))
            continue;
        trap_at[trap_count++] = (uint32_t)a;
    }
    fclose(f);
    qsort(trap_at, (size_t)trap_count, sizeof *trap_at, compare);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_trap;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGTRAP, &sa, 0);
    for (i = 0; i < trap_count; i++) {
        uint8_t *p = (uint8_t *)(uintptr_t)trap_at[i];
        trap_byte[i] = *p;
        *p = 0xcc;
    }
    snprintf(path, sizeof path, "%s/exe_coverage.txt", nt_config.data_dir);
    hits = fopen(path, "a");
    nt_log("exe coverage: %d breakpoints from %s", trap_count, list);
}
