/* Windows paths to host paths: C: and D: map to configured directories,
 * relative paths start at the current directory, and every part is found
 * case-insensitively (Windows names are; the CD's are upper case). */
#include "nt.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static char current[512] = "C:\\";

const char *nt_current_directory(void)
{
    return current;
}

/* Collapses "." and ".." and separators of a full Windows path in place. */
static void normalise(char *path)
{
    char *parts[64], *p, *out;
    int count = 0, i;
    char buf[1024];

    snprintf(buf, sizeof buf, "%s", path + 3); /* after "X:\" */
    for (p = buf; *p; p++)
        if (*p == '/')
            *p = '\\';
    p = strtok(buf, "\\");
    while (p && count < 64) {
        if (!strcmp(p, ".")) {
        } else if (!strcmp(p, "..")) {
            if (count)
                count--;
        } else {
            parts[count++] = p;
        }
        p = strtok(0, "\\");
    }
    out = path + 3;
    *out = 0;
    for (i = 0; i < count; i++) {
        size_t len = strlen(out);
        snprintf(out + len, 1024 - 3 - len, i ? "\\%s" : "%s", parts[i]);
    }
}

void nt_full_path(const char *windows, char *out, size_t size)
{
    char buf[1024];

    if (windows[0] && windows[1] == ':') {
        if (windows[2] == '\\' || windows[2] == '/')
            snprintf(buf, sizeof buf, "%c:\\%s", windows[0] & ~0x20, windows + 3);
        else /* "D:file": relative to that drive's root here */
            snprintf(buf, sizeof buf, "%c:\\%s", windows[0] & ~0x20, windows + 2);
    } else if (windows[0] == '\\' || windows[0] == '/') {
        snprintf(buf, sizeof buf, "%c:\\%s", current[0], windows + 1);
    } else {
        snprintf(buf, sizeof buf, "%s%s%s", current, current[3] ? "\\" : "", windows);
    }
    normalise(buf);
    snprintf(out, size, "%s", buf);
}

int nt_set_current_directory(const char *windows)
{
    char full[1024], host[1024];
    struct stat st;
    nt_full_path(windows, full, sizeof full);
    if (!nt_host_path(full, host, sizeof host, 1) || stat(host, &st) || !S_ISDIR(st.st_mode))
        return 0;
    snprintf(current, sizeof current, "%s", full);
    return 1;
}

/* Appends `part` to the host directory `host`, matching case-insensitively
 * when there is no exact match. */
static int append_part(char *host, size_t size, const char *part, int must_exist)
{
    size_t len = strlen(host);
    struct stat st;
    DIR *dir;
    struct dirent *e;

    snprintf(host + len, size - len, "/%s", part);
    if (!lstat(host, &st))
        return 1;
    host[len] = 0;
    dir = opendir(len ? host : "/");
    if (dir) {
        while ((e = readdir(dir)) != 0) {
            if (!strcasecmp(e->d_name, part)) {
                snprintf(host + len, size - len, "/%s", e->d_name);
                closedir(dir);
                return 1;
            }
        }
        closedir(dir);
    }
    snprintf(host + len, size - len, "/%s", part);
    return !must_exist;
}

int nt_host_path(const char *windows, char *host, size_t size, int must_exist)
{
    char full[1024], *part, *next;
    const char *root;

    nt_full_path(windows, full, sizeof full);
    if (full[0] == 'C')
        root = nt_config.drive_c;
    else if (full[0] == 'D')
        root = nt_config.drive_d;
    else if (full[0] == 'Z') /* Wine's whole-filesystem drive */
        root = "";
    else
        return 0;
    snprintf(host, size, "%s", root);
    for (part = full + 3; *part; part = next) {
        next = strchr(part, '\\');
        if (next)
            *next++ = 0;
        else
            next = part + strlen(part);
        if (!append_part(host, size, part, must_exist || *next))
            return 0;
    }
    if (!host[0])
        snprintf(host, size, "/");
    return 1;
}
