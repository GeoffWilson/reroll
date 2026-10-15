/* KERNEL32 for the native build: files, memory, threads and synchronisation,
 * environment and locale, as far as Rollcage.exe (its C runtime included)
 * and the reimplementation use them. */
#include "nt.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <malloc.h>
#include <sched.h>

typedef unsigned int UINT;

/* ---- errors ---- */

static __thread DWORD last_error;

void nt_set_last_error(DWORD error)
{
    last_error = error;
}

DWORD nt_last_error(void)
{
    return last_error;
}

DWORD WINAPI GetLastError(void)
{
    return last_error;
}

void WINAPI SetLastError(DWORD error)
{
    last_error = error;
}

static DWORD errno_error(void)
{
    switch (errno) {
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_FILE_EXISTS;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    default: return ERROR_INVALID_PARAMETER;
    }
}

/* ---- handles ---- */

struct nt_object {
    int kind;
    int refs;
    int persistent;            /* made before the game started: survives a restart */
    char name[64];             /* named events / mutexes */
    int fd;                    /* file */
    pthread_mutex_t lock;      /* event / mutex / thread state */
    pthread_cond_t cond;
    int signalled, manual;     /* event; thread: finished */
    pthread_t owner;           /* mutex */
    int count;                 /* mutex recursion */
    DWORD exit_code;           /* thread */
    int suspended;
    DWORD(WINAPI *start)(void *);
    void *param;
    pthread_t thread;
    DIR *dir;                  /* find */
    char pattern[260], dir_host[600];
};

#define MAX_HANDLES 1024
#define HANDLE_BASE 0x100
static struct nt_object *objects[MAX_HANDLES];
static pthread_mutex_t handles_lock = PTHREAD_MUTEX_INITIALIZER;

static HANDLE new_handle(struct nt_object *o)
{
    int i;
    pthread_mutex_lock(&handles_lock);
    for (i = 1; i < MAX_HANDLES; i++)
        if (!objects[i]) {
            objects[i] = o;
            o->refs++;
            pthread_mutex_unlock(&handles_lock);
            return (HANDLE)(uintptr_t)(HANDLE_BASE + i * 4);
        }
    pthread_mutex_unlock(&handles_lock);
    nt_fatal("out of handles");
}

struct nt_object *nt_handle_object(HANDLE h, int kind)
{
    uintptr_t v = (uintptr_t)h;
    struct nt_object *o;
    if (v < HANDLE_BASE || (v - HANDLE_BASE) % 4 || (v - HANDLE_BASE) / 4 >= MAX_HANDLES)
        return 0;
    o = objects[(v - HANDLE_BASE) / 4];
    return o && (kind < 0 || o->kind == kind) ? o : 0;
}

static struct nt_object *new_object(int kind)
{
    struct nt_object *o = calloc(1, sizeof *o);
    pthread_mutexattr_t attr;
    if (!o)
        nt_fatal("out of memory");
    o->kind = kind;
    o->fd = -1;
    o->persistent = !nt_game_started;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&o->lock, &attr);
    pthread_cond_init(&o->cond, 0);
    return o;
}

/* The game restarts in place (plan step 7): its handles go. */
void nt_handles_reset(void)
{
    int i, closed = 0;
    for (i = 1; i < MAX_HANDLES; i++) {
        struct nt_object *o = objects[i];
        if (!o || o->persistent)
            continue;
        objects[i] = 0;
        if (o->kind == NT_FILE && o->fd >= 0)
            close(o->fd);
        if (o->kind == NT_FIND && o->dir)
            closedir(o->dir);
        closed++;
    }
    nt_log("kernel32: closed %d handles of the old run", closed);
}

/* the standard handles */
static struct nt_object std_objects[3];

HANDLE WINAPI GetStdHandle(DWORD which)
{
    int i = which == (DWORD)-10 ? 0 : which == (DWORD)-11 ? 1 : which == (DWORD)-12 ? 2 : -1;
    if (i < 0)
        return INVALID_HANDLE;
    std_objects[i].kind = NT_STD;
    std_objects[i].fd = i;
    return (HANDLE)(uintptr_t)(0x10 + i * 4);
}

BOOL WINAPI SetStdHandle(DWORD which, HANDLE h)
{
    (void)which;
    (void)h;
    return TRUE;
}

static int fd_of(HANDLE h)
{
    struct nt_object *o;
    uintptr_t v = (uintptr_t)h;
    if (v >= 0x10 && v <= 0x18)
        return (int)(v - 0x10) / 4;
    o = nt_handle_object(h, NT_FILE);
    return o ? o->fd : -1;
}

BOOL WINAPI CloseHandle(HANDLE h)
{
    uintptr_t v = (uintptr_t)h;
    struct nt_object *o = nt_handle_object(h, -1);
    if (v >= 0x10 && v <= 0x18)
        return TRUE;
    if (!o) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    pthread_mutex_lock(&handles_lock);
    objects[(v - HANDLE_BASE) / 4] = 0;
    pthread_mutex_unlock(&handles_lock);
    if (o->kind == NT_FILE && o->fd >= 0)
        close(o->fd);
    if (o->kind == NT_FIND && o->dir)
        closedir(o->dir);
    /* events, mutexes and threads may still be waited on elsewhere: kept */
    if (o->kind == NT_FILE || o->kind == NT_FIND)
        free(o);
    return TRUE;
}

/* ---- files ---- */

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

