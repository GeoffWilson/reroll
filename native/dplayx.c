/* DPLAYX for the native build: DirectPlay 4, as far as the game uses it,
 * on our own UDP transport (TCP/IP only: no IPX, serial or modem).
 *
 * The game's network code (src/game/orig_pu.c, pv.c, xd.c and the network
 * menus) stays as it was; this answers its IDirectPlay4A and
 * IDirectPlayLobby2A calls:
 *
 *  - the one connection, "TCP/IP", whose address is the text of the game's
 *    "TCP IP Address" box: "host" or "host:port"; empty searches the local
 *    network (broadcast, and this machine);
 *  - a session is a host and its clients in a star: every message goes
 *    through the host, so only the host needs a reachable port (UDP
 *    NET_PORT, REROLL_NET_PORT to change it). Clients can be behind NAT;
 *  - every message is delivered reliably and in order (the race waits for
 *    every player's controls each step, so a lost one would stall it): a
 *    sequence number per link, acknowledgements, resends after a
 *    round-trip-based time-out, pings when idle and a link lost after
 *    LINK_TIMEOUT ms of silence;
 *  - DirectPlay's system messages the game handles: a player created (3) or
 *    destroyed (5), the session lost (0x31) and chat (0x109).
 *
 * A thread of ours services the socket (receiving, acknowledging,
 * resending, pinging); the game's calls take the same lock. Messages for
 * the local player wait in a queue for Receive, and the event the game
 * gave CreatePlayer is set when one arrives (its receive thread, when
 * "Background Message Reception" is on).
 *
 * Environment: REROLL_NET_PORT (the host's port), REROLL_NET_LOG=1 (every
 * packet); for testing, REROLL_NET_LOSS=n (drop n% of the packets sent) and
 * REROLL_NET_DELAY=ms (send every packet that much later). */
#include "nt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

void *nt_named_stub(const char *module, const char *name);

#define NET_PORT 47700
#define NET_MAGIC 0x314e4352u /* "RCN1" */
#define NET_VERSION 1
#define MAX_LINKS 8
#define MAX_PLAYERS 8
#define MAX_FOUND 16
#define MAX_DATA 4000       /* the game's largest message is 0x540 bytes */
#define WINDOW 64           /* out-of-order messages kept per link */
#define LINK_TIMEOUT 15000  /* ms of silence before a link is lost */
#define PING_EVERY 200      /* ms without sending before a ping */
#define JOIN_TIMEOUT 6000
#define FOUND_FOR 4000      /* ms a search answer stays listed */

/* DirectPlay results */
#define DPERR(n) (0x88770000u + (n))
#define DPERR_ALREADYINITIALIZED DPERR(5)
#define DPERR_ACCESSDENIED DPERR(10)
#define DPERR_BUFFERTOOSMALL DPERR(30)
#define DPERR_CANTCREATEPLAYER DPERR(40)
#define DPERR_CANTCREATESESSION DPERR(60)
#define DPERR_INVALIDPLAYER DPERR(150)
#define DPERR_NOCONNECTION DPERR(170)
#define DPERR_NOMESSAGES DPERR(190)
#define DPERR_NOSESSIONS DPERR(200)
#define DPERR_UNAVAILABLE DPERR(220)
#define DPERR_TIMEOUT DPERR(230)
#define DPERR_UNINITIALIZED DPERR(0x30)
#define DPERR_CONNECTIONLOST DPERR(500)
#define DPERR_INVALIDPARAMS 0x80070057u

/* DirectPlay flags and message types */
#define DPOPEN_JOIN 1
#define DPOPEN_CREATE 2
#define DPENUMSESSIONS_STOPASYNC 0x20
#define DPESC_TIMEDOUT 1
#define DPPLAYER_SERVERPLAYER 0x100
#define DPPLAYER_LOCAL 8
#define DPID_SERVERPLAYER 1
#define DPSYS_CREATEPLAYERORGROUP 3
#define DPSYS_DESTROYPLAYERORGROUP 5
#define DPSYS_SESSIONLOST 0x31
#define DPSYS_CHAT 0x109
#define SESSION_LOCKED 0x21 /* DPSESSION_NEWPLAYERSDISABLED | JOINDISABLED */

/* GUIDs (as their bytes) */
static const uint8_t GUID_SP_TCPIP[16] = {0xe0, 0x5e, 0xe9, 0x36, 0x77, 0x85, 0xcf, 0x11,
                                         0x96, 0x0c, 0x00, 0x80, 0xc7, 0x53, 0x4e, 0x82};
static const uint8_t DPAID_SERVICEPROVIDER[16] = {0xc0, 0x16, 0xd9, 0x07, 0xaf, 0xe0, 0xcf, 0x11,
                                                  0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e};
static const uint8_t DPAID_INET[16] = {0xa0, 0x4d, 0xa5, 0xc4, 0xaf, 0xe0, 0xcf, 0x11,
                                       0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e};

/* A connection (InitializeConnection's argument): our own form. */
#define CONN_MAGIC 0x544e5252u /* "RRNT" */
#define CONN_MAX 0x80

/* Game memory is addressed with 32 bits. */
#define LOW(p) ((uint32_t)(uintptr_t)(p))
#define AT(a) ((uint8_t *)(uintptr_t)(a))
#define U32(a, off) (*(uint32_t *)(AT(a) + (off)))

/* ---- packets ----
 * Every packet: magic, type, 3 spare bytes. The link packets (REL, ACK,
 * PING, PONG, BYE) carry the link's token next. */
enum {
    P_ENUM_REQ = 1, /* app guid, version */
    P_ENUM_REPLY,   /* instance guid, flags, max, players, name[64] */
    P_JOIN_REQ,     /* instance guid, version, nonce */
    P_JOIN_OK,      /* nonce, token, your id, flags, max, name[64], count, {id, flags, name[32]} */
    P_JOIN_NO,      /* nonce, result */
    P_REL,          /* token, seq, kind (+3), payload */
    P_ACK,          /* token, next expected seq, bits for the 32 after it */
    P_PING,         /* token, time */
    P_PONG,         /* token, time echoed */
    P_BYE,          /* token */
};
/* REL kinds */
enum {
    R_GAME = 1, /* from, to, data */
    R_CHAT,     /* from, to, text */
    R_CREATE,   /* client -> host: its player: flags, name[32] */
    R_ADD,      /* host -> client: a player: id, flags, quiet, name[32] */
    R_DEL,      /* a player gone: id */
    R_DESC,     /* host -> client: flags, max, name[64] */
};

#define HDR 8
#define NAME_LEN 32
#define SESSION_NAME_LEN 64

/* ---- state ---- */

struct pending {
    struct pending *next;
    uint32_t seq;
    uint64_t first_sent, last_sent;
    int tries;
    int len;
    uint8_t data[];
};

struct link {
    int used;
    struct sockaddr_in addr;
    uint32_t token;
    uint32_t nonce;       /* the join request it came from */
    uint32_t id;          /* host: the id given to its player */
    int has_player;       /* host: the client created its player */
    uint32_t send_seq;    /* the next to send */
    uint32_t recv_next;   /* the next expected */
    struct pending *head, *tail;
    int pending;
    uint8_t *ooo[WINDOW]; /* received ahead: {len u32, kind, payload} by seq % WINDOW */
    uint32_t ooo_seq[WINDOW];
    uint64_t last_heard, last_sent;
    uint32_t srtt;        /* ms */
    uint32_t sent, resent; /* reliable messages, for the log */
    uint64_t last_report;
};

struct player {
    int used;
    uint32_t id, flags;
    char name[NAME_LEN];
    int local;
};

enum { M_DATA, M_CREATE, M_DESTROY, M_LOST, M_CHAT };
struct msg {
    struct msg *next;
    int kind;
    uint32_t from, to;
    uint32_t id; /* the player created or destroyed */
    char name[NAME_LEN];
    uint32_t len;
    uint8_t data[];
};

struct found {
    uint8_t guid[16];
    struct sockaddr_in addr;
    uint32_t flags, max, players;
    char name[SESSION_NAME_LEN];
    uint64_t seen;
};

enum { NET_IDLE, NET_HOST, NET_CLIENT, NET_LOST };

struct dplay;

static struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int thread_started;
    int fd;
    uint16_t fd_port; /* the port bound (0: any) */
    int mode;
    struct dplay *owner;
    /* the session */
    uint8_t instance[16], app[16];
    uint32_t sflags, smax;
    char sname[SESSION_NAME_LEN];
    struct link links[MAX_LINKS];
    struct player players[MAX_PLAYERS];
    uint32_t local_id;   /* client: given at the join, before CreatePlayer */
    int local_created;
    HANDLE event;
    uint32_t next_id;
    struct msg *qhead, *qtail;
    int qcount;
    uint32_t qbytes;
    uint32_t msg_id;
    /* joining */
    uint32_t join_nonce;
    int join_state; /* 0 waiting, 1 joined, 2 refused */
    uint32_t join_result;
    /* searching */
    struct dplay *searcher;
    struct sockaddr_in targets[2];
    int ntargets;
    uint64_t last_enum;
    struct found found[MAX_FOUND];
    int nfound;
} net = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, -1};

