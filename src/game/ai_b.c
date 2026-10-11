/* The AI's choice of where to drive (0x412290, from the AI's thinking
 * when its counter +0x6c9 comes round): gather up to 20 candidate places
 * to head for (racing-line nodes along the car's line and the lines it
 * links to, plus set points), score each by how straight, safe and quick
 * it is to reach, and take the best: a line and node (+0x6b0/+0x6b2) or a
 * set point (+0x6b9 mode, +0x6bc position).
 *
 * Candidates (0x6cf220, 0x34 bytes each, filled from index 19 down):
 * +0 line, +2 node, +3 hazard id, +4 unit direction to it, +0x10 the
 * line's direction there, +0x1c how well the car's travel points at it,
 * +0x20 how straight the line runs there, +0x24 how many more nodes may
 * follow, +0x25 set-point mode, +0x26 ruled out, +0x27 end of its line,
 * +0x2a state to adopt (car +0x6b4), +0x2c score, +0x30 cost. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_lines (*(uint8_t **)(*(uint8_t **)0x6c1e30 + 0x60))
#define g_lines_copy (*(uint8_t **)0x6bfb20)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

#define LINE_IN(lines, i) ((lines) + (uint16_t)(i) * 0xc)
#define LINE(i) LINE_IN(g_lines, i)
#define NODE(line, i) (*(uint8_t **)((line) + 8) + (uint16_t)(i) * 0x10)
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))

#define CAND(i) ((uint8_t *)0x6cf220 + (i) * 0x34)
#define CAND_COUNT 20
#define C_LINE 0x00
#define C_NODE 0x02
#define C_HAZARD 0x03
#define C_DIR 0x04
#define C_LINE_DIR 0x10
#define C_FACING 0x1c
#define C_STRAIGHT 0x20
#define C_LEVEL 0x24
#define C_MODE 0x25
#define C_OUT 0x26
#define C_END 0x27
#define C_STATE 0x2a
#define C_SCORE 0x2c
#define C_COST 0x30

#define g_set_points ((int16_t *)0x6cf180)      /* 8 bytes each: x, y, z */
#define g_set_point_limit ((uint8_t *)0x6cf640) /* per set point */
#define g_waypoints ((int16_t *)0x4b7db0)       /* 8 bytes per candidate */

/* Objects and cars ahead (0x403200, 0x403a40) */
#define g_objects (*(uint8_t **)0x600664) /* 36 x {object, u8 active} */
#define g_object_distance ((int32_t *)0x6d0e40)
#define g_objects_ahead ((uint8_t *)0x6d0e00)
#define g_objects_ahead_count (*(uint8_t *)0x4b5e54)
#define g_bodies ((uint8_t **)0x6d0a80) /* 12-byte records, body first */
#define g_body_count (*(uint32_t *)0x4b5f98)
#define g_body_distance ((int32_t *)0x6d0ce0)
#define g_bodies_ahead ((uint8_t *)0x6d0dc0)
#define g_bodies_ahead_count (*(uint32_t *)0x4b5f9c)
#define g_hazards (*(uint8_t **)0x6c1eb0) /* +0xf6 count, +0xf8 8-byte records */

/* Still original (other parts of the AI) */
typedef int32_t(GAMEAPI *crossing_fn)(uint32_t line, uint32_t node, int32_t *a, int32_t *b, int32_t *c);
#define line_crossing ((crossing_fn)0x47f9f0)
typedef int32_t(GAMEAPI *leg_cost_fn)(uint8_t *car, const int16_t *a, const int16_t *b, uint32_t section,
                                      int32_t speed, int32_t length);
#define leg_cost ((leg_cost_fn)0x403350)
typedef int32_t(GAMEAPI *leg_blocked_fn)(const int16_t *a, const int16_t *b, uint32_t section, int32_t speed,
                                         int32_t length);
