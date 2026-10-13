/* The game's built-in data without the original exe (docs/native/plan64.md,
 * phase 2). `rollcage --extract-data` (or the first run, when the file is
 * missing) copies the user's Rollcage.exe, minus its code, to
 * reroll_data.bin next to it: the headers and every section except .text,
 * whose raw data is dropped (raw size 0). Only the version our C is built
 * for is accepted (757760 bytes, sha256 0648503b...). The loader maps that
 * file like the exe (the default; REROLL_NO_EXE=0 or REROLL_VERIFY map the
 * original instead) and fills the code range with int3: only our hooks'
 * jumps are written into it (src/hook.c), so calls by address still reach
 * the C, and any original code still needed traps and is named in
 * native.log. */
#include "nt.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>

int nt_no_exe;

static uint32_t get32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

int nt_data_extract(const char *exe, const char *out_path)
{
    int fd = open(exe, O_RDONLY);
    struct stat st;
    uint8_t *file, *out, *nt, *sections;
    uint32_t header_size, at, i, count;
    FILE *f;

    if (fd < 0 || fstat(fd, &st)) {
        fprintf(stderr, "cannot open %s\n", exe);
        return 0;
    }
    file = malloc((size_t)st.st_size);
    out = calloc(1, (size_t)st.st_size);
    if (!file || !out || read(fd, file, (size_t)st.st_size) != st.st_size) {
        fprintf(stderr, "cannot read %s\n", exe);
        return 0;
    }
    close(fd);
    nt = file + get32(file + 0x3c);
    if (file[0] != 'M' || file[1] != 'Z' || memcmp(nt, "PE\0\0", 4)) {
        fprintf(stderr, "%s is not an exe\n", exe);
        return 0;
    }
    {
        /* the version the C is built for: FNV-1a 64 of the whole file */
        uint64_t h = 0xcbf29ce484222325ull;
        for (off_t k = 0; k < st.st_size; k++)
            h = (h ^ file[k]) * 0x100000001b3ull;
        if (st.st_size != 757760 || h != 0x6277a76425c1ac1bull) {
            fprintf(stderr, "%s is not the Rollcage.exe this build needs (the 1999 PC release, 757760 bytes, sha256 0648503b...)\n",
                    exe);
            return 0;
        }
    }
    header_size = get32(nt + 0x18 + 60);
    count = nt[6] | nt[7] << 8;
    memcpy(out, file, header_size);
    sections = out + get32(file + 0x3c) + 0x18 + (nt[0x14] | nt[0x15] << 8);
    at = header_size;
    for (i = 0; i < count; i++) {
        uint8_t *s = sections + i * 40;
        uint32_t raw = get32(s + 20), size = get32(s + 16);
        if (get32(s + 36) & 0x20) { /* code: dropped */
            put32(s + 16, 0);
            put32(s + 20, 0);
            continue;
        }
        memcpy(out + at, file + raw, size);
        put32(s + 20, at);
        at += (size + 15) & ~15u;
    }
    f = fopen(out_path, "wb");
    if (!f || fwrite(out, 1, at, f) != at) {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 0;
    }
    fclose(f);
    printf("wrote %s (%u bytes: the exe's data, no code)\n", out_path, at);
    free(file);
    free(out);
    return 1;
}

/* REROLL_NO_EXE: an int3 in the code range means original code that is
 * still needed. */
static void on_trap(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;
    uint32_t at = (uint32_t)uc->uc_mcontext.gregs[REG_EIP] - 1;
    uint32_t ret = *(uint32_t *)(uintptr_t)(uint32_t)uc->uc_mcontext.gregs[REG_ESP];
    (void)sig;
    (void)info;
    if (at < 0x401000 || at >= 0x4a3000)
        nt_log("no exe: the original of a wrapped hook is still needed (trampoline %08x)", at);
    nt_log("no exe: original code at %06x is still needed (return address %08x)", at, ret);
    fprintf(stderr, "no exe: original code at %06x is still needed (return address %08x)\n", at, ret);
    _exit(3);
}

void nt_data_trap_install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_trap;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGTRAP, &sa, 0);
}