static int net_log_packets, net_loss, net_delay;

/* REROLL_NET_DELAY: packets waiting to be sent */
struct delayed {
    struct delayed *next;
    uint64_t due;
    struct sockaddr_in to;
    int len;
    uint8_t data[];
};
static struct delayed *delayed_head, *delayed_tail;
static uint16_t net_port = NET_PORT;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint32_t random32(void)
{
    static uint64_t s;
    if (!s)
        s = (uint64_t)time(0) ^ ((uint64_t)getpid() << 32) ^ now_ms() * 0x9e3779b97f4a7c15ull;
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (uint32_t)(s >> 16);
}

static void put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void put_str(uint8_t *p, const char *s, int size)
{
    memset(p, 0, (size_t)size);
    if (s)
        strncpy((char *)p, s, (size_t)size - 1);
}

static void get_str(char *d, const uint8_t *p, int size)
{
    memcpy(d, p, (size_t)size);
    d[size - 1] = 0;
}

static const char *addr_text(const struct sockaddr_in *a)
{
    static char text[4][32];
    static int n;
    char *t = text[n++ & 3];
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a->sin_addr, ip, sizeof ip);
    snprintf(t, 32, "%s:%u", ip, ntohs(a->sin_port));
    return t;
}

static int same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

/* ---- the socket ---- */

static void raw_send(const struct sockaddr_in *to, const uint8_t *p, int len)
{
    if (net.fd < 0)
        return;
    if (net_loss && (int)(random32() % 100) < net_loss)
        return;
    if (net_log_packets)
        nt_log("net: send type %d (%d bytes) to %s", p[4], len, addr_text(to));
    if (net_delay) {
        struct delayed *d = malloc(sizeof *d + (size_t)len);
        if (!d)
            return;
        d->next = 0;
        d->due = now_ms() + (uint64_t)net_delay;
        d->to = *to;
        d->len = len;
        memcpy(d->data, p, (size_t)len);
        if (delayed_tail)
            delayed_tail->next = d;
        else
            delayed_head = d;
        delayed_tail = d;
        return;
    }
    sendto(net.fd, p, (size_t)len, 0, (const struct sockaddr *)to, sizeof *to);
}

static void delayed_flush(uint64_t now)
{
    while (delayed_head && delayed_head->due <= now) {
        struct delayed *d = delayed_head;
        delayed_head = d->next;
        if (!delayed_head)
            delayed_tail = 0;
        if (net.fd >= 0)
            sendto(net.fd, d->data, (size_t)d->len, 0, (const struct sockaddr *)&d->to, sizeof d->to);
        free(d);
    }
}

static void header(uint8_t *p, int type)
{
    put32(p, NET_MAGIC);
    p[4] = (uint8_t)type;
    p[5] = p[6] = p[7] = 0;
}

static void *net_thread(void *arg);

/* A socket bound to `port` (0: any port; an existing socket of any port
 * does). Called with the lock held. */
static int socket_open(uint16_t port)
{
    struct sockaddr_in a;
    int fd, one = 1;

    if (net.fd >= 0 && (port == 0 || net.fd_port == port))
        return 1;
    if (net.fd >= 0) {
        close(net.fd);
        net.fd = -1;
    }
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        nt_log("net: socket: %s", strerror(errno));
        return 0;
    }
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        nt_log("net: can't use UDP port %u: %s", port, strerror(errno));
        close(fd);
        return 0;
    }
    net.fd = fd;
    net.fd_port = port;
    if (!net.thread_started) {
        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &attr, net_thread, 0) == 0)
            net.thread_started = 1;
        pthread_attr_destroy(&attr);
    }
    return 1;
}

static void socket_close(void)
{
    if (net.fd >= 0)
        close(net.fd);
    net.fd = -1;
    net.fd_port = 0;
}

/* ---- the received-message queue ---- */

static void queue_push(struct msg *m)
{
    m->next = 0;
    if (net.qtail)
        net.qtail->next = m;
    else
        net.qhead = m;
    net.qtail = m;
    net.qcount++;
    net.qbytes += m->len;
    if (net.event)
        nt_event_set(net.event);
}

static struct msg *msg_new(int kind, uint32_t from, uint32_t to, const void *data, uint32_t len)
{
    struct msg *m = calloc(1, sizeof *m + len + 1);
    if (!m)
        return 0;
    m->kind = kind;
    m->from = from;
    m->to = to;
    m->len = len;
    if (len)
        memcpy(m->data, data, len);
    return m;
}

static void queue_system(int kind, uint32_t id, const char *name)
{
    struct msg *m = msg_new(kind, 0, net.local_id, 0, 0);
    if (!m)
        return;
    m->id = id;
    if (name)
        snprintf(m->name, sizeof m->name, "%s", name);
    queue_push(m);
}

static void queue_clear(void)
{
    while (net.qhead) {
        struct msg *m = net.qhead;
        net.qhead = m->next;
        free(m);
    }
    net.qtail = 0;
    net.qcount = 0;
    net.qbytes = 0;
}

/* ---- players ---- */

static struct player *player_find(uint32_t id)
{
    int i;
    for (i = 0; i < MAX_PLAYERS; i++)
        if (net.players[i].used && net.players[i].id == id)
            return &net.players[i];
    return 0;
}

static struct player *player_add(uint32_t id, uint32_t flags, const char *name, int local)
{
    struct player *p = player_find(id);
    int i;
    for (i = 0; !p && i < MAX_PLAYERS; i++)
        if (!net.players[i].used)
            p = &net.players[i];
    if (!p)
        return 0;
    memset(p, 0, sizeof *p);
    p->used = 1;
    p->id = id;
    p->flags = flags;
    p->local = local;
    snprintf(p->name, sizeof p->name, "%s", name ? name : "");
    return p;
}

static int player_count(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_PLAYERS; i++)
        n += net.players[i].used;
    return n;
}

/* ---- links: reliable, ordered messages ---- */

static struct link *link_by_addr(const struct sockaddr_in *a, uint32_t token)
{
    int i;
    for (i = 0; i < MAX_LINKS; i++)
        if (net.links[i].used && net.links[i].token == token && same_addr(&net.links[i].addr, a))
            return &net.links[i];
    return 0;
}

/* host: the link whose player is `id` */
static struct link *link_by_player(uint32_t id)
{
    int i;
    for (i = 0; i < MAX_LINKS; i++)
        if (net.links[i].used && net.links[i].has_player && net.links[i].id == id)
            return &net.links[i];
    return 0;
}

static void link_free(struct link *l)
{
    int i;
    while (l->head) {
        struct pending *p = l->head;
        l->head = p->next;
        free(p);
    }
    for (i = 0; i < WINDOW; i++)
        free(l->ooo[i]);
    memset(l, 0, sizeof *l);
}

static void link_send_raw(struct link *l, const uint8_t *p, int len)
{
    raw_send(&l->addr, p, len);
    l->last_sent = now_ms();
}

/* A reliable message of `kind` on the link. */
static void link_send(struct link *l, int kind, const void *payload, int len)
{
    struct pending *p;
    uint64_t now = now_ms();

    if (!l->used || len < 0 || len > MAX_DATA + 64)
        return;
    p = malloc(sizeof *p + (size_t)(HDR + 12 + len));
    if (!p)
        return;
    p->next = 0;
    p->seq = l->send_seq++;
    p->first_sent = p->last_sent = now;
    p->tries = 1;
    p->len = HDR + 12 + len;
    header(p->data, P_REL);
    put32(p->data + HDR, l->token);
    put32(p->data + HDR + 4, p->seq);
    p->data[HDR + 8] = (uint8_t)kind;
    p->data[HDR + 9] = p->data[HDR + 10] = p->data[HDR + 11] = 0;
    if (len)
        memcpy(p->data + HDR + 12, payload, (size_t)len);
    if (l->tail)
        l->tail->next = p;
    else
        l->head = p;
    l->tail = p;
    l->pending++;
    l->sent++;
    link_send_raw(l, p->data, p->len);
}

static void link_send_simple(struct link *l, int type)
{
    uint8_t p[HDR + 4];
    header(p, type);
    put32(p + HDR, l->token);
    link_send_raw(l, p, sizeof p);
}

