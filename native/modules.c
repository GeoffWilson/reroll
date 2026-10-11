/* Looking up the native build's exports (the tables are generated:
 * tools/native_exports.py), and fake module handles. */
#include "nt.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MODULE_HANDLE_BASE 0x10000000u

/* "KERNEL32.dll", "kernel32", "c:\\windows\\system\\dinput.dll" -> index, or -1 */
static int module_index(const char *dll)
{
    char name[64];
    const char *base = dll;
    const char *p;
    int i;

    for (p = dll; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    snprintf(name, sizeof name, "%s", base);
    if (!strchr(name, '.'))
        strncat(name, ".dll", sizeof name - strlen(name) - 1);
    for (i = 0; nt_modules[i].name; i++)
        if (!strcasecmp(nt_modules[i].name, name))
            return i;
    return -1;
}

/* exports imported by ordinal */
static const struct {
    const char *dll, *ordinal, *name;
} ordinals[] = {
    {"dsound.dll", "#1", "DirectSoundCreate"},
    {"dplayx.dll", "#1", "DirectPlayCreate"},
    {"dplayx.dll", "#4", "DirectPlayLobbyCreateA"},
};

void *nt_find_export(const char *dll, const char *name)
{
    int m = module_index(dll);
    const struct nt_export *e;
    size_t i;
    if (m < 0)
        return 0;
    if (!name)
        return (void *)&nt_modules[m];
    if (name[0] == '#')
        for (i = 0; i < sizeof ordinals / sizeof *ordinals; i++)
            if (!strcasecmp(ordinals[i].dll, nt_modules[m].name) && !strcmp(ordinals[i].ordinal, name))
                name = ordinals[i].name;
    for (e = nt_modules[m].exports; e->name; e++)
        if (!strcmp(e->name, name))
            return e->fn;
    return 0;
}

uint32_t nt_module_handle(const char *dll)
{
    int m = module_index(dll);
    return m < 0 ? 0 : MODULE_HANDLE_BASE + (uint32_t)m * 0x10000u;
}

const char *nt_module_name(uint32_t handle)
{
    uint32_t i = (handle - MODULE_HANDLE_BASE) / 0x10000u;
    int n;
    if (handle == IMAGE_BASE)
        return "rollcage.exe";
    if (handle < MODULE_HANDLE_BASE || handle % 0x10000u)
        return 0;
    for (n = 0; nt_modules[n].name; n++)
        if ((uint32_t)n == i)
            return nt_modules[n].name;
    return 0;
}
