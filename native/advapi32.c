/* ADVAPI32 for the native build: the registry, kept in reroll_registry.reg
 * next to the exe (Wine's .reg syntax), imported once from the Wine prefix
 * (Rollcage's keys: the installer paths and language, and BootSys, the
 * settings the game also hands from process to process). */
#include "nt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HKEY_CURRENT_USER 0x80000001u
#define HKEY_LOCAL_MACHINE 0x80000002u
#define ERROR_MORE_DATA 234

struct value {
    char name[64];
    DWORD type;
    uint8_t *data;
    DWORD size;
};

struct key {
    char path[256]; /* "HKLM\Software\Psygnosis\Rollcage" */
    struct value *values;
    int count;
};

#define MAX_KEYS 64
static struct key keys[MAX_KEYS];
static int key_count;
static char registry_file[600];

static struct key *find_key(const char *path, int create)
{
    int i;
    for (i = 0; i < key_count; i++)
        if (!strcasecmp(keys[i].path, path))
            return &keys[i];
    if (!create || key_count == MAX_KEYS)
        return 0;
    snprintf(keys[key_count].path, sizeof keys[0].path, "%s", path);
    return &keys[key_count++];
}

static struct value *find_value(struct key *k, const char *name, int create)
{
    int i;
    for (i = 0; i < k->count; i++)
        if (!strcasecmp(k->values[i].name, name))
            return &k->values[i];
    if (!create)
        return 0;
    k->values = realloc(k->values, (size_t)(k->count + 1) * sizeof *k->values);
    memset(&k->values[k->count], 0, sizeof *k->values);
    snprintf(k->values[k->count].name, sizeof k->values[0].name, "%s", name);
    return &k->values[k->count++];
}

static void set_data(struct value *v, DWORD type, const void *data, DWORD size)
{
    free(v->data);
    v->type = type;
    v->size = size;
    v->data = malloc(size ? size : 1);
    memcpy(v->data, data, size);
}

/* ---- the .reg text form ---- */

static void save(void)
{
    FILE *f = fopen(registry_file, "w");
    int i, j;
    DWORD b;
    if (!f) {
        nt_log("registry: cannot write %s", registry_file);
        return;
    }
    fprintf(f, "; reroll native registry (Wine .reg syntax)\n");
    for (i = 0; i < key_count; i++) {
        fprintf(f, "\n[%s]\n", keys[i].path);
        for (j = 0; j < keys[i].count; j++) {
            const struct value *v = &keys[i].values[j];
            fprintf(f, "\"%s\"=", v->name);
            if (v->type == 1) {
                const char *s = (const char *)v->data;
                fputc('"', f);
                for (b = 0; b < v->size && s[b]; b++) {
                    if (s[b] == '\\' || s[b] == '"')
                        fputc('\\', f);
                    fputc(s[b], f);
                }
                fputs("\"\n", f);
            } else if (v->type == 4 && v->size == 4) {
                fprintf(f, "dword:%08x\n", *(const DWORD *)v->data);
            } else {
                if (v->type == 3)
                    fprintf(f, "hex:");
                else
                    fprintf(f, "hex(%x):", v->type);
                for (b = 0; b < v->size; b++)
                    fprintf(f, "%02x%s", v->data[b], b + 1 < v->size ? "," : "");
                fputc('\n', f);
            }
        }
    }
    fclose(f);
}

/* Reads a .reg file. `map` turns a section name into our key path (or
 * returns 0 to skip it). */
