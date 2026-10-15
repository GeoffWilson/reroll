/* ole32 for the native build: COM is only used for DirectPlay and its
 * lobby (native/dplayx.c). */
#include "nt.h"

#define REGDB_E_CLASSNOTREG 0x80040154u

LONG WINAPI CoInitialize(void *reserved)
{
    (void)reserved;
    return 0;
}

void WINAPI CoUninitialize(void)
{
}

void *nt_dplay_create(void);       /* dplayx.c */
void *nt_dplay_lobby_create(void);

/* out: a 4-byte slot in game memory; the object is low memory */
LONG WINAPI CoCreateInstance(const void *clsid, void *outer, DWORD context, const void *iid, uint32_t *out)
{
    (void)outer;
    (void)context;
    (void)iid;
    if (clsid && *(const uint32_t *)clsid == 0xd1eb6d20u) { /* CLSID_DirectPlay */
        *out = (uint32_t)(uintptr_t)nt_dplay_create();
        return 0;
    }
    if (clsid && *(const uint32_t *)clsid == 0x2fe8f810u) { /* CLSID_DirectPlayLobby */
        *out = (uint32_t)(uintptr_t)nt_dplay_lobby_create();
        return 0;
    }
    nt_log("CoCreateInstance(%08x): not available", clsid ? *(const uint32_t *)clsid : 0);
    if (out)
        *out = 0;
    return (LONG)REGDB_E_CLASSNOTREG;
}
