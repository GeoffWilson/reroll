/* WINMM for the native build: the clock, the multimedia timer (the 16 ms
 * timer that drives the front end's animation, timer_start 0x476fb0, runs
 * its callback on its own thread as Windows does), and no mixer / aux / CD
 * devices for now (CD music: plan step 6). */
#include "nt.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef unsigned int UINT;
/* as the game's callbacks are declared (mm_timer_tick, tick_callback,
 * movie_sound_timer: five uint32_t) */
typedef void(WINAPI *timer_callback)(uint32_t id, uint32_t message, uint32_t user, uint32_t reserved1,
                                     uint32_t reserved2);

DWORD WINAPI timeGetTime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

UINT WINAPI timeBeginPeriod(UINT ms)
{
    (void)ms;
    return 0;
}

UINT WINAPI timeEndPeriod(UINT ms)
{
    (void)ms;
    return 0;
}

UINT WINAPI timeGetDevCaps(UINT *caps, UINT size)
{
    if (size >= 8) {
        caps[0] = 1;       /* minimum period */
        caps[1] = 1000000; /* maximum */
    }
    return 0;
}

struct timer {
    UINT id;
    UINT delay;
    timer_callback callback;
    DWORD user;
    UINT flags;
    volatile int running;
    pthread_t thread;
};

#define MAX_TIMERS 8
static struct timer timers[MAX_TIMERS];
static UINT next_id = 1;

static void *timer_thread(void *arg)
{
    struct timer *t = arg;
    struct timespec next;
    nt_teb_init_thread();
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (t->running) {
        next.tv_nsec += (long)t->delay * 1000000L;
        while (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, 0);
        if (!t->running)
            break;
        t->callback(t->id, 0, t->user, 0, 0);
        if (!(t->flags & 1)) /* TIME_ONESHOT is 0, TIME_PERIODIC 1 */
            break;
    }
    return 0;
}

UINT WINAPI timeSetEvent(UINT delay, UINT resolution, timer_callback callback, DWORD user, UINT flags)
{
    int i;
    (void)resolution;
    for (i = 0; i < MAX_TIMERS; i++)
        if (!timers[i].running) {
            struct timer *t = &timers[i];
            if (t->id)
                pthread_join(t->thread, 0);
            t->id = next_id++;
            t->delay = delay ? delay : 1;
            t->callback = callback;
            t->user = user;
            t->flags = flags;
            t->running = 1;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            nt_low_stack(&attr, 0x40000);
            if (pthread_create(&t->thread, &attr, timer_thread, t)) {
                t->running = 0;
                t->id = 0;
                return 0;
            }
            return t->id;
        }
    return 0;
}

UINT WINAPI timeKillEvent(UINT id)
{
    int i;
    for (i = 0; i < MAX_TIMERS; i++)
        if (timers[i].id == id && timers[i].running) {
            timers[i].running = 0;
            if (!pthread_equal(timers[i].thread, pthread_self()))
                pthread_join(timers[i].thread, 0);
            timers[i].id = 0;
            return 0;
        }
    return 97; /* TIMERR_NOCANDO */
}

/* The game restarts in place: its timers stop. */
void nt_timers_reset(void)
{
    int i;
    for (i = 0; i < MAX_TIMERS; i++)
        if (timers[i].running)
            timeKillEvent(timers[i].id);
}

/* ---- mixer devices: none; aux device 0 is the CD (its volume) ---- */

#define MMSYSERR_NODRIVER 6
#define MMSYSERR_BADDEVICEID 2

UINT WINAPI mixerGetNumDevs(void)
{
    return 0;
}

UINT WINAPI mixerOpen(void *mixer, UINT id, DWORD callback, DWORD instance, DWORD flags)
{
    (void)mixer;
    (void)id;
    (void)callback;
    (void)instance;
    (void)flags;
    return MMSYSERR_NODRIVER;
}

UINT WINAPI mixerClose(void *mixer)
{
    (void)mixer;
    return 0;
}

UINT WINAPI mixerGetDevCapsA(UINT id, void *caps, UINT size)
{
    (void)id;
    (void)caps;
    (void)size;
    return MMSYSERR_NODRIVER;
}