/* BYE isn't acknowledged: three times, against loss */
static void link_bye(struct link *l)
{
    int k;
    for (k = 0; k < 3; k++)
        link_send_simple(l, P_BYE);
}

static void link_ack(struct link *l)
{
    uint8_t p[HDR + 12];
    uint32_t bits = 0;
    int i;
    for (i = 0; i < 32 && i < WINDOW - 1; i++) {
        uint32_t seq = l->recv_next + 1 + (uint32_t)i;
        if (l->ooo[seq % WINDOW] && l->ooo_seq[seq % WINDOW] == seq)
            bits |= 1u << i;
    }
    header(p, P_ACK);
    put32(p + HDR, l->token);
    put32(p + HDR + 4, l->recv_next);
    put32(p + HDR + 8, bits);
    link_send_raw(l, p, sizeof p);
}

static void link_acked(struct link *l, uint32_t next, uint32_t bits)
{
    struct pending **pp = &l->head, *p;
    uint64_t now = now_ms();

    l->tail = 0;
    while ((p = *pp)) {
        int32_t d = (int32_t)(p->seq - next);
        if (d < 0 || (d > 0 && d <= 32 && (bits >> (d - 1) & 1))) {
            if (p->tries == 1) {
                uint32_t rtt = (uint32_t)(now - p->first_sent);
                l->srtt = l->srtt ? (l->srtt * 7 + rtt) / 8 : rtt;
            }
            *pp = p->next;
            free(p);
            l->pending--;
            continue;
        }
        l->tail = p;
        pp = &p->next;
    }
}

static void link_lost(struct link *l, const char *why);
static void deliver(struct link *l, int kind, const uint8_t *p, int len);

/* A REL packet's message: delivered in order (with what waited behind it),
 * kept when early, dropped when old; acknowledged either way. */
static void link_received(struct link *l, uint32_t seq, int kind, const uint8_t *payload, int len)
{
    int32_t d = (int32_t)(seq - l->recv_next);

    if (d == 0) {
        l->recv_next++;
        deliver(l, kind, payload, len);
        while (l->used) {
            uint32_t s = l->recv_next;
            uint8_t *e = l->ooo[s % WINDOW];
            if (!e || l->ooo_seq[s % WINDOW] != s)
                break;
            l->ooo[s % WINDOW] = 0;
            l->recv_next++;
            deliver(l, e[4], e + 8, (int)get32(e));
            free(e);
        }
    } else if (d > 0 && d < WINDOW) {
        uint32_t k = seq % WINDOW;
        if (!l->ooo[k] || l->ooo_seq[k] != seq) {
            uint8_t *e = malloc((size_t)len + 8);
            if (e) {
                free(l->ooo[k]);
                put32(e, (uint32_t)len);
                e[4] = (uint8_t)kind;
                memcpy(e + 8, payload, (size_t)len);
                l->ooo[k] = e;
                l->ooo_seq[k] = seq;
            }
        }
    }
    if (l->used)
        link_ack(l);
}

/* resends, pings and the time-out */
static void link_service(struct link *l, uint64_t now)
{
    struct pending *p;
    uint32_t rto = l->srtt ? l->srtt + l->srtt / 2 + 15 : 150;

    if (rto < 30)
        rto = 30;
    if (rto > 1000)
        rto = 1000;
    if (now - l->last_heard > LINK_TIMEOUT) {
        link_lost(l, "timed out");
        return;
    }
    for (p = l->head; p; p = p->next)
        if (now - p->last_sent >= rto * (uint32_t)(p->tries < 4 ? p->tries : 4)) {
            p->last_sent = now;
            p->tries++;
            l->resent++;
            link_send_raw(l, p->data, p->len);
        }
    if (now - l->last_report >= 30000) {
        if (l->last_report)
            nt_log("net: %s: round trip %u ms, %u messages sent, %u resent, %d waiting", addr_text(&l->addr), l->srtt,
                   l->sent, l->resent, l->pending);
        l->last_report = now;
    }
    if (now - l->last_sent >= PING_EVERY) {
        uint8_t q[HDR + 8];
        header(q, P_PING);
        put32(q + HDR, l->token);
        put32(q + HDR + 4, (uint32_t)now);
        link_send_raw(l, q, sizeof q);
    }
}

/* ---- the session ---- */

static void host_announce(uint32_t id, uint32_t flags, const char *name, struct link *except);

/* A player gone from the host's session: the others told, the host's game
 * told (unless it removed the player itself). */
static void host_player_gone(uint32_t id, int tell_local)
{
    struct player *p = player_find(id);
    uint8_t b[4];
    int i;

    if (!p)
        return;
    nt_log("net: %s (%08x) left", p->name, id);
    p->used = 0;
    put32(b, id);
    for (i = 0; i < MAX_LINKS; i++)
        if (net.links[i].used && !(net.links[i].has_player && net.links[i].id == id))
            link_send(&net.links[i], R_DEL, b, 4);
    if (tell_local && net.local_created)
        queue_system(M_DESTROY, id, 0);
}

static void link_lost(struct link *l, const char *why)
{
    if (!l->used)
        return;
    if (net.mode == NET_HOST) {
        uint32_t id = l->id;
        int had = l->has_player;
        nt_log("net: client %s %s", addr_text(&l->addr), why);
        link_free(l);
        if (had)
            host_player_gone(id, 1);
    } else if (net.mode == NET_CLIENT) {
        int i;
        nt_log("net: host %s %s", addr_text(&l->addr), why);
        link_free(l);
        net.mode = NET_LOST;
        for (i = 0; i < MAX_PLAYERS; i++)
            if (!net.players[i].local)
                net.players[i].used = 0;
        queue_system(M_LOST, 0, 0);
    }
}

/* The session ended (Close, Release, another Open): the others told. */
static void session_end(void)
{
    int i;

    if (net.mode == NET_HOST || net.mode == NET_CLIENT)
        for (i = 0; i < MAX_LINKS; i++)
            if (net.links[i].used)
                link_bye(&net.links[i]);
    if (net.mode != NET_IDLE)
        nt_log("net: session closed");
    for (i = 0; i < MAX_LINKS; i++)
        if (net.links[i].used)
            link_free(&net.links[i]);
    memset(net.players, 0, sizeof net.players);
    queue_clear();
    net.mode = NET_IDLE;
    net.owner = 0;
    net.local_created = 0;
    net.local_id = 0;
    net.event = 0;
    net.join_state = 0;
    if (net.fd_port)
        socket_close(); /* the host's port is free again */
}

/* host: tells every client with a player (loud) or without one yet (quiet,
 * so it isn't announced before its own) about a player. */
static void host_announce(uint32_t id, uint32_t flags, const char *name, struct link *except)
{
    uint8_t b[12 + NAME_LEN];
    int i;

    put32(b, id);
    put32(b + 4, flags);
    put_str(b + 12, name, NAME_LEN);
    for (i = 0; i < MAX_LINKS; i++) {
        struct link *l = &net.links[i];
        if (!l->used || l == except)
            continue;
        put32(b + 8, !l->has_player);
        link_send(l, R_ADD, b, sizeof b);
    }
}

/* A message routed by the host: to its own player, to another client, or
 * (to 0) to everyone but the sender. */
static void host_route(struct link *from_link, int kind, uint32_t from, uint32_t to, const uint8_t *data, int len)
{
    uint8_t b[8 + MAX_DATA];
    int i;

    if (len > MAX_DATA)
        return;
    put32(b, from);
    put32(b + 4, to);
    memcpy(b + 8, data, (size_t)len);
    if ((to == 0 && from != net.local_id) || to == net.local_id) {
        if (net.local_created) {
            struct msg *m = msg_new(kind == R_CHAT ? M_CHAT : M_DATA, from, net.local_id, data, (uint32_t)len);
            if (m)
                queue_push(m);
        }
    }
    for (i = 0; i < MAX_LINKS; i++) {
        struct link *l = &net.links[i];
        if (!l->used || !l->has_player || l == from_link)
            continue;
        if (to == 0 || l->id == to)
            link_send(l, kind, b, 8 + len);
    }
}