HANDLE WINAPI CreateFileA(const char *name, DWORD access, DWORD share, void *security, DWORD disposition,
                          DWORD flags, HANDLE template_file)
{
    char host[1024];
    int mode, fd;
    struct nt_object *o;
    (void)share;
    (void)security;
    (void)flags;
    (void)template_file;

    if (!nt_host_path(name, host, sizeof host, disposition == OPEN_EXISTING || disposition == TRUNCATE_EXISTING)) {
        nt_log("CreateFileA(%s): no such path", name);
        last_error = ERROR_PATH_NOT_FOUND;
        return INVALID_HANDLE;
    }
    mode = (access & GENERIC_WRITE) || (access & 4 /* FILE_APPEND_DATA */)
               ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY)
               : O_RDONLY;
    if (access & 4)
        mode |= O_APPEND;
    switch (disposition) {
    case CREATE_NEW: mode |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: mode |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: mode |= O_CREAT; break;
    case TRUNCATE_EXISTING: mode |= O_TRUNC; break;
    default: break;
    }
    fd = open(host, mode, 0644);
    if (fd < 0 && (mode & O_ACCMODE) == O_RDWR && errno == EROFS) /* the CD */
        fd = open(host, (mode & ~O_ACCMODE) | O_RDONLY, 0644);
    if (fd < 0) {
        last_error = errno_error();
        nt_log("CreateFileA(%s): %s (%s)", name, strerror(errno), host);
        return INVALID_HANDLE;
    }
    o = new_object(NT_FILE);
    o->fd = fd;
    last_error = 0;
    if (getenv("REROLL_TRACE_FILES"))
        nt_log("CreateFileA(%s) -> %s", name, host);
    return new_handle(o);
}