static int load(const char *path, int (*map)(const char *section, char *out, size_t size))
{
    FILE *f = fopen(path, "r");
    char *line = 0, *joined = 0;
    size_t cap = 0, joined_len = 0;
    struct key *current = 0;
    int keys_read = 0;

    if (!f)
        return -1;
    while (getline(&line, &cap, f) > 0) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        /* continuation lines end in a backslash */
        joined = realloc(joined, joined_len + len + 1);
        memcpy(joined + joined_len, line, len + 1);
        joined_len += len;
        if (len && line[len - 1] == '\\' && joined[0] == '"' && strstr(joined, "=hex")) {
            joined[--joined_len] = 0;
            continue;
        }
        if (joined[0] == '[') {
            char section[512], out[256], *end = strchr(joined, ']'), *s, *o;
            current = 0;
            if (end) {
                *end = 0;
                /* unescape "\\" */
                for (s = joined + 1, o = section; *s && o < section + sizeof section - 1; s++) {
                    if (s[0] == '\\' && s[1] == '\\')
                        s++;
                    *o++ = *s;
                }
                *o = 0;
                if (map(section, out, sizeof out)) {
                    current = find_key(out, 1);
                    keys_read++;
                }
            }
        } else if (current && joined[0] == '"') {
            char name[64], *p = joined + 1, *n = name;
            struct value *v;
            while (*p && *p != '"' && n < name + sizeof name - 1)
                *n++ = *p++;
            *n = 0;
            if (*p == '"' && p[1] == '=') {
                p += 2;
                v = find_value(current, name, 1);
                if (*p == '"') {
                    char text[1024], *t = text;
                    for (p++; *p && *p != '"' && t < text + sizeof text - 1; p++) {
                        if (*p == '\\' && p[1])
                            p++;
                        *t++ = *p;
                    }
                    *t = 0;
                    set_data(v, 1, text, (DWORD)(t - text + 1));
                } else if (!strncmp(p, "dword:", 6)) {
                    DWORD d = (DWORD)strtoul(p + 6, 0, 16);
                    set_data(v, 4, &d, 4);
                } else if (!strncmp(p, "hex", 3)) {
                    DWORD type = 3, size = 0;
                    uint8_t *bytes = malloc(strlen(p) / 2 + 1);
                    if (p[3] == '(')
                        type = (DWORD)strtoul(p + 4, 0, 16);
                    p = strchr(p, ':');
                    while (p && *p) {
                        p++;
                        while (*p == ' ' || *p == '\\')
                            p++;
                        if (!*p)
                            break;
                        bytes[size++] = (uint8_t)strtoul(p, &p, 16);
                    }
                    set_data(v, type, bytes, size);
                    free(bytes);
                }
            }
        }
        joined_len = 0;
    }
    free(line);
    free(joined);
    fclose(f);
    return keys_read;
}

static int map_own(const char *section, char *out, size_t size)
{
    snprintf(out, size, "%s", section);
    return 1;
}

/* Wine: 32-bit HKLM\Software lives under Software\Wow6432Node in system.reg */
static int map_wine_system(const char *section, char *out, size_t size)
{
    if (strncasecmp(section, "Software\\Wow6432Node\\Psygnosis", 30))
        return 0;
    snprintf(out, size, "HKLM\\Software\\%s", section + 21);
    return 1;
}

static int map_wine_user(const char *section, char *out, size_t size)
{
    if (strncasecmp(section, "Software\\Psygnosis", 18))
        return 0;
    snprintf(out, size, "HKCU\\%s", section);
    return 1;
}

void nt_registry_load(void)
{
    char prefix[600], path[700], *slash;

    snprintf(registry_file, sizeof registry_file, "%s/reroll_registry.reg", nt_config.data_dir);
    if (load(registry_file, map_own) >= 0) {
        nt_log("registry: %d keys from %s", key_count, registry_file);
        return;
    }
    /* first run: the Wine prefix is drive_c's parent */
    snprintf(prefix, sizeof prefix, "%s", nt_config.drive_c);
    slash = strrchr(prefix, '/');
    if (slash)
        *slash = 0;
    snprintf(path, sizeof path, "%s/system.reg", prefix);
    load(path, map_wine_system);
    snprintf(path, sizeof path, "%s/user.reg", prefix);
    load(path, map_wine_user);
    nt_log("registry: imported %d keys from the Wine prefix %s", key_count, prefix);
    save();
}

/* ---- the API ---- */

/* Open keys are small handles, KEY_BASE + 4 * their entry's index (the game
 * keeps them in 32 bits); the roots are the predefined values. */
#define KEY_BASE 0x1000u

static DWORD key_handle(const struct key *k)
{
    return KEY_BASE + 4u * (DWORD)(k - keys);
}