/* A message that arrived in order on a link. */
static void deliver(struct link *l, int kind, const uint8_t *p, int len)
{
    if (net.mode == NET_HOST) {
        switch (kind) {
        case R_CREATE: {
            char name[NAME_LEN];
            if (len < 4 + NAME_LEN || l->has_player)
                return;
            get_str(name, p + 4, NAME_LEN);
            if (!player_add(l->id, get32(p) & ~(uint32_t)(DPPLAYER_SERVERPLAYER | DPPLAYER_LOCAL), name, 0)) {
                link_bye(l);
                link_free(l);
                return;
            }
            l->has_player = 1;
            nt_log("net: %s (%08x) joined from %s", name, l->id, addr_text(&l->addr));
            host_announce(l->id, 0, name, l);
            if (net.local_created)
                queue_system(M_CREATE, l->id, name);
            return;
        }
        case R_GAME:
        case R_CHAT:
            if (len < 8 || !l->has_player || get32(p) != l->id)
                return;
            host_route(l, kind, get32(p), get32(p + 4), p + 8, len - 8);
            return;
        case R_DEL:
            if (l->has_player && len >= 4 && get32(p) == l->id) {
                l->has_player = 0;
                host_player_gone(l->id, 1);
            }
            return;
        }
        return;
    }
    if (net.mode != NET_CLIENT)
        return;
    switch (kind) {
    case R_ADD: {
        char name[NAME_LEN];
        uint32_t id;
        if (len < 12 + NAME_LEN)
            return;
        id = get32(p);
        get_str(name, p + 12, NAME_LEN);
        if (id == net.local_id || player_find(id))
            return;
        player_add(id, get32(p + 4), name, 0);
        if (!get32(p + 8) && net.local_created)
            queue_system(M_CREATE, id, name);
        return;
    }
    case R_DEL: {
        struct player *pl;
        if (len < 4)
            return;
        pl = player_find(get32(p));
        if (!pl || pl->local)
            return;
        pl->used = 0;
        if (net.local_created)
            queue_system(M_DESTROY, get32(p), 0);
        return;
    }
    case R_GAME:
    case R_CHAT: {
        struct msg *m;
        if (len < 8 || !net.local_created)
            return;
        m = msg_new(kind == R_CHAT ? M_CHAT : M_DATA, get32(p), net.local_id, p + 8, (uint32_t)(len - 8));
        if (m)
            queue_push(m);
        return;
    }
    case R_DESC:
        if (len >= 8 + SESSION_NAME_LEN) {
            net.sflags = get32(p);
            net.smax = get32(p + 4);
            get_str(net.sname, p + 8, SESSION_NAME_LEN);
        }
        return;
    }
}

/* ---- packets in ---- */

static void enum_reply(const struct sockaddr_in *to)
{
    uint8_t p[HDR + 28 + SESSION_NAME_LEN];
    header(p, P_ENUM_REPLY);
    memcpy(p + HDR, net.instance, 16);
    put32(p + HDR + 16, net.sflags);
    put32(p + HDR + 20, net.smax);
    put32(p + HDR + 24, (uint32_t)player_count());
    put_str(p + HDR + 28, net.sname, SESSION_NAME_LEN);
    raw_send(to, p, sizeof p);
}

static void join_reply_no(const struct sockaddr_in *to, uint32_t nonce, uint32_t result)
{
    uint8_t p[HDR + 8];
    header(p, P_JOIN_NO);
    put32(p + HDR, nonce);
    put32(p + HDR + 4, result);
    raw_send(to, p, sizeof p);
}

static void join_reply_ok(struct link *l)
{
    uint8_t p[HDR + 24 + SESSION_NAME_LEN + MAX_PLAYERS * (8 + NAME_LEN)];
    uint8_t *q;
    uint32_t n = 0;
    int i;

    header(p, P_JOIN_OK);
    put32(p + HDR, l->nonce);
    put32(p + HDR + 4, l->token);
    put32(p + HDR + 8, l->id);
    put32(p + HDR + 12, net.sflags);
    put32(p + HDR + 16, net.smax);
    put_str(p + HDR + 20, net.sname, SESSION_NAME_LEN);
    q = p + HDR + 24 + SESSION_NAME_LEN;
    for (i = 0; i < MAX_PLAYERS; i++) {
        struct player *pl = &net.players[i];
        if (!pl->used)
            continue;
        put32(q + 4 + n * (8 + NAME_LEN), pl->id);
        put32(q + 8 + n * (8 + NAME_LEN), pl->local ? pl->flags & DPPLAYER_SERVERPLAYER : pl->flags);
        put_str(q + 12 + n * (8 + NAME_LEN), pl->name, NAME_LEN);
        n++;
    }
    put32(q, n);
    raw_send(&l->addr, p, (int)(q + 4 + n * (8 + NAME_LEN) - p));
    l->last_sent = now_ms();
}

static void host_join_request(const struct sockaddr_in *from, const uint8_t *p, int len)
{
    uint32_t nonce, links = 0;
    struct link *l = 0;
    int i;

    if (len < HDR + 24)
        return;
    nonce = get32(p + HDR + 20);
    for (i = 0; i < MAX_LINKS; i++)
        if (net.links[i].used && same_addr(&net.links[i].addr, from) && net.links[i].nonce == nonce) {
            join_reply_ok(&net.links[i]); /* its answer was lost */
            return;
        }
    if (memcmp(p + HDR, net.instance, 16) != 0) {
        join_reply_no(from, nonce, DPERR_NOSESSIONS);
        return;
    }
    if (get32(p + HDR + 16) != NET_VERSION) {
        nt_log("net: %s has another version of the game's network play", addr_text(from));
        join_reply_no(from, nonce, DPERR_INVALIDPARAMS);
        return;
    }
    for (i = 0; i < MAX_LINKS; i++) {
        if (net.links[i].used) {
            links += !net.links[i].has_player; /* joining, player to come */
            if (same_addr(&net.links[i].addr, from))
                link_lost(&net.links[i], "joined again");
        }
    }
    if ((net.sflags & SESSION_LOCKED) || (uint32_t)player_count() + links >= net.smax) {
        nt_log("net: %s refused: %s", addr_text(from), net.sflags & SESSION_LOCKED ? "the game has started" : "full");
        join_reply_no(from, nonce, DPERR_ACCESSDENIED);
        return;
    }
    for (i = 0; i < MAX_LINKS && !l; i++)
        if (!net.links[i].used)
            l = &net.links[i];
    if (!l) {
        join_reply_no(from, nonce, DPERR_ACCESSDENIED);
        return;
    }
    memset(l, 0, sizeof *l);
    l->used = 1;
    l->addr = *from;
    l->nonce = nonce;
    l->token = random32() | 1;
    l->id = net.next_id++;
    l->last_heard = now_ms();
    join_reply_ok(l);
}

static void client_join_ok(const struct sockaddr_in *from, const uint8_t *p, int len)
{
    const uint8_t *q;
    struct link *l = &net.links[0];
    uint32_t n, i;

    if (len < HDR + 28 + SESSION_NAME_LEN || net.join_state != 0 || get32(p + HDR) != net.join_nonce)
        return;
    q = p + HDR + 24 + SESSION_NAME_LEN;
    n = get32(q);
    if (n > MAX_PLAYERS || len < (int)(q + 4 + n * (8 + NAME_LEN) - p))
        return;
    memset(l, 0, sizeof *l);
    l->used = 1;
    l->addr = *from;
    l->token = get32(p + HDR + 4);
    l->last_heard = now_ms();
    net.local_id = get32(p + HDR + 8);
    net.sflags = get32(p + HDR + 12);
    net.smax = get32(p + HDR + 16);
    get_str(net.sname, p + HDR + 20, SESSION_NAME_LEN);
    for (i = 0; i < n; i++) {
        char name[NAME_LEN];
        get_str(name, q + 12 + i * (8 + NAME_LEN), NAME_LEN);
        player_add(get32(q + 4 + i * (8 + NAME_LEN)), get32(q + 8 + i * (8 + NAME_LEN)), name, 0);
    }
    net.mode = NET_CLIENT;
    net.join_state = 1;
    pthread_cond_broadcast(&net.cond);
}