#define leg_blocked ((leg_blocked_fn)0x403ba0)
typedef void(GAMEAPI *candidates_fn)(uint8_t *car, int32_t *n);
#define ai_avoid_cars ((candidates_fn)0x480d70)
#define ai_add_set_points ((candidates_fn)0x429350)
typedef int32_t(GAMEAPI *slot_fn)(uint8_t *slot);
#define slot_player_flag ((slot_fn)0x480030)
typedef int32_t(GAMEAPI *in_list_fn)(uint32_t value, const uint16_t *list, int32_t count);
#define u16_in_list ((in_list_fn)0x47f430)

static int32_t shl16(int32_t v)
{
    return (int32_t)((uint32_t)v << 16);
}

/* 0x4117a0 */
void GAMEAPI ai_candidates_clear(void)
{
    int32_t i;

    for (i = 0; i < CAND_COUNT; i++) {
        uint8_t *c = CAND(i);

        U32(c, C_LINE) = 0;
        U32(c, C_LEVEL) = 0;
        U16(c, C_STATE) = 0;
        I32(c, C_SCORE) = 0;
        I32(c, C_COST) = 0;
    }
}

/* 0x47fee0: is the car carrying item 4 in either slot? */
int32_t GAMEAPI car_has_item4(const uint8_t *car)
{
    return car[0x67c] == 4 || car[0x67d] == 4;
}

/* 0x47ff70: is the node within k nodes of the end of its line? */
int32_t GAMEAPI line_node_near_end(uint32_t line_index, uint32_t node, uint32_t k)
{
    const uint8_t *line = LINE(line_index);
    uint16_t count = *(const uint16_t *)line, n = (uint16_t)node, near = (uint16_t)k;

    if (near > count)
        return 1;
    if (line[2])
        return (int32_t)((uint32_t)count - n - 1) < (int32_t)near;
    return n < near;
}

/* 0x411cb0: is p on the same side of both edges b and c from a (the
 * side by sign)? Only x and z count. */
int32_t GAMEAPI point_between(const int32_t *p, const int32_t *a, const int32_t *b, const int32_t *c,
                              int32_t sign)
{
    int32_t dx = p[0] - a[0], dz = p[2] - a[2];
    int32_t s1 = mul16(b[2], dx) - mul16(b[0], dz), s2 = mul16(c[2], dx) - mul16(c[0], dz);

    if (sign < 0)
        return s1 < 0 && s2 < 0;
    return s1 > 0 && s2 > 0;
}

/* 0x411870: the best cost among candidates at either node the given one
 * links to (node +0xc: line 5 bits, node 9 bits, twice); as soon as both
 * links are matched, that cost; 0 if none. */
int32_t GAMEAPI ai_link_cost(uint32_t line, uint32_t node)
{
    const uint8_t *p = NODE(LINE(line), node);
    uint32_t link = U32(p, 0xc);
    uint16_t line_a = p[0xc] & 0x1f, node_a = link >> 5 & 0x1ff;
    uint16_t line_b = link >> 14 & 0x1f, node_b = link >> 19 & 0x1ff;
    int32_t found_a = line_a == 0, found_b = line_b == 0, best = -50000, i;

    if (found_a && found_b)
        return 0;
    for (i = 0; i < CAND_COUNT; i++) {
        const uint8_t *c = CAND(i);
        uint16_t l = U16(c, C_LINE), n = c[C_NODE];

        if (!l)
            continue;
        if (n == node_a && l == line_a && I32(c, C_COST) > best) {
            best = I32(c, C_COST);
            found_a = 1;
        } else {
            if (n == node_b && l == line_b && I32(c, C_COST) > best) {
                best = I32(c, C_COST);
                found_b = 1;
            }
            if (!found_a)
                continue;
        }
        if (found_b)
            return best;
    }
    return best != -50000 ? best : 0;
}

/* 0x4119a0: a candidate's direction from pos, the line's direction there,
 * how well the travel direction points at it (1.0 when slow) and how
 * straight the line runs, which also adjusts its score. */
