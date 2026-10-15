/* Network play test (make net-test): native/dplayx.c's DirectPlay on our
 * UDP transport, driven the way the game drives it, by a host and two
 * clients in three processes on this machine.
 *
 *  - the clients find the session (by address, and on the local network),
 *    join it and create their players; everyone sees the others arrive
 *    (system message 3) as DirectPlay would tell them;
 *  - messages go host -> client, client -> host and client -> client (the
 *    host relays), all of them in order, also with 20% of the packets lost
 *    (REROLL_NET_LOSS in the second run);
 *  - chat reaches everyone else (0x109); a locked session refuses a fourth
 *    player (Carol, searching until she sees it locked); a client leaving is seen by the others (5); the host closing
 *    loses the session for the client still there (0x31).
 *
 *     build/net_test [port]    (exit status 0: passed) */
#include "../native/nt.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---- what dplayx.c needs from the rest of native/ ---- */

static const char *who = "?";

void nt_log(const char *fmt, ...)
{
    va_list ap;
    if (!getenv("NET_TEST_VERBOSE"))
        return;
    fprintf(stderr, "[%s] ", who);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void unexpected(void)
{
    fprintf(stderr, "[%s] an unexpected DirectPlay method was called\n", who);
    _exit(3);
}

void *nt_named_stub(const char *module, const char *name)
{
    (void)module;
    (void)name;
    return (void *)unexpected;
}

static uint8_t *low, *low_end;

void *nt_lowmem_alloc(uint32_t size)
{
    uint8_t *p;
    if (!low) {
        low = mmap(0, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (low == MAP_FAILED)
            abort();
        low_end = low + (1 << 20);
    }
    size = (size + 15) & ~15u;
    if (low + size > low_end)
        abort();
    p = low;
    low += size;
    return p;
}

int nt_lowmem_arena(int game)
{
    (void)game;
    return 0;
}

int nt_event_set(HANDLE h)
{
    (void)h;
    return 1;
}

void *nt_dplay_create(void);
void *nt_dplay_lobby_create(void);

/* ---- the game's side ---- */

#define CALL(o, i, type) ((type)(*(void ***)(o))[i])
#define LOW(p) ((uint32_t)(uintptr_t)(p))

static const uint8_t APP[16] = {0x08, 0xd6, 0xe5, 0x69, 0xa4, 0x7f, 0xd2, 0x11,
                                0x90, 0xdc, 0x00, 0x40, 0x95, 0x03, 0xb8, 0xd0};
static const uint8_t SP_TCPIP[16] = {0xe0, 0x5e, 0xe9, 0x36, 0x77, 0x85, 0xcf, 0x11,
                                     0x96, 0x0c, 0x00, 0x80, 0xc7, 0x53, 0x4e, 0x82};
static const uint8_t AID_SP[16] = {0xc0, 0x16, 0xd9, 0x07, 0xaf, 0xe0, 0xcf, 0x11,
                                   0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e};
static const uint8_t AID_INET[16] = {0xa0, 0x4d, 0xa5, 0xc4, 0xaf, 0xe0, 0xcf, 0x11,
                                     0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e};

static void *dp;
static uint32_t my_id;

#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "[%s] FAILED: ", who);                                                                     \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fputc('\n', stderr);                                                                                       \
            _exit(1);                                                                                                  \
        }                                                                                                              \
    } while (0)

static uint64_t ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* the game's dplay_connection_init: a compound address, InitializeConnection */
static void connect_to(const char *address)
{
    void *lobby = nt_dplay_lobby_create();
    uint32_t *elem = nt_lowmem_alloc(48), *guid = nt_lowmem_alloc(16);
    char *text = nt_lowmem_alloc(64);
    uint8_t buf[0x100];
    DWORD size = sizeof buf;
    LONG hr;

    memcpy(guid, SP_TCPIP, 16);
    strcpy(text, address);
    memcpy(elem, AID_SP, 16);
    elem[4] = 16;
    elem[5] = LOW(guid);
    memcpy(elem + 6, AID_INET, 16);
    elem[10] = (uint32_t)strlen(text) + 1;
    elem[11] = LOW(text);
    hr = CALL(lobby, 14, LONG (*)(void *, const void *, DWORD, void *, DWORD *))(lobby, elem, 2, buf, &size);
    CHECK(hr == 0, "CreateCompoundAddress %08x", (unsigned)hr);
    {
        uint8_t *conn = nt_lowmem_alloc(size);
        memcpy(conn, buf, size);
        hr = CALL(dp, 38, LONG (*)(void *, DWORD, DWORD))(dp, LOW(conn), 0);
        CHECK(hr == 0, "InitializeConnection %08x", (unsigned)hr);
    }
}

