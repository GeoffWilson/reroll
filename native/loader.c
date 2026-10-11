/* The native build's start (docs/native/plan.md, step 1): maps
 * Rollcage.exe at its own addresses, fills its import table from native/,
 * installs the reimplementation's hooks (the DLL's initialisation, src/main.c)
 * and runs the exe's entry point (its C runtime start-up, then WinMain).
 *
 *     build/rollcage [arguments for the game]
 *
 * Environment: REROLL_DRIVE_C / REROLL_DRIVE_D (host directories of C: and
 * the CD), REROLL_EXE (the exe's Windows path), as well as everything the
 * DLL reads (REROLL_VULKAN, ...). */
#include "nt.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <setjmp.h>

struct nt_config nt_config;
_Noreturn void WINAPI ExitProcess(unsigned int code);
static void start_game(void);
static FILE *log_file;

void nt_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (log_file) {
        va_start(ap, fmt);
        vfprintf(log_file, fmt, ap);
        va_end(ap);
        fputc('\n', log_file);
        fflush(log_file);
    }
}

_Noreturn void nt_fatal(const char *fmt, ...)
{
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    nt_log("fatal: %s", text);
    _exit(70);
}

/* ---- the PE image ---- */

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* unresolved imports: each slot points at a thunk "mov eax, index; jmp report" */
#define MAX_UNRESOLVED 1024
static char unresolved_names[MAX_UNRESOLVED][96];
static int unresolved_count;
static uint8_t *thunks;

__attribute__((used)) static void unresolved_called(uint32_t index)
{
    nt_fatal("the game called %s, which the native build does not implement yet",
             index < (uint32_t)unresolved_count ? unresolved_names[index] : "?");
}

void nt_unresolved_entry(void);
__asm__(".text\n"
        "nt_unresolved_entry:\n"
        "    andl $-16, %esp\n"
        "    subl $12, %esp\n"
        "    pushl %eax\n"
        "    call unresolved_called\n");