void GAMEAPI ai_candidate_measure(int32_t index, const int32_t *pos, const int32_t *travel, int32_t speed)
{
    uint8_t *c = CAND(index);
    const uint8_t *line = LINE(U16(c, C_LINE));
    uint16_t node = c[C_NODE], other = 0;
    const int16_t *p = (const int16_t *)NODE(line, node), *q;
    int32_t ax = shl16(p[0]), ay = shl16(p[1]), az = shl16(p[2]), d[3], t[3], straight;

    d[0] = ax - pos[0];
    d[1] = ay - pos[1];
    d[2] = az - pos[2];
    vec_set_length(d, 0x10000);
    memcpy(c + C_DIR, d, 12);
    if (line_prev(node, line, &other)) {
        q = (const int16_t *)NODE(line, other);
        t[0] = ax - shl16(q[0]);
        t[1] = ay - shl16(q[1]);
        t[2] = az - shl16(q[2]);
    } else {
        /* the original reads an unset index if there is no next node */
        line_next(node, line, &other);
        q = (const int16_t *)NODE(line, other);
        t[0] = shl16(q[0]) - ax;
        t[1] = shl16(q[1]) - ay;
        t[2] = shl16(q[2]) - az;
    }
    vec_set_length(t, 0x10000);
    memcpy(c + C_LINE_DIR, t, 12);
    I32(c, C_FACING) = speed < 10000 ? 0x10000 : vec_dot(travel, d);
    straight = vec_dot(t, d);
    I32(c, C_STRAIGHT) = straight;
    I32(c, C_SCORE) += (int32_t)((uint32_t)(straight - 0x10000) << 6) >> 16;
}

/* 0x411b90: for skilled drivers on a curving line, candidates on other
 * lines whose node lies inside the curve score 3 more (and so do
 * following candidates on the same line). */
void GAMEAPI ai_mark_inside_curve(uint8_t *car, uint32_t line, uint32_t node)
{
    int32_t a[3], b[3], c[3], p[3], turn, inside = 0, i;
    uint16_t last = 0;

    if (car[0x6e1] <= 1)
        return;
    turn = line_crossing(line, node, a, b, c);
    if (abs32(turn) < 0x17d7)
        return;
    for (i = 0; i < CAND_COUNT; i++) {
        uint8_t *e = CAND(i);
        uint16_t l = U16(e, C_LINE);

        if (!l || l == (uint16_t)line)
            continue;
        if (l == last) {
            if (inside == 1)
                I32(e, C_SCORE) += 3;
            continue;
        }
        {
            const int16_t *q = (const int16_t *)NODE(LINE(l), e[C_NODE]);

            p[0] = shl16(q[0]);
            p[1] = shl16(q[1]);
            p[2] = shl16(q[2]);
        }
        last = l;
        if (point_between(p, a, b, c, turn)) {
            I32(e, C_SCORE) += 3;
            inside = 1;
        } else {
            inside = 0;
        }
    }
}

/* 0x411d80: nodes flagged 0x20 near a line's end: +20 if the car takes
 * shortcuts (flag 0x400) at least as hard as the node's grade, else -10. */
void GAMEAPI ai_score_line_ends(uint8_t *car, int32_t n)
{
    uint32_t shortcuts = U32(car, 0x6dc) >> 10 & 1, grade;
    int32_t i;

    switch (U32(car, 0x6e8) >> 18 & 7) {
    case 2:
    case 3:
        grade = 1;
        break;
    case 4:
    case 5:
        grade = 2;
        break;
    case 6:
    case 7:
        grade = 3;
        break;
    default:
        grade = 0;
    }
    for (i = CAND_COUNT - 1; i > n; i--) {
        uint8_t *e = CAND(i);
        uint16_t l = U16(e, C_LINE);
        const uint8_t *p;

        if (!l)
            continue;
        p = NODE(LINE(l), e[C_NODE]);
        if (!(p[8] & 0x20) || !line_node_near_end(l, e[C_NODE], 5))
            continue;
        if (shortcuts && (U32(p, 8) >> 28 & 3) <= grade)
            I32(e, C_SCORE) += 0x14;
        else
            I32(e, C_SCORE) -= 10;
    }
}

/* 0x411e90: each line's bias (line +6) for skilled drivers; only the
 * penalties in the first 350 ticks. */