static void create_player(const char *name, DWORD flags)
{
    uint32_t *dpname = nt_lowmem_alloc(16);
    char *text = nt_lowmem_alloc(32);
    LONG hr;
    strcpy(text, name);
    memset(dpname, 0, 16);
    dpname[0] = 16;
    dpname[2] = LOW(text);
    hr = CALL(dp, 6, LONG (*)(void *, DWORD *, void *, void *, void *, DWORD, DWORD))(dp, &my_id, dpname, 0, 0, 0,
                                                                                      flags);
    CHECK(hr == 0, "CreatePlayer %08x", (unsigned)hr);
}

/* the game's net_session_enum_cb: the sessions into a list */
static struct {
    uint8_t guid[16];
    char name[64];
    uint32_t flags, max, players;
} sessions[8];
static int nsessions;

static int32_t enum_cb(const void *desc, uint32_t *timeout, uint32_t flags, void *ctx)
{
    const uint8_t *d = desc;
    (void)timeout;
    (void)ctx;
    if (flags & 1)
        return 0;
    if (nsessions < 8) {
        memcpy(sessions[nsessions].guid, d + 8, 16);
        snprintf(sessions[nsessions].name, 64, "%s", (const char *)(uintptr_t) * (const uint32_t *)(d + 0x30));
        sessions[nsessions].flags = *(const uint32_t *)(d + 4);
        sessions[nsessions].max = *(const uint32_t *)(d + 0x28);
        sessions[nsessions].players = *(const uint32_t *)(d + 0x2c);
        nsessions++;
    }
    return 1;
}

/* searches for up to `wait` ms, as the game's search screen polls */
static void search(int wait)
{
    uint8_t *desc = nt_lowmem_alloc(0x50);
    uint64_t end = ms() + (uint64_t)wait;
    memset(desc, 0, 0x50);
    *(uint32_t *)desc = 0x50;
    memcpy(desc + 0x18, APP, 16);
    do {
        LONG hr;
        nsessions = 0;
        hr = CALL(dp, 13, LONG (*)(void *, void *, DWORD, void *, void *, DWORD))(dp, desc, 0, (void *)enum_cb, 0,
                                                                                   0x92);
        CHECK(hr == 0, "EnumSessions %08x", (unsigned)hr);
        if (nsessions)
            return;
        usleep(100000);
    } while (ms() < end);
}

static LONG join(int index)
{
    uint8_t *desc = nt_lowmem_alloc(0x50);
    memset(desc, 0, 0x50);
    *(uint32_t *)desc = 0x50;
    memcpy(desc + 8, sessions[index].guid, 16);
    return CALL(dp, 24, LONG (*)(void *, DWORD, DWORD))(dp, LOW(desc), 0x81);
}

static LONG send_to(uint32_t to, const void *data, DWORD size)
{
    DWORD id;
    return CALL(dp, 49, LONG (*)(void *, DWORD, DWORD, DWORD, void *, DWORD, DWORD, DWORD, void *, DWORD *))(
        dp, my_id, to, 1, (void *)data, size, 0, 0, 0, &id);
}

static uint8_t *rbuf;

/* the next message, waiting up to `wait` ms: its size (-1: none) */
static int receive(uint32_t *from, int wait)
{
    uint64_t end = ms() + (uint64_t)wait;
    if (!rbuf)
        rbuf = nt_lowmem_alloc(0x1000);
    for (;;) {
        DWORD f = 0, t = 0, size = 0x1000;
        LONG hr = CALL(dp, 25, LONG (*)(void *, DWORD *, DWORD *, DWORD, void *, DWORD *))(dp, &f, &t, 1, rbuf, &size);
        if (hr == 0) {
            *from = f;
            return (int)size;
        }
        CHECK(hr == (LONG)0x887700be, "Receive %08x", (unsigned)hr);
        if (ms() >= end)
            return -1;
        usleep(2000);
    }
}