static void *unresolved_thunk(const char *dll, const char *name)
{
    uint8_t *t;
    int32_t rel;
    if (!thunks) {
        thunks = mmap(0, MAX_UNRESOLVED * 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (thunks == MAP_FAILED)
            nt_fatal("no memory for import thunks");
    }
    if (unresolved_count == MAX_UNRESOLVED)
        nt_fatal("too many unresolved imports");
    snprintf(unresolved_names[unresolved_count], sizeof unresolved_names[0], "%s!%s", dll, name);
    t = thunks + unresolved_count * 16;
    t[0] = 0xb8;
    memcpy(t + 1, &unresolved_count, 4);
    t[5] = 0xe9;
    rel = (int32_t)((uintptr_t)nt_unresolved_entry - (uintptr_t)(t + 10));
    memcpy(t + 6, &rel, 4);
    unresolved_count++;
    return t;
}

/* A stub that stops the game naming `module`!`name` (COM methods too). */
void *nt_named_stub(const char *module, const char *name)
{
    return unresolved_thunk(module, name);
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s = (char)(*s - 'A' + 'a');
}

static uint32_t entry_point;

/* the exe's writable sections as loaded, to start the game again in place */
struct saved_section {
    uint32_t va, size, raw_size;
    uint8_t *data;
};
static struct saved_section saved[8];
static int saved_count;

static void map_exe(const char *path)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    uint8_t *file, *image, *nt, *sections, *imports;
    uint32_t image_size, header_size, import_rva, i;
    uint16_t section_count, optional_size;
    int resolved = 0;

    if (fd < 0 || fstat(fd, &st))
        nt_fatal("cannot open %s: %s", path, strerror(errno));
    file = malloc((size_t)st.st_size);
    if (!file || read(fd, file, (size_t)st.st_size) != st.st_size)
        nt_fatal("cannot read %s", path);
    close(fd);
    if (file[0] != 'M' || file[1] != 'Z')
        nt_fatal("%s is not an exe", path);
    nt = file + u32(file + 0x3c);
    if (memcmp(nt, "PE\0\0", 4) || u16(nt + 4) != 0x14c)
        nt_fatal("%s is not a 32-bit PE", path);
    section_count = u16(nt + 6);
    optional_size = u16(nt + 0x14);
    if (u32(nt + 0x18 + 28) != IMAGE_BASE)
        nt_fatal("unexpected image base %x", u32(nt + 0x18 + 28));
    entry_point = IMAGE_BASE + u32(nt + 0x18 + 16);
    image_size = u32(nt + 0x18 + 56);
    header_size = u32(nt + 0x18 + 60);
    import_rva = u32(nt + 0x18 + 96 + 8);

    image = mmap((void *)IMAGE_BASE, image_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (image != (void *)IMAGE_BASE)
        nt_fatal("cannot map the exe at %x (%s)", IMAGE_BASE, strerror(errno));
    memcpy(image, file, header_size);
    sections = nt + 0x18 + optional_size;
    for (i = 0; i < section_count; i++) {
        const uint8_t *s = sections + i * 40;
        uint32_t va = u32(s + 12), raw_size = u32(s + 16), raw = u32(s + 20), vsize = u32(s + 8);
        if (raw_size > vsize && vsize)
            raw_size = vsize;
        memcpy(image + va, file + raw, raw_size);
        if ((u32(s + 36) & 0x20) && !raw_size) /* the data file: no code, only traps */
            memset(image + va, 0xcc, vsize);
        /* writable, and not the one with the import table (.rdata is read-only) */
        if ((u32(s + 36) & 0x80000000u) && saved_count < 8) {
            saved[saved_count].va = va;
            saved[saved_count].size = vsize > raw_size ? vsize : raw_size;
            saved[saved_count].raw_size = raw_size;
            saved[saved_count].data = malloc(raw_size ? raw_size : 1);
            memcpy(saved[saved_count].data, file + raw, raw_size);
            saved_count++;
        }
    }

    /* imports: (characteristics/OFT, time, chain, name, FirstThunk) */
    for (imports = image + import_rva; u32(imports + 12); imports += 20) {
        char dll[64];
        uint32_t lookup = u32(imports) ? u32(imports) : u32(imports + 16);
        uint32_t *slot = (uint32_t *)(image + u32(imports + 16));
        uint32_t *names = (uint32_t *)(image + lookup);
        snprintf(dll, sizeof dll, "%s", (const char *)image + u32(imports + 12));
        lower(dll);
        for (; *names; names++, slot++) {
            char name[64];
            void *fn;
            if (*names & 0x80000000u)
                snprintf(name, sizeof name, "#%u", *names & 0xffff);
            else
                snprintf(name, sizeof name, "%s", (const char *)image + *names + 2);
            fn = nt_find_export(dll, name);
            if (fn)
                resolved++;
            else
                fn = unresolved_thunk(dll, name);
            *slot = (uint32_t)(uintptr_t)fn;
        }
    }
    nt_log("loader: %s mapped at %x (%x bytes), %d imports implemented, %d not", path, IMAGE_BASE, image_size,
           resolved, unresolved_count);
    free(file);
}

/* ---- configuration ---- */

static void config_default(char *out, size_t size, const char *env, const char *fallback)
{
    const char *v = getenv(env);
    snprintf(out, size, "%s", v && *v ? v : fallback);
}

static void configure(int argc, char **argv)
{
    char fallback[512];
    const char *home = getenv("HOME"), *user = getenv("USER");
    char *p;
    size_t len;
    int i;

    snprintf(fallback, sizeof fallback, "%s/Games/rollcage/prefix/drive_c", home ? home : "");
    config_default(nt_config.drive_c, sizeof nt_config.drive_c, "REROLL_DRIVE_C", fallback);
    snprintf(fallback, sizeof fallback, "/run/media/%s/ROLLCAGE", user ? user : "");
    config_default(nt_config.drive_d, sizeof nt_config.drive_d, "REROLL_DRIVE_D", fallback);
    config_default(nt_config.exe_windows, sizeof nt_config.exe_windows, "REROLL_EXE",
                   "C:\\Program Files (x86)\\Psygnosis\\Rollcage\\Direct3D\\Rollcage.exe");
    nt_no_exe = getenv("REROLL_NO_EXE") && atoi(getenv("REROLL_NO_EXE"));
    {
        /* the game's folder: the exe's, whether or not the exe is there */
        char folder[512];
        snprintf(folder, sizeof folder, "%s", nt_config.exe_windows);
        p = strrchr(folder, '\\');
        if (p)
            *p = 0;
        if (!nt_host_path(folder, nt_config.data_dir, sizeof nt_config.data_dir, 1))
            nt_fatal("cannot find %s (C: is %s)", folder, nt_config.drive_c);
    }
    if (nt_no_exe)
        snprintf(nt_config.exe_host, sizeof nt_config.exe_host, "%s/reroll_data.bin", nt_config.data_dir);
    else if (!nt_host_path(nt_config.exe_windows, nt_config.exe_host, sizeof nt_config.exe_host, 1))
        nt_fatal("cannot find %s (C: is %s)", nt_config.exe_windows, nt_config.drive_c);

    /* the command line as Windows would show it: "exe" args */
    snprintf(nt_config.command_line, sizeof nt_config.command_line, "\"%s\"", nt_config.exe_windows);
    for (i = 1; i < argc; i++) {
        len = strlen(nt_config.command_line);
        snprintf(nt_config.command_line + len, sizeof nt_config.command_line - len, " %s", argv[i]);
    }
}

/* Window decorations on Wayland come from libdecor. Its GTK plugin (the
 * one it prefers under GNOME) loads but shows no title bar for this 32-bit
 * program; the cairo plugin does. So, unless LIBDECOR_PLUGIN_DIR is set,
 * libdecor is pointed at a folder holding only the cairo plugin. */
static void choose_decorations(void)
{
    static const char *plugin = "/usr/lib32/libdecor/plugins-1/libdecor-cairo.so";
    const char *base = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    char dir[512], link[600];

    if (getenv("LIBDECOR_PLUGIN_DIR") || access(plugin, R_OK) != 0)
        return;
    if (base && *base)
        snprintf(dir, sizeof dir, "%s/reroll", base);
    else if (home)
        snprintf(dir, sizeof dir, "%s/.cache/reroll", home);
    else
        return;
    mkdir(dir, 0755);
    strncat(dir, "/libdecor", sizeof dir - strlen(dir) - 1);
    mkdir(dir, 0755);
    snprintf(link, sizeof link, "%s/libdecor-cairo.so", dir);
    if (access(link, R_OK) != 0) {
        unlink(link);
        if (symlink(plugin, link) != 0)
            return;
    }
    setenv("LIBDECOR_PLUGIN_DIR", dir, 1);
}

int main(int argc, char **argv)
{
    char log_path[600], directory[512];
    char *slash;

    if (argc > 1 && !strcmp(argv[1], "--extract-data")) {
        char out[600];
        unsetenv("REROLL_NO_EXE");
        configure(1, argv);
        snprintf(out, sizeof out, "%s/reroll_data.bin", nt_config.data_dir);
        return nt_data_extract(nt_config.exe_host, out) ? 0 : 1;
    }
    configure(argc, argv);
    setenv("REROLL_VULKAN", "1", 0); /* there is no other renderer here */
    choose_decorations();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO))
        nt_fatal("SDL_Init: %s", SDL_GetError());
    snprintf(log_path, sizeof log_path, "%s/native.log", nt_config.data_dir);
    log_file = fopen(log_path, "w");
    nt_log("rollcage native: C: = %s, D: = %s", nt_config.drive_c, nt_config.drive_d);
    nt_log("command line: %s", nt_config.command_line);

    /* Windows (and Wine) have the low megabyte mapped; the original reads
     * through null record pointers there and carries on (e.g. an AI car's
     * replay record, car +0x650, in car_controls_read 0x41b520). */
    if (mmap((void *)0x10000, 0x100000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1,
             0) != (void *)0x10000)
        nt_log("loader: cannot map the low memory at 0x10000 (%s)", strerror(errno));
    nt_lowmem_init();
    nt_lowmem_arena(0); /* ours until the game starts */
    if (nt_no_exe && access(nt_config.exe_host, R_OK) != 0)
        nt_fatal("no %s: run rollcage --extract-data once (it reads Rollcage.exe)", nt_config.exe_host);
    map_exe(nt_config.exe_host);
    if (nt_no_exe)
        nt_data_trap_install();
    nt_registry_load();
    nt_profile_start();

    /* the game starts in its own directory, as from the launcher */
    snprintf(directory, sizeof directory, "%s", nt_config.exe_windows);
    slash = strrchr(directory, '\\');
    if (slash)
        *slash = 0;
    nt_set_current_directory(directory);
    if (chdir(nt_config.data_dir))
        nt_log("cannot enter %s", nt_config.data_dir);

    /* the game runs on a stack below 2 GB, as on Windows */
    {
        uint32_t size = 0x1000000;
        uint8_t *stack = nt_lowmem_alloc(size);
        extern uint32_t nt_profile_stack_top;
        if (!stack)
            nt_fatal("no memory for the game's stack");
        nt_profile_stack_top = (uint32_t)(stack + size);
        __asm__ volatile("movl %0, %%esp\n"
                         "call start_game\n"
                         "ud2\n"
                         :
                         : "r"(stack + size - 64)
                         : "memory");
    }
    return 0;
}