static void packet_in(const struct sockaddr_in *from, const uint8_t *p, int len)
{
    struct link *l;
    int type;

    if (len < HDR || get32(p) != NET_MAGIC)
        return;
    type = p[4];
    if (net_log_packets)
        nt_log("net: received type %d (%d bytes) from %s", type, len, addr_text(from));
    switch (type) {
    case P_ENUM_REQ:
        if (net.mode == NET_HOST && len >= HDR + 20 && !memcmp(p + HDR, net.app, 16))
            enum_reply(from);
        return;
    case P_ENUM_REPLY:
        if (net.searcher && len >= HDR + 28 + SESSION_NAME_LEN) {
            struct found *f = 0;
            int i;
            for (i = 0; i < net.nfound; i++)
                if (!memcmp(net.found[i].guid, p + HDR, 16))
                    f = &net.found[i];
            if (!f && net.nfound < MAX_FOUND)
                f = &net.found[net.nfound++];
            if (!f)
                return;
            memcpy(f->guid, p + HDR, 16);
            f->addr = *from;
            f->flags = get32(p + HDR + 16);
            f->max = get32(p + HDR + 20);
            f->players = get32(p + HDR + 24);
            get_str(f->name, p + HDR + 28, SESSION_NAME_LEN);
            f->seen = now_ms();
        }
        return;
    case P_JOIN_REQ:
        if (net.mode == NET_HOST)
            host_join_request(from, p, len);
        return;
    case P_JOIN_OK:
        client_join_ok(from, p, len);
        return;
    case P_JOIN_NO:
        if (len >= HDR + 8 && net.join_state == 0 && get32(p + HDR) == net.join_nonce) {
            net.join_state = 2;
            net.join_result = get32(p + HDR + 4);
            pthread_cond_broadcast(&net.cond);
        }
        return;
    }
    if (len < HDR + 4 || !(l = link_by_addr(from, get32(p + HDR))))
        return;
    l->last_heard = now_ms();
    switch (type) {
    case P_REL:
        if (len >= HDR + 12)
            link_received(l, get32(p + HDR + 4), p[HDR + 8], p + HDR + 12, len - HDR - 12);
        return;
    case P_ACK:
        if (len >= HDR + 12)
            link_acked(l, get32(p + HDR + 4), get32(p + HDR + 8));
        return;
    case P_PING:
        if (len >= HDR + 8) {
            uint8_t q[HDR + 8];
            memcpy(q, p, sizeof q);
            q[4] = P_PONG;
            link_send_raw(l, q, sizeof q);
        }
        return;
    case P_PONG:
        if (len >= HDR + 8) {
            uint32_t rtt = (uint32_t)now_ms() - get32(p + HDR + 4);
            if (rtt < 5000)
                l->srtt = l->srtt ? (l->srtt * 7 + rtt) / 8 : rtt;
        }
        return;
    case P_BYE:
        link_lost(l, "left");
        return;
    }
}

static void *net_thread(void *arg)
{
    static uint8_t buf[MAX_DATA + 256];
    (void)arg;
    for (;;) {
        struct pollfd pfd;
        uint64_t now;
        int fd, i;

        pthread_mutex_lock(&net.lock);
        fd = net.fd;
        pthread_mutex_unlock(&net.lock);
        if (fd < 0) {
            usleep(20000);
            continue;
        }
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll(&pfd, 1, 5);
        pthread_mutex_lock(&net.lock);
        if (net.fd == fd) {
            for (;;) {
                struct sockaddr_in from;
                socklen_t flen = sizeof from;
                ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
                if (n < 0)
                    break;
                if (from.sin_family == AF_INET)
                    packet_in(&from, buf, (int)n);
            }
            now = now_ms();
            delayed_flush(now);
            for (i = 0; i < MAX_LINKS; i++)
                if (net.links[i].used && (net.mode == NET_HOST || net.mode == NET_CLIENT))
                    link_service(&net.links[i], now);
        }
        pthread_mutex_unlock(&net.lock);
    }
    return 0;
}

/* ---- searching ---- */

/* the search targets for a connection address: "host[:port]", or the local
 * network (broadcast and this machine) when empty */
static int targets_set(const char *address)
{
    char host[CONN_MAX];
    const char *colon;
    uint16_t port = net_port;
    struct addrinfo hints, *res = 0;

    while (*address == ' ')
        address++;
    snprintf(host, sizeof host, "%s", address);
    for (char *e = host + strlen(host); e > host && e[-1] == ' ';)
        *--e = 0;
    colon = strrchr(host, ':');
    if (colon) {
        long v = strtol(colon + 1, 0, 10);
        if (v > 0 && v < 65536)
            port = (uint16_t)v;
        host[colon - host] = 0;
    }
    memset(net.targets, 0, sizeof net.targets);
    net.ntargets = 0;
    if (!host[0]) {
        net.targets[0].sin_family = AF_INET;
        net.targets[0].sin_port = htons(port);
        net.targets[0].sin_addr.s_addr = htonl(INADDR_BROADCAST);
        net.targets[1] = net.targets[0];
        net.targets[1].sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        net.ntargets = 2;
        nt_log("net: searching the local network (port %u)", port);
        return 1;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, 0, &hints, &res) != 0 || !res) {
        nt_log("net: can't find the address of \"%s\"", host);
        return 0;
    }
    memcpy(&net.targets[0], res->ai_addr, sizeof net.targets[0]);
    net.targets[0].sin_port = htons(port);
    net.ntargets = 1;
    freeaddrinfo(res);
    nt_log("net: searching %s", addr_text(&net.targets[0]));
    return 1;
}

static void enum_request(void)
{
    uint8_t p[HDR + 20];
    int i;
    header(p, P_ENUM_REQ);
    memcpy(p + HDR, net.app, 16);
    put32(p + HDR + 16, NET_VERSION);
    for (i = 0; i < net.ntargets; i++)
        raw_send(&net.targets[i], p, sizeof p);
    net.last_enum = now_ms();
}

/* ---- the DirectPlay object ---- */

struct dplay {
    void **vtable;
    LONG refs;
    int initialized;
    char address[CONN_MAX];
};

static LONG WINAPI dp_query(struct dplay *d, const void *iid, uint32_t *out)
{
    (void)iid;
    *out = LOW(d);
    d->refs++;
    return 0;
}

static LONG WINAPI dp_add_ref(struct dplay *d)
{
    return ++d->refs;
}

static void dp_forget(struct dplay *d)
{
    if (net.owner == d)
        session_end();
    if (net.searcher == d)
        net.searcher = 0;
}

static LONG WINAPI dp_release(struct dplay *d)
{
    if (--d->refs > 0)
        return d->refs;
    d->refs = 0;
    pthread_mutex_lock(&net.lock);
    dp_forget(d);
    pthread_mutex_unlock(&net.lock);
    return 0;
}

static LONG WINAPI dp_close(struct dplay *d)
{
    pthread_mutex_lock(&net.lock);
    if (net.owner == d)
        session_end();
    pthread_mutex_unlock(&net.lock);
    return 0;
}

/* CreatePlayer(&id, name, event, data, size, flags): the one local player */
static LONG WINAPI dp_create_player(struct dplay *d, DWORD *id, void *name, void *event, void *data, DWORD size,
                                    DWORD flags)
{
    const char *short_name = "";
    uint32_t pid;
    LONG hr = 0;

    (void)data;
    (void)size;
    if (name && U32(LOW(name), 8))
        short_name = (const char *)AT(U32(LOW(name), 8));
    pthread_mutex_lock(&net.lock);
    if (net.owner != d || (net.mode != NET_HOST && net.mode != NET_CLIENT)) {
        hr = (LONG)DPERR_NOCONNECTION;
    } else if (net.local_created) {
        hr = (LONG)DPERR_CANTCREATEPLAYER;
    } else {
        if (net.mode == NET_HOST)
            pid = flags & DPPLAYER_SERVERPLAYER ? DPID_SERVERPLAYER : net.next_id++;
        else
            pid = net.local_id;
        net.local_id = pid;
        player_add(pid, (flags & DPPLAYER_SERVERPLAYER) | DPPLAYER_LOCAL, short_name, 1);
        net.local_created = 1;
        net.event = event;
        if (net.mode == NET_HOST) {
            host_announce(pid, flags & DPPLAYER_SERVERPLAYER, short_name, 0);
        } else {
            uint8_t b[4 + NAME_LEN];
            put32(b, flags & ~(uint32_t)DPPLAYER_SERVERPLAYER);
            put_str(b + 4, short_name, NAME_LEN);
            link_send(&net.links[0], R_CREATE, b, sizeof b);
        }
        if (id)
            *id = pid;
        nt_log("net: local player %s (%08x)", short_name, pid);
    }
    pthread_mutex_unlock(&net.lock);
    return hr;
}

/* DestroyPlayer(id): the local player leaves; the host can also remove a
 * client's (the game dropping it) */
static LONG WINAPI dp_destroy_player(struct dplay *d, DWORD id)
{
    LONG hr = 0;
    struct player *p;

    pthread_mutex_lock(&net.lock);
    p = player_find(id);
    if (net.owner != d || !p) {
        hr = (LONG)DPERR_INVALIDPLAYER;
    } else if (p->local) {
        uint8_t b[4];
        put32(b, id);
        if (net.mode == NET_HOST) {
            host_player_gone(id, 0);
        } else {
            p->used = 0;
            if (net.links[0].used)
                link_send(&net.links[0], R_DEL, b, 4);
        }
        net.local_created = 0;
    } else if (net.mode == NET_HOST) {
        struct link *l = link_by_player(id);
        if (l) {
            nt_log("net: removing %s", p->name);
            link_bye(l);
            link_free(l);
        }
        host_player_gone(id, 0);
    } else {
        hr = (LONG)DPERR_ACCESSDENIED;
    }
    pthread_mutex_unlock(&net.lock);
    return hr;
}