/* the next system message of `type` (others of the kinds expected skipped) */
static int expect_system(uint32_t type, int wait)
{
    uint64_t end = ms() + (uint64_t)wait;
    while (ms() < end) {
        uint32_t from;
        int n = receive(&from, (int)(end - ms()));
        if (n < 0)
            break;
        if (from == 0 && *(uint32_t *)rbuf == type)
            return 1;
        if (from == 0 && (*(uint32_t *)rbuf == 3 || *(uint32_t *)rbuf == 5))
            continue;
        CHECK(0, "expected system message %x, got %s %x", (unsigned)type, from ? "data from" : "system",
              from ? (unsigned)from : (unsigned)*(uint32_t *)rbuf);
    }
    return 0;
}

static uint32_t player_flags(uint32_t id, LONG *hr)
{
    uint32_t *f = nt_lowmem_alloc(4);
    *hr = CALL(dp, 46, LONG (*)(void *, DWORD, DWORD))(dp, id, LOW(f));
    return *f;
}

/* messages "n" numbered from 0, `count` of them, to `to` */
static void send_numbered(uint32_t to, int count, int size)
{
    uint8_t m[0x540];
    int i;
    for (i = 0; i < count; i++) {
        memset(m, (uint8_t)i, (size_t)size);
        memcpy(m, &i, 4);
        CHECK(send_to(to, m, (DWORD)size) == 0, "SendEx %d", i);
        if (i % 50 == 49)
            usleep(5000);
    }
}

static void receive_numbered(uint32_t from_id, int count, int size)
{
    int i;
    for (i = 0; i < count; i++) {
        uint32_t from;
        int n = receive(&from, 8000);
        CHECK(n >= 0, "message %d of %d from %08x not received", i, count, (unsigned)from_id);
        if (from == 0) { /* someone joining or leaving meanwhile */
            i--;
            continue;
        }
        CHECK(from == from_id, "message %d from %08x, expected %08x", i, (unsigned)from, (unsigned)from_id);
        CHECK(n == size, "message %d is %d bytes, expected %d", i, n, size);
        CHECK(*(int *)rbuf == i, "message %d arrived as %d (out of order)", i, *(int *)rbuf);
        CHECK(rbuf[size - 1] == (uint8_t)i || size <= 4, "message %d damaged", i);
    }
}

/* ---- the three players ---- */

static int carol_pipe[2]; /* Carol tells Bob she has been refused */