BOOL WINAPI ReadFile(HANDLE h, void *buf, DWORD len, DWORD *read_count, void *overlapped)
{
    int fd = fd_of(h);
    ssize_t n;
    (void)overlapped;
    if (fd < 0) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    n = read(fd, buf, len);
    if (read_count)
        *read_count = n > 0 ? (DWORD)n : 0;
    if (n < 0) {
        last_error = errno_error();
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI WriteFile(HANDLE h, const void *buf, DWORD len, DWORD *written, void *overlapped)
{
    int fd = fd_of(h);
    ssize_t n;
    (void)overlapped;
    if (fd < 0) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    n = write(fd, buf, len);
    if (written)
        *written = n > 0 ? (DWORD)n : 0;
    if (n < 0) {
        last_error = errno_error();
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI GetFileSize(HANDLE h, DWORD *high)
{
    struct stat st;
    int fd = fd_of(h);
    if (fd < 0 || fstat(fd, &st))
        return 0xffffffffu;
    if (high)
        *high = 0;
    return (DWORD)st.st_size;
}

DWORD WINAPI SetFilePointer(HANDLE h, LONG distance, LONG *high, DWORD method)
{
    int fd = fd_of(h);
    off_t r;
    if (high)
        *high = 0;
    if (fd < 0)
        return 0xffffffffu;
    r = lseek(fd, distance, method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END);
    return r < 0 ? 0xffffffffu : (DWORD)r;
}

BOOL WINAPI SetEndOfFile(HANDLE h)
{
    int fd = fd_of(h);
    off_t pos = fd >= 0 ? lseek(fd, 0, SEEK_CUR) : -1;
    return pos >= 0 && !ftruncate(fd, pos);
}

BOOL WINAPI FlushFileBuffers(HANDLE h)
{
    (void)h;
    return TRUE;
}

DWORD WINAPI GetFileType(HANDLE h)
{
    uintptr_t v = (uintptr_t)h;
    if (v >= 0x10 && v <= 0x18)
        return 2; /* FILE_TYPE_CHAR */
    return nt_handle_object(h, NT_FILE) ? 1 /* DISK */ : 0;
}

BOOL WINAPI DeleteFileA(const char *name)
{
    char host[1024];
    if (!nt_host_path(name, host, sizeof host, 1) || unlink(host)) {
        last_error = ERROR_FILE_NOT_FOUND;
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI CreateDirectoryA(const char *name, void *security)
{
    char host[1024];
    (void)security;
    if (!nt_host_path(name, host, sizeof host, 0))
        return FALSE;
    if (mkdir(host, 0755)) {
        last_error = errno == EEXIST ? ERROR_ALREADY_EXISTS : errno_error();
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI GetFileAttributesA(const char *name)
{
    char host[1024];
    struct stat st;
    if (!nt_host_path(name, host, sizeof host, 1) || stat(host, &st)) {
        last_error = ERROR_FILE_NOT_FOUND;
        return 0xffffffffu;
    }
    return S_ISDIR(st.st_mode) ? 0x10 : 0x80; /* DIRECTORY : NORMAL */
}

UINT WINAPI GetDriveTypeA(const char *root)
{
    char d = root ? (char)(root[0] & ~0x20) : 'C';
    return d == 'D' ? 5 /* CDROM */ : d == 'C' ? 3 /* FIXED */ : 1 /* NO_ROOT_DIR */;
}

/* unique 0: a new name, and the empty file is created to reserve it (as Windows does) */
UINT WINAPI GetTempFileNameA(const char *path, const char *prefix, UINT unique, char *out)
{
    static UINT counter;
    UINT n = unique;
    int tries;
    for (tries = 0; tries < 0x10000; tries++) {
        char host[1024];
        int fd;
        if (!unique)
            n = (++counter + (UINT)getpid()) & 0xffff;
        snprintf(out, 260, "%s%s%.3s%04X.TMP", path, path[0] && path[strlen(path) - 1] != '\\' ? "\\" : "", prefix,
                 n & 0xffff);
        if (unique)
            return n;
        if (!nt_host_path(out, host, sizeof host, 0))
            return 0;
        fd = open(host, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd >= 0) {
            close(fd);
            return n;
        }
        if (errno != EEXIST)
            return 0;
    }
    return 0;
}

DWORD WINAPI GetCurrentDirectoryA(DWORD size, char *buf)
{
    const char *cur = nt_current_directory();
    DWORD len = (DWORD)strlen(cur);
    if (len + 1 > size)
        return len + 1;
    memcpy(buf, cur, len + 1);
    return len;
}

BOOL WINAPI SetCurrentDirectoryA(const char *path)
{
    if (!nt_set_current_directory(path)) {
        last_error = ERROR_PATH_NOT_FOUND;
        return FALSE;
    }
    return TRUE;
}

/* file_part: a 4-byte slot in game memory */
DWORD WINAPI GetFullPathNameA(const char *name, DWORD size, char *buf, uint32_t *file_part)
{
    char full[1024];
    DWORD len;
    nt_full_path(name, full, sizeof full);
    len = (DWORD)strlen(full);
    if (len + 1 > size)
        return len + 1;
    memcpy(buf, full, len + 1);
    if (file_part) {
        char *slash = strrchr(buf, '\\');
        *file_part = (uint32_t)(uintptr_t)(slash ? slash + 1 : buf);
    }
    return len;
}

DWORD WINAPI GetModuleFileNameA(HANDLE module, char *buf, DWORD size)
{
    DWORD len = (DWORD)strlen(nt_config.exe_windows);
    (void)module;
    if (!size)
        return 0;
    if (len >= size)
        len = size - 1;
    memcpy(buf, nt_config.exe_windows, len);
    buf[len] = 0;
    return len;
}

/* FindFirstFileA / FindNextFileA: WIN32_FIND_DATAA (attributes +0, times
 * +4.., size high +0x1c, low +0x20, name +0x2c (260), short name +0x130). */
static int find_next(struct nt_object *o, uint8_t *data)
{
    struct dirent *e;
    while ((e = readdir(o->dir)) != 0) {
        char path[1200];
        struct stat st;
        if (fnmatch(o->pattern, e->d_name, FNM_CASEFOLD))
            continue;
        snprintf(path, sizeof path, "%s/%s", o->dir_host, e->d_name);
        if (stat(path, &st))
            continue;
        memset(data, 0, 0x140);
        *(DWORD *)data = S_ISDIR(st.st_mode) ? 0x10 : 0x80;
        *(DWORD *)(data + 0x20) = (DWORD)st.st_size;
        snprintf((char *)data + 0x2c, 260, "%s", e->d_name);
        return 1;
    }
    return 0;
}

HANDLE WINAPI FindFirstFileA(const char *pattern, uint8_t *data)
{
    char full[1024], host[1024], *slash;
    struct nt_object *o;
    DIR *dir;

    nt_full_path(pattern, full, sizeof full);
    slash = strrchr(full, '\\');
    if (!slash)
        return INVALID_HANDLE;
    *slash = 0;
    if (!nt_host_path(slash - full == 2 ? "C:\\" : full, host, sizeof host, 1) || !(dir = opendir(host))) {
        last_error = ERROR_PATH_NOT_FOUND;
        return INVALID_HANDLE;
    }
    if (slash - full == 2) /* "X:" root */
        nt_host_path((char[]){full[0], ':', '\\', 0}, host, sizeof host, 1), closedir(dir), dir = opendir(host);
    o = new_object(NT_FIND);
    o->dir = dir;
    snprintf(o->dir_host, sizeof o->dir_host, "%s", host);
    snprintf(o->pattern, sizeof o->pattern, "%s", slash + 1);
    if (!strcmp(o->pattern, "*.*"))
        snprintf(o->pattern, sizeof o->pattern, "*");
    if (!find_next(o, data)) {
        closedir(dir);
        free(o);
        last_error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE;
    }
    return new_handle(o);
}

BOOL WINAPI FindNextFileA(HANDLE h, uint8_t *data)
{
    struct nt_object *o = nt_handle_object(h, NT_FIND);
    if (!o || !find_next(o, data)) {
        last_error = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE h)
{
    return CloseHandle(h);
}

/* ---- memory ---- */

void *WINAPI GlobalAlloc(UINT flags, DWORD size)
{
    void *p = (flags & 0x40) ? nt_lowmem_calloc(1, size) : nt_lowmem_alloc(size);
    if (!p)
        last_error = ERROR_NOT_ENOUGH_MEMORY;
    return p;
}

void *WINAPI GlobalFree(void *p)
{
    nt_lowmem_free(p);
    return 0;
}

HANDLE WINAPI HeapCreate(DWORD options, DWORD initial, DWORD maximum)
{
    (void)options;
    (void)initial;
    (void)maximum;
    return (HANDLE)0x5a5a0000;
}

BOOL WINAPI HeapDestroy(HANDLE heap)
{
    (void)heap;
    return TRUE;
}

HANDLE WINAPI GetProcessHeap(void)
{
    return (HANDLE)0x5a5a0000;
}

void *WINAPI HeapAlloc(HANDLE heap, DWORD flags, DWORD size)
{
    (void)heap;
    return (flags & 8) ? nt_lowmem_calloc(1, size) : nt_lowmem_alloc(size);
}

void *WINAPI HeapReAlloc(HANDLE heap, DWORD flags, void *p, DWORD size)
{
    size_t old = p ? nt_lowmem_size(p) : 0;
    void *q;
    (void)heap;
    if (flags & 0x10) /* HEAP_REALLOC_IN_PLACE_ONLY */
        return size <= old ? p : 0;
    q = nt_lowmem_realloc(p, size);
    if (q && (flags & 8) && size > old)
        memset((uint8_t *)q + old, 0, size - old);
    return q;
}

BOOL WINAPI HeapFree(HANDLE heap, DWORD flags, void *p)
{
    (void)heap;
    (void)flags;
    nt_lowmem_free(p);
    return TRUE;
}

/* Virtual memory: reserve = PROT_NONE mapping, commit = make it read/write. */
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000

static int protection(DWORD prot)
{
    switch (prot & 0xff) {
    case 0x01: return PROT_NONE;
    case 0x02: return PROT_READ;
    case 0x04: return PROT_READ | PROT_WRITE;
    case 0x10: return PROT_EXEC;
    case 0x20: return PROT_READ | PROT_EXEC;
    default: return PROT_READ | PROT_WRITE | PROT_EXEC;
    }
}

void *WINAPI VirtualAlloc(void *address, DWORD size, DWORD type, DWORD prot)
{
    uint32_t base = (uint32_t)(uintptr_t)address & ~0xfffu;
    void *p;

    size = (size + ((uint32_t)(uintptr_t)address & 0xfff) + 0xfff) & ~0xfffu;
    if (!address) {
        p = nt_lowmem_reserve(size);
        if (!p) {
            last_error = ERROR_NOT_ENOUGH_MEMORY;
            return 0;
        }
        if (type & MEM_COMMIT)
            mprotect(p, size, protection(prot));
        return p;
    }
    if (!(type & MEM_COMMIT)) {
        nt_log("VirtualAlloc: reserving at a given address (%x) is not supported", base);
        last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }
    if (mprotect((void *)(uintptr_t)base, size, protection(prot))) {
        last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }
    return (void *)(uintptr_t)base;
}

BOOL WINAPI VirtualFree(void *address, DWORD size, DWORD type)
{
    uint32_t base = (uint32_t)(uintptr_t)address;
    if (type & MEM_RELEASE)
        return nt_lowmem_unreserve(address);
    size = (size + (base & 0xfff) + 0xfff) & ~0xfffu;
    base &= ~0xfffu;
    mprotect((void *)(uintptr_t)base, size, PROT_NONE);
    madvise((void *)(uintptr_t)base, size, MADV_DONTNEED);
    return TRUE;
}

BOOL WINAPI VirtualProtect(void *address, DWORD size, DWORD prot, DWORD *old)
{
    uint32_t base = (uint32_t)(uintptr_t)address & ~0xfffu;
    uint32_t end = ((uint32_t)(uintptr_t)address + size + 0xfff) & ~0xfffu;
    if (old)
        *old = 0x40;
    return !mprotect((void *)(uintptr_t)base, end - base, protection(prot));
}

/* VirtualQuery from /proc/self/maps (cached; re-read on a miss). */
struct map_range {
    uint32_t start, end;
    int prot;
};
static struct map_range ranges[4096];
static int range_count;
static pthread_mutex_t ranges_lock = PTHREAD_MUTEX_INITIALIZER;

static void read_maps(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    range_count = 0;
    if (!f)
        return;
    while (fgets(line, sizeof line, f) && range_count < 4096) {
        unsigned long start, end;
        char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3)
            continue;
        if (start >= 0xfffff000ul) /* above 4 GB: not the game's (64-bit) */
            continue;
        if (end > 0xfffff000ul)
            end = 0xfffff000ul;
        ranges[range_count].start = (uint32_t)start;
        ranges[range_count].end = (uint32_t)end;
        ranges[range_count].prot = (perms[0] == 'r') | (perms[1] == 'w') << 1 | (perms[2] == 'x') << 2;
        range_count++;
    }
    fclose(f);
}

static const struct map_range *find_range(uint32_t a)
{
    int i;
    for (i = 0; i < range_count; i++)
        if (a >= ranges[i].start && a < ranges[i].end)
            return &ranges[i];
    return 0;
}

DWORD WINAPI VirtualQuery(const void *address, uint32_t *info, DWORD size)
{
    uint32_t a = (uint32_t)(uintptr_t)address;
    const struct map_range *r;
    static const DWORD prot_map[8] = {0x01, 0x02, 0x04, 0x04, 0x10, 0x20, 0x40, 0x40};
    (void)size;
    pthread_mutex_lock(&ranges_lock);
    r = find_range(a);
    if (!r || !r->prot) {
        read_maps();
        r = find_range(a);
    }
    memset(info, 0, 28);
    info[0] = a & ~0xfffu;
    if (r && r->prot) {
        info[1] = r->start;
        info[2] = prot_map[r->prot];
        info[3] = r->end - (a & ~0xfffu);
        info[4] = MEM_COMMIT;
        info[5] = prot_map[r->prot];
        info[6] = 0x20000; /* MEM_PRIVATE */
    } else {
        info[3] = 0x1000;
        info[4] = r ? MEM_RESERVE : 0x10000 /* MEM_FREE */;
        info[5] = 0x01;
    }
    pthread_mutex_unlock(&ranges_lock);
    return 28;
}

BOOL WINAPI FlushInstructionCache(HANDLE process, const void *address, DWORD size)
{
    (void)process;
    (void)address;
    (void)size;
    return TRUE;
}

/* ---- processes and threads ---- */

/* thread stacks below 2 GB too (the game may link stack data) */
void nt_low_stack(pthread_attr_t *attr, uint32_t size)
{
    void *stack = nt_lowmem_alloc(size);
    if (stack)
        pthread_attr_setstack(attr, stack, size);
    else
        pthread_attr_setstacksize(attr, size);
}

HANDLE WINAPI GetCurrentProcess(void)
{
    return (HANDLE)(intptr_t)-1;
}

DWORD WINAPI GetCurrentProcessId(void)
{
    return (DWORD)getpid();
}

DWORD WINAPI GetCurrentThreadId(void)
{
    return (DWORD)syscall(SYS_gettid);
}

_Noreturn void WINAPI ExitProcess(UINT code)
{
    if (nt_restart_pending)
        nt_restart();
    nt_log("ExitProcess(%u)", code);
    fflush(0);
    _exit((int)code);
}

BOOL WINAPI TerminateProcess(HANDLE process, UINT code)
{
    if ((uint32_t)(uintptr_t)process == 0xffffffffu) /* also as kept in 32 bits */
        ExitProcess(code);
    return FALSE;
}

static void *thread_main(void *arg)
{
    struct nt_object *o = arg;
    DWORD code;
    nt_teb_init_thread();
    pthread_mutex_lock(&o->lock);
    while (o->suspended)
        pthread_cond_wait(&o->cond, &o->lock);
    pthread_mutex_unlock(&o->lock);
    code = o->start(o->param);
    pthread_mutex_lock(&o->lock);
    o->exit_code = code;
    o->signalled = 1;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return 0;
}

static __thread struct nt_object *current_thread_object;

HANDLE WINAPI CreateThread(void *security, DWORD stack, DWORD(WINAPI *start)(void *), void *param, DWORD flags,
                           DWORD *id)
{
    struct nt_object *o = new_object(NT_THREAD);
    pthread_attr_t attr;
    (void)security;
    o->start = start;
    o->param = param;
    o->exit_code = 259; /* STILL_ACTIVE */
    o->suspended = (flags & 4) != 0;
    pthread_attr_init(&attr);
    nt_low_stack(&attr, stack > 0x100000 ? stack : 0x100000);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&o->thread, &attr, thread_main, o)) {
        free(o);
        return 0;
    }
    if (id)
        *id = (DWORD)(uintptr_t)o;
    return new_handle(o);
}

DWORD WINAPI ResumeThread(HANDLE h)
{
    struct nt_object *o = nt_handle_object(h, NT_THREAD);
    int was;
    if (!o)
        return (DWORD)-1;
    pthread_mutex_lock(&o->lock);
    was = o->suspended;
    o->suspended = 0;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return (DWORD)was;
}

_Noreturn void WINAPI ExitThread(DWORD code)
{
    struct nt_object *o = current_thread_object;
    if (o) {
        pthread_mutex_lock(&o->lock);
        o->exit_code = code;
        o->signalled = 1;
        pthread_cond_broadcast(&o->cond);
        pthread_mutex_unlock(&o->lock);
    }
    pthread_exit(0);
}

BOOL WINAPI GetExitCodeThread(HANDLE h, DWORD *code)
{
    struct nt_object *o = nt_handle_object(h, NT_THREAD);
    if (!o)
        return FALSE;
    *code = o->exit_code;
    return TRUE;
}

/* ---- synchronisation ---- */

static struct nt_object *find_named(int kind, const char *name)
{
    int i;
    if (!name || !*name)
        return 0;
    for (i = 1; i < MAX_HANDLES; i++)
        if (objects[i] && objects[i]->kind == kind && !strcmp(objects[i]->name, name))
            return objects[i];
    return 0;
}

static HANDLE make_named(int kind, const char *name, int *existed)
{
    struct nt_object *o;
    pthread_mutex_lock(&handles_lock);
    o = find_named(kind, name);
    pthread_mutex_unlock(&handles_lock);
    *existed = o != 0;
    if (!o) {
        o = new_object(kind);
        if (name)
            snprintf(o->name, sizeof o->name, "%s", name);
    }
    return new_handle(o);
}

HANDLE WINAPI CreateEventA(void *security, BOOL manual, BOOL initial, const char *name)
{
    int existed;
    HANDLE h = make_named(NT_EVENT, name, &existed);
    struct nt_object *o = nt_handle_object(h, NT_EVENT);
    (void)security;
    if (!existed) {
        o->manual = manual;
        o->signalled = initial;
    }
    last_error = existed ? ERROR_ALREADY_EXISTS : 0;
    return h;
}

HANDLE WINAPI OpenEventA(DWORD access, BOOL inherit, const char *name)
{
    struct nt_object *o;
    (void)access;
    (void)inherit;
    pthread_mutex_lock(&handles_lock);
    o = find_named(NT_EVENT, name);
    pthread_mutex_unlock(&handles_lock);
    if (!o) {
        last_error = ERROR_FILE_NOT_FOUND;
        return 0;
    }
    return new_handle(o);
}

BOOL WINAPI SetEvent(HANDLE h)
{
    struct nt_object *o = nt_handle_object(h, NT_EVENT);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    o->signalled = 1;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

int nt_event_set(HANDLE h)
{
    return SetEvent(h);
}

BOOL WINAPI ResetEvent(HANDLE h)
{
    struct nt_object *o = nt_handle_object(h, NT_EVENT);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    o->signalled = 0;
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

HANDLE WINAPI CreateMutexA(void *security, BOOL owned, const char *name)
{
    int existed;
    HANDLE h = make_named(NT_MUTEX, name, &existed);
    struct nt_object *o = nt_handle_object(h, NT_MUTEX);
    (void)security;
    if (!existed && owned) {
        o->owner = pthread_self();
        o->count = 1;
    }
    last_error = existed ? ERROR_ALREADY_EXISTS : 0;
    return h;
}

HANDLE WINAPI OpenMutexA(DWORD access, BOOL inherit, const char *name)
{
    struct nt_object *o;
    (void)access;
    (void)inherit;
    pthread_mutex_lock(&handles_lock);
    o = find_named(NT_MUTEX, name);
    pthread_mutex_unlock(&handles_lock);
    if (!o) {
        last_error = ERROR_FILE_NOT_FOUND;
        return 0;
    }
    return new_handle(o);
}

BOOL WINAPI ReleaseMutex(HANDLE h)
{
    struct nt_object *o = nt_handle_object(h, NT_MUTEX);
    if (!o)
        return FALSE;
    pthread_mutex_lock(&o->lock);
    if (o->count > 0 && pthread_equal(o->owner, pthread_self()))
        o->count--;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

/* Whether `o` can be taken now (and takes it). Called with o->lock held. */
static int try_take(struct nt_object *o)
{
    switch (o->kind) {
    case NT_EVENT:
        if (!o->signalled)
            return 0;
        if (!o->manual)
            o->signalled = 0;
        return 1;
    case NT_MUTEX:
        if (o->count && !pthread_equal(o->owner, pthread_self()))
            return 0;
        o->owner = pthread_self();
        o->count++;
        return 1;
    case NT_THREAD:
        return o->signalled;
    default:
        return 1;
    }
}

static void deadline(struct timespec *ts, DWORD ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms)
{
    struct nt_object *o = nt_handle_object(h, -1);
    struct timespec ts;
    DWORD result = 0;
    if (!o)
        return 0xffffffffu; /* WAIT_FAILED */
    deadline(&ts, ms);
    pthread_mutex_lock(&o->lock);
    while (!try_take(o)) {
        if (ms == 0 || (ms != 0xffffffffu && pthread_cond_timedwait(&o->cond, &o->lock, &ts) == ETIMEDOUT)) {
            result = 0x102; /* WAIT_TIMEOUT */
            break;
        }
        if (ms == 0xffffffffu)
            pthread_cond_wait(&o->cond, &o->lock);
    }
    pthread_mutex_unlock(&o->lock);
    return result;
}

DWORD WINAPI WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL all, DWORD ms)
{
    struct timespec start, now;
    DWORD i;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        DWORD ready = 0;
        for (i = 0; i < count; i++) {
            struct nt_object *o = nt_handle_object(handles[i], -1);
            int taken;
            if (!o)
                return 0xffffffffu;
            pthread_mutex_lock(&o->lock);
            taken = try_take(o);
            pthread_mutex_unlock(&o->lock);
            if (taken && !all)
                return i;
            ready += (DWORD)taken;
        }
        if (all && ready == count)
            return 0;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (ms != 0xffffffffu &&
            (DWORD)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000) >= ms)
            return 0x102;
        usleep(1000);
    }
}

/* CRITICAL_SECTION (24 bytes in the caller's memory): the first word holds
 * the (32-bit) address of our recursive mutex, in low memory. */
#define CS_MUTEX(cs) ((pthread_mutex_t *)(uintptr_t)(cs)[0])

void WINAPI InitializeCriticalSection(uint32_t *cs)
{
    pthread_mutex_t *m = nt_lowmem_alloc(sizeof *m);
    pthread_mutexattr_t attr;
    if (!m)
        nt_fatal("out of memory");
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &attr);
    memset(cs, 0, 24);
    cs[0] = (uint32_t)(uintptr_t)m;
}

void WINAPI DeleteCriticalSection(uint32_t *cs)
{
    if (cs[0]) {
        pthread_mutex_destroy(CS_MUTEX(cs));
        nt_lowmem_free(CS_MUTEX(cs));
        cs[0] = 0;
    }
}

void WINAPI EnterCriticalSection(uint32_t *cs)
{
    if (!cs[0])
        InitializeCriticalSection(cs);
    pthread_mutex_lock(CS_MUTEX(cs));
}

void WINAPI LeaveCriticalSection(uint32_t *cs)
{
    if (cs[0])
        pthread_mutex_unlock(CS_MUTEX(cs));
}

LONG WINAPI InterlockedIncrement(LONG *p)
{
    return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST);
}

LONG WINAPI InterlockedDecrement(LONG *p)
{
    return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST);
}

LONG WINAPI InterlockedExchange(LONG *p, LONG v)
{
    return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}

/* ---- thread-local storage ---- */

#define TLS_SLOTS 64
static __thread void *tls_values[TLS_SLOTS];
static int tls_used[TLS_SLOTS];

DWORD WINAPI TlsAlloc(void)
{
    int i;
    for (i = 0; i < TLS_SLOTS; i++)
        if (!__atomic_exchange_n(&tls_used[i], 1, __ATOMIC_SEQ_CST))
            return (DWORD)i;
    return 0xffffffffu;
}

void *WINAPI TlsGetValue(DWORD i)
{
    last_error = 0;
    return i < TLS_SLOTS ? tls_values[i] : 0;
}

BOOL WINAPI TlsSetValue(DWORD i, void *v)
{
    if (i >= TLS_SLOTS)
        return FALSE;
    tls_values[i] = v;
    return TRUE;
}

/* ---- time ---- */

void WINAPI Sleep(DWORD ms)
{
    if (ms)
        usleep(ms * 1000u);
    else
        sched_yield();
}

DWORD WINAPI GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

BOOL WINAPI QueryPerformanceCounter(uint64_t *count)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *count = (uint64_t)ts.tv_sec * 10000000u + (uint64_t)ts.tv_nsec / 100u;
    return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(uint64_t *frequency)
{
    *frequency = 10000000u;
    return TRUE;
}

void WINAPI GetSystemTimeAsFileTime(uint64_t *ft)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    *ft = ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000u + (uint64_t)ts.tv_nsec / 100u;
}

/* ---- environment, command line, start-up ---- */

DWORD WINAPI GetEnvironmentVariableA(const char *name, char *buf, DWORD size)
{
    const char *v = getenv(name);
    DWORD len;
    if (!v) {
        last_error = ERROR_ENVVAR_NOT_FOUND;
        return 0;
    }
    len = (DWORD)strlen(v);
    if (len + 1 > size)
        return len + 1;
    memcpy(buf, v, len + 1);
    return len;
}

BOOL WINAPI SetEnvironmentVariableA(const char *name, const char *value)
{
    return value ? !setenv(name, value, 1) : !unsetenv(name);
}

extern char **environ;

/* the blocks are the game's to read: low memory */
char *WINAPI GetEnvironmentStrings(void)
{
    size_t total = 1, pos = 0;
    char **e, *block;
    for (e = environ; *e; e++)
        total += strlen(*e) + 1;
    block = nt_lowmem_alloc((uint32_t)total);
    if (!block)
        return 0;
    for (e = environ; *e; e++) {
        size_t len = strlen(*e) + 1;
        memcpy(block + pos, *e, len);
        pos += len;
    }
    block[pos] = 0;
    return block;
}

WCHAR *WINAPI GetEnvironmentStringsW(void)
{
    char *a = GetEnvironmentStrings();
    size_t n = 0, i;
    WCHAR *w;
    if (!a)
        return 0;
    while (a[n] || a[n + 1])
        n++;
    n += 2;
    w = nt_lowmem_alloc((uint32_t)(n * 2));
    if (w)
        for (i = 0; i < n; i++)
            w[i] = (uint8_t)a[i];
    nt_lowmem_free(a);
    return w;
}

BOOL WINAPI FreeEnvironmentStringsA(char *p)
{
    nt_lowmem_free(p);
    return TRUE;
}

BOOL WINAPI FreeEnvironmentStringsW(WCHAR *p)
{
    nt_lowmem_free(p);
    return TRUE;
}

char *WINAPI GetCommandLineA(void)
{
    return nt_config.command_line;
}

void WINAPI GetStartupInfoA(uint8_t *info)
{
    memset(info, 0, 68);
    *(DWORD *)info = 68;
}

DWORD WINAPI GetVersion(void)
{
    return 0x0a280105; /* Windows XP 5.1, build 2600 */
}

void WINAPI GetSystemInfo(DWORD *info)
{
    memset(info, 0, 36);
    info[1] = 4096;        /* page size */
    info[2] = 0x10000;     /* minimum application address */
    info[3] = 0x7ffeffff;  /* maximum */
    info[4] = 1;           /* active processor mask */
    info[5] = 1;           /* processors */
    info[6] = 586;         /* PROCESSOR_INTEL_PENTIUM */
    info[7] = 0x10000;     /* allocation granularity */
    info[8] = 6;           /* level 6, revision */
}

HANDLE WINAPI GetModuleHandleA(const char *name)
{
    if (!name)
        return (HANDLE)IMAGE_BASE;
    return nt_find_export(name, 0) ? (HANDLE)(uintptr_t)nt_module_handle(name) : 0;
}

HANDLE WINAPI LoadLibraryA(const char *name)
{
    if (nt_find_export(name, 0))
        return (HANDLE)(uintptr_t)nt_module_handle(name);
    nt_log("LoadLibraryA(%s): not available", name);
    last_error = ERROR_FILE_NOT_FOUND;
    return 0;
}

void *WINAPI GetProcAddress(HANDLE module, const char *name)
{
    const char *dll = nt_module_name((uint32_t)(uintptr_t)module);
    void *fn = dll && (uintptr_t)name > 0xffff ? nt_find_export(dll, name) : 0;
    if (!fn)
        nt_log("GetProcAddress(%s, %s): not available", dll ? dll : "?",
               (uintptr_t)name > 0xffff ? name : "#ordinal");
    return fn;
}

UINT WINAPI SetHandleCount(UINT n)
{
    return n;
}

/* ---- locale: one byte per character, code page 1252 ---- */

UINT WINAPI GetACP(void)
{
    return 1252;
}

UINT WINAPI GetOEMCP(void)
{
    return 437;
}

BOOL WINAPI GetCPInfo(UINT page, uint8_t *info)
{
    (void)page;
    memset(info, 0, 20);
    *(UINT *)info = 1; /* MaxCharSize */
    info[4] = '?';     /* DefaultChar */
    return TRUE;
}

WORD WINAPI GetUserDefaultLangID(void)
{
    return 0x0409;
}

int WINAPI MultiByteToWideChar(UINT page, DWORD flags, const char *in, int in_len, WCHAR *out, int out_len)
{
    int n = in_len < 0 ? (int)strlen(in) + 1 : in_len, i;
    (void)page;
    (void)flags;
    if (!out_len)
        return n;
    if (n > out_len)
        n = out_len;
    for (i = 0; i < n; i++)
        out[i] = (uint8_t)in[i];
    return n;
}

int WINAPI WideCharToMultiByte(UINT page, DWORD flags, const WCHAR *in, int in_len, char *out, int out_len,
                               const char *def, BOOL *used_def)
{
    int n = in_len, i;
    (void)page;
    (void)flags;
    (void)def;
    if (n < 0)
        for (n = 0; in[n]; n++) {
        }
    if (in_len < 0)
        n++;
    if (used_def)
        *used_def = FALSE;
    if (!out_len)
        return n;
    if (n > out_len)
        n = out_len;
    for (i = 0; i < n; i++)
        out[i] = in[i] < 0x100 ? (char)in[i] : '?';
    return n;
}

/* LCMAP_LOWERCASE 0x100, LCMAP_UPPERCASE 0x200 */
int WINAPI LCMapStringA(DWORD locale, DWORD flags, const char *in, int in_len, char *out, int out_len)
{
    int n = in_len < 0 ? (int)strlen(in) + 1 : in_len, i;
    (void)locale;
    if (!out_len)
        return n;
    if (n > out_len)
        return 0;
    for (i = 0; i < n; i++) {
        char c = in[i];
        if ((flags & 0x100) && c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if ((flags & 0x200) && c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        out[i] = c;
    }
    return n;
}

int WINAPI LCMapStringW(DWORD locale, DWORD flags, const WCHAR *in, int in_len, WCHAR *out, int out_len)
{
    int n = in_len, i;
    (void)locale;
    if (n < 0) {
        for (n = 0; in[n]; n++) {
        }
        n++;
    }
    if (!out_len)
        return n;
    if (n > out_len)
        return 0;
    for (i = 0; i < n; i++) {
        WCHAR c = in[i];
        if ((flags & 0x100) && c >= 'A' && c <= 'Z')
            c = (WCHAR)(c - 'A' + 'a');
        if ((flags & 0x200) && c >= 'a' && c <= 'z')
            c = (WCHAR)(c - 'a' + 'A');
        out[i] = c;
    }
    return n;
}

/* CT_CTYPE1 classes: upper 1, lower 2, digit 4, space 8, punct 0x10,
 * control 0x20, blank 0x40, hex 0x80, alpha 0x100 */
static WORD char_type(unsigned c)
{
    WORD t = 0;
    if (c >= 'A' && c <= 'Z')
        t |= 0x1 | 0x100;
    if (c >= 'a' && c <= 'z')
        t |= 0x2 | 0x100;
    if (c >= '0' && c <= '9')
        t |= 0x4;
    if (c == ' ' || (c >= 9 && c <= 13))
        t |= 0x8;
    if (c == ' ' || c == 9)
        t |= 0x40;
    if (c < 32 || c == 127)
        t |= 0x20;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
        t |= 0x80;
    if (c > 32 && c < 127 && !(t & 0x107))
        t |= 0x10;
    return t;
}

BOOL WINAPI GetStringTypeA(DWORD locale, DWORD type, const char *in, int n, WORD *out)
{
    int i;
    (void)locale;
    (void)type;
    if (n < 0)
        n = (int)strlen(in) + 1;
    for (i = 0; i < n; i++)
        out[i] = char_type((uint8_t)in[i]);
    return TRUE;
}

BOOL WINAPI GetStringTypeW(DWORD type, const WCHAR *in, int n, WORD *out)
{
    int i;
    (void)type;
    if (n < 0)
        for (n = 0; in[n]; n++) {
        }
    for (i = 0; i < n; i++)
        out[i] = in[i] < 0x100 ? char_type(in[i]) : 0;
    return TRUE;
}

/* ---- exceptions: the game's C runtime raises none in practice ---- */

void WINAPI RaiseException(DWORD code, DWORD flags, DWORD count, const DWORD *args)
{
    (void)flags;
    (void)count;
    (void)args;
    nt_fatal("RaiseException(%x)", code);
}

void WINAPI RtlUnwind(void *frame, void *ip, void *record, void *value)
{
    (void)frame;
    (void)ip;
    (void)record;
    (void)value;
    nt_fatal("RtlUnwind");
}

LONG WINAPI UnhandledExceptionFilter(void *info)
{
    (void)info;
    nt_fatal("UnhandledExceptionFilter");
}

/* src/crash.c's handler: the native build reports faults with a signal handler */
static void segv(int sig, siginfo_t *si, void *context)
{
#if defined(__i386__)
    const uint32_t *gregs = (const uint32_t *)((uint8_t *)context + 20); /* ucontext_t.uc_mcontext.gregs */
    (void)sig;
    nt_log("crash: fault at eip %x accessing %p (eax %x ebx %x ecx %x edx %x esi %x edi %x ebp %x esp %x)",
           gregs[14], si->si_addr, gregs[11], gregs[8], gregs[10], gregs[9], gregs[5], gregs[4], gregs[6],
           gregs[7]);
#else
    const greg_t *g = ((ucontext_t *)context)->uc_mcontext.gregs;
    (void)sig;
    nt_log("crash: fault at rip %llx accessing %p (rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx rbp %llx "
           "rsp %llx)",
           (unsigned long long)g[REG_RIP], si->si_addr, (unsigned long long)g[REG_RAX],
           (unsigned long long)g[REG_RBX], (unsigned long long)g[REG_RCX], (unsigned long long)g[REG_RDX],
           (unsigned long long)g[REG_RSI], (unsigned long long)g[REG_RDI], (unsigned long long)g[REG_RBP],
           (unsigned long long)g[REG_RSP]);
#endif
    _exit(71);
}

void *WINAPI AddVectoredExceptionHandler(DWORD first, void *handler)
{
    struct sigaction sa;
    (void)first;
    (void)handler;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = segv;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGILL, &sa, 0);
    sigaction(SIGFPE, &sa, 0);
    sigaction(SIGBUS, &sa, 0);
    return (void *)1;
}

/* live capture's shared memory: not in the native build */
HANDLE WINAPI CreateFileMappingA(HANDLE file, void *security, DWORD prot, DWORD high, DWORD low, const char *name)
{
    (void)file;
    (void)security;
    (void)prot;
    (void)high;
    (void)low;
    (void)name;
    return 0;
}

void *WINAPI MapViewOfFile(HANDLE map, DWORD access, DWORD high, DWORD low, DWORD size)
{
    (void)map;
    (void)access;
    (void)high;
    (void)low;
    (void)size;
    return 0;
}

/* The game starts its next copy (race -> front end, 0x474cf0), waits up to
 * 10 s for its "ATDGFXCHAINING" event, then exits: here the next copy is
 * this one, started again in place once the old run has exited (plan step
 * 7). The event is set now, so the old run doesn't wait. */
BOOL WINAPI CreateProcessA(const char *app, char *command_line, void *process_security, void *thread_security,
                           BOOL inherit, DWORD flags, void *environment, const char *directory, void *startup,
                           DWORD *info)
{
    struct nt_object *event;
    (void)app;
    (void)process_security;
    (void)thread_security;
    (void)inherit;
    (void)flags;
    (void)environment;
    (void)directory;
    (void)startup;
    nt_log("CreateProcessA(%s): the game will restart in place", command_line ? command_line : "");
    nt_restart_request(command_line ? command_line : "");
    pthread_mutex_lock(&handles_lock);
    event = find_named(NT_EVENT, "ATDGFXCHAINING");
    pthread_mutex_unlock(&handles_lock);
    if (event) {
        pthread_mutex_lock(&event->lock);
        event->signalled = 1;
        pthread_cond_broadcast(&event->cond);
        pthread_mutex_unlock(&event->lock);
    }
    if (info) { /* PROCESS_INFORMATION: process, thread, ids */
        info[0] = (DWORD)(uintptr_t)new_handle(new_object(NT_PROCESS));
        info[1] = (DWORD)(uintptr_t)new_handle(new_object(NT_PROCESS));
        info[2] = (DWORD)getpid();
        info[3] = (DWORD)getpid();
    }
    return TRUE;
}

BOOL WINAPI IsProcessorFeaturePresent(DWORD feature)
{
    /* 0..3 floating-point quirks (none), 6 SSE, 7 3DNow!, 10 SSE2 */
    return feature == 6 || feature == 10;
}
