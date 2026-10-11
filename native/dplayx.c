/* DPLAYX for the native build: no network play (imported by ordinal 4,
 * DirectPlayLobbyCreateA). */
#include "nt.h"

#define DPERR_UNAVAILABLE 0x88770000u + 220

LONG WINAPI DirectPlayLobbyCreateA(void *guid, void **out, void *outer, void *data, DWORD size)
{
    (void)guid;
    (void)outer;
    (void)data;
    (void)size;
    if (out)
        *out = 0;
    return (LONG)DPERR_UNAVAILABLE;
}

LONG WINAPI DirectPlayCreate(void *guid, void **out, void *outer)
{
    (void)guid;
    (void)outer;
    if (out)
        *out = 0;
    return (LONG)DPERR_UNAVAILABLE;
}

/* ---- IDirectPlay4A from CoCreateInstance (native/ole32.c) ----
 * The game makes one at every load step (0x48a600) and stops if it can't;
 * this one has no connections, sessions or messages. */

#include <stdio.h>
#include <stdlib.h>

void *nt_named_stub(const char *module, const char *name);

#define DPERR_NOMESSAGES 0x887700beu
#define DPERR_NOSESSIONS 0x88770000u + 0x5c

struct dplay {
    void **vtable;
    LONG refs;
};

static LONG WINAPI dp_query(struct dplay *d, const void *iid, void **out)
{
    (void)iid;
    *out = d;
    d->refs++;
    return 0;
}

static LONG WINAPI dp_add_ref(struct dplay *d)
{
    return ++d->refs;
}

static LONG WINAPI dp_release(struct dplay *d)
{
    return --d->refs > 0 ? d->refs : 0;
}

static LONG WINAPI dp_close(struct dplay *d)
{
    (void)d;
    return 0;
}

/* EnumConnections(guid, callback, context, flags): none */
static LONG WINAPI dp_enum_connections(struct dplay *d, const void *app, void *callback, void *ctx, DWORD flags)
{
    (void)d;
    (void)app;
    (void)callback;
    (void)ctx;
    (void)flags;
    return 0;
}

/* GetMessageCount(player, &count) */
static LONG WINAPI dp_message_count(struct dplay *d, DWORD player, DWORD *count)
{
    (void)d;
    (void)player;
    *count = 0;
    return 0;
}

/* Receive(&from, &to, flags, data, &size) */
static LONG WINAPI dp_receive(struct dplay *d, DWORD *from, DWORD *to, DWORD flags, void *data, DWORD *size)
{
    (void)d;
    (void)from;
    (void)to;
    (void)flags;
    (void)data;
    (void)size;
    return (LONG)DPERR_NOMESSAGES;
}

void *nt_dplay_create(void)
{
    static void **vtable;
    struct dplay *d = nt_lowmem_calloc(1, sizeof *d);
    int i;
    if (!vtable) {
        int arena = nt_lowmem_arena(0);
        vtable = nt_lowmem_calloc(53, sizeof *vtable);
        for (i = 0; i < 53; i++) {
            char name[64];
            snprintf(name, sizeof name, "IDirectPlay4A method %d (+0x%x)", i, i * 4);
            vtable[i] = nt_named_stub("com", name);
        }
        vtable[0] = (void *)dp_query;
        vtable[1] = (void *)dp_add_ref;
        vtable[2] = (void *)dp_release;
        vtable[4] = (void *)dp_close;
        vtable[17] = (void *)dp_message_count;
        vtable[25] = (void *)dp_receive;
        vtable[35] = (void *)dp_enum_connections;
        nt_lowmem_arena(arena);
    }
    d->vtable = vtable;
    d->refs = 1;
    return d;
}