UINT WINAPI mixerGetLineInfoA(void *mixer, void *line, DWORD flags)
{
    (void)mixer;
    (void)line;
    (void)flags;
    return MMSYSERR_NODRIVER;
}

UINT WINAPI mixerGetLineControlsA(void *mixer, void *controls, DWORD flags)
{
    (void)mixer;
    (void)controls;
    (void)flags;
    return MMSYSERR_NODRIVER;
}

UINT WINAPI mixerGetControlDetailsA(void *mixer, void *details, DWORD flags)
{
    (void)mixer;
    (void)details;
    (void)flags;
    return MMSYSERR_NODRIVER;
}

UINT WINAPI mixerSetControlDetails(void *mixer, void *details, DWORD flags)
{
    (void)mixer;
    (void)details;
    (void)flags;
    return MMSYSERR_NODRIVER;
}

/* ---- CD audio from the disc image's track files ----
 * The cue sheet: REROLL_CD_CUE, else the first *.cue in ~/Games/rollcage or
 * ~/Documents/rollcage. Tracks are raw 44.1 kHz 16-bit stereo; INDEX 01 is
 * where each starts in its file. MCI as the game uses it (0x496510..): open,
 * TMSF time format, status (tracks, track length, mode), play one track
 * from its start to its end, stop, close; the game polls the mode and picks
 * the next track when one stops. */

#include <SDL3/SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <strings.h>

#define MAX_TRACKS 99
#define SECTOR 2352

struct cd_track {
    char path[600];
    uint32_t start, length; /* bytes */
    int audio;
};

static struct cd_track tracks[MAX_TRACKS + 1];
static int track_count = -1;
static SDL_AudioStream *cd_stream;
static FILE *cd_file;
static int cd_track_playing, cd_paused;
static uint32_t cd_left, cd_pos; /* bytes left in the track, position */
static float cd_volume_left = 1.0f, cd_volume_right = 1.0f;
static DWORD cd_volume_word = 0xffffffffu;
static DWORD time_format;

static int find_cue(char *out, size_t size)
{
    const char *env = getenv("REROLL_CD_CUE"), *home = getenv("HOME");
    const char *dirs[2] = {"Games/rollcage", "Documents/rollcage"};
    int i;
    if (env && *env) {
        snprintf(out, size, "%s", env);
        return 1;
    }
    for (i = 0; i < 2 && home; i++) {
        char dir[600];
        DIR *d;
        struct dirent *e;
        snprintf(dir, sizeof dir, "%s/%s", home, dirs[i]);
        d = opendir(dir);
        if (!d)
            continue;
        while ((e = readdir(d)) != 0) {
            size_t n = strlen(e->d_name);
            if (n > 4 && !strcasecmp(e->d_name + n - 4, ".cue")) {
                snprintf(out, size, "%s/%s", dir, e->d_name);
                closedir(d);
                return 1;
            }
        }
        closedir(d);
    }
    return 0;
}

static void load_cue(void)
{
    char cue[700], line[1024], dir[700], file[600] = "", *slash;
    FILE *f;
    int track = 0;
    if (track_count >= 0)
        return;
    track_count = 0;
    if (!find_cue(cue, sizeof cue) || !(f = fopen(cue, "r"))) {
        nt_log("cd: no cue sheet (REROLL_CD_CUE), no music");
        return;
    }
    snprintf(dir, sizeof dir, "%s", cue);
    slash = strrchr(dir, '/');
    if (slash)
        *slash = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line, name[600];
        int n, m, sec, fr;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!strncmp(p, "FILE \"", 6)) {
            char *end = strrchr(p + 6, '"');
            if (end) {
                *end = 0;
                snprintf(name, sizeof name, "%s", p + 6);
                snprintf(file, sizeof file, "%s/%s", dir, name);
            }
        } else if (sscanf(p, "TRACK %d", &n) == 1 && n > 0 && n <= MAX_TRACKS) {
            track = n;
            snprintf(tracks[n].path, sizeof tracks[n].path, "%s", file);
            tracks[n].audio = strstr(p, "AUDIO") != 0;
            if (n > track_count)
                track_count = n;
        } else if (track && sscanf(p, "INDEX 01 %d:%d:%d", &m, &sec, &fr) == 3) {
            FILE *t = fopen(tracks[track].path, "rb");
            long size = 0;
            if (t) {
                fseek(t, 0, SEEK_END);
                size = ftell(t);
                fclose(t);
            }
            tracks[track].start = (uint32_t)((m * 60 + sec) * 75 + fr) * SECTOR;
            tracks[track].length = size > (long)tracks[track].start ? (uint32_t)size - tracks[track].start : 0;
        }
    }
    fclose(f);
    nt_log("cd: %d tracks from %s", track_count, cue);
}

