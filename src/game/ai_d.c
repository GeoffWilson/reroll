/* The AI reacting to nearby cars (0x480d70, from the AI's target choice):
 * for each car close by, maybe dodge (lowering the scores of the route
 * records that pass it), follow it, or pick a point to overtake it. New
 * target points go in a table of 20 (0x6cf180, 3 int16 each, count
 * 0x4b7e50, weight 0x6cf640) tied to the route records (0x6cf220, 0x34
 * bytes each: +0 line, +2 target, +4 direction, +0x25 kind, +0x26 used,
 * +0x2c score) counted down by *slots.
 *
 * Racing lines link to others at a node (node +0xc: line in bits 0..4,
 * node in 5..13; a second link in bits 14..18, node 19..27); a small
 * queue (0x4d5808 head, 0x4d580a tail, 20 entries) walks those links. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_lines (*(uint8_t **)(*(uint8_t **)0x6c1e30 + 0x60))
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

#define LINE(i) (g_lines + (uint16_t)(i) * 0xc)
#define NODE(line, i) (*(uint8_t **)((line) + 8) + (uint16_t)(i) * 0x10)
#define S16(p, off) (*(int16_t *)((p) + (off)))

#define g_queue_head (*(uint16_t *)0x4d5808)
#define g_queue_tail (*(uint16_t *)0x4d580a)
#define g_visited ((uint16_t *)0x4d580c)
#define g_queue_line ((uint16_t *)0x5d63c0)
#define g_queue_node ((uint16_t *)0x5d6380)
#define QUEUE_SIZE 0x14

#define g_target_count (*(uint8_t *)0x4b7e50)
#define g_targets ((int16_t *)0x6cf180)
#define g_target_weight ((uint8_t *)0x6cf640)
#define g_routes ((uint8_t *)0x6cf220)
#define ROUTE_SIZE 0x34
#define ROUTE_COUNT 20
#define g_dodge_odds ((const uint16_t *)0x4a7c68)

/* 0x47f1b0..0x47f270: the link queue. */
void GAMEAPI link_queue_reset(void)
{
    g_queue_head = 0;
    g_queue_tail = 0;
}

void GAMEAPI link_queue_push(uint32_t line, uint32_t node)
{
    uint16_t tail = g_queue_tail;

    if (tail == (int32_t)g_queue_head - 1 || (tail == 0x13 && !g_queue_head))
        return;
    g_queue_line[tail] = (uint16_t)line;
    g_queue_node[tail] = (uint16_t)node;
    tail++;
    g_queue_tail = tail == QUEUE_SIZE ? 0 : tail;
}

void GAMEAPI link_queue_pop(uint16_t *line, uint16_t *node)
{
    *line = g_queue_line[g_queue_head];
    *node = g_queue_node[g_queue_head];
    g_queue_head++;
    if (g_queue_head == QUEUE_SIZE)
        g_queue_head = 0;
}

int32_t GAMEAPI link_queue_empty(void)
{
    return g_queue_tail == g_queue_head;
}

/* 0x47f430: is value among the first n of list? (0 never is.) */
int32_t GAMEAPI u16_list_contains(uint32_t value, const uint16_t *list, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        if ((uint16_t)value == list[i])
            return 1;
        if (!(uint16_t)value)
            return 0;
    }
    return 0;
}

/* 0x47f610: a body's sideways axis (16.16). */
void GAMEAPI body_side(int32_t *out, const uint8_t *body)
{
    out[0] = (int32_t)((uint32_t)S16(body, MATRIX) << 4);
    out[1] = (int32_t)((uint32_t)S16(body, MATRIX + 6) << 4);
    out[2] = (int32_t)((uint32_t)S16(body, MATRIX + 12) << 4);
}

/* 0x47fcf0: the line direction at a car slot's planned node. */
void GAMEAPI slot_plan_direction(int32_t *out, const uint8_t *slot)
{
    line_direction(out, (*(uint8_t **)(slot + 0x64))[5], *(const uint16_t *)(slot + 0x68));
}