static struct key *open_key(DWORD h)
{
    if (h >= KEY_BASE && h < KEY_BASE + 4u * MAX_KEYS && !(h & 3) && (int)(h - KEY_BASE) / 4 < key_count)
        return &keys[(h - KEY_BASE) / 4];
    return 0;
}

static int key_path(DWORD parent, const char *sub, char *out, size_t size)
{
    const char *base;
    if (parent == HKEY_LOCAL_MACHINE)
        base = "HKLM";
    else if (parent == HKEY_CURRENT_USER)
        base = "HKCU";
    else if (open_key(parent))
        base = open_key(parent)->path;
    else
        return 0;
    if (sub && *sub)
        snprintf(out, size, "%s\\%s", base, sub);
    else
        snprintf(out, size, "%s", base);
    for (; *out; out++)
        if (*out == '/')
            *out = '\\';
    return 1;
}

LONG WINAPI RegOpenKeyExA(DWORD parent, const char *sub, DWORD options, DWORD sam, DWORD *result)
{
    char path[512];
    struct key *k;
    (void)options;
    (void)sam;
    if (!key_path(parent, sub, path, sizeof path) || !(k = find_key(path, 0)))
        return ERROR_FILE_NOT_FOUND;
    *result = key_handle(k);
    return ERROR_SUCCESS;
}

LONG WINAPI RegCreateKeyExA(DWORD parent, const char *sub, DWORD reserved, char *class_name, DWORD options,
                            DWORD sam, void *security, DWORD *result, DWORD *disposition)
{
    char path[512];
    struct key *k;
    int existed;
    (void)reserved;
    (void)class_name;
    (void)options;
    (void)sam;
    (void)security;
    if (!key_path(parent, sub, path, sizeof path))
        return ERROR_FILE_NOT_FOUND;
    existed = find_key(path, 0) != 0;
    k = find_key(path, 1);
    if (!k)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!existed)
        save();
    *result = key_handle(k);
    if (disposition)
        *disposition = existed ? 2 /* REG_OPENED_EXISTING_KEY */ : 1 /* REG_CREATED_NEW_KEY */;
    return ERROR_SUCCESS;
}

LONG WINAPI RegCloseKey(DWORD key)
{
    (void)key;
    return ERROR_SUCCESS;
}

LONG WINAPI RegQueryValueExA(DWORD key, const char *name, DWORD *reserved, DWORD *type, uint8_t *data, DWORD *size)
{
    struct key *k = open_key(key);
    struct value *v = k ? find_value(k, name ? name : "", 0) : 0;
    (void)reserved;
    if (!v)
        return ERROR_FILE_NOT_FOUND;
    if (type)
        *type = v->type;
    if (data) {
        if (!size || *size < v->size) {
            if (size)
                *size = v->size;
            return ERROR_MORE_DATA;
        }
        memcpy(data, v->data, v->size);
    }
    if (size)
        *size = v->size;
    return ERROR_SUCCESS;
}

LONG WINAPI RegSetValueExA(DWORD key, const char *name, DWORD reserved, DWORD type, const uint8_t *data, DWORD size)
{
    struct key *k = open_key(key);
    (void)reserved;
    if (!k)
        return ERROR_INVALID_HANDLE;
    set_data(find_value(k, name ? name : "", 1), type, data, size);
    save();
    return ERROR_SUCCESS;
}

LONG WINAPI RegDeleteValueA(DWORD key, const char *name)
{
    struct key *k = open_key(key);
    struct value *v = k ? find_value(k, name ? name : "", 0) : 0;
    if (!v)
        return ERROR_FILE_NOT_FOUND;
    free(v->data);
    *v = k->values[--k->count];
    save();
    return ERROR_SUCCESS;
}

LONG WINAPI RegUnLoadKeyA(DWORD key, const char *sub)
{
    (void)key;
    (void)sub;
    return ERROR_ACCESS_DENIED;
}

BOOL WINAPI GetUserNameA(char *buf, DWORD *size)
{
    const char *user = getenv("USER");
    DWORD len;
    if (!user)
        user = "player";
    len = (DWORD)strlen(user) + 1;
    if (*size < len) {
        *size = len;
        return FALSE;
    }
    memcpy(buf, user, len);
    *size = len;
    return TRUE;
}