static void host(void)
{
    uint8_t *desc = nt_lowmem_alloc(0x50);
    char *name = nt_lowmem_alloc(64);
    uint32_t c1 = 0, c2 = 0, from;
    uint8_t ids[8];
    LONG hr;
    int n, seen = 0;

    who = "host";
    dp = nt_dplay_create();
    connect_to("");
    strcpy(name, "Test Game");
    memset(desc, 0, 0x50);
    *(uint32_t *)desc = 0x50;
    *(uint32_t *)(desc + 4) = 0xa800;
    memcpy(desc + 0x18, APP, 16);
    *(uint32_t *)(desc + 0x28) = 6;
    *(uint32_t *)(desc + 0x30) = LOW(name);
    hr = CALL(dp, 24, LONG (*)(void *, DWORD, DWORD))(dp, LOW(desc), 0x82);
    CHECK(hr == 0, "Open (create) %08x", (unsigned)hr);
    create_player("Hosty", 0x100);
    CHECK(my_id == 1, "the host's player is %08x, not 1", (unsigned)my_id);

    /* both clients arrive */
    while (seen < 2) {
        n = receive(&from, 10000);
        CHECK(n >= 0, "the clients didn't arrive");
        CHECK(from == 0 && *(uint32_t *)rbuf == 3, "expected a player created");
        {
            const char *pname = (const char *)(uintptr_t) * (uint32_t *)(rbuf + 0x20);
            uint32_t id = *(uint32_t *)(rbuf + 8);
            if (!strcmp(pname, "Alice"))
                c1 = id;
            else if (!strcmp(pname, "Bob"))
                c2 = id;
            else
                CHECK(0, "unknown player \"%s\"", pname);
            seen++;
        }
    }
    {
        LONG e;
        CHECK((player_flags(c1, &e) & 8) == 0 && e == 0, "Alice's flags");
        CHECK((player_flags(1, &e) & 0x108) == 0x108 && e == 0, "the host's own flags");
    }
    /* the ids to everyone (as the game's player list) */
    memcpy(ids, &c1, 4);
    memcpy(ids + 4, &c2, 4);
    CHECK(send_to(c1, ids, 8) == 0 && send_to(c2, ids, 8) == 0, "sending the ids");

    send_numbered(c1, 300, 0x40);
    receive_numbered(c1, 300, 0x90);
    send_numbered(0, 50, 0x540); /* to everyone */

    /* chat from Alice */
    CHECK(expect_system(0x109, 8000), "no chat from Alice");
    {
        const char *text = (const char *)(uintptr_t) * (uint32_t *)(uintptr_t)(*(uint32_t *)(rbuf + 0x14) + 8);
        CHECK(!strcmp(text, "Alice: hello"), "chat text \"%s\"", text);
        CHECK(*(uint32_t *)(rbuf + 8) == c1, "chat sender");
    }

    /* lock the session: a third client is refused */
    {
        uint32_t *size = nt_lowmem_alloc(4); /* game memory, as the game's stack */
        uint8_t *sd;
        *size = 0;
        hr = CALL(dp, 22, LONG (*)(void *, DWORD, DWORD))(dp, 0, LOW(size));
        CHECK(hr == (LONG)0x8877001e, "GetSessionDesc for the size %08x", (unsigned)hr);
        sd = nt_lowmem_alloc(*size);
        hr = CALL(dp, 22, LONG (*)(void *, DWORD, DWORD))(dp, LOW(sd), LOW(size));
        CHECK(hr == 0, "GetSessionDesc %08x", (unsigned)hr);
        CHECK(!strcmp((const char *)(uintptr_t) * (uint32_t *)(sd + 0x30), "Test Game"), "session name");
        *(uint32_t *)(sd + 4) |= 0x21;
        hr = CALL(dp, 31, LONG (*)(void *, DWORD, DWORD))(dp, LOW(sd), 0);
        CHECK(hr == 0, "SetSessionDesc %08x", (unsigned)hr);
    }
    CHECK(send_to(c2, "locked", 7) == 0, "telling Bob");

    /* Alice leaves */
    CHECK(expect_system(5, 20000), "Alice's leaving not seen");
    CHECK(*(uint32_t *)(rbuf + 8) == c1, "the wrong player left");
    {
        LONG e;
        player_flags(c1, &e);
        CHECK(e != 0, "Alice still a player");
    }
    /* Bob says done; the host closes */
    n = receive(&from, 10000);
    CHECK(n == 5 && from == c2 && !memcmp(rbuf, "done", 5), "Bob's last message");
    CALL(dp, 4, LONG (*)(void *))(dp);
    CALL(dp, 2, LONG (*)(void *))(dp);
    _exit(0);
}