/* 0x47f9f0: how sharply the line bends at a node (signed by the turn's
 * up component): pos = the node, back and ahead = unit vectors from the
 * node two before and to the node two after. 0 at an end. */
int32_t GAMEAPI line_bend(uint32_t line_index, uint32_t node, int32_t *pos, int32_t *back, int32_t *ahead)
{
    const uint8_t *line = LINE(line_index);
    const int16_t *p = (const int16_t *)NODE(line, node), *q, *r;
    uint16_t prev, prev2, next, next2;
    int32_t c[3], sum;

    if (!line_prev(node, line, &prev))
        return 0;
    q = (const int16_t *)NODE(line, line_prev(prev, line, &prev2) ? prev2 : prev);
    if (!line_next(node, line, &next))
        return 0;
    r = (const int16_t *)NODE(line, line_next(next, line, &next2) ? next2 : next);
    pos[0] = (int32_t)((uint32_t)p[0] << 16);
    pos[1] = (int32_t)((uint32_t)p[1] << 16);
    pos[2] = (int32_t)((uint32_t)p[2] << 16);
    back[0] = pos[0] - (int32_t)((uint32_t)q[0] << 16);
    back[1] = pos[1] - (int32_t)((uint32_t)q[1] << 16);
    back[2] = pos[2] - (int32_t)((uint32_t)q[2] << 16);
    vec_set_length(back, 0x10000);
    ahead[0] = (int32_t)((uint32_t)r[0] << 16) - pos[0];
    ahead[1] = (int32_t)((uint32_t)r[1] << 16) - pos[1];
    ahead[2] = (int32_t)((uint32_t)r[2] << 16) - pos[2];
    vec_set_length(ahead, 0x10000);
    vec_cross(back, ahead, c);
    sum = (c[2] * c[2] >> 16) + (c[1] * c[1] >> 16) + (c[0] * c[0] >> 16);
    return c[1] < 0 ? -sum : sum;
}

/* 0x4819c0: closing / along (16.16), or 0x70000000 if along is tiny or
 * the result would not fit in 15 bits. */
int32_t GAMEAPI approach_rate(int32_t closing, int32_t along)
{
    if (abs32(along) < 100)
        return 0x70000000;
    if (abs32(fix_div16(closing >> 16, along)) & 0xffff8000)
        return 0x70000000;
    return fix_div16(closing, along);
}

/* 0x481c80: which side of the line at a node pos is (cross product of
 * the line's direction there with the way to pos, in the ground plane);
 * backwards takes the previous node first. 0 for a lone node. */
int32_t GAMEAPI line_side(uint32_t line_index, uint32_t node, uint32_t backwards, const int32_t *pos)
{
    const uint8_t *line = LINE(line_index);
    const int16_t *p = (const int16_t *)NODE(line, node), *q;
    int32_t a[3], base[3], t[3], r[3];
    uint16_t other;
    int from_other;

    a[0] = (int32_t)((uint32_t)p[0] << 16);
    a[1] = (int32_t)((uint32_t)p[1] << 16);
    a[2] = (int32_t)((uint32_t)p[2] << 16);
    if (!(uint8_t)backwards) {
        if (line_next(node, line, &other))
            from_other = 0;
        else if (line_prev(node, line, &other))
            from_other = 1;
        else
            return 0;
    } else {
        if (line_prev(node, line, &other))
            from_other = 1;
        else if (line_next(node, line, &other))
            from_other = 0;
        else
            return 0;
    }
    q = (const int16_t *)NODE(line, other);
    if (from_other) {
        base[0] = (int32_t)((uint32_t)q[0] << 16);
        base[1] = (int32_t)((uint32_t)q[1] << 16);
        base[2] = (int32_t)((uint32_t)q[2] << 16);
        t[0] = a[0] - base[0];
        t[1] = a[1] - base[1];
        t[2] = a[2] - base[2];
    } else {
        memcpy(base, a, 12);
        t[0] = (int32_t)((uint32_t)q[0] << 16) - a[0];
        t[1] = (int32_t)((uint32_t)q[1] << 16) - a[1];
        t[2] = (int32_t)((uint32_t)q[2] << 16) - a[2];
    }
    r[0] = (base[0] - pos[0]) >> 8;
    r[2] = (base[2] - pos[2]) >> 8;
    t[0] >>= 8;
    t[2] >>= 8;
    return mul16(r[0], t[2]) - mul16(r[2], t[0]);
}