static jmp_buf restart_point;
static char restart_line[1024];
int nt_restart_pending;

/* CreateProcessA (kernel32.c): the game starts its next copy ("Rollcage.exe
 * /FRONTEND") and is about to exit; that copy runs here instead, once this
 * one has (plan step 7). */
void nt_restart_request(const char *command_line)
{
    snprintf(restart_line, sizeof restart_line, "%s", command_line);
    nt_restart_pending = 1;
}

void nt_handles_reset(void);
void nt_timers_reset(void);
void nt_user_reset(void);
void nt_cd_reset(void);
void nt_qmixer_reset(void);
void reroll_restart(void); /* src/main.c: the reimplementation's own state */

/* From ExitProcess when a restart is pending: the old run is over; clear
 * what it left (handles, timers, its memory, the data section) and run the
 * exe's start-up again with the new command line, in the same window. */
_Noreturn void nt_restart(void)
{
    char directory[512], *slash, *args;
    int i;
    uint16_t cw = 0x37f;

    nt_restart_pending = 0;
    nt_log("loader: restarting in place: %s", restart_line);
    nt_timers_reset();
    nt_cd_reset();
    nt_qmixer_reset();
    nt_handles_reset();
    nt_user_reset();
    nt_lowmem_reset_game();
    for (i = 0; i < saved_count; i++) {
        memcpy((void *)(uintptr_t)(IMAGE_BASE + saved[i].va), saved[i].data, saved[i].raw_size);
        memset((void *)(uintptr_t)(IMAGE_BASE + saved[i].va + saved[i].raw_size), 0,
               saved[i].size - saved[i].raw_size);
    }
    reroll_restart();
    /* the command line: the exe as before, the new arguments */
    args = restart_line;
    while (*args == ' ')
        args++;
    if (*args == '"') {
        args = strchr(args + 1, '"');
        args = args ? args + 1 : restart_line + strlen(restart_line);
    } else {
        while (*args && *args != ' ')
            args++;
    }
    snprintf(nt_config.command_line, sizeof nt_config.command_line, "\"%s\"%s", nt_config.exe_windows, args);
    snprintf(directory, sizeof directory, "%s", nt_config.exe_windows);
    slash = strrchr(directory, '\\');
    if (slash)
        *slash = 0;
    nt_set_current_directory(directory);
    *(uint32_t *)(uintptr_t)nt_teb_address() = 0xffffffffu; /* no exception frames */
    __asm__ volatile("fldcw %0" : : "m"(cw));
    longjmp(restart_point, 1);
}

int nt_game_started;

__attribute__((used, noreturn)) static void start_game(void)
{
    BOOL WINAPI reroll_install(HANDLE instance, DWORD reason, void *reserved); /* src/main.c */
    nt_teb_init_thread();
    reroll_install((HANDLE)IMAGE_BASE, 1 /* attach */, 0);
    nt_exe_coverage_start(); /* after the hooks: their entries are ours */
    nt_lowmem_arena(1); /* from here on, what's allocated is the game's */
    nt_game_started = 1;
    if (setjmp(restart_point))
        nt_log("loader: the game starts again");
    nt_log("loader: starting the game at %x", entry_point);
    ((void (*)(void))(uintptr_t)entry_point)();
    ExitProcess(0);
}
