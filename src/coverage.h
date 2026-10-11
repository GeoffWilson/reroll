/* Counts of how often interesting code paths ran, logged by verify.c, so a
 * clean verification run also says what it covered. */
#ifndef REROLL_COVERAGE_H
#define REROLL_COVERAGE_H

#include "runtime.h"

enum coverage_id {
    COVER_TRACK_BOUNCE,      /* contact type 1 */
    COVER_TRACK_PUSH_OUT,    /* type 2 */
    COVER_TRACK_SLIDE,       /* type 8, sliding */
    COVER_TRACK_SLIDE_SPIN,  /* type 8, keeping normal spin */
    COVER_TRACK_KIND_16,     /* type 0x10 */
    COVER_TRACK_SOAK,        /* type 0x40 */
    COVER_TRACK_OTHER,
    COVER_BODY_ALONE,        /* two-body contact with no second body */
    COVER_BODY_IMPULSE,
    COVER_BODY_PUSH_OUT,     /* type 2 */
    COVER_BODY_SLIDE,        /* type 8 */
    COVER_BODY_WEIGHTED,     /* impulse with a weight-class shift */
    COVER_BODY_STOPPED,      /* body_stop */
    COVER_SLOW_MOTION,       /* integrate with slow motion scaling */
    COVER_CAR_SETTLED,       /* car_settle stopped a car */
    COVER_CAR_ROLL_DAMPED,   /* car_roll_damping slowed a roll */
    COVER_CAR_SELF_RIGHT,    /* car_self_right twisted a car */
    COVER_TYRE_HOLD,         /* tyre_grip below the slip limit */
    COVER_TYRE_SLIDE,        /* tyre_grip sliding */
    COVER_TYRE_SPARKS,       /* sparks spawned */
    COVER_POWER_SLIDE,       /* grip_level: sliding grip */
    COVER_SLIDE_RECOVERY,    /* grip_level: recovery grip */
    COVER_OFF_TRACK,         /* track_cell used the nearest cell */
    COVER_TRIANGLE_CONTACT,  /* polygon_contacts added a triangle */
    COVER_WALL_CONTACT,      /* polygon_contacts added an edge wall */
    COVER_DUPLICATE_CONTACT, /* already in the list */
    COVER_EDGE_REJECTED,     /* failed the edge test near the limit */
    COVER_OBJECT_POINT,      /* object_polygon_test: sphere against a point */
    COVER_OBJECT_EDGE,       /* against an edge */
    COVER_OBJECT_FACE,       /* inside all of a face's planes */
    COVER_OBJECT_MISS,       /* outside one of them */
    COVER_SWEEP_HIT,         /* body_track_sweep: hit within the step */
    COVER_SWEEP_RESTING,     /* still touching at the limit (kind 8) */
    COVER_CONTACT_SAVED,     /* body_track_contact: saved contact (0x2000) */
    COVER_CONTACT_LOST,      /* no contact after all (0x10) */
    COVER_PAIR_EVENT,        /* group_pair_contacts queued a hit */
    COVER_TRACK_EDGE_HIT,    /* track_edge_hit: sphere hit a polygon edge */
    COVER_TRACK_FOLD,        /* averaged across a shallow fold */
    COVER_TRACK_FOLLOWED,    /* slot_track_sweep moved on across an edge */
    COVER_OBJECT_FACE_HIT,   /* object_polygon_sweep: onto a face */
    COVER_OBJECT_EDGE_HIT,   /* onto an edge between two faces */
    COVER_OBJECT_INSIDE,     /* inside all faces */
    COVER_OBJECT_CORNER_HIT, /* onto a corner or point */
    COVER_SPIN_AROUND,       /* car_spin_around turned a car */
    COVER_TRACK_SPECIAL,     /* track_special_section found one */
    COVER_SCRIPT_PATH,       /* body_script_step followed a path */
    COVER_SCRIPT_TURNED,     /* and turned the body along it */
    COVER_BLAST_CAR,         /* body_explosion_force pushed a car */
    COVER_BLAST_REACTION,    /* a car reacted through 0x46d760 */
    COVER_BLAST_OTHER,       /* pushed another body */
    COVER_COUNT
};

extern uint32_t coverage[COVER_COUNT];
extern const char *const coverage_names[COVER_COUNT];

#define COVER(id) (coverage[id]++)

#endif