/* Queue a node's links (not yet visited) for 0x481a10. */
static uint16_t *queue_links(const uint8_t *node, uint16_t *visit)
{
    uint32_t links = U32(node, 0xc), line = links & 0x1f;

    if (line && !u16_list_contains(line, g_visited, 0x40)) {
        *visit++ = (uint16_t)line;
        link_queue_push(line, links >> 5 & 0x1ff);
    }
    links = U32(node, 0xc);
    line = links >> 14 & 0x1f;
    if (line && !u16_list_contains(line, g_visited, 0x40)) {
        *visit++ = (uint16_t)line;
        link_queue_push(line, U32(node, 0xc) >> 19 & 0x1ff);
    }
    return visit;
}

/* 0x481a10: does any line linked (directly or through others) from the
 * car's planned node run on the other side of the car from it? */
int32_t GAMEAPI ai_route_crosses(uint8_t *car)
{
    const uint8_t *slot = g_car_slots + *(uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE;
    const int32_t *pos = VEC(car, POSITION);
    uint16_t line_index = (*(uint8_t **)(slot + 0x64))[5], node = *(const uint16_t *)(slot + 0x68);
    const uint8_t *line = LINE(line_index), *at = NODE(line, node);
    uint32_t backwards;
    int32_t side;
    uint16_t *visit;

    memset(g_visited + 1, 0, 0x7e);
    g_visited[0] = line_index;
    link_queue_reset();
    backwards = !node_passed(node, line, pos);
    side = line_side(line_index, node, backwards, pos);
    visit = queue_links(at, g_visited + 1);
    while (!link_queue_empty()) {
        link_queue_pop(&line_index, &node);
        if (bit27_clear((uint32_t)line_side(line_index, node, backwards, pos)) != bit27_clear((uint32_t)side))
            return 1;
        visit = queue_links(NODE(LINE(line_index), node), visit);
    }
    return 0;
}

/* 0x481800: dodge another car: route records on its line (`line`) gain
 * or lose by how fast this car closes on it, and every unused route
 * whose direction at this car's speed passes near it loses (more the
 * nearer; scaled by the approach rate). */
void GAMEAPI ai_dodge_scores(const uint8_t *car, const uint8_t *other, const int32_t *d, int32_t dist,
                             int32_t rate, uint32_t line)
{
    int32_t speed = I32(car, 0x654), closing = speed - I32(other, 0x654), adjust, k;
    int32_t width = *(const uint16_t *)(*(uint8_t **)(car + 0x10) + 0x32), width_sq = width * width;

    if (closing < 0) {
        adjust = 0;
    } else if (dist <= width * 4) {
        adjust = -200;
    } else {
        int32_t high = (int32_t)((uint32_t)(dist - width * 2) << 12), low = high >> 2;

        if (closing > high)
            adjust = -200;
        else if (closing < low)
            adjust = 0;
        else
            adjust = (low - closing) * 200 / (high - low);
    }
    if (rate > 0x10000)
        rate = 0x10000;
    for (k = 0; k < ROUTE_COUNT; k++) {
        uint8_t *route = g_routes + k * ROUTE_SIZE;
        uint16_t route_line = *(uint16_t *)route;
        int32_t p[3], c[3], sq;
        int16_t s[3];

        if (!route_line || route[0x26])
            continue;
        if (route_line == line)
            I32(route, 0x2c) += adjust;
        vec_scale(speed, VEC(route, 4), p);
        p[0] -= I32(other, VELOCITY);
        p[1] -= I32(other, VELOCITY + 4);
        p[2] -= I32(other, VELOCITY + 8);
        vec_set_length(p, 0x10000);
        vec_cross(d, p, c);
        s[0] = (int16_t)(c[0] >> 16);
        s[1] = (int16_t)(c[1] >> 16);
        s[2] = (int16_t)(c[2] >> 16);
        sq = svec_length_sq(s);
        if (sq > width_sq * 4)
            continue;
        if (sq <= width_sq)
            I32(route, 0x2c) += -(rate >> 4);
        else
            I32(route, 0x2c) += (width_sq * 4 - sq) * (rate >> 4) / -(width_sq * 3);
    }
}

/* Store a target point for route record *slots. */
static void add_target(const uint8_t *car, const int32_t *offset, int32_t *slots, uint8_t kind, uint8_t weight)
{
    uint8_t n = g_target_count;
    uint8_t *route = g_routes + *slots * ROUTE_SIZE;

    g_targets[n * 4] = (int16_t)((uint32_t)(I32(car, POSITION) + offset[0]) >> 16);
    g_targets[n * 4 + 1] = (int16_t)((uint32_t)(I32(car, POSITION + 4) + offset[1]) >> 16);
    g_targets[n * 4 + 2] = (int16_t)((uint32_t)(I32(car, POSITION + 8) + offset[2]) >> 16);
    route[2] = n;
    route[0x25] = kind;
    g_target_weight[n] = weight;
    I32(route, 0x2c) += kind == 2 ? 0x1c : 0x1f;
    (*slots)--;
    g_target_count++;
}

/* 0x4814f0: follow a car ahead: alongside it (if not much faster and
 * close) at its side, else aim where it will be. Only when lined up with
 * the plan and allowed by the cars' +0x6ac/+0x2280 states. */
void GAMEAPI ai_follow_target(const uint8_t *my_slot, const uint8_t *other_slot, const int32_t *d, int32_t dist,
                              int32_t width, int32_t along, int32_t rate, int32_t *slots)
{
    const uint8_t *car = *(uint8_t **)my_slot, *other = *(uint8_t **)other_slot;
    int32_t faster = S16(car, 0x658) - S16(other, 0x658), dir[3], forward[3], offset[3];

    if (*slots < 0 || g_target_count >= 0x14)
        return;
    if (I32(car, 0x6ac) == -1 && (I32(other, 0x6ac) != -1 || I32(other, 0x2280) - I32(car, 0x2280) >= 2))
        return;
    slot_plan_direction(dir, my_slot);
    body_forward(forward, car);
    if (vec_dot(dir, forward) < 0xf08f)
        return;
    if (along < (int32_t)((uint32_t)width << 16)) {
        int32_t side[3], k;

        if (faster > 0x32 || dist > 0x4000000)
            return;
        k = (0x10000000 - dist * 2 - dist) >> 12;
        body_side(side, car);
        if (mul16(d[0], I32(car, VELOCITY + 8) >> 2) - mul16(d[2], I32(car, VELOCITY) >> 2) < 0) {
            side[0] = -side[0];
            side[1] = -side[1];
            side[2] = -side[2];
        }
        vec_scale(dist, side, offset);
        add_target(car, offset, slots, 1, (uint8_t)((0x14000 - k) >> 15));
    } else {
        int32_t weight, shift;

        if (rate < 4000) {
            weight = 0x10000;
            shift = 4;
        } else if (rate < 8000) {
            weight = 0xc000;
            shift = 3;
        } else {
            weight = 0x8000;
            shift = 1;
        }
        offset[0] = d[0] + (int32_t)((uint32_t)I32(other, VELOCITY) << shift);
        offset[1] = d[1] + (int32_t)((uint32_t)I32(other, VELOCITY + 4) << shift);
        offset[2] = d[2] + (int32_t)((uint32_t)I32(other, VELOCITY + 8) << shift);
        add_target(car, offset, slots, 1, (uint8_t)(weight >> 14));
    }
}

/* 0x481160: overtake a car close ahead on a straight bit of both cars'
 * lines with no crossing route: a point beside it, by which side of this
 * car it is and how it steers. Otherwise routes on its line gain. */
void GAMEAPI ai_overtake_target(const int32_t *d, int32_t dist, int32_t rate, const uint8_t *my_slot,
                                const uint8_t *other_slot, int32_t along, int32_t *slots)
{
    uint8_t *car = *(uint8_t **)my_slot, *other = *(uint8_t **)other_slot;
    uint16_t my_line = *(uint16_t *)(car + 0x6b0), my_node = *(uint16_t *)(car + 0x6b2), other_line, other_node;
    int32_t my_at, other_at, pos[3], back[3], ahead[3], bend_mine, bend_other, forward[3], s, t, k;

    (void)rate;
    (void)along;
    if (other[0x70c] == 8) {
        other_line = *(uint16_t *)(other + 0x6b0);
        other_node = *(uint16_t *)(other + 0x6b2);
    } else {
        other_line = (*(uint8_t **)(other_slot + 0x64))[5];
        other_node = *(const uint16_t *)(other_slot + 0x68);
    }
    my_at = (int32_t)(uintptr_t)NODE(LINE(my_line), my_node);
    other_at = (int32_t)(uintptr_t)NODE(LINE(other_line), other_node);
    line_advance(LINE(my_line), &my_node, &my_at, VEC(car, POSITION));
    line_advance(LINE(other_line), &other_node, &other_at, VEC(other, POSITION));
    bend_mine = line_bend(my_line, my_node, pos, back, ahead);
    bend_other = line_bend(other_line, other_node, pos, back, ahead);
    if (dist >= 500 || abs32(bend_mine) >= 0x2540 || abs32(bend_other) >= 0x2540 || !ai_route_crosses(car)) {
        uint8_t plan_line = (*(uint8_t **)(other_slot + 0x64))[5];

        for (k = 0; k < ROUTE_COUNT; k++) {
            uint8_t *route = g_routes + k * ROUTE_SIZE;

            if (*(uint16_t *)route == plan_line && !route[0x26])
                I32(route, 0x2c) += 0xe;
        }
        return;
    }
    if (*slots < 0 || g_target_count >= 0x14)
        return;
    slot_plan_direction(ahead, my_slot);
    body_forward(pos, car);
    if (vec_dot(ahead, pos) < 55000)
        return;
    body_forward(forward, other);
    if (vec_dot(ahead, forward) < 55000)
        return;
    memcpy(back, d, 12);
    vec_div(back, (int32_t)((uint32_t)dist << 16));
    s = mul16(back[0], ahead[2]) - mul16(back[2], ahead[0]);
    t = mul16(mul16(s, abs32(s)), abs32(s)) + (I32(other, 0x634) >> 1);
    if (t > 0x8000)
        t = 0x8000;
    else if (t < -0x8000)
        t = -0x8000;
    body_side(back, car);
    vec_scale(t, back, back);
    ahead[0] = back[0] + forward[0];
    ahead[1] = back[1] + forward[1];
    ahead[2] = back[2] + forward[2];
    vec_scale(I32(car, 0x654), ahead, ahead);
    ahead[0] = (int32_t)((uint32_t)ahead[0] << 6);
    ahead[1] = (int32_t)((uint32_t)ahead[1] << 6);
    ahead[2] = (int32_t)((uint32_t)ahead[2] << 6);
    add_target(car, ahead, slots, 2, 0x1f);
}

/* 0x480d70: look at every car within 2000 units: dodge (flag 0x20) one
 * ahead that this car is closing on, follow one ahead, overtake one
 * behind-ish that is no faster. Easy drivers (and skill 3 near a car
 * just behind) also react to AI cars. */
void GAMEAPI ai_react_to_cars(uint8_t *car, int32_t *slots)
{
    const uint8_t *my_slot = g_car_slots + *(uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE;
    int32_t width = *(const uint16_t *)(*(uint8_t **)(car + 0x10) + 0x32), loose = 0;
    uint16_t odds = g_dodge_odds[U32(car, 0x6e8) >> 12 & 7];
    uint8_t i;

    g_target_count = 0;
    if (car[0x6e1] <= 2) {
        loose = 1;
    } else if (car[0x6e1] != 4) {
        int32_t at = (int32_t)((uint32_t)my_slot[0x2c] << 16) + I32(my_slot, 0x34);
        uint8_t count = g_race[0x54d];

        for (i = 0; i < count; i++) {
            const uint8_t *slot = g_car_slots + i * CAR_SLOT_SIZE;
            int32_t gap;

            if ((*(uint8_t **)slot)[0x70c] == 8)
                continue;
            gap = (int32_t)((uint32_t)slot[0x2c] << 16) - at + I32(slot, 0x34);
            if (gap < 0 && gap > -1500) {
                loose = 1;
                break;
            }
        }
    }
    for (i = 0; i < g_race[0x54d]; i++) {
        const uint8_t *slot = g_car_slots + i * CAR_SLOT_SIZE;
        uint8_t *other = *(uint8_t **)slot;
        int32_t d[3], forward[3], rel[3], sq, dist, along, closing, rate, same_way;
        int16_t s[3];
        int pass;

        if (i == *(uint16_t *)(car + 0x64e))
            continue;
        d[2] = I32(other, POSITION + 8) - I32(car, POSITION + 8);
        d[0] = I32(other, POSITION) - I32(car, POSITION);
        d[1] = I32(other, POSITION + 4) - I32(car, POSITION + 4);
        s[2] = (int16_t)(d[2] >> 16);
        s[0] = (int16_t)(d[0] >> 16);
        s[1] = (int16_t)(d[1] >> 16);
        sq = svec_length_sq(s);
        if (sq > 0x3d0900)
            continue;
        dist = (int32_t)fix_sqrt_table(sq);
        body_forward(forward, car);
        along = vec_dot(forward, d);
        rel[0] = I32(car, VELOCITY) - I32(other, VELOCITY);
        rel[1] = I32(car, VELOCITY + 4) - I32(other, VELOCITY + 4);
        rel[2] = I32(car, VELOCITY + 8) - I32(other, VELOCITY + 8);
        closing = vec_dot(forward, rel);
        rate = approach_rate(closing, along);
        same_way = vec_dot(VEC(car, VELOCITY), VEC(other, VELOCITY));
        if ((car[0x6dc] & 0x20) && along > (int32_t)((uint32_t)width << 16) && closing > 0 && rate > 1000)
            ai_dodge_scores(car, other, d, dist, rate, (*(uint8_t **)(slot + 0x64))[5]);
        if (((car[0x6dc] & 1) || *(uint16_t *)(car + 0x6ec + i * 2) > odds) && same_way > 0 &&
            along > (int32_t)((uint32_t)-width << 16) &&
            (along < (int32_t)((uint32_t)width << 16) || rate > 2000) && S16(other, 0x658) > 0xfa &&
            (loose || other[0x70c] != 8)) {
            pass = car[0x6b9] == 1 ? car[0x6ba] != 0 : car[0x6bb] == 0;
            if (pass)
                ai_follow_target(my_slot, slot, d, (int32_t)((uint32_t)dist << 16), width, along, rate, slots);
        }
        if (((car[0x6dc] & 2) || *(uint16_t *)(car + 0x6ec + i * 2) > odds) &&
            along < (int32_t)((uint32_t)-width << 16) && same_way >= 0 &&
            S16(car, 0x658) <= S16(other, 0x658) + 5 && (rate > 2000 || dist < 2000) &&
            (loose || other[0x70c] != 8))
            ai_overtake_target(d, dist, rate, my_slot, slot, along, slots);
    }
}
