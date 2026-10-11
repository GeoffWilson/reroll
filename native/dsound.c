/* DSOUND for the native build: no DirectSound device (sound goes through the
 * QMixer replacement). Imported by ordinal 1. */
#include "nt.h"

#define DSERR_NODRIVER 0x88780078u

LONG WINAPI DirectSoundCreate(const void *guid, void **out, void *outer)
{
    (void)guid;
    (void)outer;
    if (out)
        *out = 0;
    return (LONG)DSERR_NODRIVER;
}