void GAMEAPI ai_score_line_bias(uint8_t *car, int32_t n)
{
    int32_t early = g_race_ticks < 0x15e, i;

    if (car[0x6e1] < 3 || n >= 0x13)
        return;
    for (i = CAND_COUNT - 1; i > n; i--) {
        uint8_t *e = CAND(i);
        uint16_t l = U16(e, C_LINE);

        if (l) {
            int8_t bias = (int8_t)LINE(l)[6];

            if (!early || bias < 0)
                I32(e, C_SCORE) += bias;
        }
    }
}

/* A node's look-ahead grade (node +0xa, low 4 bits), one less for easy
 * drivers. */
static uint8_t node_grade(const uint8_t *p, uint8_t skill)
{
    uint8_t grade = (uint8_t)(U32(p, 8) >> 16) & 0xf;

    if (skill <= 1 && grade > 1)
        grade--;
    return grade;
}

/* 0x4120b0: add candidates along a line from a node (while *n >= 0,
 * counting down), as far ahead as the grades allow. */
void GAMEAPI ai_candidates_along(uint32_t line_index, uint32_t node, int32_t *n, uint32_t level, uint32_t skill)
{
    const uint8_t *line = LINE(line_index);
    const uint8_t *p;
    uint8_t *c;
    uint8_t out, lvl = (uint8_t)level, grade;
    uint16_t at = (uint16_t)node, other;

    if (*n < 0)
        return;
    out = line[3] != 0;
    c = CAND(*n);
    p = NODE(line, at);
    grade = node_grade(p, (uint8_t)skill);
    if (lvl >= grade)
        lvl = grade;
    U16(c, C_LINE) = (uint16_t)line_index;
    c[C_NODE] = (uint8_t)at;
    c[C_LEVEL] = lvl;
    c[C_OUT] = out;
    c[C_END] = 0;
    if (p[8] & 3)
        U16(c, C_STATE) = p[8] & 3;
    if (!line_prev(at, line, &other))
        c[C_OUT] = 1;
    if (--*n < 0)
        return;
    while (lvl && line_next(at, line, &other)) {
        int32_t next;

        at = other;
        p = NODE(line, at);
        grade = node_grade(p, (uint8_t)skill);
        next = (int32_t)lvl - 1;
        lvl = next < grade ? (uint8_t)next : grade;
        c = CAND(*n);
        c[C_LEVEL] = lvl;
        U16(c, C_LINE) = (uint16_t)line_index;
        c[C_NODE] = (uint8_t)at;
        c[C_OUT] = out;
        c[C_END] = 0;
        if (p[8] & 3)
            U16(c, C_STATE) = p[8] & 3;
        if (--*n < 0)
            return;
    }
    if (!line_next(at, line, &other)) {
        CAND(*n + 1)[C_OUT] = 1;
        CAND(*n + 1)[C_END] = 1;
    }
}

/* 0x411f00: candidates along the car's line from its node, then along
 * each line that a candidate's node links to (each line once). Returns
 * the index below the last candidate. */
int32_t GAMEAPI ai_collect_candidates(uint8_t *car, uint32_t line, uint32_t node)
{
    uint16_t seen[64];
    uint16_t *next_seen = seen + 1;
    uint8_t skill = car[0x6e1];
    int32_t n = 0x13, i;
    const uint8_t *p = NODE(LINE(line), node);

    memset(seen, 0, sizeof seen);
    seen[0] = (uint16_t)line;
    ai_candidates_along(line, node, &n, node_grade(p, skill), skill);
    for (i = 0x13; n < 0x13 && i > n; i--) {
        const uint8_t *c = CAND(i);
        uint16_t l = U16(c, C_LINE), link;
        uint8_t level;

        if (!l)
            continue;
        p = NODE(LINE(l), c[C_NODE]);
        level = c[C_LEVEL];
        link = p[0xc] & 0x1f;
        if (link && !u16_in_list(link, seen, 0x40)) {
            *next_seen++ = link;
            ai_candidates_along(link, U32(p, 0xc) >> 5 & 0x1ff, &n, level, skill);
        }
        link = U32(p, 0xc) >> 14 & 0x1f;
        if (link && !u16_in_list(link, seen, 0x40)) {
            *next_seen++ = link;
            ai_candidates_along(link, U32(p, 0xc) >> 19 & 0x1ff, &n, level, skill);
        }
    }
    return n;
}