typedef int32_t(WINAPI *enum_sessions_cb)(const void *desc, uint32_t *timeout, uint32_t flags, void *ctx);

/* EnumSessions(desc, timeout, callback, context, flags), asynchronous as
 * the game asks: each call asks again (at most every 500 ms) and reports
 * the sessions that answered lately, then the time-out. */
static LONG WINAPI dp_enum_sessions(struct dplay *d, void *desc, DWORD timeout, void *callback, void *ctx, DWORD flags)
{
    static uint8_t *out;
    static uint32_t *out_timeout;
    struct found list[MAX_FOUND];
    int n = 0, i;
    uint64_t now;

    (void)timeout;
    if (!out) {
        int arena = nt_lowmem_arena(0);
        out = nt_lowmem_calloc(1, 0x50 + SESSION_NAME_LEN + 4);
        out_timeout = nt_lowmem_calloc(1, 4);
        nt_lowmem_arena(arena);
    }
    pthread_mutex_lock(&net.lock);
    if (flags & DPENUMSESSIONS_STOPASYNC) {
        if (net.searcher == d)
            net.searcher = 0;
        pthread_mutex_unlock(&net.lock);
        return 0;
    }
    if (!d->initialized) {
        pthread_mutex_unlock(&net.lock);
        return (LONG)DPERR_UNINITIALIZED;
    }
    if (net.searcher != d) {
        net.searcher = d;
        net.nfound = 0;
        if (desc)
            memcpy(net.app, (uint8_t *)desc + 0x18, 16);
        if (!socket_open(0) || !targets_set(d->address))
            net.ntargets = 0;
        net.last_enum = 0;
    }
    now = now_ms();
    if (net.ntargets && now - net.last_enum >= 500)
        enum_request();
    for (i = 0; i < net.nfound; i++)
        if (now - net.found[i].seen <= FOUND_FOR)
            list[n++] = net.found[i];
    pthread_mutex_unlock(&net.lock);

    /* the game's callback (0x48bb30) reads the session description */
    for (i = 0; i < n; i++) {
        memset(out, 0, 0x50);
        U32(LOW(out), 0) = 0x50;
        U32(LOW(out), 4) = list[i].flags;
        memcpy(out + 8, list[i].guid, 16);
        memcpy(out + 0x18, net.app, 16);
        U32(LOW(out), 0x28) = list[i].max;
        U32(LOW(out), 0x2c) = list[i].players;
        put_str(out + 0x50, list[i].name, SESSION_NAME_LEN);
        U32(LOW(out), 0x30) = LOW(out + 0x50);
        *out_timeout = 0;
        if (!((enum_sessions_cb)callback)(out, out_timeout, 0, ctx))
            return 0;
    }
    memset(out, 0, 0x50 + SESSION_NAME_LEN);
    U32(LOW(out), 0) = 0x50;
    U32(LOW(out), 0x30) = LOW(out + 0x50);
    *out_timeout = 0;
    ((enum_sessions_cb)callback)(out, out_timeout, DPESC_TIMEDOUT, ctx);
    return 0;
}

/* GetCaps(caps, flags): DPCAPS (the game reads the time-out) */
static LONG WINAPI dp_get_caps(struct dplay *d, DWORD caps, DWORD flags)
{
    (void)d;
    (void)flags;
    if (!caps)
        return (LONG)DPERR_INVALIDPARAMS;
    memset(AT(caps), 0, 0x28);
    U32(caps, 0) = 0x28;
    U32(caps, 0x8) = MAX_DATA;  /* largest message */
    U32(caps, 0x10) = 6;        /* players */
    U32(caps, 0x18) = 50;       /* latency */
    U32(caps, 0x1c) = 1;        /* local players */
    U32(caps, 0x24) = 5000;     /* time-out */
    return 0;
}

/* GetMessageCount(player, &count) */
static LONG WINAPI dp_message_count(struct dplay *d, DWORD player, DWORD *count)
{
    (void)d;
    (void)player;
    pthread_mutex_lock(&net.lock);
    *count = (DWORD)net.qcount;
    pthread_mutex_unlock(&net.lock);
    return 0;
}

static LONG WINAPI dp_player_address(struct dplay *d, DWORD id, DWORD data, DWORD size)
{
    (void)d, (void)id, (void)data, (void)size;
    return (LONG)DPERR_UNAVAILABLE;
}

/* GetSessionDesc(data, &size): DPSESSIONDESC2 with the name after it */
static LONG WINAPI dp_session_desc(struct dplay *d, DWORD data, DWORD size)
{
    uint32_t need;
    LONG hr = 0;

    pthread_mutex_lock(&net.lock);
    need = 0x50 + (uint32_t)strlen(net.sname) + 1;
    if (net.owner != d || net.mode == NET_IDLE) {
        hr = (LONG)DPERR_NOCONNECTION;
    } else if (!size) {
        hr = (LONG)DPERR_INVALIDPARAMS;
    } else if (!data || U32(size, 0) < need) {
        U32(size, 0) = need;
        hr = (LONG)DPERR_BUFFERTOOSMALL;
    } else {
        memset(AT(data), 0, 0x50);
        U32(data, 0) = 0x50;
        U32(data, 4) = net.sflags;
        memcpy(AT(data) + 8, net.instance, 16);
        memcpy(AT(data) + 0x18, net.app, 16);
        U32(data, 0x28) = net.smax;
        U32(data, 0x2c) = (uint32_t)player_count();
        strcpy((char *)AT(data) + 0x50, net.sname);
        U32(data, 0x30) = data + 0x50;
        U32(size, 0) = need;
    }
    pthread_mutex_unlock(&net.lock);
    return hr;
}

/* Open(desc, flags): create (host) or join a session found by the search */
static LONG WINAPI dp_open(struct dplay *d, DWORD desc, DWORD flags)
{
    LONG hr = 0;

    if (!desc)
        return (LONG)DPERR_INVALIDPARAMS;
    pthread_mutex_lock(&net.lock);
    if (!d->initialized) {
        hr = (LONG)DPERR_UNINITIALIZED;
    } else if (flags & DPOPEN_CREATE) {
        session_end();
        net.searcher = 0;
        if (!socket_open(net_port)) {
            hr = (LONG)DPERR_CANTCREATESESSION;
        } else {
            uint32_t name = U32(desc, 0x30);
            uint32_t i;
            net.mode = NET_HOST;
            net.owner = d;
            for (i = 0; i < 16; i += 4)
                put32(net.instance + i, random32());
            memcpy(net.app, AT(desc) + 0x18, 16);
            net.sflags = U32(desc, 4);
            net.smax = U32(desc, 0x28) ? U32(desc, 0x28) : 6;
            if (net.smax > 6)
                net.smax = 6;
            put_str((uint8_t *)net.sname, name ? (const char *)AT(name) : "", SESSION_NAME_LEN);
            net.next_id = (random32() & 0x7fff0000u) | 0x10;
            nt_log("net: hosting \"%s\" on UDP port %u", net.sname, net_port);
        }
    } else if (flags & DPOPEN_JOIN) {
        struct sockaddr_in to;
        int i, found = 0;
        for (i = 0; i < net.nfound; i++)
            if (!memcmp(net.found[i].guid, AT(desc) + 8, 16)) {
                to = net.found[i].addr;
                found = 1;
            }
        if (!found) {
            hr = (LONG)DPERR_NOSESSIONS;
        } else {
            uint8_t p[HDR + 24];
            uint64_t start = now_ms(), last = 0;
            session_end();
            net.searcher = 0;
            if (!socket_open(0)) {
                hr = (LONG)DPERR_NOCONNECTION;
            } else {
                net.join_nonce = random32();
                net.join_state = 0;
                header(p, P_JOIN_REQ);
                memcpy(p + HDR, AT(desc) + 8, 16);
                put32(p + HDR + 16, NET_VERSION);
                put32(p + HDR + 20, net.join_nonce);
                memcpy(net.instance, AT(desc) + 8, 16);
                nt_log("net: joining %s", addr_text(&to));
                while (net.join_state == 0) {
                    uint64_t now = now_ms();
                    struct timespec ts;
                    if (now - start >= JOIN_TIMEOUT)
                        break;
                    if (now - last >= 300) {
                        raw_send(&to, p, sizeof p);
                        last = now;
                    }
                    clock_gettime(CLOCK_REALTIME, &ts);
                    ts.tv_nsec += 50 * 1000000L;
                    if (ts.tv_nsec >= 1000000000L) {
                        ts.tv_sec++;
                        ts.tv_nsec -= 1000000000L;
                    }
                    pthread_cond_timedwait(&net.cond, &net.lock, &ts);
                }
                if (net.join_state == 1) {
                    net.owner = d;
                    nt_log("net: joined \"%s\"", net.sname);
                } else {
                    hr = (LONG)(net.join_state == 2 ? net.join_result : DPERR_TIMEOUT);
                    nt_log("net: joining failed (%s)", net.join_state == 2 ? "refused" : "no answer");
                    net.join_state = 0;
                    net.join_nonce = 0;
                }
            }
        }
    } else {
        hr = (LONG)DPERR_INVALIDPARAMS;
    }
    pthread_mutex_unlock(&net.lock);
    return hr;
}

