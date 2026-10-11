/* GT20 decompressor: replaces 0x497860 in Rollcage.exe.
 *
 * Called by the archive loader (0x496590) as gt20_decompress(blob, out),
 * where blob is a GT20 entry (16-byte header) and out its destination.
 * The loader decompresses in place: blob sits at the tail of the output
 * buffer, so input reads and output writes must happen in the original's
 * order, including the point at which each flag word is refilled.
 * Returns a pointer just past the last byte written. */
#include "../runtime.h"
#include "game.h"

static uint32_t read_u32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t read_u16(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}

uint8_t *GAMEAPI gt20_decompress(const uint8_t *blob, uint8_t *out)
{
    const uint8_t *in = blob + 16 + read_u32(blob + 12);
    uint32_t flags = read_u32(in);
    int left = 32;

    in += 4;

/* Drop the current flag bit; fetch the next flag word once 32 are used. */
#define CONSUME_FLAG()                \
    do {                              \
        flags >>= 1;                  \
        if (--left == 0) {            \
            flags = read_u32(in);     \
            in += 4;                  \
            left = 32;                \
        }                             \
    } while (0)

    for (;;) {
        const uint8_t *from;
        uint32_t len;

        if (!(flags & 1)) {
            *out++ = *in++;
            CONSUME_FLAG();
            continue;
        }
        CONSUME_FLAG();

        if (flags & 1) {
            /* Long match: 13-bit distance, length in the low 3 bits or an extra byte. */
            uint32_t w = read_u16(in);
            in += 2;
            from = out - (0x2000 - (w >> 3));
            len = w & 7;
            if (len) {
                len += 2;
            } else {
                uint8_t ext = *in++;
                if (ext & 0x80)
                    from -= 0x2000;
                ext &= 0x7F;
                if (ext == 1)
                    return out; /* end of stream */
                if (ext == 0) {
                    len = read_u16(in);
                    in += 2;
                } else {
                    len = ext + 2u;
                }
            }
            /* Byte-wise: overlapping matches repeat recent output. */
            while (len--)
                *out++ = *from++;
            CONSUME_FLAG();
        } else {
            /* Short match: 8-bit distance, length 2 + 2*A + B from two more flags. */
            from = out - (256 - *in++);
            *out++ = *from++;
            *out++ = *from++;
            CONSUME_FLAG();
            if (flags & 1) {
                *out++ = *from++;
                *out++ = *from++;
            }
            CONSUME_FLAG();
            if (flags & 1)
                *out++ = *from++;
            CONSUME_FLAG();
        }
    }
#undef CONSUME_FLAG
}
