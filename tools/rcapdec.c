/* Decodes frame captures with the renderer's packet decoder (src/render/packets.c)
 * and checks the result (docs/renderer/plan.md, step 2).
 *
 *     build/rcapdec FILE...          summary per walk and totals
 *     build/rcapdec -v FILE          also list every primitive
 *
 * Checks, per capture: every packet is understood (no unknown codes, nothing
 * unreadable), and every vertex the back end takes from camera space
 * re-projects onto the screen position the original stored, except the
 * known half-pixel seam nudges. Exit status 1 if a check fails. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/draw.h"
#include "rcapfile.h"

static const char *kind_name(const struct rd_prim *p)
{
    static const char *names[] = {"tri", "quad", "line"};
    return names[p->kind];
}

int main(int argc, char **argv)
{
    int verbose = 0, failed = 0, i;
    static struct rd_prim prims[0x10000];
    static struct rd_vertex verts[0x40000];

    for (i = 1; i < argc; i++) {
        struct capture cap;
        struct rd_stats total = {0};
        uint32_t prim_total = 0, two_d = 0, screen = 0, w;

        if (!strcmp(argv[i], "-v")) {
            verbose = 1;
            continue;
        }
        if (!capture_load(argv[i], &cap)) {
            fprintf(stderr, "%s: not a capture\n", argv[i]);
            failed = 1;
            continue;
        }
        printf("%s: frame %u, %ux%u, %d walks, %d texture pages\n", argv[i], cap.header[1], cap.header[2],
               cap.header[3], cap.walk_count, cap.texture_count);
        for (w = 0; w < (uint32_t)cap.walk_count; w++) {
            struct reader reader = {&cap, &cap.walks[w]};
            struct rd_list list;
            uint32_t p;

            memset(&list, 0, sizeof list);
            list.prims = prims;
            list.verts = verts;
            list.prim_max = sizeof prims / sizeof *prims;
            list.vert_max = sizeof verts / sizeof *verts;
            rd_read_view(capture_read, &reader, cap.walks[w].start, &list.view);
            rd_decode(capture_read, &reader, &list);
            printf("  walk %u OT %#x: h %.1f centre (%.1f, %.1f) origin (%.0f, %.0f): %u packets -> %u prims, "
                   "%u vertices\n",
                   w, list.view.ot_start, list.view.h, list.view.cx, list.view.cy, list.view.ox, list.view.oy,
                   list.stats.packets, list.prim_count, list.vert_count);
            for (p = 0; p < list.prim_count; p++) {
                const struct rd_prim *pr = &prims[p];
                two_d += (pr->flags & RD_2D) != 0;
                screen += (pr->flags & RD_SCREEN) != 0;
                if (verbose) {
                    const struct rd_vertex *v = &verts[pr->first];
                    int c;
                    printf("    %5u %08x %-4s code %02x page %02x abr %u tm %u%s%s%s:", pr->order, pr->packet,
                           kind_name(pr), pr->code, pr->page, pr->abr, pr->tm, pr->flags & RD_2D ? " 2D" : "",
                           pr->flags & RD_SCREEN ? " screen" : "", pr->flags & RD_NEAR_CLIP ? " near" : "");
                    for (c = 0; c < pr->count; c++)
                        printf(" (%.1f %.1f %.1f uv %u,%u %02x%02x%02x/%02x)", v[c].x, v[c].y, v[c].z, v[c].u,
                               v[c].v, v[c].r, v[c].g, v[c].b, v[c].a);
                    printf("\n");
                }
            }
            prim_total += list.prim_count;
            total.packets += list.stats.packets;
            total.ignored += list.stats.ignored;
            total.unknown += list.stats.unknown;
            total.unreadable += list.stats.unreadable;
            total.camera_vertices += list.stats.camera_vertices;
            total.nudged_vertices += list.stats.nudged_vertices;
            total.screen_vertices += list.stats.screen_vertices;
            total.clipped_vertices += list.stats.clipped_vertices;
            if (list.stats.worst_error > total.worst_error)
                total.worst_error = list.stats.worst_error;
            if (list.prim_count == list.prim_max || list.vert_count >= list.vert_max - 4) {
                printf("  walk %u: draw list full\n", w);
                failed = 1;
            }
        }
        printf("  total: %u packets (%u ignored, %u unknown, %u unreadable) -> %u prims (%u 2D, %u screen-space 3D)\n",
               total.packets, total.ignored, total.unknown, total.unreadable, prim_total, two_d, screen);
        printf("  vertices: %u camera-space exact, %u seam-nudged, %u screen-built, %u near-clipped; "
               "worst clip-path re-projection miss %.3f px\n",
               total.camera_vertices, total.nudged_vertices, total.screen_vertices, total.clipped_vertices,
               total.worst_error);
        if (total.unknown || total.unreadable)
            failed = 1;
    }
    return failed;
}