static void SDLCALL cd_feed(void *user, SDL_AudioStream *s, int needed, int total)
{
    int16_t buf[4096];
    float out[4096];
    (void)user;
    (void)total;
    while (needed > 0) {
        size_t want = sizeof buf, got = 0, i;
        if (cd_file && cd_track_playing && !cd_paused && cd_left) {
            if (want > cd_left)
                want = cd_left;
            if ((int)(want / 2 * sizeof(float)) > needed)
                want = (size_t)needed / sizeof(float) * 2;
            want &= ~(size_t)3;
            got = fread(buf, 1, want, cd_file);
            cd_left -= (uint32_t)got;
            cd_pos += (uint32_t)got;
            if (!got || !cd_left)
                cd_track_playing = 0; /* the game sees "stopped" and picks the next */
        }
        if (!got) { /* silence */
            memset(out, 0, sizeof out);
            SDL_PutAudioStreamData(s, out, needed < (int)sizeof out ? needed : (int)sizeof out);
            needed -= (int)sizeof out;
            continue;
        }
        for (i = 0; i < got / 2; i++)
            out[i] = (float)buf[i] / 32768.0f * ((i & 1) ? cd_volume_right : cd_volume_left);
        if (getenv("REROLL_CD_DUMP")) {
            static FILE *dump;
            if (!dump)
                dump = fopen(getenv("REROLL_CD_DUMP"), "wb");
            if (dump)
                fwrite(out, sizeof(float), got / 2, dump);
        }
        SDL_PutAudioStreamData(s, out, (int)(got / 2 * sizeof(float)));
        needed -= (int)(got / 2 * sizeof(float));
    }
}

static void cd_lock(void)
{
    if (cd_stream)
        SDL_LockAudioStream(cd_stream);
}

static void cd_unlock(void)
{
    if (cd_stream)
        SDL_UnlockAudioStream(cd_stream);
}

static int cd_play(int track)
{
    load_cue();
    if (track < 1 || track > track_count || !tracks[track].audio || !tracks[track].length)
        return 0;
    if (!cd_stream) {
        SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 44100};
        cd_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, cd_feed, 0);
        if (!cd_stream) {
            nt_log("cd: no audio device (%s)", SDL_GetError());
            return 0;
        }
        SDL_ResumeAudioStreamDevice(cd_stream);
    }
    cd_lock();
    if (cd_file)
        fclose(cd_file);
    cd_file = fopen(tracks[track].path, "rb");
    if (cd_file)
        fseek(cd_file, (long)tracks[track].start, SEEK_SET);
    cd_left = tracks[track].length;
    cd_pos = 0;
    cd_track_playing = cd_file ? track : 0;
    cd_paused = 0;
    cd_unlock();
    nt_log("cd: playing track %d", track);
    return cd_file != 0;
}

static void cd_stop(void)
{
    cd_lock();
    cd_track_playing = 0;
    cd_paused = 0;
    cd_unlock();
}

/* MSF of a byte length (75 frames a second) */
static DWORD msf(uint32_t bytes)
{
    uint32_t frames = bytes / SECTOR;
    return (frames / 75 / 60) | (frames / 75 % 60) << 8 | (frames % 75) << 16;
}

