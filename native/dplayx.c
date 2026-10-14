/* DPLAYX for the native build: no network play (imported by ordinal 4,
 * DirectPlayLobbyCreateA). */
#include "nt.h"

#define DPERR_UNAVAILABLE 0x88770000u + 220

/* out: a 4-byte slot in game memory */
LONG WINAPI DirectPlayLobbyCreateA(void *guid, uint32_t *out, void *outer, void *data, DWORD size)
{
    (void)guid;
    (void)outer;
    (void)data;
    (void)size;
    if (out)
        *out = 0;
    return (LONG)DPERR_UNAVAILABLE;
}

LONG WINAPI DirectPlayCreate(void *guid, uint32_t *out, void *outer)
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

static LONG WINAPI dp_query(struct dplay *d, const void *iid, uint32_t *out)
{
    (void)iid;
    *out = (uint32_t)(uintptr_t)d; /* low memory */
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

/* The rest the game can reach (src/game/orig_pu.c / pv.c / xd.c, the
 * network screens): no sessions are found, nothing can be opened, created
 * or sent; the game shows its own "no network" messages. Argument counts
 * as IDirectPlay4A's (stdcall, this first). */
static LONG WINAPI dp_enum_sessions(struct dplay *d, void *desc, DWORD timeout, void *callback, void *ctx, DWORD flags)
{
    (void)d, (void)desc, (void)timeout, (void)callback, (void)ctx, (void)flags;
    return 0; /* searched; none */
}

static LONG WINAPI dp_unavailable2(struct dplay *d, DWORD a, DWORD b)
{
    (void)d, (void)a, (void)b;
    return (LONG)DPERR_UNAVAILABLE;
}

static LONG WINAPI dp_unavailable3(struct dplay *d, DWORD a, DWORD b, DWORD c)
{
    (void)d, (void)a, (void)b, (void)c;
    return (LONG)DPERR_UNAVAILABLE;
}

static LONG WINAPI dp_unavailable4(struct dplay *d, DWORD a, DWORD b, DWORD c, DWORD e)
{
    (void)d, (void)a, (void)b, (void)c, (void)e;
    return (LONG)DPERR_UNAVAILABLE;
}

/* CreatePlayer(&id, name, event, data, size, flags) */
static LONG WINAPI dp_create_player(struct dplay *d, DWORD *id, void *name, void *event, void *data, DWORD size,
                                    DWORD flags)
{
    (void)d, (void)name, (void)event, (void)data, (void)size, (void)flags;
    if (id)
        *id = 0;
    return (LONG)DPERR_UNAVAILABLE;
}

/* DestroyPlayer(id), CancelMessage(id, flags): nothing to undo */
static LONG WINAPI dp_ok1(struct dplay *d, DWORD a)
{
    (void)d, (void)a;
    return 0;
}

static LONG WINAPI dp_ok2(struct dplay *d, DWORD a, DWORD b)
{
    (void)d, (void)a, (void)b;
    return 0;
}

/* SendEx(from, to, flags, data, size, priority, timeout, context, &id) */
static LONG WINAPI dp_send_ex(struct dplay *d, DWORD from, DWORD to, DWORD flags, void *data, DWORD size,
                              DWORD priority, DWORD timeout, void *context, DWORD *id)
{
    (void)d, (void)from, (void)to, (void)flags, (void)data, (void)size, (void)priority, (void)timeout,
        (void)context;
    if (id)
        *id = 0;
    return (LONG)DPERR_UNAVAILABLE;
}

/* GetMessageQueue(from, to, flags, &messages, &bytes): empty */
static LONG WINAPI dp_message_queue(struct dplay *d, DWORD from, DWORD to, DWORD flags, DWORD *messages, DWORD *bytes)
{
    (void)d, (void)from, (void)to, (void)flags;
    if (messages)
        *messages = 0;
    if (bytes)
        *bytes = 0;
    return 0;
}

void *nt_dplay_create(void)
{
    static void **vtable;
    struct dplay *d = nt_lowmem_calloc(1, (uint32_t)sizeof *d);
    int i;
    if (!vtable) {
        int arena = nt_lowmem_arena(0);
        vtable = nt_lowmem_calloc(53, (uint32_t)sizeof *vtable);
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
        vtable[6] = (void *)dp_create_player;     /* +0x18 */
        vtable[9] = (void *)dp_ok1;               /* +0x24 DestroyPlayer */
        vtable[13] = (void *)dp_enum_sessions;    /* +0x34 */
        vtable[14] = (void *)dp_unavailable2;     /* +0x38 GetCaps(caps, flags) */
        vtable[18] = (void *)dp_unavailable3;     /* +0x48 GetPlayerAddress(id, data, &size) */
        vtable[22] = (void *)dp_unavailable2;     /* +0x58 GetSessionDesc(data, &size) */
        vtable[24] = (void *)dp_unavailable2;     /* +0x60 Open(desc, flags) */
        vtable[31] = (void *)dp_unavailable2;     /* +0x7c SetSessionDesc(desc, flags) */
        vtable[38] = (void *)dp_unavailable2;     /* +0x98 InitializeConnection(connection, flags) */
        vtable[40] = (void *)dp_unavailable4;     /* +0xa0 SendChatMessage(from, to, flags, message) */
        vtable[46] = (void *)dp_unavailable2;     /* +0xb8 GetPlayerFlags(id, &flags) */
        vtable[49] = (void *)dp_send_ex;          /* +0xc4 */
        vtable[50] = (void *)dp_message_queue;    /* +0xc8 */
        vtable[51] = (void *)dp_ok2;              /* +0xcc CancelMessage(id, flags) */
        nt_lowmem_arena(arena);
    }
    d->vtable = vtable;
    d->refs = 1;
    return d;
}