/* Receive(&from, &to, flags, data, &size): the next message; system
 * messages (from 0) are laid out as DirectPlay's, their pointers into
 * `data` (game memory). */
static LONG WINAPI dp_receive(struct dplay *d, DWORD *from, DWORD *to, DWORD flags, void *data, DWORD *size)
{
    struct msg *m;
    uint32_t need, base = LOW(data);
    uint8_t *b = data;
    LONG hr = 0;

    (void)d;
    (void)flags;
    pthread_mutex_lock(&net.lock);
    m = net.qhead;
    if (!m) {
        pthread_mutex_unlock(&net.lock);
        return (LONG)DPERR_NOMESSAGES;
    }
    switch (m->kind) {
    case M_CREATE:
        need = 0x30 + (uint32_t)strlen(m->name) + 1;
        break;
    case M_DESTROY:
        need = 0x34;
        break;
    case M_LOST:
        need = 4;
        break;
    case M_CHAT:
        need = 0x18 + 0xc + m->len + 1;
        break;
    default:
        need = m->len;
        break;
    }
    if (!size || *size < need || !data) {
        if (size)
            *size = need;
        pthread_mutex_unlock(&net.lock);
        return (LONG)DPERR_BUFFERTOOSMALL;
    }
    memset(b, 0, need);
    switch (m->kind) {
    case M_CREATE: /* DPMSG_CREATEPLAYERORGROUP */
        U32(base, 0) = DPSYS_CREATEPLAYERORGROUP;
        U32(base, 4) = 1; /* a player */
        U32(base, 8) = m->id;
        U32(base, 0xc) = (uint32_t)player_count();
        U32(base, 0x18) = 0x10; /* DPNAME */
        strcpy((char *)b + 0x30, m->name);
        U32(base, 0x20) = base + 0x30;
        U32(base, 0x24) = base + 0x30;
        break;
    case M_DESTROY: /* DPMSG_DESTROYPLAYERORGROUP */
        U32(base, 0) = DPSYS_DESTROYPLAYERORGROUP;
        U32(base, 4) = 1;
        U32(base, 8) = m->id;
        U32(base, 0x1c) = 0x10;
        break;
    case M_LOST:
        U32(base, 0) = DPSYS_SESSIONLOST;
        break;
    case M_CHAT: /* DPMSG_CHAT, then its DPCHAT and the text */
        U32(base, 0) = DPSYS_CHAT;
        U32(base, 8) = m->from;
        U32(base, 0xc) = m->to;
        U32(base, 0x14) = base + 0x18;
        U32(base, 0x18) = 0xc;
        memcpy(b + 0x24, m->data, m->len);
        U32(base, 0x20) = base + 0x24;
        break;
    default:
        memcpy(b, m->data, m->len);
        break;
    }
    if (from)
        *from = m->kind == M_DATA ? m->from : 0;
    if (to)
        *to = m->to;
    *size = need;
    net.qhead = m->next;
    if (!net.qhead)
        net.qtail = 0;
    net.qcount--;
    net.qbytes -= m->len;
    free(m);
    pthread_mutex_unlock(&net.lock);
    return hr;
}

/* SetSessionDesc(desc, flags): the host's flags (the game locks joining
 * when the race is set up) and name */
static LONG WINAPI dp_set_session_desc(struct dplay *d, DWORD desc, DWORD flags)
{
    LONG hr = 0;
    (void)flags;
    if (!desc)
        return (LONG)DPERR_INVALIDPARAMS;
    pthread_mutex_lock(&net.lock);
    if (net.owner != d || net.mode == NET_IDLE) {
        hr = (LONG)DPERR_NOCONNECTION;
    } else if (net.mode != NET_HOST) {
        hr = (LONG)DPERR_ACCESSDENIED;
    } else {
        uint8_t b[8 + SESSION_NAME_LEN];
        int i;
        net.sflags = U32(desc, 4);
        if (U32(desc, 0x30))
            put_str((uint8_t *)net.sname, (const char *)AT(U32(desc, 0x30)), SESSION_NAME_LEN);
        put32(b, net.sflags);
        put32(b + 4, net.smax);
        put_str(b + 8, net.sname, SESSION_NAME_LEN);
        for (i = 0; i < MAX_LINKS; i++)
            if (net.links[i].used)
                link_send(&net.links[i], R_DESC, b, sizeof b);
    }
    pthread_mutex_unlock(&net.lock);
    return hr;
}

typedef int32_t(WINAPI *enum_connections_cb)(uint32_t guid, uint32_t conn, uint32_t size, uint32_t name, uint32_t flags,
                                             uint32_t ctx);

/* EnumConnections(app, callback, context, flags): TCP/IP only */
static LONG WINAPI dp_enum_connections(struct dplay *d, const void *app, void *callback, void *ctx, DWORD flags)
{
    static uint8_t *block;
    (void)d;
    (void)app;
    (void)flags;
    if (!block) {
        int arena = nt_lowmem_arena(0);
        block = nt_lowmem_calloc(1, 0x80);
        nt_lowmem_arena(arena);
        memcpy(block, GUID_SP_TCPIP, 16);           /* +0 the service provider */
        put32(block + 0x10, CONN_MAGIC);            /* +0x10 the connection: no address */
        put32(block + 0x14, 0);
        U32(LOW(block), 0x20) = 0x10;               /* +0x20 DPNAME */
        U32(LOW(block), 0x28) = LOW(block + 0x40);
        strcpy((char *)block + 0x40, "TCP/IP");
    }
    ((enum_connections_cb)callback)(LOW(block), LOW(block + 0x10), 9, LOW(block + 0x20), 0, LOW(ctx));
    return 0;
}

/* InitializeConnection(connection, flags): ours, with the address */
static LONG WINAPI dp_initialize_connection(struct dplay *d, DWORD conn, DWORD flags)
{
    uint32_t len;
    (void)flags;
    if (!conn)
        return (LONG)DPERR_INVALIDPARAMS;
    if (U32(conn, 0) != CONN_MAGIC)
        return (LONG)DPERR_UNAVAILABLE;
    if (d->initialized)
        return (LONG)DPERR_ALREADYINITIALIZED;
    len = U32(conn, 4);
    if (len >= CONN_MAX)
        len = CONN_MAX - 1;
    memcpy(d->address, AT(conn) + 8, len);
    d->address[len] = 0;
    d->initialized = 1;
    return 0;
}

/* host: a message from its own player to `to` (0: everyone) */
static void host_send_own(int kind, uint32_t from, uint32_t to, const uint8_t *data, int len)
{
    host_route(0, kind, from, to, data, len);
}

/* SendChatMessage(from, to, flags, DPCHAT): to everyone else */
static LONG WINAPI dp_send_chat(struct dplay *d, DWORD from, DWORD to, DWORD flags, DWORD chat)
{
    const char *text;
    LONG hr = 0;
    int len;

    (void)flags;
    if (!chat || !U32(chat, 8))
        return (LONG)DPERR_INVALIDPARAMS;
    text = (const char *)AT(U32(chat, 8));
    len = (int)strnlen(text, 255);
    pthread_mutex_lock(&net.lock);
    if (net.owner != d || !net.local_created || from != net.local_id) {
        hr = (LONG)DPERR_INVALIDPLAYER;
    } else if (net.mode == NET_HOST) {
        host_send_own(R_CHAT, from, to, (const uint8_t *)text, len);
    } else if (net.mode == NET_CLIENT) {
        uint8_t b[8 + 256];
        put32(b, from);
        put32(b + 4, to);
        memcpy(b + 8, text, (size_t)len);
        link_send(&net.links[0], R_CHAT, b, 8 + len);
    } else {
        hr = (LONG)DPERR_CONNECTIONLOST;
    }
    pthread_mutex_unlock(&net.lock);
    return hr;
}

/* GetPlayerFlags(id, &flags): local / server player; an error for one not
 * (or no longer) in the session */