#define MCI_OPEN 0x803
#define MCI_CLOSE 0x804
#define MCI_PLAY 0x806
#define MCI_SEEK 0x807
#define MCI_STOP 0x808
#define MCI_PAUSE 0x809
#define MCI_SET 0x80d
#define MCI_STATUS 0x814
#define MCI_RESUME 0x855
#define MCI_FROM 4
#define MCI_TRACK 0x10
#define MCI_STATUS_ITEM 0x100
#define MCI_SET_TIME_FORMAT 0x400

DWORD WINAPI mciSendCommandA(UINT device, UINT message, DWORD flags, DWORD params)
{
    DWORD *p = (DWORD *)(uintptr_t)params;
    (void)device;
    load_cue();
    switch (message) {
    case MCI_OPEN:
        if (!track_count)
            return 263; /* MCIERR_INVALID_DEVICE_NAME: no CD */
        if (p)
            p[1] = 1; /* device id */
        return 0;
    case MCI_CLOSE:
    case MCI_STOP:
        cd_stop();
        return 0;
    case MCI_PAUSE:
        cd_lock();
        cd_paused = 1;
        cd_unlock();
        return 0;
    case MCI_RESUME:
        cd_lock();
        cd_paused = 0;
        cd_unlock();
        return 0;
    case MCI_SET:
        if ((flags & MCI_SET_TIME_FORMAT) && p)
            time_format = p[1];
        return 0;
    case MCI_PLAY:
        /* {callback, from, to}: from is TMSF (track in the low byte) */
        if (p && (flags & MCI_FROM))
            return cd_play((int)(p[1] & 0xff)) ? 0 : 1;
        cd_lock();
        cd_paused = 0;
        cd_unlock();
        return 0;
    case MCI_SEEK:
        return 0;
    case MCI_STATUS:
        /* {callback, return, item, track} */
        if (!p || !(flags & MCI_STATUS_ITEM))
            return 1;
        switch (p[2]) {
        case 1: /* length */
            if ((flags & MCI_TRACK) && p[3] >= 1 && (int)p[3] <= track_count)
                p[1] = msf(tracks[p[3]].length);
            else
                p[1] = msf(0);
            return 0;
        case 2: /* position */
            p[1] = (DWORD)cd_track_playing | msf(cd_pos) << 8;
            return 0;
        case 3: /* number of tracks */
            p[1] = (DWORD)track_count;
            return 0;
        case 4: /* mode */
            p[1] = cd_track_playing ? (cd_paused ? 0x211 : 0x20e) : 0x20d;
            return 0;
        case 5: /* media present */
        case 7: /* ready */
            p[1] = 1;
            return 0;
        case 6:
            p[1] = time_format;
            return 0;
        case 8: /* current track */
            p[1] = (DWORD)cd_track_playing;
            return 0;
        default:
            return 1;
        }
    default:
        nt_log("mciSendCommandA(%x): not handled", message);
        return 1;
    }
}

void nt_cd_reset(void)
{
    cd_stop();
}

/* aux device 0: the CD's volume */
UINT WINAPI auxGetNumDevs(void)
{
    load_cue();
    return track_count > 0;
}

/* AUXCAPS: mid, pid, driver version, name[32] +8, technology +0x28 (1 = CD), support +0x2c (volume, L/R) */
UINT WINAPI auxGetDevCapsA(UINT id, uint8_t *caps, UINT size)
{
    if (id != 0 || size < 0x30)
        return MMSYSERR_BADDEVICEID;
    memset(caps, 0, size);
    snprintf((char *)caps + 8, 32, "CD Audio");
    *(uint16_t *)(caps + 0x28) = 1;
    *(DWORD *)(caps + 0x2c) = 3;
    return 0;
}

UINT WINAPI auxGetVolume(UINT id, DWORD *volume)
{
    if (id != 0)
        return MMSYSERR_BADDEVICEID;
    if (volume)
        *volume = cd_volume_word;
    return 0;
}

UINT WINAPI auxSetVolume(UINT id, DWORD volume)
{
    if (id != 0)
        return MMSYSERR_BADDEVICEID;
    cd_lock();
    cd_volume_word = volume;
    cd_volume_left = (float)(volume & 0xffff) / 65535.0f;
    cd_volume_right = (float)(volume >> 16) / 65535.0f;
    cd_unlock();
    return 0;
}