/* 0x403200: the objects (of 36) within 10000 units ahead of the car
 * along its line, and every active one's squared distance. */
void GAMEAPI ai_find_objects_ahead(uint8_t *car)
{
    int16_t me[3] = {(int16_t)(I32(car, POSITION) >> 16), (int16_t)(I32(car, POSITION + 4) >> 16),
                     (int16_t)(I32(car, POSITION + 8) >> 16)};
    int32_t dir[3], v[3];
    uint16_t i;

    ai_line_direction(dir, g_car_slots + *(uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE);
    g_objects_ahead_count = 0;
    for (i = 0; i < 0x24; i++) {
        const uint8_t *entry = g_objects + i * 8;
        const uint8_t *object;
        int16_t d[3];
        int32_t length;

        if (!entry[4])
            continue;
        object = *(uint8_t **)entry;
        d[0] = (int16_t)((int16_t)(I32(object, POSITION) >> 16) - me[0]);
        d[1] = (int16_t)((int16_t)(I32(object, POSITION + 4) >> 16) - me[1]);
        d[2] = (int16_t)((int16_t)(I32(object, POSITION + 8) >> 16) - me[2]);
        length = svec_length_sq(d);
        g_object_distance[i] = length;
        if (length > 0x5f5e10)
            continue;
        v[0] = shl16(d[0]);
        v[1] = shl16(d[1]);
        v[2] = shl16(d[2]);
        if (vec_dot(dir, v) >= 0)
            g_objects_ahead[g_objects_ahead_count++] = (uint8_t)i;
    }
}

/* 0x403a40: the bodies (not kind 5) within 1000 units ahead of the car,
 * with their squared distances. */
void GAMEAPI ai_find_bodies_ahead(uint8_t *car)
{
    int16_t me[3] = {(int16_t)(I32(car, POSITION) >> 16), (int16_t)(I32(car, POSITION + 4) >> 16),
                     (int16_t)(I32(car, POSITION + 8) >> 16)};
    int32_t dir[3], v[3];
    uint32_t i;

    ai_line_direction(dir, g_car_slots + *(uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE);
    g_bodies_ahead_count = 0;
    for (i = 0; (i & 0xffff) < g_body_count; i++) {
        uint32_t k = i & 0xffff;
        const uint8_t *body = g_bodies[k * 3];
        int16_t d[3];
        int32_t length;

        if (body[0x19a] == 5)
            continue;
        d[0] = (int16_t)((int16_t)(I32(body, POSITION) >> 16) - me[0]);
        d[1] = (int16_t)((int16_t)(I32(body, POSITION + 4) >> 16) - me[1]);
        d[2] = (int16_t)((int16_t)(I32(body, POSITION + 8) >> 16) - me[2]);
        length = svec_length_sq(d);
        if (length > 1000000)
            continue;
        v[0] = shl16(d[0]);
        v[1] = shl16(d[1]);
        v[2] = shl16(d[2]);
        if (vec_dot(dir, v) >= 0) {
            g_body_distance[k] = length;
            g_bodies_ahead[g_bodies_ahead_count++] = (uint8_t)i;
        }
    }
}

/* 0x403620: the cost of following a line from a node for up to count
 * legs, each leg's cost by its length from `from`. */
int32_t GAMEAPI ai_line_cost(uint8_t *car, uint32_t line_index, uint32_t node, const int16_t *from,
                             uint32_t section, int32_t speed, uint32_t count)
{
    const uint8_t *line = LINE_IN(g_lines_copy, line_index);
    const int16_t *p = (const int16_t *)NODE(line, node), *q;
    int32_t total = 0;
    uint8_t legs = (uint8_t)count;
    uint16_t at = (uint16_t)node;

    if (!legs)
        return 0;
    do {
        int16_t a[3], b[3], d[3];

        if (!line_next(at, line, &at))
            return total;
        q = (const int16_t *)NODE(line, at);
        a[0] = p[0];
        a[1] = p[1];
        a[2] = p[2];
        d[0] = (int16_t)(a[0] - from[0]);
        d[1] = (int16_t)(a[1] - from[1]);
        d[2] = (int16_t)(a[2] - from[2]);
        b[0] = q[0];
        b[1] = q[1];
        b[2] = q[2];
        total += leg_cost(car, a, b, section, speed, svec_length_sq(d));
        p = q;
    } while (--legs);
    return total;
}

/* 0x42a260: hazards (pickups and the like) at candidates: rule a
 * candidate out, or make it costlier (+35), by the hazard's kind against
 * the car's aggression (+0x6e0). */
void GAMEAPI ai_score_hazards(uint8_t *car, int32_t n)
{
    uint32_t count = g_hazards[0xf6], k;
    const uint8_t *last = *(uint8_t **)(g_hazards + 0xf8) + count * 8 - 8;
    int32_t player = slot_player_flag(g_car_slots + *(uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE), i;

    for (i = CAND_COUNT - 1; i > n; i--) {
        uint8_t *e = CAND(i);

        if (e[C_MODE] == 4) {
            for (k = 0; k < count; k++) {
                const uint8_t *h = last - k * 8;

                if (h[4] != 1 || U32(h, 0) != e[C_HAZARD])
                    continue;
                if ((h[5] == 1 && player == 1) || car[0x6e0] >= h[6])
                    e[C_OUT] = 1;
                else
                    I32(e, C_SCORE) += 0x23;
            }
        } else if (e[C_MODE] == 0) {
            for (k = 0; k < count; k++) {
                const uint8_t *h = last - k * 8;
                uint8_t kind = h[4];

                if (kind == 1 || U16(h, 0) != U16(e, C_LINE) || abs32((int32_t)U16(h, 2) - e[C_NODE]) > 3)
                    continue;
                if (h[5] == 1 && player == 1) {
                    e[C_OUT] = 1;
                } else if (U16(car, 0x6b0) == U16(h, 0)) {
                    I32(e, C_SCORE) += 0x23;
                } else if (kind == 4 && !car_has_item4(car)) {
                    e[C_OUT] = 1;
                } else {
                    if (car[0x6e0] < h[6]) {
                        if (h[6] != 0xff)
                            I32(e, C_SCORE) += 0x23;
                    } else {
                        e[C_OUT] = 1;
                    }
                    if (h[4] == 4)
                        U16(e, C_STATE) = 0x20;
                }
            }
        }
    }
}

/* 0x412290 */
void GAMEAPI ai_choose_target(uint8_t *car)
{
    uint16_t line = U16(car, 0x6b0), node = U16(car, 0x6b2), last_line = 0;
    int16_t speed = S16(car, 0x658), p[3], a[3] = {0, 0, 0}, q[3];
    int32_t sq = I32(car, 0x654) * I32(car, 0x654), near_cost = 11000, clear = 11000, have_a = 0;
    int32_t node_at = (int32_t)(uintptr_t)NODE(LINE(line), node), n, travel[3], i, best, chosen;
    uint32_t section;
    uint8_t *c;

    ai_candidates_clear();
    ai_nearest_player(car);
    line_advance(LINE(line), &node, &node_at, VEC(car, POSITION));
    n = ai_collect_candidates(car, line, node);
    p[0] = (int16_t)(I32(car, POSITION) >> 16);
    p[1] = (int16_t)(I32(car, POSITION + 4) >> 16);
    p[2] = (int16_t)(I32(car, POSITION + 8) >> 16);
    memcpy(travel, car + VELOCITY, 12);
    if (speed > 1)
        vec_set_length(travel, 0x10000);
    section = *(uint16_t *)(*(uint8_t **)(car + 0x10) + 0x32);
    for (i = n + 1; i < CAND_COUNT; i++)
        if (U16(CAND(i), C_LINE) && !CAND(i)[C_OUT])
            ai_candidate_measure(i, VEC(car, POSITION), travel, I32(car, 0x654));
    ai_mark_inside_curve(car, line, node);
    ai_find_objects_ahead(car);
    ai_find_bodies_ahead(car);
    ai_avoid_cars(car, &n);

    /* rule out what the car can't take, favour staying on its line */
    for (i = n + 1; i < CAND_COUNT; i++) {
        uint8_t *e = CAND(i);
        uint16_t l = U16(e, C_LINE), min_speed;
        const uint8_t *np;

        if (!l)
            continue;
        np = NODE(LINE(l), e[C_NODE]);
        min_speed = (uint16_t)(np[6] << 1);
        if (min_speed && speed < (int32_t)min_speed) {
            e[C_OUT] = 1;
            continue;
        }
        if ((np[8] & 0x40) &&
            g_race[0x586] != g_car_slots[*(uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE + 0x2c]) {
            e[C_OUT] = 1;
            continue;
        }
        if (np[8] & 8) {
            if (l != line) {
                e[C_OUT] = 1;
                continue;
            }
            I32(e, C_SCORE) -= 0x6e;
        }
        if ((np[8] & 0x10) && g_race[0x553])
            I32(e, C_SCORE) += 0x14;
        if (l == line)
            I32(e, C_SCORE) += (np[8] & 4) ? 0xc : 2;
    }

    /* cost of getting there */
    for (i = n + 1; i < CAND_COUNT; i++) {
        uint8_t *e = CAND(i);
        int16_t *waypoint = g_waypoints + i * 4;
        uint16_t l = U16(e, C_LINE);
        const int16_t *np;
        int16_t d[3];
        int32_t length, cost;

        if (!l) {
            if (e[C_MODE])
                I32(e, C_SCORE) += leg_cost(car, p, g_set_points + e[C_NODE] * 4, section, speed, 0);
            continue;
        }
        np = (const int16_t *)NODE(LINE(l), e[C_NODE]);
        q[0] = np[0];
        q[1] = np[1];
        q[2] = np[2];
        d[0] = (int16_t)(q[0] - p[0]);
        d[1] = (int16_t)(q[1] - p[1]);
        d[2] = (int16_t)(q[2] - p[2]);
        length = svec_length_sq(d);
        if (l == last_line) {
            const int16_t *prev = (const int16_t *)NODE(LINE(l), CAND(i - 1)[C_NODE]);
            int16_t from[3] = {prev[0], prev[1], prev[2]};

            cost = leg_cost(car, q, from, section, speed, length) + I32(CAND(i - 1), C_COST);
        } else {
            last_line = l;
            if (!e[C_END] || (((const uint8_t *)np)[8] & 8))
                cost = ai_line_cost(car, l, e[C_NODE], p, section, speed, 3);
            else
                cost = ai_link_cost(l, e[C_NODE]);
        }
        I32(e, C_COST) = cost;
        if (e[C_OUT])
            continue;
        if (car[0x6e1] >= 3) {
            if (length < 0x64000 && e[C_LEVEL])
                I32(e, C_SCORE) -= 0x96;
        } else if (length > 0x9c400) {
            I32(e, C_SCORE) -= 10;
        } else if (length > 0x3d090) {
            I32(e, C_SCORE) -= 5;
        } else if (length > 0x9c40) {
            I32(e, C_SCORE) -= 2;
        }
        if (I32(e, C_FACING) >= 60000 || speed <= 100) {
            I32(e, C_SCORE) += leg_cost(car, p, q, section, speed, 0);
            continue;
        }
        {
            /* where the car will be in 4 steps, and a point past that
             * towards the candidate */
            int32_t w[3] = {shl16(d[0]), shl16(d[1]), shl16(d[2])};
            int32_t ax, ay, az;

            vec_set_length(w, (int32_t)((uint32_t)I32(car, 0x654) << 3));
            ax = I32(car, POSITION) + (int32_t)((uint32_t)I32(car, VELOCITY) << 2);
            ay = I32(car, POSITION + 4) + (int32_t)((uint32_t)I32(car, VELOCITY + 4) << 2);
            az = I32(car, POSITION + 8) + (int32_t)((uint32_t)I32(car, VELOCITY + 8) << 2);
            if (!have_a) {
                a[0] = (int16_t)(ax >> 16);
                a[1] = (int16_t)(ay >> 16);
                a[2] = (int16_t)(az >> 16);
                have_a = 1;
            }
            waypoint[0] = (int16_t)(((int32_t)((uint32_t)I32(car, VELOCITY) << 3) + ax + w[0]) >> 16);
            waypoint[1] = (int16_t)(((int32_t)((uint32_t)I32(car, VELOCITY + 4) << 3) + ay + w[1]) >> 16);
            waypoint[2] = (int16_t)(((int32_t)((uint32_t)I32(car, VELOCITY + 8) << 3) + az + w[2]) >> 16);
        }
        if (near_cost == 11000)
            near_cost = leg_cost(car, p, a, section, speed, 0);
        I32(e, C_SCORE) += near_cost;
        I32(e, C_SCORE) += leg_cost(car, a, waypoint, section, speed, sq >> 10);
        I32(e, C_SCORE) += leg_cost(car, waypoint, q, section, speed, sq >> 8);
    }

    /* blocked legs */
    for (i = n + 1; i < CAND_COUNT; i++) {
        uint8_t *e = CAND(i);
        int16_t *waypoint = g_waypoints + i * 4;
        uint16_t l = U16(e, C_LINE);
        int32_t blocked;

        if (e[C_OUT])
            continue;
        if (!l) {
            if (e[C_MODE])
                I32(e, C_SCORE) += leg_blocked(p, g_set_points + e[C_NODE] * 4, section, speed, 0);
            continue;
        }
        {
            const int16_t *np = (const int16_t *)NODE(LINE(l), e[C_NODE]);

            q[0] = np[0];
            q[1] = np[1];
            q[2] = np[2];
        }
        if (I32(e, C_FACING) < 60000 && speed > 100) {
            if (clear == 11000)
                clear = leg_blocked(p, a, section, speed, 0);
            blocked = clear;
            if (!blocked) {
                blocked = leg_blocked(a, waypoint, section, speed, sq >> 10);
                if (!blocked)
                    blocked = leg_blocked(waypoint, q, section, speed, sq >> 8);
            }
        } else {
            blocked = leg_blocked(p, q, section, speed, 0);
        }
        I32(e, C_SCORE) += blocked;
    }

    ai_add_set_points(car, &n);
    ai_score_hazards(car, n);
    ai_score_line_bias(car, n);
    ai_score_line_ends(car, n);

    chosen = -1;
    best = -50000;
    if (n < 0x13) {
        for (i = 0x13; i > n; i--) {
            const uint8_t *e = CAND(i);

            if ((U16(e, C_LINE) || e[C_MODE]) && I32(e, C_SCORE) + I32(e, C_COST) > best && !e[C_OUT]) {
                best = I32(e, C_SCORE) + I32(e, C_COST);
                chosen = i;
            }
        }
    }
    if ((int16_t)chosen == -1)
        chosen = 0x13;
    c = CAND((int16_t)chosen);
    if (c[C_MODE]) {
        uint8_t point = c[C_NODE];

        car[0x6b9] = c[C_MODE];
        I32(car, 0x6bc) = shl16(g_set_points[point * 4]);
        I32(car, 0x6c0) = shl16(g_set_points[point * 4 + 1]);
        I32(car, 0x6c4) = shl16(g_set_points[point * 4 + 2]);
        if (c[C_MODE] == 1) {
            int32_t limit = g_set_point_limit[point], lower = (int16_t)car[0x6ba] - 1;

            if (lower < limit)
                limit = lower;
            if ((int16_t)limit < 0)
                limit = 0;
            car[0x6ba] = (uint8_t)limit;
            car[0x6bb] = 3;
            return;
        }
        if (car[0x6bb])
            car[0x6bb]--;
        return;
    }
    car[0x6b9] = 0;
    car[0x6ba] = 0x1f;
    if (car[0x6bb])
        car[0x6bb]--;
    if (U16(c, C_STATE))
        U16(car, 0x6b4) = U16(c, C_STATE);
    U16(car, 0x6b0) = U16(c, C_LINE);
    U16(car, 0x6b2) = c[C_NODE];
}