static LONG WINAPI dp_player_flags(struct dplay *d, DWORD id, DWORD out)
{
    struct player *p;
    LONG hr = 0;

    pthread_mutex_lock(&net.lock);
    p = player_find(id);
    if (net.owner != d || !p || net.mode == NET_IDLE) {
        hr = (LONG)DPERR_INVALIDPLAYER;
    } else if (out) {
        U32(out, 0) = p->flags | (p->local ? DPPLAYER_LOCAL : 0) | (p->id == DPID_SERVERPLAYER ? DPPLAYER_SERVERPLAYER : 0);
    }
    pthread_mutex_unlock(&net.lock);
    return hr;
}

/* SendEx(from, to, flags, data, size, priority, timeout, context, &id):
 * every message reliable and in order, whatever the flags */
static LONG WINAPI dp_send_ex(struct dplay *d, DWORD from, DWORD to, DWORD flags, void *data, DWORD size,
                              DWORD priority, DWORD timeout, void *context, DWORD *id)
{
    LONG hr = 0;

    (void)flags, (void)priority, (void)timeout, (void)context;
    if (size > MAX_DATA || (size && !data))
        return (LONG)DPERR_INVALIDPARAMS;
    pthread_mutex_lock(&net.lock);
    if (net.owner != d || net.mode == NET_IDLE) {
        hr = (LONG)DPERR_NOCONNECTION;
    } else if (net.mode == NET_LOST) {
        hr = (LONG)DPERR_CONNECTIONLOST;
    } else if (!net.local_created || from != net.local_id || to == from || (to && !player_find(to))) {
        hr = (LONG)DPERR_INVALIDPLAYER;
    } else if (net.mode == NET_HOST) {
        host_send_own(R_GAME, from, to, data, (int)size);
    } else {
        uint8_t b[8 + MAX_DATA];
        put32(b, from);
        put32(b + 4, to);
        memcpy(b + 8, data, size);
        link_send(&net.links[0], R_GAME, b, 8 + (int)size);
    }
    if (id)
        *id = ++net.msg_id;
    pthread_mutex_unlock(&net.lock);
    return hr;
}

/* GetMessageQueue(from, to, flags, &messages, &bytes): nothing waits to be
 * sent (it goes at once); the receive queue (flag 2) */
static LONG WINAPI dp_message_queue(struct dplay *d, DWORD from, DWORD to, DWORD flags, DWORD *messages, DWORD *bytes)
{
    (void)d, (void)from, (void)to;
    pthread_mutex_lock(&net.lock);
    if (messages)
        *messages = flags & 2 ? (DWORD)net.qcount : 0;
    if (bytes)
        *bytes = flags & 2 ? net.qbytes : 0;
    pthread_mutex_unlock(&net.lock);
    return 0;
}

/* CancelMessage(id, flags): sent already */
static LONG WINAPI dp_cancel_message(struct dplay *d, DWORD id, DWORD flags)
{
    (void)d, (void)id, (void)flags;
    return 0;
}

static void net_config(void)
{
    static int done;
    const char *e;
    if (done)
        return;
    done = 1;
    if ((e = getenv("REROLL_NET_PORT")) && atoi(e) > 0 && atoi(e) < 65536)
        net_port = (uint16_t)atoi(e);
    net_log_packets = (e = getenv("REROLL_NET_LOG")) && *e && *e != '0';
    if ((e = getenv("REROLL_NET_LOSS")))
        net_loss = atoi(e);
    if ((e = getenv("REROLL_NET_DELAY")))
        net_delay = atoi(e) > 0 ? atoi(e) : 0;
}

/* ---- IDirectPlay4A from CoCreateInstance (native/ole32.c) ---- */

void *nt_dplay_create(void)
{
    static void **vtable;
    struct dplay *d;
    int i, arena = nt_lowmem_arena(0);

    net_config();
    d = nt_lowmem_calloc(1, (uint32_t)sizeof *d);
    if (!vtable) {
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
        vtable[6] = (void *)dp_create_player;
        vtable[9] = (void *)dp_destroy_player;
        vtable[13] = (void *)dp_enum_sessions;
        vtable[14] = (void *)dp_get_caps;
        vtable[17] = (void *)dp_message_count;
        vtable[18] = (void *)dp_player_address;
        vtable[22] = (void *)dp_session_desc;
        vtable[24] = (void *)dp_open;
        vtable[25] = (void *)dp_receive;
        vtable[31] = (void *)dp_set_session_desc;
        vtable[35] = (void *)dp_enum_connections;
        vtable[38] = (void *)dp_initialize_connection;
        vtable[40] = (void *)dp_send_chat;
        vtable[46] = (void *)dp_player_flags;
        vtable[49] = (void *)dp_send_ex;
        vtable[50] = (void *)dp_message_queue;
        vtable[51] = (void *)dp_cancel_message;
    }
    nt_lowmem_arena(arena);
    d->vtable = vtable;
    d->refs = 1;
    return d;
}

/* ---- IDirectPlayLobby2A: compound addresses ---- */

struct lobby {
    void **vtable;
    LONG refs;
};

static LONG WINAPI lobby_query(struct lobby *o, const void *iid, uint32_t *out)
{
    (void)iid;
    *out = LOW(o);
    o->refs++;
    return 0;
}

static LONG WINAPI lobby_add_ref(struct lobby *o)
{
    return ++o->refs;
}

static LONG WINAPI lobby_release(struct lobby *o)
{
    return --o->refs > 0 ? o->refs : 0;
}

/* EnumAddress(callback, address, size, context): not needed (the modem
 * probe stops before it) */
static LONG WINAPI lobby_enum_address(struct lobby *o, void *callback, const void *address, DWORD size, void *ctx)
{
    (void)o, (void)callback, (void)address, (void)size, (void)ctx;
    return (LONG)DPERR_UNAVAILABLE;
}

/* CreateCompoundAddress(elements, count, out, &size): {guid, size, data
 * (32-bit)} elements; the TCP/IP service provider and its address become
 * our connection; other providers are unavailable */
static LONG WINAPI lobby_compound_address(struct lobby *o, const void *elements, DWORD count, void *out, DWORD *size)
{
    const uint32_t *e = elements;
    const char *address = "";
    int tcp = 0;
    uint32_t len, need;
    DWORD i;

    (void)o;
    if (!e || !size)
        return (LONG)DPERR_INVALIDPARAMS;
    for (i = 0; i < count; i++, e += 6) {
        if (!memcmp(e, DPAID_SERVICEPROVIDER, 16) && e[4] >= 16 && e[5])
            tcp = !memcmp(AT(e[5]), GUID_SP_TCPIP, 16);
        else if (!memcmp(e, DPAID_INET, 16) && e[5])
            address = (const char *)AT(e[5]);
    }
    if (!tcp)
        return (LONG)DPERR_UNAVAILABLE;
    len = (uint32_t)strnlen(address, CONN_MAX - 1);
    need = 8 + len + 1;
    if (!out || *size < need) {
        *size = need;
        return (LONG)DPERR_BUFFERTOOSMALL;
    }
    put32(out, CONN_MAGIC);
    put32((uint8_t *)out + 4, len);
    memcpy((uint8_t *)out + 8, address, len);
    ((uint8_t *)out)[8 + len] = 0;
    *size = need;
    return 0;
}

void *nt_dplay_lobby_create(void)
{
    static void **vtable;
    struct lobby *o;
    int i, arena = nt_lowmem_arena(0);

    o = nt_lowmem_calloc(1, (uint32_t)sizeof *o);
    if (!vtable) {
        vtable = nt_lowmem_calloc(32, (uint32_t)sizeof *vtable);
        for (i = 0; i < 32; i++) {
            char name[64];
            snprintf(name, sizeof name, "IDirectPlayLobby2A method %d (+0x%x)", i, i * 4);
            vtable[i] = nt_named_stub("com", name);
        }
        vtable[0] = (void *)lobby_query;
        vtable[1] = (void *)lobby_add_ref;
        vtable[2] = (void *)lobby_release;
        vtable[5] = (void *)lobby_enum_address;
        vtable[14] = (void *)lobby_compound_address;
    }
    nt_lowmem_arena(arena);
    o->vtable = vtable;
    o->refs = 1;
    return o;
}

/* out: a 4-byte slot in game memory */
LONG WINAPI DirectPlayLobbyCreateA(void *guid, uint32_t *out, void *outer, void *data, DWORD size)
{
    (void)guid;
    (void)outer;
    (void)data;
    (void)size;
    if (!out)
        return (LONG)DPERR_INVALIDPARAMS;
    *out = LOW(nt_dplay_lobby_create());
    return 0;
}

LONG WINAPI DirectPlayCreate(void *guid, uint32_t *out, void *outer)
{
    (void)guid;
    (void)outer;
    if (out)
        *out = 0;
    return (LONG)DPERR_UNAVAILABLE;
}
