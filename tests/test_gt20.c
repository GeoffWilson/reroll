/* Native test for gt20_decompress against every entry in ROLLCAGE.IMG.
 *
 * Each compressed entry is decoded twice: into a separate buffer, and in place
 * the way the game's loader (0x496590) does it, with the blob copied to the
 * 4-byte aligned tail of the output buffer plus the entry's safety margin.
 * Both must give the indexed size and
 * identical bytes. Prints "<entry> <fnv1a32>" per entry so the result can be
 * compared with tools/rollcage_img.py (see `make test`). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

static uint8_t *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(*size);
    if (fread(data, 1, *size, f) != (size_t)*size) {
        fprintf(stderr, "%s: short read\n", path);
        exit(2);
    }
    fclose(f);
    return data;
}

int main(int argc, char **argv)
{
    char path[4096];
    long idx_size, img_size;
    uint8_t *idx, *img;
    int failures = 0, tested = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <IDXData dir>\n", argv[0]);
        return 2;
    }
    snprintf(path, sizeof(path), "%s/Rollcage.idx", argv[1]);
    idx = read_file(path, &idx_size);
    snprintf(path, sizeof(path), "%s/ROLLCAGE.IMG", argv[1]);
    img = read_file(path, &img_size);

    for (long i = 0; i < idx_size / 16; i++) {
        uint32_t off, stored, unpacked;
        int16_t margin;
        uint8_t *plain, *inplace, *end, *blob;

        memcpy(&off, idx + i * 16, 4);
        memcpy(&stored, idx + i * 16 + 4, 4);
        memcpy(&unpacked, idx + i * 16 + 8, 4);
        memcpy(&margin, idx + i * 16 + 12, 2);
        if (stored == unpacked) {
            printf("%ld %08x\n", i, fnv1a(img + off, stored));
            continue;
        }

        plain = malloc(unpacked);
        end = gt20_decompress(img + off, plain);
        if ((uint32_t)(end - plain) != unpacked) {
            fprintf(stderr, "entry %ld: wrote %ld bytes, expected %u\n", i, (long)(end - plain), unpacked);
            failures++;
        }

        /* In place, as the loader lays it out: buffer of margin + unpacked + 7
         * bytes, blob at (margin + unpacked - stored + 7) rounded down to a
         * multiple of 4. The margin is the per-entry s16 at +12 in the index. */
        inplace = aligned_alloc(4, (margin + unpacked + 7 + 3) & ~3u);
        blob = inplace + ((margin + unpacked - stored + 7) & ~3u);
        memmove(blob, img + off, stored);
        end = gt20_decompress(blob, inplace);
        if ((uint32_t)(end - inplace) != unpacked || memcmp(plain, inplace, unpacked) != 0) {
            fprintf(stderr, "entry %ld: in-place result differs\n", i);
            failures++;
        }

        printf("%ld %08x\n", i, fnv1a(plain, unpacked));
        tested++;
        free(plain);
        free(inplace);
    }
    fprintf(stderr, "%d compressed entries tested, %d failures\n", tested, failures);
    free(idx);
    free(img);
    return failures != 0;
}