static void client(const char *name, const char *address)
{
    uint32_t from, alice, bob, other;
    LONG hr;
    int n, is_alice = !strcmp(name, "Alice");

    who = name;
    dp = nt_dplay_create();
    connect_to(address);
    search(5000);
    CHECK(nsessions == 1, "%d sessions found at \"%s\"", nsessions, address);
    CHECK(!strcmp(sessions[0].name, "Test Game") && sessions[0].max == 6, "the session found: \"%s\", %u",
          sessions[0].name, (unsigned)sessions[0].max);
    hr = join(0);
    CHECK(hr == 0, "Open (join) %08x", (unsigned)hr);
    create_player(name, 0);
    {
        LONG e;
        CHECK((player_flags(1, &e) & 0x100) && e == 0, "the host isn't the server player");
        CHECK((player_flags(my_id, &e) & 8) && e == 0, "our player isn't local");
    }
    /* the ids, from the host */
    for (;;) {
        n = receive(&from, 10000);
        CHECK(n >= 0, "no ids from the host");
        if (from == 1 && n == 8)
            break;
        CHECK(from == 0, "data before the ids");
    }
    memcpy(&alice, rbuf, 4);
    memcpy(&bob, rbuf + 4, 4);
    CHECK(my_id == (is_alice ? alice : bob), "the host's idea of our id");
    other = is_alice ? bob : alice;

    if (is_alice) {
        receive_numbered(1, 300, 0x40);
        send_numbered(1, 300, 0x90);
        receive_numbered(1, 50, 0x540);
        send_numbered(other, 400, 0x24); /* relayed by the host */
        {
            uint32_t *chat = nt_lowmem_alloc(12);
            char *text = nt_lowmem_alloc(32);
            strcpy(text, "Alice: hello");
            chat[0] = 12;
            chat[1] = 0;
            chat[2] = LOW(text);
            hr = CALL(dp, 40, LONG (*)(void *, DWORD, DWORD, DWORD, DWORD))(dp, my_id, 0, 0, LOW(chat));
            CHECK(hr == 0, "SendChatMessage %08x", (unsigned)hr);
        }
        /* wait for Bob's go, then leave */
        n = receive(&from, 15000);
        CHECK(n == 3 && from == other && !memcmp(rbuf, "go", 3), "Bob's go");
        usleep(200000);
        CALL(dp, 4, LONG (*)(void *))(dp);
        CALL(dp, 2, LONG (*)(void *))(dp);
        _exit(0);
    }

    receive_numbered(1, 50, 0x540);
    receive_numbered(other, 400, 0x24);
    CHECK(expect_system(0x109, 8000), "no chat from Alice");
    n = receive(&from, 8000);
    CHECK(n == 7 && from == 1 && !memcmp(rbuf, "locked", 7), "the host's locked message (%d bytes from %08x: %08x)", n,
          (unsigned)from, n >= 4 ? (unsigned)*(uint32_t *)rbuf : 0);
    /* Carol (her own process) has tried to join the locked session */
    {
        char c;
        CHECK(read(carol_pipe[0], &c, 1) == 1 && c == 'y', "Carol's test");
    }
    CHECK(send_to(other, "go", 3) == 0, "go to Alice");
    CHECK(expect_system(5, 20000), "Alice's leaving not seen");
    CHECK(*(uint32_t *)(rbuf + 8) == other, "the wrong player left");
    CHECK(send_to(1, "done", 5) == 0, "done to the host");
    CHECK(expect_system(0x31, 25000), "the session wasn't lost when the host closed");
    {
        LONG e;
        player_flags(1, &e);
        CHECK(e != 0, "the host is still a player");
        CHECK(send_to(1, "x", 1) != 0, "sending in a lost session worked");
    }
    _exit(0);
}

/* a fourth player, refused once the host has locked the session */
static void carol(const char *address)
{
    uint64_t end = ms() + 60000;
    LONG hr;

    who = "Carol";
    dp = nt_dplay_create();
    connect_to(address);
    for (;;) {
        search(1000);
        if (nsessions == 1 && (sessions[0].flags & 0x21))
            break;
        CHECK(ms() < end, "the session never showed as locked");
        usleep(200000);
    }
    hr = join(0);
    CHECK(hr != 0, "Carol joined a locked session");
    CHECK(write(carol_pipe[1], "y", 1) == 1, "telling Bob");
    _exit(0);
}

static int run(const char *address_a, const char *address_b)
{
    pid_t pids[4];
    int i, ok = 1;

    if (pipe(carol_pipe) != 0)
        return 0;
    pids[0] = fork();
    if (pids[0] == 0)
        host();
    usleep(300000);
    pids[1] = fork();
    if (pids[1] == 0)
        client("Alice", address_a);
    usleep(100000);
    pids[2] = fork();
    if (pids[2] == 0)
        client("Bob", address_b);
    pids[3] = fork();
    if (pids[3] == 0)
        carol(address_a);
    for (i = 0; i < 4; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = 0;
    }
    return ok;
}

int main(int argc, char **argv)
{
    const char *port = argc > 1 ? argv[1] : "47790";
    char address[64];
    int ok;

    alarm(120);
    setenv("REROLL_NET_PORT", port, 1);
    snprintf(address, sizeof address, "127.0.0.1:%s", port);

    printf("net_test: host and two clients, by address and on the local network\n");
    unsetenv("REROLL_NET_LOSS");
    ok = run(address, "");
    printf("net_test: %s\n", ok ? "passed" : "FAILED");
    if (!ok)
        return 1;

    printf("net_test: again with 20%% of the packets lost\n");
    setenv("REROLL_NET_LOSS", "20", 1);
    ok = run(address, address);
    printf("net_test: %s\n", ok ? "passed" : "FAILED");
    return ok ? 0 : 1;
}
