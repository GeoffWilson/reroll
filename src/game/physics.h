/* Rigid-body physics (physics.c). Addresses are the originals. */
#ifndef REROLL_PHYSICS_H
#define REROLL_PHYSICS_H

#include "../runtime.h"

int32_t GAMEAPI physics_world_step(int32_t dt);                         /* 0x461410 */
void GAMEAPI bodies_begin_step(void);                                   /* 0x45c0c0 */
void GAMEAPI bodies_apply_forces(void);                                 /* 0x462160 */
void GAMEAPI bodies_prepare(void);                                      /* 0x4621b0 */
void GAMEAPI body_move(uint8_t *body, int32_t dt, const int32_t *push); /* 0x461310 */
void GAMEAPI body_move_simple(uint8_t *body, int32_t dt);               /* 0x4621f0 */
void GAMEAPI bodies_advance_to(int32_t dt);                             /* 0x461ef0 */
int32_t GAMEAPI event_next(uint8_t **event);                            /* 0x462060 */
void GAMEAPI collision_order(uint8_t *event, int32_t *time);            /* 0x461510 */
int32_t GAMEAPI event_queue_drop_bodies(uint8_t *a, uint8_t *b);        /* 0x4620a0 */
int32_t GAMEAPI collisions_find_for(uint8_t *a, uint8_t *b, int32_t dt); /* 0x418960 */
int32_t GAMEAPI collisions_find_all(int32_t dt);                        /* 0x4188c0 */

void GAMEAPI quat_normalise(int32_t *q);                                /* 0x480430 */
void GAMEAPI quat_normalise_small(int32_t *q);                          /* 0x480490 */
void GAMEAPI quat_normalise_large(int32_t *q);                          /* 0x480580 */
void GAMEAPI quat_to_matrix(const int32_t *q, int16_t *m);              /* 0x480780 */
void GAMEAPI body_integrate(uint8_t *body, int32_t dt);                 /* 0x465a60 */
uint8_t *GAMEAPI group_add(uint8_t *body, uint32_t cell, uint8_t *groups, int32_t index); /* 0x45c300 */
void GAMEAPI body_groups_build(void);                                   /* 0x45c180 */


/* forces.c */
void GAMEAPI body_forces(uint8_t *body);                                              /* 0x466520 */
void GAMEAPI shake_start(const int32_t *origin, int32_t kind);                        /* 0x42abe0 */
void GAMEAPI body_explosion_damping(uint8_t *body);                                   /* 0x466e40 */
void GAMEAPI car_drag(uint8_t *car);                                                  /* 0x40e460 */
void GAMEAPI car_gravity(uint8_t *car);                                               /* 0x40e4b0 */
int32_t GAMEAPI body_ground_contacts(uint8_t *body);                                  /* 0x419340 */
void GAMEAPI contact_normal_force(uint8_t *body, uint8_t *contact, int32_t *out, int32_t *size); /* 0x466120 */
void GAMEAPI body_add_force_torque(uint8_t *body, const int32_t *force, const int32_t *torque);  /* 0x466240 */
void GAMEAPI vec_set_length(int32_t *v, int32_t length);                              /* 0x480220 */
void GAMEAPI vec_set_length_small(int32_t *v, int32_t length);                        /* 0x480360 */
void GAMEAPI vec_set_length_large(int32_t *v, int32_t length);                        /* 0x480270 */
void GAMEAPI contact_friction(uint8_t *body, const int32_t *normal_force, const int32_t *slip, int32_t *force,
                              int32_t *torque, int32_t parts);                         /* 0x465d50 */
void GAMEAPI body_apply_accumulated(uint8_t *body, int32_t dt);                       /* 0x465bd0 */
void GAMEAPI body_update_velocity(uint8_t *at_position);                              /* 0x465890 */

/* wheels.c */
void GAMEAPI cars_prepare(void);                                                      /* 0x466710 */
void GAMEAPI body_prepare(uint8_t *at_position);                                      /* 0x465740 */
void GAMEAPI mat_mul(const int32_t *a, const int32_t *b, int32_t *out);              /* 0x4800f0 */
void GAMEAPI mat_diag_mul_transpose(const int32_t *d, const int16_t *m, int32_t *out); /* 0x4801a0 */
int32_t GAMEAPI sin_table(int32_t angle);                                             /* 0x480b10 */
void GAMEAPI car_steering(uint8_t *car);                                              /* 0x40e220 */
void GAMEAPI car_suspension(uint8_t *car);                                            /* 0x475e80 */
void GAMEAPI car_settle(uint8_t *car);                                                /* 0x466840 */
void GAMEAPI car_roll_damping(uint8_t *car);                                          /* 0x466950 */
void GAMEAPI car_self_right(uint8_t *car);                                            /* 0x4669e0 */

/* drive.c */
void GAMEAPI car_drive(uint8_t *car);                                                 /* 0x40d190 */
void GAMEAPI grip_level(uint8_t *car, int32_t *grip, int32_t *shift);                 /* 0x40d490 */
int32_t GAMEAPI surface_kind(uint8_t *car);                                           /* 0x40d6f0 */
int32_t GAMEAPI tyre_grip(int32_t torque, uint8_t *wheel, const int32_t *velocity, int32_t *force, int32_t index,
                          int32_t grip, int32_t surface);                             /* 0x40d760 */
void GAMEAPI wheel_spin(uint8_t *wheel, int32_t road, int32_t torque);                /* 0x40d8a0 */

/* track.c */
void GAMEAPI track_contacts_find(int32_t dt);                                         /* 0x419550 */
uint8_t *GAMEAPI track_cell(int32_t x, int32_t y, int32_t z, int32_t *off_track);     /* 0x419420 */
uint8_t *GAMEAPI track_nearest_cell(uint8_t *rows, int16_t z, uint32_t x);            /* 0x461030 */
void GAMEAPI body_object_contacts(uint8_t *body, uint8_t *slot, int32_t dt);          /* 0x41a0f0 */
void GAMEAPI slot_follow(uint8_t *body, uint8_t *slot, int32_t dt, int32_t slow);
void GAMEAPI body_slots_at(uint8_t *body, int32_t t);
int32_t GAMEAPI bodies_related(const uint8_t *a, const uint8_t *b);
void GAMEAPI event_add(uint8_t *a, uint8_t *b, const uint8_t *contact, uint32_t *type, int16_t surface,
                       int32_t *velocity, int32_t t);
void GAMEAPI body_track_sweep(uint8_t *body, int32_t dt);
void GAMEAPI bodies_track_sweep(uint8_t **bodies, int32_t count, int32_t t);
int32_t GAMEAPI body_track_contact(uint8_t *body, uint8_t *contact, int32_t *count, int32_t resting);
int32_t GAMEAPI group_contacts(void *group, int32_t t);
int32_t GAMEAPI group_pair_contacts(void *group, void *other, int32_t t);
int32_t GAMEAPI sphere_pair_bounds(const uint8_t *a, const uint8_t *b, uint32_t *type, int32_t dt);
int32_t GAMEAPI sphere_pair_contact(const uint8_t *a, const uint8_t *b, uint32_t *type, int32_t *point,
                                    int32_t *normal, int32_t *depth, int32_t dt);
int32_t GAMEAPI body_pair_test(uint8_t *a, uint8_t *b, uint32_t *type, uint8_t *contact, int32_t *velocity,
                               int32_t dt);
int32_t GAMEAPI face_edges_test(const int16_t *corners, uint32_t shape, const int32_t *n, const int32_t *p);
int32_t GAMEAPI object_polygon_sweep(uint8_t *body, uint8_t *object, const uint8_t *polygon, uint8_t *slot,
                                     int32_t *out, int32_t dt);
int32_t GAMEAPI slot_object_sweep(uint8_t *body, uint8_t *slot, int32_t *out, int32_t limit);
void GAMEAPI body_object_hits(uint8_t *body);
void GAMEAPI probe_place(uint8_t *car, const uint8_t *wheel, int32_t *probe, uint8_t *slot);
void GAMEAPI probes_place(int32_t x, int32_t z, uint8_t *car, uint8_t *probes, uint8_t *slots);
void GAMEAPI probes_sweep(uint8_t *car, uint8_t *slots, uint8_t *hits, uint32_t *times);
void GAMEAPI probe_contact_velocity(uint8_t *car, const uint8_t *wheel, uint8_t *probe);
int32_t GAMEAPI probe_result(uint8_t *car, uint8_t *probe, uint8_t *wheel, int32_t t, const uint8_t *slot,
                             const int32_t *hit);
void GAMEAPI car_ground_probes(uint8_t *car);
int32_t GAMEAPI track_direction_sign(const int16_t *a, const int16_t *b, uint32_t car_index);
int32_t GAMEAPI track_special_section(const uint8_t *car);
int32_t GAMEAPI track_direction(int32_t *dir, int32_t *up, const uint8_t *car);
void GAMEAPI car_spin_around(uint8_t *car);
int32_t GAMEAPI vec_set_length_shifted(int32_t *v, int32_t length);
void GAMEAPI wheel_force_torque(int32_t *force, int32_t *torque, const uint8_t *wheel, const uint8_t *car_position);
void GAMEAPI cars_wheel_forces(void);
void GAMEAPI vec_step_diff(int32_t *out, const int32_t *a, const int32_t *b, int32_t scale);
void GAMEAPI spline_velocity(int32_t *out, const int32_t *p0, const int32_t *p1, const int32_t *p2,
                             const int32_t *p3, int32_t t, int32_t dt);
void GAMEAPI body_script_step(uint8_t *body);
void GAMEAPI body_explosion_force(uint8_t *body);
void GAMEAPI controls_digital(const uint8_t *slot, uint8_t *record);
void GAMEAPI controls_decode(int32_t player, const uint8_t *raw, uint8_t *packet);
void GAMEAPI controls_fetch_packet(int32_t player, uint8_t *raw, uint8_t **packet);
void GAMEAPI controls_analogue(int32_t player);
int32_t GAMEAPI car_controls_read(uint8_t *car, int32_t player);
void GAMEAPI car_handbrake(uint8_t *car, const uint8_t *record);
void GAMEAPI car_controls_apply(uint8_t *car);
int32_t GAMEAPI fix_sin(uint32_t angle);                                          /* 0x4270a0 */
int32_t GAMEAPI fix_cos(uint32_t angle);                                          /* 0x4270c0 */
void GAMEAPI matrix_from_angles(const int16_t *angles, int16_t *m);               /* 0x426f80 */
void GAMEAPI gte_rotate_short(void);                                              /* 0x427fc0 */
void GAMEAPI wheel_bounce(uint8_t *wheel, const uint8_t *car);                    /* 0x40e1a0 */
void GAMEAPI wheel_place(uint8_t *wheel, const uint8_t *car, int32_t steering);   /* 0x40dff0 */
void GAMEAPI car_wheels_place(uint8_t *car);                                      /* 0x40de60 */
void GAMEAPI slot_time_digits(int32_t all, uint8_t *slot);                        /* 0x439910 */
uint32_t GAMEAPI race_clock(void);                                                /* 0x4351c0 */
void GAMEAPI car_speed_and_clock(uint8_t *slot);                                  /* 0x438620 */
void GAMEAPI car_boost(uint8_t *slot);                                            /* 0x47aa00 */
void GAMEAPI quat_rotate(const int32_t *q, int32_t *v);                           /* 0x480940 */
void GAMEAPI vec_bend_by_velocity(int32_t *out, const int32_t *a, const uint8_t *car); /* 0x407e00 */
void GAMEAPI vec_blend(int32_t *out, const int32_t *a, const int32_t *b, int32_t ka, int32_t kb,
                       const uint8_t *car);                                       /* 0x407cf0 */
void GAMEAPI vec_blend_scaled(int32_t *v, const int32_t *w, int32_t ka, int32_t kb); /* 0x407bd0 */
int32_t GAMEAPI matrix_look(int32_t *dir, const int32_t *up, int16_t *m);         /* 0x407220 */
void GAMEAPI camera_fade_step(uint8_t *cam);                                      /* 0x40cba0 */
void GAMEAPI camera_mode(uint8_t *cam, uint32_t pressed, uint32_t held);          /* 0x407620 */
int32_t GAMEAPI camera_target(uint8_t *cam, const uint8_t *car, const int32_t *dir, int32_t *offset,
                              int32_t *normal);                                   /* 0x407300 */
void GAMEAPI camera_settle(uint8_t *cam, const uint8_t *car);                     /* 0x4074c0 */
void GAMEAPI camera_snap(uint8_t *cam, const uint8_t *car);                       /* 0x406f80 */
void GAMEAPI camera_far(uint8_t *cam);                                            /* 0x407850 */
void GAMEAPI camera_near(uint8_t *cam);                                           /* 0x407870 */
void GAMEAPI camera_range_check(uint8_t *cam, const int32_t *v);                  /* 0x4077e0 */
void GAMEAPI camera_follow(uint8_t *cam, const uint8_t *car);                     /* 0x406ff0 */
void GAMEAPI camera_aim(uint8_t *cam, const uint8_t *car);                        /* 0x4070a0 */
void GAMEAPI camera_direction(int32_t *out, const uint8_t *car, uint8_t *cam);    /* 0x4078a0 */
void GAMEAPI camera_track(uint8_t *cam, const uint8_t *car);                      /* 0x406ed0 */
void GAMEAPI camera_update(uint8_t *slot);
void GAMEAPI ai_press_accelerate(uint8_t *car);
void GAMEAPI ai_release_accelerate(uint8_t *car);
void GAMEAPI ai_press_brake(uint8_t *car);
void GAMEAPI ai_press_handbrake(uint8_t *car);
void GAMEAPI ai_press_steer_down(uint8_t *car);
void GAMEAPI ai_press_steer_up(uint8_t *car);
uint32_t GAMEAPI bit27_clear(uint32_t v);
uint32_t GAMEAPI svec_length(const int16_t *v);
int32_t GAMEAPI svec_length_sq(const int16_t *v);
int32_t GAMEAPI line_next(uint32_t node, const uint8_t *line, uint16_t *out);
int32_t GAMEAPI line_prev(uint32_t node, const uint8_t *line, uint16_t *out);
void GAMEAPI body_forward(int32_t *out, const uint8_t *body);
void GAMEAPI body_up(int32_t *out, const uint8_t *body);
int32_t GAMEAPI vec_turn_about(const int32_t *a, const int32_t *b, const int32_t *n);
int32_t GAMEAPI node_near(uint32_t node, const uint8_t *line, const int32_t *pos, int32_t r);
int32_t GAMEAPI node_passed(uint32_t node, const uint8_t *line, const int32_t *pos);
void GAMEAPI line_advance(const uint8_t *line, uint16_t *node, int32_t *at, const int32_t *pos);
void GAMEAPI ai_line_direction(int32_t *out, const uint8_t *slot);
void GAMEAPI line_direction(int32_t *out, uint32_t line_index, uint32_t node);
int32_t GAMEAPI car_surface_kind(const uint8_t *car);
int32_t GAMEAPI ai_steer_amount(int32_t turn, int32_t ahead, int32_t limit, const uint8_t *car);
void GAMEAPI ai_speed_control(uint8_t *car, uint32_t target);
void GAMEAPI car_heading(uint8_t *car, int32_t *ahead, int32_t *side);
void GAMEAPI ai_steer_press(int32_t want, uint8_t *car);
void GAMEAPI ai_aim_at_node(uint8_t *car, int32_t *v, uint32_t line_index, uint32_t node);
void GAMEAPI ai_steer_to(uint8_t *car, const int32_t *target, int32_t check, int32_t reverse);
void GAMEAPI ai_drive(uint8_t *car);
void GAMEAPI ai_pace(uint8_t *car);
void GAMEAPI trail_point_add(const int32_t *pos, const int16_t *ahead, const int32_t *up); /* 0x433d90 */
void GAMEAPI trails_draw(void); /* 0x433f00 */
void GAMEAPI beams_draw(void); /* 0x4345c0 */
int32_t __attribute__((stdcall)) window_proc(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam); /* 0x435ed1 */
void GAMEAPI window_destroy_quit(void); /* 0x436034 */
void GAMEAPI window_command(uint32_t hwnd, uint32_t id); /* 0x436542 */
void GAMEAPI input_focus_poll(void); /* 0x4368f0 */
uint32_t GAMEAPI game_time_get(void); /* 0x43692a */
void GAMEAPI loading_picture_draw(int32_t which); /* 0x436c2c */
void GAMEAPI mode_dialog_languages_fill(uint32_t dlg); /* 0x436ed6 */
void GAMEAPI mode_dialog_texts_set(uint32_t dlg); /* 0x436f3b */
int32_t __attribute__((stdcall)) mode_dialog_proc(uint32_t dlg, uint32_t msg, uint32_t wparam, uint32_t lparam); /* 0x437171 */
int32_t __attribute__((stdcall)) error_dialog_proc(uint32_t dlg, uint32_t msg, uint32_t wparam, uint32_t lparam); /* 0x4375c7 */
void GAMEAPI hud_update(uint32_t *slot); /* 0x4386f0 */
void GAMEAPI hud_update_extra(uint32_t *slot); /* 0x438760 */
uint8_t *GAMEAPI hud_sprite_set(int32_t n, int32_t a, int32_t frame); /* 0x4387d0 */
void GAMEAPI decimal9_format(char *out, uint32_t v); /* 0x42fc40 */
int32_t GAMEAPI decimal_parse(const uint8_t *s, int32_t *value); /* 0x42fcd0 */
int32_t GAMEAPI text_slot_free(void); /* 0x42fd10 */
uint32_t GAMEAPI text_tab_next(uint32_t x); /* 0x42fd60 */
int32_t GAMEAPI glyph_place(uint32_t font_id, uint32_t code, const uint8_t *colours, int32_t y, int16_t *x, int16_t *height, uint32_t flags); /* 0x42fd90 */
uint32_t GAMEAPI language_get(void); /* 0x430120 */
void GAMEAPI fe_sprite_gouraud_set(uint8_t *p, int32_t image, const uint8_t *colours, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t z); /* 0x430ea0 */
void GAMEAPI fe_sprite_set(uint8_t *p, int32_t image, const uint8_t *colour, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t z); /* 0x4310f0 */
void GAMEAPI fe_sprite_set_512(uint8_t *p, int32_t image, const uint8_t *colour, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t z); /* 0x431300 */
void GAMEAPI fe_quad_flat_set(uint8_t *p, const uint8_t *colour, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t z); /* 0x4314d0 */
void GAMEAPI fe_frame_sprites_init(uint8_t *p); /* 0x4316d0 */
void GAMEAPI fe_box_frame_place(uint32_t list, uint8_t *p, const uint8_t *colour, int32_t x, int32_t y, int32_t w, int32_t h); /* 0x431780 */
void GAMEAPI colour_fade_apply(uint8_t *out, const uint8_t *in); /* 0x432820 */
int32_t GAMEAPI fade_scale(int32_t v); /* 0x4328a0 */
void GAMEAPI colour_pulse_scale(uint8_t *out, const uint8_t *in, int32_t count, uint32_t flags); /* 0x4328c0 */
int32_t GAMEAPI front_mode_race_setup(int32_t mode, uint32_t kind, uint32_t choice); /* 0x432cd0 */
void GAMEAPI shake_flash_tint(uint32_t car); /* 0x42be20 */
void GAMEAPI fade_tint_add(int32_t n); /* 0x42bf20 */
uint32_t GAMEAPI screen_tint_add(uint32_t colour); /* 0x42bfc0 */
void GAMEAPI screen_flash_set(int32_t prio, int32_t c0, int32_t c1, int32_t c2, int32_t c3, int32_t c4); /* 0x42c040 */
void GAMEAPI screen_tint_draw(uint32_t arg); /* 0x42c0a0 */
void GAMEAPI blast_flare_draw(void); /* 0x42c420 */
void GAMEAPI shock_waves_draw(void); /* 0x42c950 */
uint32_t GAMEAPI font_by_id(uint32_t id); /* 0x42e050 */
int32_t GAMEAPI text_buffers_alloc(void); /* 0x42e2a0 */
int32_t GAMEAPI text_entry_copy(uint8_t *dst, int32_t max, const char *name); /* 0x42e3d0 */
uint32_t GAMEAPI text_section_find(const char *name); /* 0x42e4b0 */
uint32_t GAMEAPI text_bytes_find(const uint8_t *pat, const uint8_t *text, uint32_t len, uint32_t n); /* 0x42e620 */
uint32_t GAMEAPI text_bytes_differ(const uint8_t *a, const uint8_t *b, uint32_t n); /* 0x42e690 */
int32_t GAMEAPI text_line_get(uint8_t *dst, int32_t max, const char *name, int32_t line); /* 0x42e6d0 */
int32_t GAMEAPI string_printf(uint32_t font, uint32_t colours, uint32_t flags, uint32_t spacing, const char *fmt, ...); /* 0x42e810 */
int32_t GAMEAPI string_width_printf(uint32_t font, uint32_t colours, uint32_t flags, uint32_t spacing, const char *fmt, ...); /* 0x42e8f0 */
uint32_t GAMEAPI string_active(int32_t i); /* 0x42ea00 */
void GAMEAPI string_on(int32_t i, int32_t x, int32_t y, int32_t z, uint32_t flags); /* 0x42ea40 */
void GAMEAPI string_off(int32_t i); /* 0x42eab0 */
int32_t GAMEAPI string_line_range(int32_t i, int32_t line, uint32_t *start, uint32_t *end, int32_t *mark); /* 0x42eae0 */
int32_t GAMEAPI string_limits(int32_t i, int32_t line, int32_t *minx, int32_t *miny, int32_t *maxx, int32_t *maxy); /* 0x42ebc0 */
int32_t GAMEAPI string_limits_all(int32_t i, int32_t *minx, int32_t *miny, int32_t *maxx, int32_t *maxy); /* 0x42ed10 */
int32_t GAMEAPI string_set_position(int32_t i, int32_t x, int32_t y, int32_t xmode, int32_t ymode); /* 0x42ed40 */
int32_t GAMEAPI string_set_position_j(int32_t i, int32_t x, int32_t mode); /* 0x42eed0 */
uint32_t GAMEAPI string_get_width(int32_t i); /* 0x42f030 */
uint32_t GAMEAPI string_get_height(int32_t i); /* 0x42f070 */
uint32_t GAMEAPI string_has_marks(int32_t i); /* 0x42f0b0 */
int32_t GAMEAPI string_colour_range(int32_t i, int32_t line, int32_t which, int32_t sel, const uint8_t *colour); /* 0x42f0f0 */
int32_t GAMEAPI string_colour_set(int32_t i, int32_t sel, const uint8_t *colour); /* 0x42f2b0 */
void GAMEAPI string_clip_x(int32_t i, int32_t n, int32_t mode); /* 0x42f2d0 */
int32_t GAMEAPI strings_draw(uint32_t ot, uint32_t unused, uint32_t which); /* 0x42f3c0 */
int32_t GAMEAPI string_build(uint32_t font_id, const uint8_t *s, uint32_t colours, uint32_t flags, uint32_t spacing, uint32_t va); /* 0x42f6a0 */
void GAMEAPI cars_view_flags_update(void); /* 0x4231f0 */
void GAMEAPI ot_frame_flush(void); /* 0x423c40 */
void GAMEAPI view_matrix_store(const uint32_t *m); /* 0x423d10 */
void GAMEAPI view_camera_build(uint8_t *view, const uint32_t *matrix, const uint8_t *pos); /* 0x423d70 */
void GAMEAPI view_camera_load(const uint8_t *view); /* 0x423df0 */
void GAMEAPI view_lod_set(uint32_t zoom); /* 0x423e10 */
void GAMEAPI view_zoom_set(uint32_t zoom); /* 0x423f00 */
int32_t GAMEAPI view_full_screen(void); /* 0x423f20 */
void GAMEAPI gte_screen_offset_set(int32_t x, int32_t y); /* 0x426200 */
void GAMEAPI gte_screen_offset_get(int32_t *x, int32_t *y); /* 0x426240 */
void GAMEAPI projection_h_set(int32_t zoom); /* 0x426260 */
void GAMEAPI gte_matrix_load(const uint8_t *m); /* 0x426330 */
void GAMEAPI gte_matrix_save(uint8_t *out); /* 0x426480 */
void GAMEAPI gte_translation_load(const uint8_t *v); /* 0x426530 */
void GAMEAPI view_rect_saved_get(uint32_t *out); /* 0x426590 */
void GAMEAPI view_saved_get(uint32_t *out); /* 0x4265b0 */
void gte_rt_float(void); /* 0x426620: asm */
void gte_rtps_float(void); /* 0x4266d0: asm */
void gte_rtps_3dnow(void); /* 0x4268c0: asm, 3DNow!, (record) through 0x6ccd78 / 0x6ccd7c */
void gte_rt_depth(void); /* 0x426cd0: asm, (record) returns z; leaves st(0) for gte_project_pending when z >= 0 */
void gte_project_pending(void); /* 0x426dc0: asm, takes st(0) from gte_rt_depth */
void GAMEAPI matrix_rotate_vec(const uint8_t *m, const int16_t *v, int32_t *out); /* 0x426e10 */
void GAMEAPI matrix_mul_into(const uint8_t *a, uint8_t *b); /* 0x426e90 */
void GAMEAPI matrix_translation_set(uint8_t *m, const int32_t *v); /* 0x426f60 */
void GAMEAPI gte_light_colour(void); /* 0x427100 */
void gte_light_colour_3dnow(void); /* 0x427250: asm, 3DNow!, through 0x6cce3c */
void gte_light_colour_float(void); /* 0x427400: asm, through 0x6cce3c */
int32_t GAMEAPI gte_nclip(void); /* 0x4278a0 */
void vec_facing_float(void); /* 0x4278f0: asm, (a, b, c) returns its float in st(0) */
int32_t GAMEAPI ot_clear_forward(uint32_t *ot, uint32_t n); /* 0x427a40 */
int32_t GAMEAPI ot_clear_reverse(uint32_t *ot, uint32_t n); /* 0x427a70 */
uint32_t GAMEAPI ot_add_prim(uint32_t entry, uint32_t *prim); /* 0x427ab0 */
void GAMEAPI filter_set_thunk(void); /* 0x428240 */
void GAMEAPI fog_on_thunk(void); /* 0x428250 */
void GAMEAPI fog_off_thunk(void); /* 0x428260 */
int32_t GAMEAPI zbuffer_restore(void); /* 0x428270 */
int32_t GAMEAPI debug_line_print(char *s); /* 0x428310 */
int32_t GAMEAPI clear_colour_store(uint32_t r, uint32_t g, uint32_t b); /* 0x428390 */
int32_t GAMEAPI depth_range_set(int32_t near, int32_t far, uint32_t c); /* 0x4283d0 */
void GAMEAPI view_border_draw(const int16_t *r, const int16_t *in, uint32_t fill); /* 0x4284b0 */
void GAMEAPI view_viewport_apply(const uint8_t *view, int32_t border); /* 0x428770 */
void vertex_project_codes(void); /* 0x4288c0: asm, (record); st(0) handed to 0x426b00 */
void vertex_project_float(void); /* 0x428900: asm, (record) */
void GAMEAPI light_matrix_load(const uint32_t *m); /* 0x428940 */
void GAMEAPI render_flags_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e); /* 0x428a50 */
void GAMEAPI projection_push(void); /* 0x428a80 */
void GAMEAPI projection_pop(void); /* 0x428b00 */
void GAMEAPI far_plane_set(uint32_t f); /* 0x428b90 */
void far_plane_get(void); /* 0x428bd0: asm, returns the float in st(0) */
void GAMEAPI matrix_transpose_s16(const uint8_t *src, int16_t *dst); /* 0x428be0 */
int32_t GAMEAPI texture_surface_lock(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f); /* 0x428c30 */
int32_t GAMEAPI texture_surface_unlock(uint32_t a, uint32_t b, uint32_t c); /* 0x428c60 */
int32_t GAMEAPI texture_handle_new(void); /* 0x428c80 */
int32_t GAMEAPI texture_surface_upload(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f); /* 0x428c90 */
void GAMEAPI dword_store(uint32_t *p, uint32_t v); /* 0x4290a0 */
void vec_normalise_x87(void); /* 0x4290b0: asm, (in, out) returns 0 */
void GAMEAPI dx_error_ignore(void); /* 0x4291f0 */
void GAMEAPI light_colour_at(const uint8_t *pos, const uint32_t *rgb, uint32_t *out); /* 0x4292b0 */
int32_t GAMEAPI textures_unusable_thunk(void); /* 0x429340 */
void GAMEAPI smoke_clouds_draw(void); /* 0x42b9a0 */
void GAMEAPI d3d_packet_draw(uint32_t packet); /* 0x420be0 */
void GAMEAPI d3d_vertex_repeat(void); /* 0x421800 */
void GAMEAPI d3d_draw_otag(uint32_t ot); /* 0x421860 */
int32_t GAMEAPI d3d_surfaces_restore(void); /* 0x4218e0 */
void GAMEAPI car_ot_insert(uint32_t car, int32_t z, int32_t bias); /* 0x421e60 */
void GAMEAPI cars_draw(void); /* 0x421ef0 */
void GAMEAPI cars_packets_next(void); /* 0x422080 */
void GAMEAPI car_packets_next(uint32_t car); /* 0x422100 */
void GAMEAPI car_ride_offset_update(uint32_t car); /* 0x422140 */
void GAMEAPI cars_effects_draw(void); /* 0x422210 */
void GAMEAPI car_model_draw(uint32_t car, uint32_t light, int32_t bias); /* 0x422440 */
void GAMEAPI car_wheels_draw(uint32_t car, uint32_t light); /* 0x4224a0 */
void GAMEAPI car_points_transform(uint32_t car); /* 0x422540 */
int32_t GAMEAPI wheel_facing_away(uint32_t wheel); /* 0x4225c0 */
int32_t GAMEAPI car_depth_offset(uint32_t car, uint32_t offset); /* 0x422650 */
void GAMEAPI model_draw_at(uint32_t model, uint32_t light, uint32_t ot, int32_t dist); /* 0x4226a0 */
void GAMEAPI model_draw_lod(uint32_t model, uint32_t ot, int32_t dist); /* 0x4226d0 */
void GAMEAPI cars_reflections_update(void); /* 0x422c80 */
void GAMEAPI car_reflection_build(uint32_t src, uint32_t dst, int32_t height); /* 0x422d30 */
void GAMEAPI matrix_mirror_y(uint32_t dst, uint32_t src); /* 0x422e30 */
void GAMEAPI position_mirror_y(uint32_t dst, uint32_t src, int32_t height); /* 0x422e80 */
int32_t GAMEAPI car_water_find(uint32_t car, uint32_t out); /* 0x422eb0 */
void GAMEAPI car_shadow_draw(uint32_t car, uint32_t ot); /* 0x422f50 */
int32_t GAMEAPI viewport_clear(uint32_t keep_target); /* 0x41e040 */
int32_t GAMEAPI d3d_materials_release(void); /* 0x41e0d0 */
void GAMEAPI material_init(uint32_t *m, uint32_t r, uint32_t g, uint32_t b); /* 0x41e110 */
int32_t GAMEAPI d3d_scene_begin(void); /* 0x41e170 */
void GAMEAPI d3d_scene_end(void); /* 0x41e190 */
void GAMEAPI pixels_rgb_to_screen(const uint8_t *src, int32_t n, uint32_t alpha); /* 0x41e1d0 */
void GAMEAPI pixels_rgb_convert(const uint8_t *src, int32_t n, uint16_t *dst, uint32_t alpha); /* 0x41e1f0 */
void GAMEAPI pixels_1555_convert(const uint16_t *src, int32_t w, int32_t h, uint16_t *dst, int32_t pitch, uint32_t alpha); /* 0x41e440 */
int32_t GAMEAPI texture_slot_free_find(void); /* 0x41ec20 */
void GAMEAPI background_colour_set(int32_t r, int32_t g, int32_t b); /* 0x41ec40 */
int32_t GAMEAPI materials_create(void); /* 0x41ed00 */
int32_t GAMEAPI surfaces_clear(void); /* 0x41eeb0 */
int32_t GAMEAPI texture_page_show(int32_t page); /* 0x41efe0 */
int32_t GAMEAPI texture_format_choose(uint32_t rgb_only, uint32_t alpha_wanted); /* 0x41f060 */
void GAMEAPI d3d_viewport_set(uint32_t x, uint32_t y, uint32_t w, uint32_t h); /* 0x41f210 */
void GAMEAPI vertex_snap_set(uint32_t on); /* 0x41f3f0 */
uint32_t GAMEAPI surface_pixel_read(uint32_t which, uint32_t x, uint32_t y, uint32_t mask); /* 0x41f400 */
int32_t GAMEAPI screen_quad_draw(uint32_t state, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t u0, uint32_t v0, uint32_t u1, uint32_t v1, uint32_t z, uint32_t colour, uint32_t in_scene, uint32_t blend); /* 0x41f550 */
uint32_t GAMEAPI bytes_sum8(const uint8_t *p, int32_t n); /* 0x41f700 */
int32_t GAMEAPI pvrc_texture_probe(void); /* 0x41fb60 */
int32_t GAMEAPI ddraw_device_id_check(void); /* 0x41fcf0 */
void GAMEAPI blend_modes_choose(void); /* 0x41fda0 */
int32_t GAMEAPI texture_lock(int32_t page, uint32_t flags, uint32_t *bits, uint32_t *palette, uint32_t *bit_count, uint32_t *pitch); /* 0x41fef0 */
int32_t GAMEAPI texture_unlock(int32_t page, uint32_t flags, const uint32_t *palette); /* 0x420000 */
void GAMEAPI texture_palettes_apply(void); /* 0x420090 */
int32_t GAMEAPI texture_reload(uint32_t slot); /* 0x420150 */
int32_t GAMEAPI image_tiles_upload(uint32_t src, const uint8_t *palette, int32_t w, int32_t h, int32_t pitch); /* 0x4201f0 */
void GAMEAPI image_tiles_draw(int32_t first, int32_t w, int32_t h, const int32_t *rect, int32_t blend, uint32_t colour); /* 0x4203a0 */
int32_t GAMEAPI texture_slots_release_thunk(void); /* 0x4205e0 */
int32_t GAMEAPI texture_page_next(void); /* 0x4205f0 */
void GAMEAPI vertices_snap(void); /* 0x420600 */
void GAMEAPI vertices_snap_thunk(void); /* 0x420650 */
void GAMEAPI vertices_draw_list_noclip(void); /* 0x420660 */
void GAMEAPI vertices_draw_list(void); /* 0x4206a0 */
void GAMEAPI vertices_draw_fan(void); /* 0x4206e0 */
void GAMEAPI vertices_draw_quads(void); /* 0x420720 */
void GAMEAPI render_blend_set(uint32_t flags, uint32_t mode); /* 0x420770 */
void GAMEAPI render_texture_set(uint32_t page); /* 0x4209c0 */
void render_state_select(void); /* 0x420a30: asm entry, register argument ebx (state) */
void GAMEAPI render_state_select_c(uint32_t state); /* 0x420a30 body */
void vertex_emit_indexed(void); /* 0x420a90: asm entry, register arguments eax (colour), ebx (texture indices), edx (vertex) */
void GAMEAPI vertex_emit_indexed_c(uint32_t colour, uint32_t uv, const uint8_t *v); /* 0x420a90 body */
void vertex_emit(void); /* 0x420b10: asm entry, register argument edx (vertex) */
void GAMEAPI vertex_emit_c(const uint8_t *v); /* 0x420b10 body */
void GAMEAPI vertex_list_draw_fan(const int32_t *list); /* 0x420bb0 */
int32_t GAMEAPI clip_far_z(uint32_t *in, uint32_t *out, int32_t *next); /* 0x417950 */
int32_t GAMEAPI clip_polygon_a(uint32_t flags); /* 0x417af0 */
int32_t GAMEAPI clip_polygon_b(uint32_t flags); /* 0x417ce0 */
void clip_vertex_add(void); /* 0x4180c0: asm entry, register arguments eax (colour bytes), ecx / edx (texture indices), ebx (six dwords) */
void GAMEAPI clip_vertex_add_c(uint32_t packed, uint32_t u, uint32_t v, const uint32_t *src); /* 0x4180c0 body */
void GAMEAPI clip_vertex_add_2d(uint32_t xy, uint32_t uv, uint32_t rgba); /* 0x418170 */
void GAMEAPI clip_vertices_restart(void); /* 0x418260 */
int32_t GAMEAPI clip_polygon_2d(void); /* 0x418290 */
void GAMEAPI screen_colour_apply(void); /* 0x41b2f0 */
void GAMEAPI replay_files_save(void); /* 0x41c550 */
int32_t GAMEAPI cpuid_available(void); /* 0x41c8c0 */
void GAMEAPI cpu_features_read(void); /* 0x41c8f0 */
int32_t GAMEAPI cpu_check(void); /* 0x41c930 */
void GAMEAPI fatal_error(const uint8_t *fmt, ...); /* 0x41c970 */
uint32_t GAMEAPI fatal_format(const uint8_t *fmt, const uint32_t *args); /* 0x41c9d0 */
uint8_t *GAMEAPI fatal_number(uint8_t *dst, uint32_t value, uint32_t conv, int32_t width); /* 0x41cd00 */
void GAMEAPI debug_hook_nop(void); /* 0x41cec0 */
int32_t GAMEAPI d3d_init(uint32_t unused, uint32_t zbuffer); /* 0x41ced0 */
void GAMEAPI d3d_tables_alloc(void); /* 0x41cfd0 */
int32_t GAMEAPI ddraw_query_d3d(void); /* 0x41d100 */
int32_t GAMEAPI d3d_shutdown(void); /* 0x41d130 */
int32_t GAMEAPI d3d_device_create(void); /* 0x41d1f0 */
int32_t GAMEAPI d3d_devices_enumerate(void); /* 0x41d250 */
int32_t GAMEAPI d3d_device_pick(void); /* 0x41d6d0 */
int32_t GAMEAPI d3d_texture_format_pick(void); /* 0x41d730 */
int32_t GAMEAPI texture_slot_release(int32_t i); /* 0x41d890 */
int32_t GAMEAPI texture_slots_release(void); /* 0x41d910 */
int32_t GAMEAPI texture_slot_handle(int32_t i); /* 0x41d940 */
int32_t GAMEAPI zbuffer_create(uint32_t width, uint32_t height); /* 0x41d980 */
int32_t GAMEAPI d3d_render_states(void); /* 0x41db00 */
void GAMEAPI d3d_filter_set(void); /* 0x41dd90 */
int32_t GAMEAPI d3d_filter_nearest(void); /* 0x41ddd0 */
void GAMEAPI d3d_fog_colour_set(uint32_t r, uint32_t g, uint32_t b); /* 0x41de10 */
void GAMEAPI d3d_fog_on(void); /* 0x41de40 */
void GAMEAPI d3d_fog_off(void); /* 0x41de60 */
void GAMEAPI d3d_zbuffer_on(void); /* 0x41de80 */
void GAMEAPI d3d_zbuffer_off(void); /* 0x41dec0 */
void GAMEAPI d3d_state_defaults(void); /* 0x41dee0 */
int32_t GAMEAPI d3d_viewport_create(uint32_t width, uint32_t height); /* 0x41df10 */
int32_t GAMEAPI cpu_has_3dnow(void); /* 0x4010c0 */
int32_t GAMEAPI cpu_ext_level(void); /* 0x4010f0 */
int32_t GAMEAPI cpu_3dnow_get(void); /* 0x401110 */
int32_t GAMEAPI track_object_vanish(uint8_t *obj); /* 0x401230 */
void GAMEAPI dome_scale(int16_t *v); /* 0x404980 */
void GAMEAPI sky_dome_draw(uint8_t *view); /* 0x4049b0 */
void GAMEAPI dome_vertex(const uint32_t *point, uint8_t *out, uint8_t *flag, float far); /* 0x404e00 */
void GAMEAPI dome_quad_add(uint8_t *packet, const uint8_t *a, const uint8_t *b, const uint8_t *c, const uint8_t *d, uint32_t fa, uint32_t fb, uint32_t fc, uint32_t fd, uint32_t colour); /* 0x404e50 */
void GAMEAPI dome_tri_add(uint8_t *packet, const uint8_t *a, const uint8_t *b, const uint8_t *c, uint32_t fa, uint32_t fb, uint32_t fc, uint32_t colour); /* 0x404ee0 */
void GAMEAPI sky_quads_draw(void); /* 0x404f50 */
int32_t GAMEAPI blast_object_hit(uint8_t *obj); /* 0x405280 */
void GAMEAPI blast_object_wreck(uint8_t *obj); /* 0x4052b0 */
int32_t GAMEAPI screenshot_dump(void); /* 0x405c10 */
int32_t GAMEAPI crumble_queue_add(uint8_t *obj); /* 0x405ed0 */
void GAMEAPI tv_camera_begin(int32_t view); /* 0x408270 */
void GAMEAPI tv_camera_end(void); /* 0x40ad60 */
void GAMEAPI camera_event_cut(uint8_t *from, int32_t *point, uint32_t kind, int16_t length); /* 0x40ae50 */
void GAMEAPI camera_blend_begin(uint8_t **rec); /* 0x40cbe0 */
void GAMEAPI camera_blend_end(uint8_t **rec); /* 0x40cd00 */
void GAMEAPI camera_blend_apply(uint8_t **rec); /* 0x40cd90 */
void GAMEAPI vec_lerp8(const int32_t *a, const int32_t *b, int32_t *out, int32_t k); /* 0x40cfa0 */
int32_t GAMEAPI charanim_upload(uint32_t c, uint32_t f); /* 0x4114b0 */
int32_t GAMEAPI charanim_load(uint32_t c, uint32_t f); /* 0x4115e0 */
int32_t GAMEAPI charanim_load_all(void); /* 0x4116f0 */
void GAMEAPI charanim_texture_info(uint16_t *handle, uint16_t *origin, int16_t *u, int16_t *v, uint16_t *uv); /* 0x411740 */
int32_t GAMEAPI object_part_hit(uint8_t *obj, uint8_t *body, int32_t part, int32_t *point); /* 0x412bf0 */
int32_t GAMEAPI clip_plane(const uint32_t *in, const float *plane, uint32_t *out, int32_t *next); /* 0x417060 */
int32_t GAMEAPI clip_plane_flat(const uint32_t *in, const float *plane, uint32_t *out, int32_t *next); /* 0x417270 */
int32_t GAMEAPI clip_near(const uint32_t *in, uint32_t *out, int32_t *next); /* 0x417450 */
int32_t GAMEAPI clip_far(const uint32_t *in, uint32_t *out, int32_t *next); /* 0x417600 */
int32_t GAMEAPI clip_near_list(const uint32_t *in, uint32_t *out, int32_t *next); /* 0x4177b0 */
void GAMEAPI crtc_entry(void); /* 0x498ec5 */
void GAMEAPI crtc_fpmath(void); /* 0x4979a0 */
void GAMEAPI crtc_clock_init(void); /* 0x499059 */
void GAMEAPI crtc_initstdio(void); /* 0x49accc */
void GAMEAPI crtc_initmbctable(void); /* 0x49d2e7 */
void GAMEAPI crtc_lockexit(void); /* 0x498d50 */
void GAMEAPI crtc_unlockexit(void); /* 0x498d59 */
void GAMEAPI crtc_initterm(uint32_t *first, uint32_t *last); /* 0x498d62 */
void GAMEAPI crtc_exit_quick(int32_t code); /* 0x498c9a */
void GAMEAPI crtc_amsg_exit(int32_t n); /* 0x498fcd */
void GAMEAPI crtc_fptrap(void); /* 0x49f925 */
int32_t GAMEAPI crtc_endstdio(void); /* 0x49ad74 */
uint32_t __attribute__((stdcall)) crtc_threadstart(uint8_t *ptd); /* 0x498df3 */
uint8_t *GAMEAPI crtc_getptd(void); /* 0x49bcf1 */
void GAMEAPI crtc_initptd(uint8_t *ptd); /* 0x49bcde */
int32_t *GAMEAPI crtc_errno(void); /* 0x49d48e */
int32_t *GAMEAPI crtc_doserrno(void); /* 0x49d497 */
void GAMEAPI crtc_dosmaperr(uint32_t oserr); /* 0x49d41b */
void GAMEAPI crtc_lock(int32_t n); /* 0x49b161 */
void GAMEAPI crtc_unlock(int32_t n); /* 0x49b1c2 */
uint8_t *GAMEAPI crtc_fopen(const char *name, const char *mode); /* 0x499459 */
uint32_t GAMEAPI crtc_fread(void *buf, uint32_t size, uint32_t num, uint8_t *s); /* 0x49908e */
int32_t GAMEAPI crtc_fseek(uint8_t *s, int32_t offset, int32_t whence); /* 0x4991a5 */
int32_t GAMEAPI crtc_ftell(uint8_t *s); /* 0x49925e */
int32_t GAMEAPI crtc_fclose(uint8_t *s); /* 0x4994f9 */
int32_t GAMEAPI crtc_fputs(const char *str, uint8_t *s); /* 0x4996dc */
int32_t GAMEAPI crtc_fflush(uint8_t *s); /* 0x499576 */
int32_t GAMEAPI crtc_printf(const char *format, ...); /* 0x497f7a */
int32_t GAMEAPI crtc_toupper_lk(int32_t c); /* 0x4985af */
int32_t GAMEAPI crtc_tolower_lk(int32_t c); /* 0x4986ea */
int32_t GAMEAPI crtc_get_string_type(uint32_t type, const char *src, int32_t n, uint16_t *out, uint32_t cp, uint32_t lcid, int32_t error); /* 0x49aee3 */
int32_t GAMEAPI crtc_lcmap_string(uint32_t lcid, uint32_t flags, const char *src, int32_t n, char *dst, int32_t dstn, uint32_t cp, int32_t error); /* 0x49ba3b */
uint8_t *GAMEAPI crtc_mbsdec(const uint8_t *start, const uint8_t *p); /* 0x49cea2 */
uint8_t *GAMEAPI crtc_mbsnbcpy(uint8_t *dst, const uint8_t *src, uint32_t n); /* 0x49d303 */
uint32_t GAMEAPI crtc_mbctoupper(uint32_t c); /* 0x49d4a0 */
void GAMEAPI crtc_fmod(void); /* 0x4987e0: asm, fmod(double, double) -> st(0) */
void GAMEAPI crtc_atan2(void); /* 0x498820: asm, atan2(double y, double x) -> st(0) */
void GAMEAPI crtc_one_arg_error(void); /* 0x49a8e7: asm, _startOneArgErrorHandling (eax, ecx, edx, st(0), frame) */
void GAMEAPI crtc_two_arg_error(void); /* 0x49a8d0: asm, the two-argument entry */
void GAMEAPI crtc_powhlp(void); /* 0x49ab37: asm, int _powhlp(double x, double y, double *result) */
void GAMEAPI crtc_except2(void); /* 0x4a0f1a: asm, double _except2(flags, op, double a, double b, double r, cw) */
void GAMEAPI crtc_handle_qnan2(void); /* 0x4a0eba: asm, double _handle_qnan2(op, double a, double b, cw) */
int32_t GAMEAPI cd_volume_get(void); /* 0x497630 */
int32_t GAMEAPI cd_volume_available(void); /* 0x4977b0 */
int32_t GAMEAPI msvc_sprintf(char *buf, const char *format, ...); /* 0x4979f0 */
void msvc_chkstk(void); /* 0x497a50: asm, size in eax, esp moved down */
void msvc_ftol(void); /* 0x497a80: asm, st(0) in, edx:eax out */
void msvc_cos(void); /* 0x497ac4: asm, double on the stack, result in st(0) */
void msvc_sin(void); /* 0x497b74: asm, as cos */
void msvc_atan(void); /* 0x497c24: asm, as cos */
void msvc_pow(void); /* 0x497cc9: asm, two doubles on the stack, result in st(0) */
int32_t GAMEAPI msvc_atoi(const char *s); /* 0x498046 */
int32_t GAMEAPI msvc_isctype(int32_t c, uint32_t mask); /* 0x498051 */
char *GAMEAPI msvc_strstr(const char *s, const char *find); /* 0x4980d0 */
uint64_t __attribute__((stdcall)) msvc_allmul(uint64_t a, uint64_t b); /* 0x498180 */
void msvc_sqrt(void); /* 0x4981d4: asm, as cos */
void GAMEAPI msvc_free(void *p); /* 0x49827a */
void *GAMEAPI msvc_malloc(uint32_t size); /* 0x4982c2 */
char *GAMEAPI msvc_strncpy(char *dst, const char *src, uint32_t n); /* 0x498350 */
char *GAMEAPI msvc_strcpy(char *dst, const char *src); /* 0x498450 */
char *GAMEAPI msvc_strcat(char *dst, const char *src); /* 0x498460 */
int32_t GAMEAPI msvc_toupper(int32_t c); /* 0x498540 */
int32_t GAMEAPI msvc_tolower(int32_t c); /* 0x49867b */
int32_t GAMEAPI msvc_rand(void); /* 0x4987b5 */
void GAMEAPI msvc_makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext); /* 0x498834 */
void GAMEAPI msvc_splitpath(const char *path, char *drive, char *dir, char *fname, char *ext); /* 0x4988c5 */
char *GAMEAPI msvc_getcwd(char *buf, int32_t size); /* 0x498a0c */
int32_t GAMEAPI msvc_chdir(const char *path); /* 0x498b3f */
int32_t GAMEAPI msvc_access(const char *path, uint32_t mode); /* 0x498bc5 */
int32_t GAMEAPI msvc_vsprintf(char *buf, const char *format, const uint8_t *args); /* 0x498c0b */
void GAMEAPI msvc_exit(int32_t code); /* 0x498c89 */
uint32_t GAMEAPI msvc_beginthread(uint32_t start, uint32_t stack_size, uint32_t arg); /* 0x498d7c */
int32_t GAMEAPI msvc_clock(void); /* 0x499016 */
int32_t GAMEAPI msvc_strncmp(const char *a, const char *b, uint32_t n); /* 0x4993f0 */
void *GAMEAPI msvc_calloc(uint32_t count, uint32_t size); /* 0x49946c */
int32_t GAMEAPI msvc_vsnprintf(char *buf, int32_t count, const char *format, const uint8_t *args); /* 0x49972a */
void GAMEAPI debug_log_close(void); /* 0x496410 */
void GAMEAPI debug_log_set_path(const char *path); /* 0x4962c0 */
void GAMEAPI debug_log_printf(const char *fmt, ...); /* 0x496340 */
void GAMEAPI debug_log_print(const char *fmt, ...); /* 0x4963c0 */
void GAMEAPI archive_select(int32_t closed); /* 0x496510 */
void GAMEAPI archive_open(const char *path, int32_t count); /* 0x496430 */
uint8_t *GAMEAPI archive_get_record(int32_t index); /* 0x496570 */
uint8_t *GAMEAPI archive_entry_load(int32_t index, uint8_t *buf); /* 0x496590 */
void GAMEAPI archive_free(void *block); /* 0x496660 */
int32_t GAMEAPI archive_state(void); /* 0x496680 */
int32_t GAMEAPI cdaudio_device_open(void); /* 0x4966b0 */
int32_t GAMEAPI cdaudio_device_close(void); /* 0x496730 */
void GAMEAPI cdaudio_open(void); /* 0x496690 */
int32_t GAMEAPI cdaudio_random(int32_t n); /* 0x496be0 */
int32_t GAMEAPI cdaudio_choose_track(int32_t next); /* 0x496a90 */
void GAMEAPI cdaudio_play_track(int32_t next); /* 0x496920 */
void GAMEAPI cdaudio_stop(void); /* 0x4967a0 */
void GAMEAPI cdaudio_close(void); /* 0x496700 */
void GAMEAPI cdaudio_play(int32_t track); /* 0x496770 */
void GAMEAPI cdaudio_pause(void); /* 0x4967f0 */
void GAMEAPI cdaudio_resume(void); /* 0x496890 */
void GAMEAPI cdaudio_poll(void); /* 0x4968b0 */
void GAMEAPI cdaudio_set_shuffle(int32_t mode); /* 0x496c50 */
void GAMEAPI cdaudio_set_skip_first(int32_t skip); /* 0x496c80 */
int32_t GAMEAPI aux_query(uint32_t dev, uint8_t *caps, uint32_t *volume); /* 0x496e50 */
void GAMEAPI aux_free(void); /* 0x496ed0 */
void GAMEAPI aux_scan(void); /* 0x496d70 */
void GAMEAPI aux_set_volume(int32_t left, int32_t right); /* 0x496d00 */
int32_t GAMEAPI mixer_add_control(uint32_t dev, uint32_t h, uint32_t dest, uint32_t src, uint32_t index, uint32_t channels, uint32_t items, uint8_t *ctl); /* 0x497410 */
int32_t GAMEAPI mixer_check_control(uint32_t dev, uint32_t h, uint32_t dest, uint32_t src, uint8_t *ctl, uint32_t index, uint32_t channels); /* 0x497320 */
int32_t GAMEAPI mixer_scan_source(uint32_t dev, uint32_t h, uint32_t dest, uint32_t src); /* 0x4971f0 */
int32_t GAMEAPI mixer_scan_destination(uint32_t dev, uint32_t h, uint32_t dest); /* 0x497160 */
int32_t GAMEAPI mixer_scan_device(uint32_t dev); /* 0x4970c0 */
int32_t GAMEAPI mixer_scan(int32_t mode); /* 0x497040 */
void GAMEAPI mixer_set_volume(int32_t restore, int32_t left, int32_t right); /* 0x496f30 */
void GAMEAPI cdaudio_set_volume(int32_t left, int32_t right); /* 0x496c90 */
void crt_dsound_create(void); /* 0x4a1c36: naked jump through the DirectSoundCreate import slot (stdcall, 3 args) */
char *GAMEAPI crt_strlwr(char *s); /* 0x4a1c3c */
char *GAMEAPI crt_strupr(char *s); /* 0x4a1d70 */
double GAMEAPI crt_hypot(double x, double y); /* 0x4a1ea4 */
int32_t __attribute__((stdcall)) d3d_enum_device(const uint8_t *guid, const uint8_t *desc, const uint8_t *name, const uint8_t *hw, const uint8_t *sw, uint32_t user); /* 0x41d280 */
int32_t GAMEAPI ddraw_depth_flag(int32_t bpp); /* 0x41d640 */
int32_t GAMEAPI network_session_poll(int32_t mode); /* 0x435d43 */
void GAMEAPI cheat_code_char(uint32_t unused, int32_t ch, int32_t press); /* 0x4360e1 */
void GAMEAPI menu_hold_clear(void); /* 0x4474e0 */
void GAMEAPI menu_hold_reset(void); /* 0x447500 */
void GAMEAPI config_flag7_toggle(void); /* 0x447f60 */
void GAMEAPI menu_pick_available(int32_t *p, int32_t step); /* 0x448330 */
void GAMEAPI config_mirror_flag(void); /* 0x448380 */
int32_t GAMEAPI menu_param_callback(int32_t cmd, uint8_t *dst, int32_t a2, int32_t a3); /* 0x44d000 */
void GAMEAPI menu_call_with_zero(int32_t a, int32_t b); /* 0x44ebb0 */
void GAMEAPI race_setting_trim(void); /* 0x4503c0 */
int32_t GAMEAPI menu_choice_callback(int32_t cmd, int32_t a1, int32_t max, int32_t *p, int32_t a4); /* 0x450c00 */
int32_t GAMEAPI control_entry_set(int32_t player, int32_t index, int32_t kind, int32_t key, int32_t extra); /* 0x452640 */
int32_t GAMEAPI control_type_set(int32_t player, int32_t index, int32_t type); /* 0x4526f0 */
void GAMEAPI menu_state_reset(void); /* 0x452730 */
void GAMEAPI control_button_set(int32_t player, int32_t value); /* 0x452900 */
int32_t GAMEAPI pair_lookup(int32_t a, int32_t b); /* 0x46b510 */
int32_t GAMEAPI pair_lookup_ok(int32_t a, int32_t b); /* 0x46b7b0 */
int32_t GAMEAPI pair_lookup_default(void); /* 0x474b70 */
int32_t GAMEAPI joy_axis_edge_call(int32_t joy, int32_t axis); /* 0x48a230 */
int32_t GAMEAPI joy_pov_edge(int32_t joy, uint32_t dir); /* 0x48a2f0 */
int32_t GAMEAPI joy_pov_edge_0(int32_t joy); /* 0x48a3e0 */
int32_t GAMEAPI joy_pov_edge_1(int32_t joy); /* 0x48a3f0 */
int32_t GAMEAPI joy_pov_edge_2(int32_t joy); /* 0x48a400 */
int32_t GAMEAPI joy_pov_edge_3(int32_t joy); /* 0x48a410 */
int32_t GAMEAPI input_capture_binding(int32_t joy); /* 0x48a420 */
int32_t GAMEAPI input_capture_any(void); /* 0x48a560 */
uint8_t *GAMEAPI joy_record(int32_t joy); /* 0x48a590 */
int32_t GAMEAPI joy_device_value(int32_t joy); /* 0x488d60 */
int32_t __attribute__((stdcall)) input_enum_joystick_object(const uint8_t *object, int32_t joy); /* 0x4890b0 */
int32_t __attribute__((stdcall)) joy_names_collect(const uint8_t *guid, uint32_t a1, uint8_t *names, uint32_t a3); /* 0x48ac40 */
void GAMEAPI video_mode_list_fill(int32_t dlg, int32_t item); /* 0x488470 */
void GAMEAPI menu_slider_step(int32_t cmd); /* 0x447d80 */
void GAMEAPI menu_slider_begin(int32_t section, int32_t index, uint32_t lo, uint32_t hi, uint32_t cur, uint32_t step, uint32_t coarse, int32_t callback, int32_t font); /* 0x44cb10 */
void GAMEAPI sound_menu_build(void); /* 0x451110 */
int32_t GAMEAPI eax_voice_fade_step(int32_t voice, uint32_t level, uint32_t step); /* 0x471580 */
void GAMEAPI voice_3d_place(int32_t voice, const int32_t *pos, const int32_t *vel, const uint8_t *mat, uint32_t a4); /* 0x472bb0 */
int32_t GAMEAPI sound_channels_refresh(void); /* 0x470ef0 */
void GAMEAPI pair_users_release(uint32_t key); /* 0x403950 */
void GAMEAPI object_trigger_1a(uint8_t *obj, uint32_t n, uint32_t value); /* 0x41ac20 */
void GAMEAPI rumble_update(void); /* 0x41c2c0 */
uint32_t GAMEAPI flag_4bcd54_clear(void); /* 0x4219c0 */
void GAMEAPI car_wheel_dust(uint8_t *car); /* 0x4219e0 */
void GAMEAPI object_trigger_set(uint8_t *obj, uint32_t n, uint32_t value); /* 0x426060 */
uint32_t GAMEAPI pause_pad_bits(uint32_t b); /* 0x427d90 */
void GAMEAPI sim_clock_start(void); /* 0x43693f */
uint32_t GAMEAPI sim_steps_due(void); /* 0x436958 */
void GAMEAPI particles_update(uint32_t unused1, uint32_t unused2, uint32_t no_tick); /* 0x441450 */
void GAMEAPI light_shape_step(uint8_t *light); /* 0x4416a0 */
void GAMEAPI light_fade_step(uint8_t *light, uint32_t index, uint32_t mode); /* 0x441720 */
void GAMEAPI light_object_step(uint8_t *light, uint32_t index); /* 0x4417e0 */
void GAMEAPI light_object_step2(uint8_t *light, uint32_t index); /* 0x441840 */
void GAMEAPI light_head_step(uint8_t *light, uint32_t index); /* 0x4418a0 */
void GAMEAPI trigger_queue_process(void); /* 0x442890 */
void GAMEAPI trigger_fire(uint8_t *obj); /* 0x442c20 */
int16_t *GAMEAPI body_point_world(uint8_t *body, const uint32_t *in, int16_t *out); /* 0x442c90 */
void GAMEAPI trigger_light_colour(uint8_t *obj, uint32_t code); /* 0x442e40 */
void GAMEAPI trigger_glint(uint32_t code); /* 0x442eb0 */
void GAMEAPI trigger_glint2(uint32_t code); /* 0x442f20 */
void GAMEAPI car_blast_hit(uint8_t *obj, const uint8_t *from); /* 0x4599c0 */
void GAMEAPI body_sparks(uint8_t *body, int32_t n); /* 0x45bd30 */
void GAMEAPI contact_particle(uint8_t *body, const uint8_t *contact, uint32_t kind); /* 0x45be10 */
uint32_t GAMEAPI pause_is_local(void); /* 0x463970 */
uint32_t GAMEAPI pause_frame(void); /* 0x463b70 */
void GAMEAPI pause_open(uint8_t **car_ref); /* 0x463ca0 */
void GAMEAPI pause_menu_input(void); /* 0x464030 */
void GAMEAPI camera_cycle(uint8_t *car, uint32_t delta); /* 0x469bb0 */
void GAMEAPI camera_setting_copy(uint8_t *dst, const uint8_t *src); /* 0x469c50 */
void GAMEAPI camera_view_copy(uint8_t *dst, const uint8_t *src); /* 0x469cd0 */
uint32_t GAMEAPI body_velocity_gap(uint8_t *a, const uint8_t *b); /* 0x46c1a0 */
uint32_t GAMEAPI blast_mark(uint8_t *car, const int32_t *dir, int32_t level); /* 0x46d760 */
void GAMEAPI wheel_skid_marks(uint8_t *w, int32_t slip, int32_t surface); /* 0x46ecb0 */
uint32_t GAMEAPI skid_mark_start(uint8_t *w, uint8_t *prev); /* 0x46ed80 */
uint32_t GAMEAPI skid_mark_point(uint8_t *w); /* 0x46ee30 */
uint32_t GAMEAPI skid_mark_moved(uint8_t *w); /* 0x46efa0 */
uint32_t GAMEAPI pair_list_remove(uint32_t key); /* 0x478430 */
void GAMEAPI snatch_mesh_update(uint8_t *obj); /* 0x48f930 */
void GAMEAPI sky_set(uint8_t *sky); /* 0x404480 */
void GAMEAPI sky_packets_build(uint8_t *arc); /* 0x404550 */
void GAMEAPI shock_falloff_apply(void); /* 0x4050f0 */
int32_t GAMEAPI shock_falloff(const int16_t *p); /* 0x405210 */
void GAMEAPI car_wheels_setup(uint8_t *car, uint32_t model, uint32_t side); /* 0x40da70 */
int32_t GAMEAPI car_hit_callback(uint8_t *car, uint8_t *obj, uint8_t *hit); /* 0x40eb60 */
void GAMEAPI car_hit_timer_start(uint8_t *car, int32_t force); /* 0x40fd40 */
void GAMEAPI car_hit_ai_hard(uint8_t *car, uint8_t *other, int32_t force); /* 0x410050 */
void GAMEAPI car_hit_ai_object(uint8_t *car, uint8_t *other, int32_t kind); /* 0x410090 */
void GAMEAPI car_hit_human(uint8_t *car, uint8_t *other, int32_t force); /* 0x4100e0 */
void GAMEAPI car_hit_human_object(uint8_t *car, uint8_t *other, int32_t kind); /* 0x410110 */
void GAMEAPI projection_init(void); /* 0x426090 */
int32_t GAMEAPI game_clock_read(void); /* 0x428290 */
void GAMEAPI effects_init(void); /* 0x42a610 */
void GAMEAPI smoke_pool_init(void); /* 0x42a710 */
void GAMEAPI hud_layout(void); /* 0x4377c0 */
int32_t GAMEAPI track_load(const char *name, uint8_t *track); /* 0x43fa60 */
void GAMEAPI object_face_blast(uint8_t *obj, int32_t face, int32_t *vel); /* 0x46bb50 */
void GAMEAPI camera_director(int32_t view); /* 0x408360 */
void GAMEAPI camera_place_offset(uint8_t *car, int32_t ox, int32_t oy, int32_t oz, uint8_t *out, int32_t *dir, int32_t flip); /* 0x40a720 */
uint32_t GAMEAPI controls_curve(int32_t value, int32_t unused, int32_t digital); /* 0x428e90 */
int32_t GAMEAPI sound_eax_open(void); /* 0x471370 */
int32_t GAMEAPI sound_eax_close(void); /* 0x4713b0 */
uint32_t GAMEAPI sound_eax_first(void); /* 0x471700 */
void GAMEAPI sound_cd_desc_init(uint8_t *p, uint32_t track); /* 0x471850 */
void GAMEAPI sound_cd_info_init(uint8_t *p); /* 0x471880 */
uint8_t GAMEAPI sound_voices_reset(uint32_t track); /* 0x471970 */
void GAMEAPI sound_volume_mix(uint32_t volume); /* 0x471a60 */
void GAMEAPI voice_set_near(uint32_t voice, uint32_t near); /* 0x471aa0 */
void GAMEAPI sound_settings_set(uint32_t a, uint32_t b, uint32_t c); /* 0x471ac0 */
void GAMEAPI sound_cd_volume(uint32_t left, uint32_t right); /* 0x471af0 */
void GAMEAPI sound_cd_volume_keep(uint32_t left, uint32_t right); /* 0x471b30 */
void GAMEAPI sound_volume_set(uint32_t volume); /* 0x471b80 */
void GAMEAPI sound_cd_channels(uint32_t a, uint32_t b); /* 0x471ba0 */
void GAMEAPI sound_cd_track(uint32_t track); /* 0x471c00 */
void GAMEAPI sound_cd_pause(uint32_t resume); /* 0x471c30 */
uint32_t GAMEAPI sound_play_flagged(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t flagged); /* 0x471d50 */
uint32_t GAMEAPI sound_moving_flagged(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t flagged); /* 0x471e60 */
void GAMEAPI voice_start(uint32_t voice, uint32_t kind, uint32_t left, uint32_t right, uint32_t pitch, uint32_t flags); /* 0x471eb0 */
void GAMEAPI sound_voice_stop(uint32_t voice); /* 0x471f40 */
void GAMEAPI voice_set_pitch(uint32_t voice, uint32_t pitch); /* 0x471f80 */
void GAMEAPI voice_set_volume(uint32_t voice, uint32_t left, uint32_t right); /* 0x471fc0 */
int32_t GAMEAPI sound_distance(const int32_t *a, const int32_t *b); /* 0x471ff0 */
void GAMEAPI sound_listener_set(uint32_t i, const uint32_t *pos, const uint32_t *vel, const uint32_t *mat); /* 0x472050 */
void GAMEAPI sound_cd_apply(const uint8_t *p); /* 0x4721f0 */
int32_t GAMEAPI sound_cd_play(uint32_t kind, const uint32_t *track); /* 0x472220 */
int32_t GAMEAPI sound_cd_command(uint32_t command); /* 0x472250 */
void GAMEAPI sound_mix(void); /* 0x4722d0 */
uint8_t GAMEAPI voice_alloc(uint32_t sound, uint32_t kind, int32_t vol, int32_t pitch, uint32_t near); /* 0x472920 */
uint32_t GAMEAPI sound_play_volume(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t vol); /* 0x472a40 */
int32_t GAMEAPI voice_play_plain(uint32_t sound, int32_t pitch, int32_t vol); /* 0x472a90 */
int32_t GAMEAPI voice_play_plain_volume(uint32_t sound, int32_t pitch, int32_t vol0, int32_t vol); /* 0x472b70 */
void GAMEAPI sound_debug_toggle(void); /* 0x472e90 */
void GAMEAPI sound_debug_tick(void); /* 0x472ed0 */
void GAMEAPI sound_engine_start(void); /* 0x472f10 */
void GAMEAPI sound_engine_restart(void); /* 0x472f70 */
void GAMEAPI race_music_start(void); /* 0x472f90 */
int32_t GAMEAPI snd_box_merge(uint8_t *holder, uint8_t *box); /* 0x46f080 */
int32_t GAMEAPI snd_vec_length(const int16_t *v); /* 0x46f1b0 */
int32_t GAMEAPI snd_init(void); /* 0x46f4f0 */
int32_t GAMEAPI snd_shutdown(void); /* 0x46f610 */
int32_t GAMEAPI snd_activate(int32_t on); /* 0x46f650 */
int32_t GAMEAPI snd_rate_for(int32_t rate); /* 0x46f6e0 */
uint32_t GAMEAPI snd_dsound(void); /* 0x46f720 */
int32_t GAMEAPI snd_set_rolloff(uint32_t rolloff); /* 0x46f760 */
int32_t GAMEAPI snd_channel_params(uint32_t *params, int32_t channel); /* 0x46f7a0 */
int32_t GAMEAPI snd_channel_kind(int32_t channel); /* 0x46f7f0 */
uint32_t GAMEAPI snd_channel_buffer(int32_t channel); /* 0x46f8f0 */
int32_t GAMEAPI snd_open_channels(void); /* 0x46f940 */
int32_t GAMEAPI snd_channels_reset(void); /* 0x46f970 */
uint8_t *GAMEAPI snd_wave_format(uint8_t *fmt, uint32_t tag, uint32_t channels, uint32_t rate, uint32_t bits); /* 0x46fa00 */
uint32_t GAMEAPI snd_wave_open(uint32_t data, uint32_t size, uint32_t format, uint32_t index); /* 0x46fa60 */
int32_t GAMEAPI snd_wave_free(uint32_t wave); /* 0x46fad0 */
void GAMEAPI snd_waves_free(void); /* 0x46fb00 */
uint32_t GAMEAPI snd_waves_load(uint32_t name); /* 0x46fb80 */
int32_t GAMEAPI snd_chan_play(int32_t wave, int32_t channel, uint32_t loop); /* 0x46fc40 */
int32_t GAMEAPI snd_chan_stop(int32_t channel); /* 0x46fd40 */
int32_t GAMEAPI snd_chan_pause(int32_t channel); /* 0x46fda0 */
int32_t GAMEAPI snd_chan_resume(int32_t channel); /* 0x46fe00 */
void GAMEAPI snd_pause_all(void); /* 0x46fe60 */
void GAMEAPI snd_resume_all(void); /* 0x46fe90 */
int32_t GAMEAPI snd_chan_busy(int32_t channel); /* 0x46fec0 */
int32_t GAMEAPI snd_chan_find(void); /* 0x46ff30 */
int32_t GAMEAPI snd_chan_claim(int32_t unused); /* 0x46ffa0 */
int32_t GAMEAPI snd_chan_frequency(int32_t channel, int32_t frequency); /* 0x46ffd0 */
int32_t GAMEAPI snd_chan_volume(int32_t channel, int32_t volume); /* 0x470040 */
int32_t GAMEAPI snd_chan_position(int32_t channel, uint32_t x, uint32_t y, uint32_t z); /* 0x4700b0 */
int32_t GAMEAPI snd_distance_map(void); /* 0x4702a0 */
int32_t GAMEAPI snd_options_apply(void); /* 0x470300 */
int32_t GAMEAPI snd_channels_enable(void); /* 0x470350 */
int32_t GAMEAPI snd_channel_limit(void); /* 0x470470 */
int32_t GAMEAPI snd_voice_flags(void); /* 0x4704b0 */
int32_t GAMEAPI snd_speakers(void); /* 0x470530 */
int32_t GAMEAPI snd_settings_apply(void); /* 0x4705d0 */
int32_t GAMEAPI snd_speaker_cycle(int32_t next, int32_t allow3); /* 0x4705f0 */
int32_t GAMEAPI snd_channel_count_add(int32_t delta); /* 0x470640 */
void GAMEAPI snd_stats_clear(void); /* 0x470680 */
uint32_t *GAMEAPI snd_zero16(uint32_t *p); /* 0x4706e0 */
uint32_t *GAMEAPI snd_zero6(uint32_t *p); /* 0x470720 */
int32_t GAMEAPI snd_profile_start(void); /* 0x470740 */
int32_t GAMEAPI snd_stats_format(char *dst); /* 0x470ca0 */
int32_t GAMEAPI snd_stats_log(void); /* 0x470e40 */
int32_t GAMEAPI game_load_step(void); /* 0x435b64 */
int32_t GAMEAPI net_session_start(void); /* 0x435df6 */
int32_t GAMEAPI tick_timer_start(void); /* 0x436565 */
void GAMEAPI tick_count_reset(void); /* 0x436595 */
void __attribute__((stdcall)) tick_callback(uint32_t id, uint32_t msg, uint32_t user, uint32_t a, uint32_t b); /* 0x4365ed */
void GAMEAPI quit_request(void); /* 0x43699c */
void GAMEAPI game_speed_set(void); /* 0x43775a */
void GAMEAPI fade_step(void); /* 0x430190 */
void GAMEAPI tick_hook_call(void); /* 0x429210 */
void GAMEAPI frame_counter_step(void); /* 0x463c70 */
void GAMEAPI frame_net_step(void); /* 0x435552 */
void GAMEAPI controls_gather(void); /* 0x4369b5 */
void GAMEAPI keyboard_update(void); /* 0x43c420 */
void GAMEAPI keyboard_init(void); /* 0x43c3f0 */
void GAMEAPI key_tables_init(void); /* 0x43c6a0 */
void GAMEAPI key_layout_set(uint32_t alt); /* 0x43d270 */
uint32_t GAMEAPI controls_read(int32_t player, int32_t no_axes); /* 0x427b10 */
uint32_t GAMEAPI controls_mask_menu_keys(int32_t player, uint32_t mask); /* 0x427d00 */
uint32_t GAMEAPI keyboard_nav_bits(void); /* 0x428d20 */
uint32_t GAMEAPI controls_nav_bits(uint32_t mask, int32_t player); /* 0x428d70 */
int32_t GAMEAPI joy_button_down(int32_t pad, int32_t button); /* 0x489770 */
int32_t GAMEAPI joy_pov_dir(int32_t pad, int32_t code); /* 0x4897c0 */
int32_t GAMEAPI joy_axis_x(int32_t pad); /* 0x4898e0 */
int32_t GAMEAPI joy_axis_y(int32_t pad); /* 0x4898f0 */
int32_t GAMEAPI joy_axis_z(int32_t pad); /* 0x489900 */
int32_t GAMEAPI car_position_sum(int32_t slot); /* 0x444050 */
int32_t GAMEAPI frontend_load(void); /* 0x4301f0 */
void GAMEAPI frontend_data_load(uint32_t models, uint32_t palette); /* 0x430c10 */
int32_t GAMEAPI frontend_backdrop_load(uint32_t unused, uint32_t entry); /* 0x430d80 */
int32_t GAMEAPI frontend_palette_set(void); /* 0x430e40 */
void GAMEAPI race_option_clamp(void); /* 0x433cd0 */
void GAMEAPI key_slots_reset(void); /* 0x439fd0 */
int32_t GAMEAPI menu_system_init(void); /* 0x4446f0 */
void GAMEAPI menu_sprites_setup(void); /* 0x4461d0 */
uint32_t GAMEAPI model_top_point(uint8_t *model); /* 0x453730 */
void GAMEAPI frontend_models_setup(void); /* 0x453780 */
void GAMEAPI particles_init(void); /* 0x462a20 */
void GAMEAPI game_heap_setup(void); /* 0x46a030 */
void GAMEAPI pad_config_reset(int32_t pad); /* 0x48a0e0 */
void GAMEAPI pad_configs_reset(void); /* 0x48a140 */
void GAMEAPI menu_dirty_set(void); /* 0x452d20 */
void GAMEAPI race_options_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e); /* 0x452fa0 */
void GAMEAPI driver_name(char *dst, int32_t index); /* 0x4484a0 */
int32_t GAMEAPI replay_files_load(void); /* 0x41c400 */
void GAMEAPI view_rect_set(int32_t x, int32_t y, int32_t w, int32_t h); /* 0x428850 */
char *GAMEAPI movie_path(const char *dir, const char *name); /* 0x42d5b0 */
void GAMEAPI movie_play(const char *path); /* 0x42d7e0 */
void GAMEAPI movie_psygnosis(void); /* 0x42da20 */
void GAMEAPI movie_atd(void); /* 0x42da40 */
void GAMEAPI movie_intro(void); /* 0x42da60 */
void GAMEAPI credits_play(void); /* 0x42da80 */
void GAMEAPI movie_cop_loses(void); /* 0x42db90 */
void GAMEAPI movie_cop_challenge(void); /* 0x42dbb0 */
void GAMEAPI movie_expert(void); /* 0x42dbd0 */
void GAMEAPI movie_win(int32_t language); /* 0x42dbf0 */
void GAMEAPI movie_texts_free(void); /* 0x42dc60 */
void GAMEAPI movie_text_add(int32_t side, uint32_t y, const char *text); /* 0x42dcb0 */
void GAMEAPI level_name_set(void); /* 0x430130 */
int32_t GAMEAPI rand_next(int32_t n); /* 0x430ae0 */
int32_t GAMEAPI rand_coin(void); /* 0x430b50 */
int32_t GAMEAPI mask_full(uint32_t mask, int32_t n); /* 0x430b70 */
int32_t GAMEAPI mask_pick(uint32_t mask, int32_t n, int32_t none); /* 0x430ba0 */
void GAMEAPI start_kind_set(void); /* 0x432b70 */
void GAMEAPI weapons_flags_set(int32_t kind, int32_t off); /* 0x432bd0 */
void GAMEAPI race_option554_set(void); /* 0x432c80 */
int32_t GAMEAPI race_init(void); /* 0x432ec0 */
int32_t GAMEAPI race_track_choose(int32_t kind, uint32_t value); /* 0x433670 */
int32_t GAMEAPI pick_next_random(uint8_t *p, int32_t n, uint32_t mask); /* 0x433980 */
int32_t GAMEAPI pick_next_step(uint8_t *p, int32_t n, uint32_t mask); /* 0x433a00 */
int32_t GAMEAPI track_choice(uint8_t *p, uint32_t world, int32_t choice); /* 0x433a50 */
void GAMEAPI race_slots_init(int32_t players, int32_t set_grid, int32_t reverse, int32_t fixed, int32_t other); /* 0x433ab0 */
int32_t GAMEAPI character_pick(int32_t fixed, int32_t other); /* 0x433bf0 */
int32_t GAMEAPI slot_value_free(uint32_t value); /* 0x433ca0 */
int32_t GAMEAPI front_end_return(void); /* 0x435607 */
int32_t GAMEAPI startup_movies(void); /* 0x4359f1 */
void GAMEAPI debug_key_event(int32_t key); /* 0x43605a */
void GAMEAPI loading_screen_show(uint32_t kind); /* 0x436b9d */
uint32_t GAMEAPI archive_find_by_name(uint32_t name); /* 0x43b550 */
uint32_t GAMEAPI archive_load_by_index(uint32_t index); /* 0x43b630 */
uint32_t GAMEAPI file_open_any(uint32_t name); /* 0x43b6c0 */
void GAMEAPI file_close_any(uint32_t handle); /* 0x43b760 */
uint32_t GAMEAPI file_slot_find(uint32_t handle); /* 0x43b7b0 */
uint32_t GAMEAPI file_read_any(uint32_t handle, uint8_t *dest, uint32_t size); /* 0x43b7f0 */
int32_t GAMEAPI bitmap_scan(const uint8_t *start, int32_t step, int32_t count, int32_t across, int32_t width); /* 0x443260 */
int32_t GAMEAPI font_cells_build(const char *name, int32_t cell, uint8_t *out, int32_t row, int32_t rows, int32_t count); /* 0x443780 */
void GAMEAPI font_map_init(void); /* 0x443a10 */
void GAMEAPI font_image_release(void); /* 0x443b00 */
void GAMEAPI font_image_info(uint32_t *pixels, uint32_t *palette, int32_t *stride); /* 0x443b20 */
int32_t GAMEAPI movie_mode(void); /* 0x444c50 */
int32_t GAMEAPI credits_wanted(void); /* 0x444ca0 */
int32_t GAMEAPI movies_wanted(void); /* 0x444ce0 */
void GAMEAPI movie_texts_build(void); /* 0x444d00 */
void GAMEAPI setting5_next(void); /* 0x4483c0 */
void GAMEAPI track_title(uint8_t *buf, int32_t index); /* 0x448580 */
int32_t GAMEAPI registry_load(uint8_t *buf, int32_t size, uint32_t name); /* 0x474030 */
int32_t GAMEAPI replay_load(void); /* 0x474200 */
int32_t GAMEAPI registry_save(uint8_t *buf, uint32_t size, uint32_t name); /* 0x474230 */
int32_t GAMEAPI boot_data_save(void); /* 0x474260 */
int32_t GAMEAPI replay_save(void); /* 0x474330 */
void GAMEAPI table_copy(int32_t op, const uint32_t *table); /* 0x474500 */
void GAMEAPI settings_table_load(void); /* 0x4745d0 */
void GAMEAPI settings_screen_store(void); /* 0x4746b0 */
int32_t GAMEAPI text_width(const uint8_t *text); /* 0x488c30 */
void GAMEAPI flags_5d622c_clear(void); /* 0x48d370 */
int32_t GAMEAPI cpu_detect_amd(void); /* 0x401000 */
void GAMEAPI records_defaults(void); /* 0x405320 */
void GAMEAPI records_mark_old(void); /* 0x405340 */
void GAMEAPI time_entry_lap(uint8_t *out, int32_t slot); /* 0x405570 */
void GAMEAPI time_entry_race(uint8_t *out, int32_t slot); /* 0x4055b0 */
void GAMEAPI record_insert_slot(int32_t track, int32_t slot); /* 0x4055d0 */
int32_t GAMEAPI record_find_rank(const uint8_t *entry, int32_t track); /* 0x405620 */
int32_t GAMEAPI record_beats(const uint8_t *old, const uint8_t *entry); /* 0x405680 */
void GAMEAPI record_insert_at(const uint8_t *entry, int32_t track, int32_t rank); /* 0x405720 */
void GAMEAPI records_insert_players(int32_t track); /* 0x4057c0 */
int32_t GAMEAPI strings_load(int32_t entry); /* 0x42df90 */
void GAMEAPI strings_unload(void); /* 0x42dfa0 */
void GAMEAPI strings_free(void); /* 0x42dfb0 */
void GAMEAPI strings_load_entry(int32_t entry); /* 0x42dfe0 */
void GAMEAPI frontend_buffers_free(void); /* 0x42e350 */
void GAMEAPI language_set(uint32_t language); /* 0x42e3c0 */
void GAMEAPI frontend_free_all(void); /* 0x430ac0 */
void GAMEAPI frontend_textures_free(void); /* 0x430e00 */
int32_t __attribute__((stdcall)) game_main(uint32_t instance, uint32_t previous, char *line, int32_t show); /* 0x4351e0 */
void GAMEAPI shutdown_final(void); /* 0x435515 */
int32_t GAMEAPI startup_init(char *line); /* 0x4356ab */
int32_t GAMEAPI startup_window(uint32_t previous); /* 0x4358b0 */
int32_t GAMEAPI startup_options(void); /* 0x435932 */
int32_t GAMEAPI startup_sound(void); /* 0x4359d3 */
int32_t GAMEAPI startup_direct3d(void); /* 0x435aae */
void GAMEAPI shutdown_all(int32_t quit); /* 0x435e2c */
void GAMEAPI shutdown_restart(void); /* 0x435e99 */
int32_t GAMEAPI timer_stop(void); /* 0x4365a4 */
char *GAMEAPI stristr(char *hay, const char *needle); /* 0x436688 */
int32_t GAMEAPI command_line_parse(char *line); /* 0x43677b */
int32_t GAMEAPI startup_mode_dialog(void); /* 0x4370b2 */
void GAMEAPI strings_load_once(void); /* 0x43756d */
void GAMEAPI error_dialog(void); /* 0x43758f */
int32_t GAMEAPI cpu_detect_intel(void); /* 0x43c320 */
void GAMEAPI menu_table_free(void); /* 0x444790 */
void GAMEAPI race_result_flow(void); /* 0x444800 */
void GAMEAPI league_won_texts(void); /* 0x444dd0 */
void GAMEAPI config_flag_9d_set(void); /* 0x445160 */
void GAMEAPI race_rankings(int32_t points, int32_t ghost); /* 0x445170 */
void GAMEAPI result_menu_return(void); /* 0x4480b0 */
void GAMEAPI config_series_set(int32_t series); /* 0x4498d0 */
void GAMEAPI bindings_reset_all(void); /* 0x4498f0 */
void GAMEAPI bindings_reset_player(int32_t set); /* 0x449910 */
void GAMEAPI binding_default(int32_t set, int32_t activity, uint32_t save); /* 0x449960 */
int32_t GAMEAPI binding_from_table(int32_t set, int32_t activity, int32_t device); /* 0x449ab0 */
uint8_t *GAMEAPI binding_saved_get(int32_t set, int32_t activity); /* 0x449c40 */
int32_t GAMEAPI activity_index(int32_t activity); /* 0x449c70 */
void GAMEAPI network_average(int32_t *out); /* 0x45e890 */
void GAMEAPI config_default_texts(int32_t all); /* 0x473c50 */
void GAMEAPI config_defaults(void); /* 0x473d50 */
int32_t GAMEAPI config_load(void); /* 0x4740c0 */
void GAMEAPI replays_delete(int32_t registry); /* 0x4743e0 */
void GAMEAPI settings_load_65(void); /* 0x4745e0 */
void GAMEAPI config_display_apply(void); /* 0x4745f0 */
uint32_t GAMEAPI language_from_registry(void); /* 0x474770 */
void GAMEAPI chain_event_signal(void); /* 0x474cb0 */
void GAMEAPI command_token_copy(char *dst, const char *src); /* 0x474dc0 */
void GAMEAPI unquote_copy(char *dst, const char *src); /* 0x474e80 */
int32_t GAMEAPI joystick_usable(int32_t device); /* 0x48a5c0 */
int32_t GAMEAPI track_record_held_by(int32_t track, int32_t slot); /* 0x405380 */
void GAMEAPI track_records_rename(int32_t track, int32_t slot); /* 0x405860 */
uint32_t *GAMEAPI word_exchange(uint32_t address, uint32_t value, uint32_t *old); /* 0x429080 */
int32_t GAMEAPI font_scaled_size(int32_t font); /* 0x42e090 */
int32_t GAMEAPI frontend_texture_width(int32_t index); /* 0x431670 */
int32_t GAMEAPI frontend_texture_height(int32_t index); /* 0x4316a0 */
uint32_t GAMEAPI timer_ratio(void); /* 0x436b7e */
uint32_t GAMEAPI key_code_lookup(int32_t code); /* 0x43d230 */
void GAMEAPI menu_text_box(uint32_t a1, uint32_t unused, uint32_t a3); /* 0x446200 */
int32_t GAMEAPI menu_add_item(const char *section, int32_t index, uint32_t a3, uint32_t a4, uint32_t a5); /* 0x448140 */
char *GAMEAPI menu_text_get(char *dst, int32_t size, const char *section, int32_t index); /* 0x4481a0 */
uint8_t *GAMEAPI menu_item_label(uint8_t *item, const char *text); /* 0x448260 */
int32_t GAMEAPI menu_item_commit(uint32_t a1, uint32_t a2, uint32_t a3, uint8_t type); /* 0x4482a0 */
void GAMEAPI menu_player_title(char *dst, int32_t player); /* 0x448430 */
int32_t GAMEAPI series_menu_kind(int32_t series); /* 0x448770 */
int32_t GAMEAPI series_track_get(int32_t series); /* 0x4487c0 */
void GAMEAPI series_track_set(int32_t series, uint8_t track); /* 0x448820 */
void GAMEAPI menu_set_title(const char *section, int32_t index); /* 0x448a00 */
void GAMEAPI name_entry_text(const char *text, int32_t width, int32_t editing, int32_t font); /* 0x448ec0 */
uint32_t GAMEAPI name_entry_setup(int32_t which, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7); /* 0x449060 */
void GAMEAPI network_lobby_build(void); /* 0x449fd0 */
void GAMEAPI menu_text_slot_set(int32_t item, uint32_t slot, const char *text); /* 0x44a1c0 */
int32_t GAMEAPI menu_add_choice(const char *section1, int32_t index1, const char *section2, int32_t index2, int32_t(GAMEAPI *callback)(int32_t command, char *text), uint32_t a6); /* 0x44c6d0 */
int32_t GAMEAPI menu_add_toggle(const char *section, int32_t index, int32_t(GAMEAPI *callback)(int32_t command, char *text), uint32_t a4, const char *section_a, int32_t index_a, const char *section_b, int32_t index_b); /* 0x44c850 */
int32_t GAMEAPI menu_add_on_off(const char *section, int32_t index, int32_t(GAMEAPI *callback)(int32_t command, char *text), uint32_t a4); /* 0x44c950 */
int32_t GAMEAPI menu_add_label(const char *section, int32_t index); /* 0x44c980 */
int32_t GAMEAPI menu_add_slider(const char *section1, int32_t index1, const char *section2, int32_t index2, int32_t(GAMEAPI *callback)(int32_t command, char *text), uint32_t a6); /* 0x44c9d0 */
int32_t GAMEAPI menu_open_dialog(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4); /* 0x44e430 */
void GAMEAPI series_validate(void); /* 0x44e560 */
void GAMEAPI track_menu_build(void); /* 0x44e8d0 */
void GAMEAPI track_menu_choice_fix(void); /* 0x44ea70 */
int32_t GAMEAPI menu_go_3(uint32_t a, uint32_t b); /* 0x44efc0 */
int32_t GAMEAPI menu_go_4(uint32_t a, uint32_t b); /* 0x44efe0 */
void GAMEAPI new_record_menu(int32_t track, int32_t slot, uint32_t a3, uint32_t a4); /* 0x44f2d0 */
int32_t GAMEAPI new_record_check(int32_t slot, uint32_t a2); /* 0x44f560 */
int32_t GAMEAPI audio_speaker_item(void); /* 0x451630 */
int32_t GAMEAPI audio_shuffle_item(void); /* 0x4516e0 */
void GAMEAPI audio_detect_menu(void); /* 0x451800 */
void GAMEAPI controls_menu_build(int32_t player); /* 0x451c00 */
void GAMEAPI menu_text_next_set(int32_t index); /* 0x451ec0 */
void GAMEAPI activity_name(char *dst, int32_t activity); /* 0x451ed0 */
void GAMEAPI binding_description(char *dst, int32_t player, int32_t activity); /* 0x451ef0 */
void GAMEAPI binding_text(char *dst, const uint8_t *binding); /* 0x451f10 */
void GAMEAPI input_text(char *dst, int32_t kind, int32_t code); /* 0x451f40 */
void GAMEAPI key_text(char *dst, int32_t code); /* 0x451fc0 */
void GAMEAPI joystick_axis_text(char *dst, uint32_t code); /* 0x452130 */
void GAMEAPI joystick_button_text(char *dst, int32_t button); /* 0x452220 */
void GAMEAPI joystick_hat_text(char *dst, int32_t direction); /* 0x4522a0 */
int32_t GAMEAPI menu_settled(void); /* 0x452ff0 */
void GAMEAPI network_chat_post(const char *text); /* 0x45d150 */
char *GAMEAPI network_players_line(void); /* 0x45f430 */
void GAMEAPI network_player_slots(int32_t *count, uint32_t *ids, uint32_t *records); /* 0x45f5e0 */
int32_t GAMEAPI network_send_setting(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5); /* 0x45f710 */
int32_t GAMEAPI sound_voices_crowded(void); /* 0x470e70 */
int32_t GAMEAPI sound_voice_flag8(void); /* 0x470eb0 */
int32_t GAMEAPI network_sessions_list(int32_t max, int32_t *count, uint32_t *names, uint32_t *ids); /* 0x48b780 */
void GAMEAPI menu_water_update(int32_t poke, uint32_t corner); /* 0x48d630 */
uint8_t *GAMEAPI player_name_at(int32_t slot); /* 0x4053e0 */
void GAMEAPI time_entry_make(uint8_t *out, int32_t slot, int32_t laps); /* 0x405460 */
void GAMEAPI time_entry_fill(uint8_t *out, int32_t slot, int32_t lap, int32_t laps); /* 0x405480 */
uint32_t GAMEAPI record_is_new(int32_t track, int32_t place); /* 0x4058f0 */
uint32_t GAMEAPI record_is_players(int32_t track, int32_t place); /* 0x405930 */
char *GAMEAPI record_name(char *out, int32_t track, int32_t place); /* 0x405970 */
void GAMEAPI record_time_text(char *out, int32_t track, int32_t place); /* 0x405a10 */
void GAMEAPI bcd_time_text(char *out, const uint8_t *bcd); /* 0x405a80 */
void GAMEAPI hex_pair(char *out, uint32_t value); /* 0x405b20 */
void GAMEAPI slot_time_text(char *out, int32_t slot, int32_t laps); /* 0x405b70 */
void GAMEAPI slot_lap_time_text(char *out, int32_t slot); /* 0x405ba0 */
void GAMEAPI slot_race_time_text(char *out, int32_t slot); /* 0x405bf0 */
int32_t GAMEAPI track_world(int32_t track); /* 0x433550 */
int32_t GAMEAPI track_world_number(int32_t track); /* 0x433570 */
int32_t GAMEAPI track_world_place(int32_t track); /* 0x4335c0 */
int32_t GAMEAPI world_track(int32_t world, int32_t place); /* 0x4335e0 */
void GAMEAPI attract_timer_start(void); /* 0x448000 */
void GAMEAPI player_network_name(char *out, int32_t slot); /* 0x448530 */
int32_t GAMEAPI world_track_count(int32_t world); /* 0x448670 */
uint32_t GAMEAPI world_locked_tracks(int32_t world); /* 0x4486b0 */
const char *GAMEAPI world_track_names(int32_t world); /* 0x4486f0 */
int32_t GAMEAPI menu_text_add(const char *s, uint32_t flags, int32_t colour, int32_t x, int32_t y, int32_t a, int32_t b, int32_t palette, int32_t c); /* 0x448a50 */
int32_t GAMEAPI menu_back_footer(int32_t back, int32_t x, int32_t y, int32_t c); /* 0x448af0 */
int32_t GAMEAPI menu_back_entry(int32_t back, int32_t index, int32_t x, int32_t y, int32_t c); /* 0x448b10 */
int32_t GAMEAPI menu_entry_select(int32_t entry); /* 0x448b50 */
int32_t GAMEAPI menu_common_entry(int32_t back, int32_t index, int32_t x, int32_t y, int32_t c); /* 0x448ba0 */
void GAMEAPI menu_common_footer(int32_t unused, int32_t c); /* 0x448be0 */
void GAMEAPI menu_entry_with_text(const char *table, int32_t index, const char *text_table, int32_t text_index, int32_t a, int32_t b, int32_t c); /* 0x448c00 */
void GAMEAPI deathmatch_report(int32_t unused, int32_t players, int32_t x, int32_t y); /* 0x448d70 */
int32_t GAMEAPI option_enabled_entry(int32_t a, int32_t b, int32_t c, int32_t d); /* 0x44c820 */
void GAMEAPI player_name_entry(int32_t a, int32_t b); /* 0x44dfd0 */
void GAMEAPI league_title(char *out, int32_t league); /* 0x44e2a0 */
int32_t GAMEAPI menu_continue_entry(int32_t back, int32_t x, int32_t y, int32_t c); /* 0x44e410 */
void GAMEAPI world_menu(int32_t x, int32_t y); /* 0x44e470 */
void GAMEAPI worlds_menu_plain(int32_t a, int32_t b, int32_t c, int32_t d); /* 0x44e5b0 */
void GAMEAPI worlds_menu(int32_t x, int32_t y); /* 0x44e640 */
void GAMEAPI records_menu(int32_t world, int32_t place, int32_t x, int32_t y); /* 0x44ebf0 */
void GAMEAPI records_menu_world2(int32_t x, int32_t y); /* 0x44efa0 */
void GAMEAPI records_menu_track(int32_t track, int32_t x, int32_t y); /* 0x44f000 */
void GAMEAPI splitscreen_report(int32_t unused, int32_t players, int32_t points, int32_t x, int32_t y); /* 0x44f030 */
void GAMEAPI game_points_menu(int32_t x, int32_t y, int32_t highlight, int32_t names); /* 0x44f5a0 */
void GAMEAPI players_menu(int32_t x, int32_t y); /* 0x44fdb0 */
int32_t GAMEAPI options_submenu(int32_t a); /* 0x44ffb0 */
int32_t GAMEAPI setup_difficulty_option(int32_t a, int32_t b); /* 0x450210 */
int32_t GAMEAPI timewarp_texture_option(void); /* 0x450620 */
int32_t GAMEAPI skidmarks_option(void); /* 0x450740 */
int32_t GAMEAPI weapons_option(void); /* 0x450e30 */
int32_t GAMEAPI mirror_option(void); /* 0x450ef0 */
int32_t GAMEAPI weather_option(void); /* 0x450f50 */
void GAMEAPI league_menu(int32_t x, int32_t y, int32_t options, int32_t done); /* 0x451900 */
void GAMEAPI network_players_list(int32_t *count, uint8_t **out); /* 0x45f570 */
int32_t GAMEAPI network_player_set(int32_t slot, const char *name, int32_t id); /* 0x48be20 */
uint32_t GAMEAPI fe_time(void); /* 0x436935 */
int32_t GAMEAPI fe_joy_axis(int32_t pad, int32_t axis); /* 0x489890 */
int32_t GAMEAPI fe_joy_valid(int32_t pad); /* 0x48a020 */
int32_t GAMEAPI fe_joy_button_count(int32_t pad); /* 0x48a040 */
int32_t GAMEAPI fe_joy_button_valid(int32_t pad, int32_t button); /* 0x48a070 */
int32_t GAMEAPI fe_joy_button_down(int32_t pad, int32_t button); /* 0x48a0a0 */
int32_t GAMEAPI fe_joy_axis_edge(int32_t pad, int32_t axis); /* 0x48a170 */
int32_t GAMEAPI fe_joy_x_edge(int32_t pad); /* 0x48a250 */
int32_t GAMEAPI fe_joy_y_edge(int32_t pad); /* 0x48a260 */
int32_t GAMEAPI fe_joy_button_edge(int32_t pad, int32_t button); /* 0x48a270 */
uint32_t GAMEAPI fe_menu_pad(int32_t player); /* 0x449e10 */
int32_t GAMEAPI fe_menu_axis_x(int32_t player, int32_t negative, int32_t positive, int32_t none); /* 0x449db0 */
int32_t GAMEAPI fe_menu_axis_y(int32_t player, int32_t negative, int32_t positive, int32_t none); /* 0x449e80 */
int32_t GAMEAPI fe_menu_action_pressed(int32_t player, uint32_t action); /* 0x449f10 */
int32_t GAMEAPI fe_menu_select_pressed(int32_t player); /* 0x449ee0 */
int32_t GAMEAPI fe_menu_back_pressed(int32_t player); /* 0x449f80 */
void GAMEAPI fe_menu_input_poll(void); /* 0x42de70 */
void GAMEAPI fe_history_clear(void); /* 0x4447d0 */
void GAMEAPI fe_menu_item_clear(int32_t index); /* 0x445580 */
void GAMEAPI fe_menu_items_clear(void); /* 0x445560 */
void GAMEAPI fe_menu_panels_reset(void); /* 0x445730 */
void GAMEAPI fe_menu_title_reset(void); /* 0x445760 */
void GAMEAPI fe_menu_reset(void); /* 0x445480 */
int32_t GAMEAPI fe_repeat(uint32_t *timer, int32_t enable, uint32_t first, uint32_t next); /* 0x447540 */
int32_t GAMEAPI fe_repeat_updown(int32_t enable); /* 0x4475d0 */
int32_t GAMEAPI fe_repeat_select(int32_t enable); /* 0x4475f0 */
int32_t GAMEAPI fe_repeat_leftright(int32_t enable); /* 0x447610 */
int32_t GAMEAPI fe_repeat_page(int32_t enable); /* 0x447630 */
void GAMEAPI fe_history_push(int32_t from, uint32_t screen); /* 0x447a60 */
int32_t GAMEAPI fe_history_pop(void); /* 0x447b40 */
void GAMEAPI fe_menu_goto_ex(int32_t screen, int32_t mode, int32_t fade); /* 0x4479f0 */
void GAMEAPI fe_menu_goto(int32_t screen, int32_t mode); /* 0x4479d0 */
void GAMEAPI fe_menu_back(void); /* 0x447ab0 */
void GAMEAPI fe_menu_cursor_move(int32_t up); /* 0x447b60 */
void GAMEAPI fe_menu_item_toggle(int32_t value); /* 0x447cb0 */
void GAMEAPI fe_menu_item_spin(int32_t value); /* 0x447c40 */
void GAMEAPI fe_menu_item_adjust(int32_t back, int32_t kind); /* 0x447bd0 */
int32_t GAMEAPI fe_menu_action(int32_t action); /* 0x447650 */
int32_t GAMEAPI fe_menu_action_b(void); /* 0x447ef0 */
int32_t GAMEAPI fe_attract_left(int32_t scale); /* 0x448030 */
int32_t GAMEAPI fe_attract_due(void); /* 0x448070 */
void GAMEAPI fe_menu_text_box(const uint8_t *src, uint8_t *dst, uint32_t size, int32_t a, int32_t b); /* 0x4480d0 */
void GAMEAPI fe_split_screen_label(char *dst); /* 0x44e320 */
void GAMEAPI fe_world_label(char *dst, int32_t unused, int32_t world); /* 0x44e840 */
void GAMEAPI fe_world_row(int32_t row); /* 0x44e800 */
void GAMEAPI fe_quit_confirm_menu(void); /* 0x44e3c0 */
void GAMEAPI fe_message(int32_t a, int32_t b); /* 0x44ebd0 */
void GAMEAPI fe_menu_buttons3(int32_t a, int32_t b, int32_t c, int32_t d); /* 0x44faa0 */
void GAMEAPI fe_league_points(int32_t buttons3, int32_t a, int32_t b, int32_t highlight); /* 0x44f860 */
void GAMEAPI fe_option_reflections(void); /* 0x450560 */
void GAMEAPI fe_option_wormhole_trails(void); /* 0x4505c0 */
void GAMEAPI fe_option_bilinear(void); /* 0x450930 */
void GAMEAPI fe_option_dithering(void); /* 0x4509a0 */
void GAMEAPI fe_option_clouds(void); /* 0x450a10 */
void GAMEAPI fe_option_daytime(void); /* 0x451040 */
uint32_t GAMEAPI fe_net_player_active(int32_t player); /* 0x48cfd0 */
uint32_t GAMEAPI fe_net_checksum(uint8_t *packet, uint32_t size); /* 0x48cab0 */
uint32_t GAMEAPI fe_net_sequence_next(void); /* 0x48c920 */
int32_t GAMEAPI fe_net_leave(void); /* 0x45e550 */
void GAMEAPI fe_net_race_abort(void); /* 0x45d170 */
void GAMEAPI fe_net_player_left(int32_t player); /* 0x45cb90 */
int32_t GAMEAPI fe_net_drop_player(uint32_t player); /* 0x48bf70 */
int32_t GAMEAPI menu_after_race(void); /* 0x42dd60 */
uint32_t GAMEAPI menu_return(void); /* 0x42de50 */
char *GAMEAPI network_messages_title(void); /* 0x45f490 */
uint32_t GAMEAPI network_messages_menu(void); /* 0x44a260 */
uint32_t GAMEAPI multiplayer_menu(void); /* 0x4518a0 */
uint32_t GAMEAPI laps_menu(uint32_t handler, int32_t arg); /* 0x44ffd0 */
uint32_t GAMEAPI laps_menu_race(int32_t arg); /* 0x4500f0 */
uint32_t GAMEAPI laps_menu_other(int32_t arg); /* 0x4501a0 */
uint32_t GAMEAPI option_meteors_add(void); /* 0x44fed0 */
uint32_t GAMEAPI option_shield_texture_add(void); /* 0x450680 */
uint32_t GAMEAPI option_water_reflections_add(void); /* 0x4506e0 */
uint32_t GAMEAPI option_rubble_add(void); /* 0x450810 */
uint32_t GAMEAPI option_ice_sheet_add(void); /* 0x450870 */
uint32_t GAMEAPI option_speed_glow_add(void); /* 0x4508d0 */
uint32_t GAMEAPI option_lov_add(void); /* 0x450ba0 */
uint32_t GAMEAPI option_catch_up_add(void); /* 0x450e90 */
uint32_t GAMEAPI option_meteors(int32_t event, int32_t arg); /* 0x44fef0 */
uint32_t GAMEAPI option_shield_texture(int32_t event); /* 0x4506a0 */
uint32_t GAMEAPI option_water_reflections(int32_t event); /* 0x450700 */
uint32_t GAMEAPI option_rubble(int32_t event); /* 0x450830 */
uint32_t GAMEAPI option_ice_sheet(int32_t event); /* 0x450890 */
uint32_t GAMEAPI option_speed_glow(int32_t event); /* 0x4508f0 */
uint32_t GAMEAPI option_lov(int32_t event); /* 0x450bc0 */
uint32_t GAMEAPI option_catch_up(int32_t event); /* 0x450eb0 */
uint32_t GAMEAPI option_laps_race(int32_t event, int32_t arg); /* 0x450110 */
uint32_t GAMEAPI option_laps_other(int32_t event, int32_t arg); /* 0x4501c0 */
int32_t GAMEAPI network_menu_mode(int32_t mode); /* 0x45f500 */
int32_t GAMEAPI car_stat_bar(uint32_t stat, int32_t car); /* 0x456f70 */
uint8_t *GAMEAPI menu_model_find(int32_t id); /* 0x4539d0 */
uint8_t *GAMEAPI matrix_identity(uint8_t *m); /* 0x4264f0 */
int32_t GAMEAPI grid_highlight_set(int32_t cell); /* 0x490ec0 */
int32_t GAMEAPI time_parts(uint8_t *out, int32_t a, int32_t b); /* 0x405590 */
int32_t GAMEAPI time_parts_text(char *out, int32_t a, int32_t b); /* 0x405bc0 */
int32_t GAMEAPI front_mode_start(uint32_t mode); /* 0x4329b0 */
uint8_t *GAMEAPI menu_item_reset(uint8_t *item, int32_t shown, int32_t origin); /* 0x445650 */
uint8_t *GAMEAPI menu_row_reset(uint8_t *row); /* 0x4455c0 */
int32_t GAMEAPI menu_extra_row_reset(void); /* 0x4456e0 */
int32_t GAMEAPI menu_item_place(uint8_t *item, int32_t x, int32_t y, int32_t hjust, int32_t vjust); /* 0x445f50 */
int32_t GAMEAPI menu_loose_items_place(void); /* 0x446180 */
int32_t GAMEAPI menu_title_place(void); /* 0x4461b0 */
int32_t GAMEAPI menu_layout(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2); /* 0x445790 */
int32_t GAMEAPI menu_back_to_main(void); /* 0x447ed0 */
int32_t GAMEAPI lap_title_text(char *out, int32_t lap); /* 0x4483f0 */
int32_t GAMEAPI world_number(int32_t world); /* 0x448730 */
int32_t GAMEAPI track_title_text(char *out, int32_t world, int32_t track); /* 0x4485b0 */
int32_t GAMEAPI time_attack_report(int32_t unused, int32_t times, int32_t a, int32_t b); /* 0x4488c0 */
void GAMEAPI front_fade_in(void); /* 0x44ce50 */
int32_t GAMEAPI driver_choose(int32_t unused, int32_t car); /* 0x44fcd0 */
int32_t GAMEAPI driver_menu(uint32_t player, int32_t a, int32_t b); /* 0x44fae0 */
int32_t GAMEAPI driver_menu_1(int32_t a, int32_t b); /* 0x44fac0 */
int32_t GAMEAPI driver_menu_2(int32_t a, int32_t b); /* 0x44fd50 */
int32_t GAMEAPI driver_menu_3(int32_t a, int32_t b); /* 0x44fd70 */
int32_t GAMEAPI driver_menu_4(int32_t a, int32_t b); /* 0x44fd90 */
int32_t GAMEAPI list_menu_open(void); /* 0x450150 */
int32_t GAMEAPI difficulty_option(int32_t a); /* 0x450460 */
void GAMEAPI menu_input_delay(void); /* 0x452940 */
uint32_t GAMEAPI network_flag9_set(uint32_t on); /* 0x45f790 */
int32_t GAMEAPI network_buffer_reset(void); /* 0x48c990 */
int32_t GAMEAPI network_restart(void); /* 0x48a840 */
int32_t GAMEAPI serial_baud(int32_t choice); /* 0x48cff0 */
int32_t GAMEAPI serial_stop_bits(int32_t choice); /* 0x48d070 */
int32_t GAMEAPI serial_parity(int32_t choice); /* 0x48d090 */
int32_t GAMEAPI serial_flow(int32_t choice); /* 0x48d0e0 */
int32_t GAMEAPI frontend_frame(void); /* 0x4302d0 */
void GAMEAPI menu_screen_build(void); /* 0x44a2a0 */
uint32_t GAMEAPI ai_difficulty_band(void); /* 0x47ff40 */
void GAMEAPI ai_lines_open_short(void); /* 0x47ffd0 */
void GAMEAPI ai_bodies_tall_faces(void); /* 0x403750 */
void GAMEAPI ai_pick_rivals(void); /* 0x411010 */
void GAMEAPI ai_skill_init(uint8_t *car); /* 0x40f3e0 */
void GAMEAPI ai_car_race_start(uint8_t *slot); /* 0x401470 */
void GAMEAPI light_cells_add(uint32_t index, const uint32_t *cells); /* 0x441000 */
void GAMEAPI light_set(uint32_t index, const int32_t *pos, const uint32_t *colour, uint32_t radius,
                       const uint32_t *cells, const uint32_t *shape, uint8_t *object); /* 0x4412e0 */
void GAMEAPI track_lights_init(void); /* 0x441b90 */
void GAMEAPI pieces_init(void); /* 0x462360 */
void GAMEAPI tyre_trails_init(void); /* 0x481e50 */
void GAMEAPI slot_glow_init(uint8_t *car, int32_t index); /* 0x490390 */
void GAMEAPI countdown_init(void); /* 0x46a940 */
void GAMEAPI glints_clear(void); /* 0x4430f0 */
uint32_t GAMEAPI sound_device_ready(void); /* 0x428060 */
uint32_t GAMEAPI sound_output_mode(uint32_t mode); /* 0x471a70 */
void GAMEAPI sound_listeners_set(uint32_t pos0, uint32_t vel0, uint32_t mat0, uint32_t pos1, uint32_t vel1,
                                 uint32_t mat1, uint32_t pos2, uint32_t vel2, uint32_t mat2, uint32_t pos3,
                                 uint32_t vel3, uint32_t mat3); /* 0x4718d0 */
void GAMEAPI race_sounds_init(void); /* 0x4730b0 */
int32_t GAMEAPI list_push(uint32_t *head, uint32_t *item); /* 0x428070 */
void GAMEAPI fonts_init(int32_t small_only); /* 0x42e0c0 */
void GAMEAPI text_redraw_set(void); /* 0x45cfd0 */
void GAMEAPI text_reset(void); /* 0x478ff0 */
void GAMEAPI image_release(uint8_t *image); /* 0x477810 */
void GAMEAPI race_fade_init(int32_t unused); /* 0x463930 */
void GAMEAPI replay_state_clear(void); /* 0x463990 */
/* setup_w.c: race setup: shield ripple, views, track water, chase camera records, weather tint, pads, ticks */
void GAMEAPI shield_uv_corners(uint16_t *page, uint16_t *base, uint16_t *u_end, uint16_t *v_end, uint16_t *far); /* 0x46d270 */
void GAMEAPI shield_link_add(uint32_t a, uint32_t b, uint32_t c); /* 0x46d590 */
void GAMEAPI shield_mesh_links(void); /* 0x46d320 */
void GAMEAPI shield_record_init(uint8_t *r); /* 0x46d600 */
void GAMEAPI shield_uvs(void); /* 0x46e160 */
int32_t GAMEAPI shield_textures_draw(void); /* 0x46d250 */
void GAMEAPI shield_setup(void); /* 0x46d2b0 */
int32_t GAMEAPI rect_set(int16_t *r, int32_t x, int32_t y, int32_t w, int32_t h); /* 0x4291c0 */
int16_t *GAMEAPI viewport_apply(int16_t *r); /* 0x4265d0 */
const uint32_t *GAMEAPI view_keep(const uint32_t *view); /* 0x426600 */
void GAMEAPI view_angles_reset(void); /* 0x423a90 */
uint32_t GAMEAPI views_setup(void); /* 0x4238c0 */
void GAMEAPI views_init(void); /* 0x423880 */
void GAMEAPI screen_waits(void); /* 0x43c300 */
void GAMEAPI heap_and_views_init(void); /* 0x46a040 */
void GAMEAPI ripple_vertices_setup(void); /* 0x43e050 */
int32_t GAMEAPI water_wave(int32_t a, int32_t b, int32_t c); /* 0x482ae0 */
void GAMEAPI water_meshes_setup(void); /* 0x482b20 */
void GAMEAPI track_water_setup(void); /* 0x41aa60 */
void GAMEAPI camera_view_setup(int32_t index, int32_t car, int32_t *cam); /* 0x406be0 */
uint8_t *GAMEAPI rgb_scale(uint8_t *c, int32_t r, int32_t g, int32_t b); /* 0x404940 */
void GAMEAPI tint_colour_tables(int32_t r, int32_t g, int32_t b); /* 0x4047e0 */
void GAMEAPI weather_tint_setup(void); /* 0x485070 */
void GAMEAPI pad_state_clear(uint32_t *p); /* 0x41b8e0 */
int32_t GAMEAPI tick_sync(int32_t n); /* 0x429220 */
int32_t GAMEAPI pads_reset(void); /* 0x41b370 */
void GAMEAPI race_start_messages(int32_t mode); /* 0x463860 */
int32_t GAMEAPI player_car_choice(int32_t i); /* 0x449ca0 */
void GAMEAPI glow_flash_reset(void); /* 0x47b140 */
void GAMEAPI time_stamp(uint32_t *seed); /* 0x486160 */
void GAMEAPI weather_level_setup(void); /* 0x486170 */
void GAMEAPI rain_setup(void); /* 0x483fa0 */
void GAMEAPI snow_setup(void); /* 0x483ff0 */
void GAMEAPI weather_emitter_setup(int32_t i); /* 0x483ea0 */
void __attribute__((fastcall)) weather_flakes_setup(const uint8_t *uv); /* 0x4856a0, uv in ecx */
void GAMEAPI weather_setup(void); /* 0x483d90 */
void GAMEAPI rock_setup(void); /* 0x457f80 */
void GAMEAPI spray_reset(uint8_t *body); /* 0x43e620 */
void GAMEAPI special_script_start(uint8_t *body); /* 0x41ad20 */
void GAMEAPI special_body_setup(uint8_t *body); /* 0x4013f0 */
void GAMEAPI special_spheres4_setup(uint8_t *body); /* 0x45bfd0 */
void GAMEAPI screen_uv_setup(uint8_t *body); /* 0x41aef0 */
void GAMEAPI track_specials_setup(void); /* 0x41ab80 */
void GAMEAPI event_queue_setup(uint32_t capacity, uint32_t *queue); /* 0x434e70 */
void GAMEAPI body_pools_setup(void); /* 0x418fa0 */
void GAMEAPI car_texture_setup(uint32_t i); /* 0x4750b0 */
void GAMEAPI car_textures_setup(void); /* 0x475090 */
void GAMEAPI freeze_setup(void); /* 0x43a8d0 */
void GAMEAPI pad_reset(uint8_t *pad); /* 0x41b440 */
int32_t GAMEAPI slots_controls_setup(uint8_t *slot); /* 0x41b460 */
void GAMEAPI timer_callback_set(uint32_t callback); /* 0x429200 */
void GAMEAPI clock_start(void); /* 0x435110 */
void GAMEAPI clock_restart(void); /* 0x4351a0 */
void GAMEAPI time_warp_reset(void); /* 0x479720 */
int32_t GAMEAPI next_pow2(int32_t n); /* 0x41e1a0 */
void GAMEAPI mem_copy(uint8_t *dst, const uint8_t *src, uint32_t n); /* 0x476f30 */
uint32_t GAMEAPI mem_fill(uint8_t *dst, uint8_t c, uint32_t n); /* 0x476f50 */
int32_t GAMEAPI archive_name_equal(const char *a, const char *b); /* 0x43b5a0 */
void GAMEAPI file_cache_init(void); /* 0x43b730 */
void GAMEAPI weapon_bodies_create(void); /* 0x4583e0 */
void GAMEAPI projectile_trail_init(uint8_t *body); /* 0x483100 */
void GAMEAPI snatch_create(void); /* 0x48d880 */
void GAMEAPI snatch_sprites_create(uint8_t *snatch); /* 0x48ec30 */
void GAMEAPI snatch_prims_init(uint8_t *snatch); /* 0x48f780 */
void GAMEAPI snatch_texture_corners(uint16_t *page, uint16_t *top_left, uint16_t *top_right, uint16_t *bottom_left,
                                    uint16_t *bottom_right); /* 0x486a40 */
void GAMEAPI pickups_create(void); /* 0x467120 */
void GAMEAPI pickup_prims_init(uint8_t *pickup); /* 0x468150 */
void GAMEAPI droppers_create(void); /* 0x425ce0 */
void GAMEAPI viewports_setup(void); /* 0x444080 */
void GAMEAPI viewport_player_order(int32_t *out); /* 0x45ed40 */
void GAMEAPI viewports_layout(int32_t a, int32_t b, int32_t c, int32_t d); /* 0x423360 */
void GAMEAPI viewport_border(int32_t *x, int32_t *y); /* 0x428b50 */
void GAMEAPI quarter_prims_init(void); /* 0x4040f0 */
void GAMEAPI uv_midpoint(uint8_t *out, const uint8_t *a, const uint8_t *b); /* 0x4040b0 */
void GAMEAPI explosions_reset(void); /* 0x405050 */
void GAMEAPI attached_points_reset(void); /* 0x442660 */
void GAMEAPI shadows_reset(void); /* 0x46c210 */
void GAMEAPI trail_prims_init(void); /* 0x433d30 */
void GAMEAPI reciprocal_table_init(void); /* 0x47c2c0 */
void GAMEAPI model_buffers_init(void); /* 0x47c330 */
void GAMEAPI model_normals_make(uint8_t *model); /* 0x40f1e0 */
void GAMEAPI model_vertex_normals(const uint8_t *model, int16_t *out); /* 0x40f2a0 */
uint8_t *GAMEAPI binding_get(uint32_t set, int32_t n); /* 0x449bf0 */
int32_t GAMEAPI binding_is_button(uint32_t set, int32_t n); /* 0x449d80 */
uint32_t GAMEAPI pad_button_flags(int32_t player, uint32_t unused, uint32_t steer, uint32_t accel,
                                  uint32_t brake); /* 0x449d00 */
int32_t GAMEAPI pad_device_bound(int32_t set, const int16_t *id); /* 0x428cc0 */
int32_t GAMEAPI device_usable(int32_t n); /* 0x489fa0 */
uint32_t GAMEAPI device_flag(int32_t n); /* 0x489ff0 */
uint8_t *GAMEAPI link_pool_take(uint32_t size); /* 0x460af0 */
void GAMEAPI grid_link_init(uint8_t *link, uint32_t owner); /* 0x460b20 */
void GAMEAPI body_header_init(uint8_t *body, uint32_t owner); /* 0x460b40 */
void GAMEAPI model_instance_init(uint32_t *inst, uint8_t *model, uint8_t *faces, uint32_t extra); /* 0x460dc0 */
uint8_t *GAMEAPI model_faces_carve(uint8_t *model, uint8_t *at, int32_t use0, int32_t use1, int32_t use2,
                                  uint8_t **first0, uint8_t **second0, uint8_t **first1, uint8_t **second1,
                                  uint8_t **first2, uint8_t **second2); /* 0x460e40 */
void GAMEAPI body_list_add(uint8_t *body); /* 0x460f50 */
void GAMEAPI crumble_queue_clear(void); /* 0x405eb0 */
void GAMEAPI car_twin_wheels(uint8_t *slot, uint8_t *wheel_model); /* 0x40de00 */
void GAMEAPI car_stats_set(uint8_t *car, int32_t index, int32_t kind, int32_t special); /* 0x40eae0 */
void GAMEAPI specials_reset(void); /* 0x41aa40 */
void GAMEAPI replay_start(uint8_t *car, int32_t mode); /* 0x41b8f0 */
void GAMEAPI car_trails_init(uint8_t *car); /* 0x421dd0 */
void GAMEAPI car_marks_init(uint8_t *car); /* 0x421e30 */
uint32_t GAMEAPI fix_sqrt_b(int32_t x); /* 0x4279d0 */
uint32_t GAMEAPI packet_tpage_set(uint8_t *packet, uint32_t a, uint32_t b, uint32_t c); /* 0x427ad0 */
uint32_t GAMEAPI packet_blend_bits(uint32_t blend); /* 0x427b00 */
void GAMEAPI smoke_clouds_init(void); /* 0x42a8f0 */
void GAMEAPI flash_packet_init(uint8_t *p); /* 0x42a9a0 */
void GAMEAPI flashes_init(void); /* 0x42a9e0 */
void GAMEAPI palette_ramp_init(void); /* 0x42c1d0 */
void GAMEAPI flares_init(void); /* 0x42c200 */
void GAMEAPI shock_waves_init(void); /* 0x42c2b0 */
void GAMEAPI markers_init(void); /* 0x434380 */
void GAMEAPI slot_find_line(uint8_t *slot); /* 0x47ad30 */
void GAMEAPI slot_race_reset(uint8_t *slot); /* 0x43d280 */
void GAMEAPI lights_reset(void); /* 0x441d70 */
int32_t GAMEAPI config_player_on(int32_t i); /* 0x449cd0 */
void GAMEAPI messages_load(void); /* 0x465270 */
void GAMEAPI lineup_setup(uint8_t *car, uint8_t kind, const uint16_t *models, const uint16_t *glow_models); /* 0x4698f0 */
void GAMEAPI bomblets_setup(void); /* 0x46ba00 */
void GAMEAPI car_shadow_init(uint8_t *shadow); /* 0x46c220 */
uint8_t *GAMEAPI car_texture_anim_attach(uint8_t *slot); /* 0x46e110 */
void GAMEAPI car_trail_init(uint8_t *car); /* 0x46e3e0 */
void GAMEAPI skid_marks_init(void); /* 0x46eab0 */
void GAMEAPI body_points_init(uint8_t *body, int32_t unused, const int16_t *points, int32_t reach); /* 0x475d50 */
void GAMEAPI car_targets_reset(void); /* 0x478340 */
void GAMEAPI slot_target_reset(uint8_t *slot); /* 0x478350 */
void GAMEAPI car_target_add(uint8_t *body, uint32_t kind); /* 0x478400 */
void GAMEAPI text_lists_reset(void); /* 0x479af0 */
void GAMEAPI car_section_reset(uint8_t *slot); /* 0x47ad00 */
void GAMEAPI glow_model_offset(uint8_t *model, int32_t kind); /* 0x47b1e0 */
uint32_t GAMEAPI glow_first_low_face(const uint8_t *glow); /* 0x47b240 */
void GAMEAPI glow_pick_vertices(uint8_t *glow); /* 0x47b280 */
void GAMEAPI glow_reset(uint8_t *glow); /* 0x47bc60 */
void GAMEAPI car_glow_init(uint8_t *glow, uint8_t *model, int32_t kind); /* 0x47b160 */
void GAMEAPI debris_init(void); /* 0x47bd70 */
void GAMEAPI car_setup(uint8_t *slot, int32_t index); /* 0x40e670 */
int32_t GAMEAPI race_setup(void); /* 0x46a060 */
void GAMEAPI track_tv_spots(void); /* 0x40b5b0 */
int32_t GAMEAPI track_models_mark_lit(int32_t count, const uint8_t *record); /* 0x478f80 */
uint8_t *GAMEAPI track_object_spawn(const uint8_t *record); /* 0x41aa70 */
uint8_t *GAMEAPI track_object_make(const uint8_t *record, uint32_t kind); /* 0x401300 */
uint8_t *GAMEAPI track_object_make_static(const uint8_t *record); /* 0x41ac60 */
uint8_t *GAMEAPI track_object_make_k5(const uint8_t *record); /* 0x45bf10 */
uint8_t *GAMEAPI track_marker_make(const uint8_t *record); /* 0x43e5c0 */
void GAMEAPI object_lights_setup(uint8_t *object, const uint8_t *record, int32_t *counter); /* 0x442680 */
void GAMEAPI object_light_spawn(uint8_t *object, uint32_t i, int32_t *counter); /* 0x4426e0 */
void GAMEAPI track_blast_register(uint8_t *object, const uint8_t *record); /* 0x405070 */
void GAMEAPI object_triggers_register(const uint8_t *record, uint32_t object); /* 0x4039d0 */
void GAMEAPI track_static_add(uint32_t object); /* 0x403a20 */
void GAMEAPI track_tables_alloc(int32_t a, int32_t b); /* 0x4044c0 */
int32_t GAMEAPI texture_bytes(int32_t id); /* 0x41f1b0 */
void GAMEAPI prim_flag2_set(uint8_t *prim, int32_t on); /* 0x4270e0 */
int32_t GAMEAPI track_textures_load(int32_t count, int32_t first, int32_t n, const uint8_t *records,
                                    uint32_t palettes, uint32_t pixels, uint32_t flags); /* 0x428130 */
void GAMEAPI ambient_colour_set(uint32_t r, uint32_t g, uint32_t b); /* 0x428350 */
void GAMEAPI object_pool_reset(int32_t count); /* 0x460aa0 */
uint8_t *GAMEAPI object_create(uint8_t *model, uint32_t kind, int32_t size, int32_t shared); /* 0x460ba0 */
void GAMEAPI body_init_from_template(uint8_t *object, const uint32_t *t); /* 0x465640 */
void GAMEAPI object_lit_faces_register(uint8_t *object); /* 0x478370 */
void GAMEAPI lit_face_add(uint32_t object, uint32_t face); /* 0x4783d0 */
void GAMEAPI faces_count_lit(const uint16_t *list, const uint8_t *model, int32_t *count); /* 0x478e70 */
void GAMEAPI model_count_lit(const uint8_t *record, const uint8_t *model, int32_t *count); /* 0x478ec0 */
void GAMEAPI sprite_spawn_marker(int32_t x, int32_t y, int32_t z, uint32_t texture, uint32_t a, uint32_t b); /* 0x47dbc0 */
uint8_t *GAMEAPI light_sprite_spawn(int32_t x, int32_t y, int32_t z, uint32_t texture, uint32_t a, uint32_t c0,
                                    uint32_t c1, uint32_t c2, int32_t index); /* 0x47dc30 */
uint8_t *GAMEAPI sprite_object_create(int32_t x, int32_t y, int32_t z, uint32_t texture, uint32_t a,
                                      uint32_t b); /* 0x47dcc0 */
void GAMEAPI quat_from_angles(int32_t x, int32_t y, int32_t z, int32_t *q); /* 0x480a80 */
void GAMEAPI track_animation_update(void); /* 0x41ac50 */
void GAMEAPI track_specials_update(void); /* 0x41ad50 */
void GAMEAPI water_ripples_update(void); /* 0x482df0 */
void GAMEAPI screen_update(uint8_t *object); /* 0x41b000 */
void GAMEAPI wreck_burn_update(uint8_t *object); /* 0x41ada0 */
void GAMEAPI spray_update(uint8_t *object); /* 0x43e640 */
void GAMEAPI mesh_rows_touch(uint32_t rows, uint32_t section); /* 0x4411c0 */
void GAMEAPI light_add(const int32_t *pos, const uint32_t *colour, uint32_t radius, uint32_t falloff, uint32_t kind,
                       const uint32_t *extra); /* 0x441d00 */
void GAMEAPI light_flash_small(const int32_t *pos, uint32_t colour); /* 0x42b2d0 */
void GAMEAPI light_flash(const int32_t *pos, uint32_t size); /* 0x42b300 */
void GAMEAPI blast_flash_set(const int32_t *pos, int32_t size); /* 0x42b330 */
void GAMEAPI blast_glow_set(const int32_t *pos, int32_t size); /* 0x42b370 */
void GAMEAPI smoke_cloud_start(const int32_t *pos, int32_t big); /* 0x42b3b0 */
void GAMEAPI shockwave_start(const int32_t *pos); /* 0x42c360 */
void GAMEAPI blast_point_set(const int32_t *pos); /* 0x42c3c0 */
int32_t GAMEAPI piece_take(void); /* 0x462450 */
void GAMEAPI pieces_spawn(const int32_t *pos, const int32_t *vel, int32_t size); /* 0x4624d0 */
void GAMEAPI particles_burst(const int32_t *pos, const int32_t *dir, uint32_t size, uint32_t owner, uint32_t kind); /* 0x42ad60 */
void GAMEAPI gte_square_ir(void); /* 0x4282d0 */
void GAMEAPI cell_row_ranges(const uint32_t *cell, uint32_t *out, int32_t level); /* 0x440c60 */
void GAMEAPI scorch_track(const int32_t *pos, uint32_t level); /* 0x42c760 */
int32_t GAMEAPI sim_step(int32_t network, int32_t step, int32_t count); /* 0x444120 */
int32_t GAMEAPI local_car_index(void); /* 0x48cef0 */
uint32_t GAMEAPI sound_clock(void); /* 0x435100 */
void GAMEAPI car_step_buttons(uint8_t *car); /* 0x48e7b0 */
void GAMEAPI cars_network_controls(void); /* 0x48e7e0 */
void GAMEAPI track_vertices_copy(void); /* 0x43e570 */
void GAMEAPI cars_catch_up(void); /* 0x4112e0 */
void GAMEAPI chat_send_macro(int32_t i); /* 0x41c6d0 */
void GAMEAPI car_extra_buttons(uint8_t *slot); /* 0x41c7a0 */
void GAMEAPI car_sounds(uint8_t *car, int32_t player); /* 0x473250 */
uint32_t GAMEAPI net_state_host(void); /* 0x45eed0 */
uint32_t GAMEAPI net_state_bit5(void); /* 0x45ef30 */
uint32_t GAMEAPI net_state_game(void); /* 0x45ef80 */
uint32_t GAMEAPI car_index_of(const uint8_t *car); /* 0x45ca10 */
uint32_t GAMEAPI net_session_kind(void); /* 0x48ce80 */
int32_t GAMEAPI net_local_slot(void); /* 0x48cf20 */
void GAMEAPI race_end_signal(void); /* 0x40a9a0 */
void GAMEAPI voice_slot_set(int32_t i, uint32_t a, uint32_t b, uint32_t c, uint32_t d); /* 0x472290 */
void GAMEAPI voice_slot_reset(uint32_t which); /* 0x472130 */
void GAMEAPI race_end_fade_set(uint32_t on); /* 0x40afb0 */
void GAMEAPI slot_nearest_line_node(uint8_t *slot); /* 0x47ae40 */
void GAMEAPI race_positions_update(void); /* 0x43d350 */
uint32_t GAMEAPI race_time_digits(int32_t t); /* 0x439c70 */
void GAMEAPI slot_lap_record(uint8_t *slot); /* 0x43d750 */
void GAMEAPI race_results_update(uint32_t final); /* 0x439a70 */
void GAMEAPI slot_finish_to_ai(uint8_t *slot); /* 0x47fbe0 */
void GAMEAPI car_award_weapon(uint8_t *car, uint32_t kind); /* 0x467880 */
void GAMEAPI slot_lap_weapon(uint8_t *slot); /* 0x467490 */
void GAMEAPI replay_checksum(uint8_t *rec, int32_t check); /* 0x41c190 */
void GAMEAPI replay_step(uint8_t *car); /* 0x41ba20 */
void GAMEAPI replay_finish_mark(uint8_t *car); /* 0x41c160 */
void GAMEAPI debris_spawn(const int32_t *at, const int32_t *velocity, int32_t kind); /* 0x42b8d0 */
void GAMEAPI car_hit_timer_tick(uint8_t *car); /* 0x4906a0 */
void GAMEAPI warp_palette_off(void); /* 0x47d7e0 */
void GAMEAPI weapon_slot_spent(uint8_t *car, uint32_t kind); /* 0x467cc0 */
uint32_t GAMEAPI slot_car_index(const uint8_t *slot); /* 0x45ca20 */
uint8_t *GAMEAPI body_by_id(uint32_t id); /* 0x460f80 */
void GAMEAPI ripple_stop(uint8_t *ripple); /* 0x46e150 */
uint8_t *GAMEAPI ripple_alloc(const uint8_t *slot); /* 0x46e0e0 */
void GAMEAPI drop_queue_push(uint32_t spot); /* 0x426040 */
void GAMEAPI shape_list_add(uint8_t *body); /* 0x45c010 */
void GAMEAPI body_path_start(uint8_t *body, int32_t path, uint8_t spot); /* 0x462e30 */
int32_t GAMEAPI beam_add(const int16_t *from, const int16_t *to, int16_t life); /* 0x4344e0 */
void GAMEAPI beam_move(const int16_t *from, const int16_t *to, int32_t index); /* 0x434540 */
void GAMEAPI beam_remove(int32_t index); /* 0x4345a0 */
void GAMEAPI pickup_drop(uint8_t *dropper); /* 0x4682b0 */
void GAMEAPI pickup_droppers_update(void); /* 0x425d70 */
void GAMEAPI pickups_update(void); /* 0x4671f0 */
int32_t GAMEAPI boost_start(uint8_t *slot, int32_t wait, int32_t flash); /* 0x47aa60 */
int32_t GAMEAPI time_warp_start(const uint8_t *slot, int32_t delay); /* 0x479890 */
void GAMEAPI warp_end_ai_burst(const uint8_t *car); /* 0x424cb0 */
void GAMEAPI time_warp_update(void); /* 0x479740 */
int32_t GAMEAPI shield_start(const uint8_t *slot); /* 0x46d6e0 */
void GAMEAPI shield_ripple_update(const uint8_t *slot); /* 0x46d950 */
void GAMEAPI car_hop_start(uint8_t *car); /* 0x40eee0 */
void GAMEAPI car_fire_buttons(uint8_t *slot); /* 0x47b0e0 */
void GAMEAPI player_markers_pass(const uint8_t *slot); /* 0x42a410 */
void GAMEAPI smoke_puff(const int32_t *at); /* 0x42b650 */
void GAMEAPI trail_update(uint8_t *obj, int32_t mode); /* 0x458a20 */
void GAMEAPI homer_steer(uint8_t *obj); /* 0x459770 */
void GAMEAPI homer_launch(uint8_t *obj); /* 0x4598f0 */
void GAMEAPI homer_end(uint8_t *obj, int32_t mode); /* 0x459610 */
void GAMEAPI homer_update(uint8_t *obj); /* 0x458810 */
int32_t GAMEAPI bit_count64(uint32_t lo, uint32_t hi); /* 0x43f360 */
int32_t GAMEAPI leader_route(uint8_t *obj); /* 0x43f0b0 */
void GAMEAPI leader_node_offset(const uint8_t *node, const int32_t *pos, int32_t *out, int32_t mode); /* 0x43f9c0 */
void GAMEAPI leader_push(uint8_t *obj, int32_t *dir, const uint8_t *node, int32_t seek); /* 0x43f790 */
int32_t GAMEAPI leader_advance(uint8_t *obj, int32_t check); /* 0x43f390 */
void GAMEAPI leader_update(uint8_t *obj); /* 0x43ebc0 */
void GAMEAPI driller_follow_ground(uint8_t *obj); /* 0x424b90 */
void GAMEAPI driller_head_update(uint8_t *part); /* 0x424380 */
void GAMEAPI driller_update(uint8_t *part); /* 0x423ff0 */
void GAMEAPI projectiles_update(void); /* 0x458710 */
void GAMEAPI body_spheres_set(uint8_t *body, uint32_t count, const uint8_t *src, int32_t unused); /* 0x419150 */
int32_t GAMEAPI leader_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which); /* 0x43e880 */
int32_t GAMEAPI homer_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which); /* 0x459050 */
int32_t GAMEAPI driller_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which); /* 0x424550 */
void GAMEAPI wormhole_init(uint8_t *obj); /* 0x48eda0 */
int32_t GAMEAPI wormhole_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which); /* 0x48d920 */
void GAMEAPI freezer_hit_cars(const uint8_t *slot, int32_t direction); /* 0x410160 */
int32_t GAMEAPI freezer_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which); /* 0x43a910 */
int32_t GAMEAPI weapon_fire(uint8_t *slot, int32_t which, int32_t local, uint8_t *unused, uint8_t *data); /* 0x467900 */
void GAMEAPI wheel_sparks(const uint8_t *car, const int32_t *at, const int32_t *add); /* 0x42b4d0 */
int32_t GAMEAPI freeze_next_section(uint32_t section, int32_t dir); /* 0x43b380 */
void GAMEAPI freeze_vertex_colour(uint8_t *vertex, uint32_t colour); /* 0x43b2a0 */
void GAMEAPI freeze_row_toggle(const uint8_t *row); /* 0x43b190 */
void GAMEAPI freezer_update(void); /* 0x43aba0 */
int32_t GAMEAPI target_blocked(const uint8_t *cell, const int32_t *n, const int32_t *p, const int16_t *target); /* 0x478bb0 */
int32_t GAMEAPI target_visible(const uint8_t *slot, int32_t sticky, const int16_t *target, int32_t *best); /* 0x4789b0 */
void GAMEAPI homer_choose_target(uint8_t *slot); /* 0x478530 */
void GAMEAPI homers_choose_targets(void); /* 0x478470 */
void GAMEAPI weapons_update(void); /* 0x47ab40 */
void GAMEAPI ripple_update(void); /* 0x43e270 */
void GAMEAPI crumble_update(void); /* 0x405f70 */
void GAMEAPI bomblet_update(void); /* 0x46bfa0 */
void GAMEAPI glint_update(void); /* 0x443180 */
int32_t GAMEAPI race_progress(void); /* 0x46a530 */
void GAMEAPI particle_spawn(const int32_t *pos, int32_t *vel, int32_t kind); /* 0x462690 */
void GAMEAPI effect_puff(const int32_t *pos); /* 0x42b8f0 */
uint8_t *GAMEAPI glint_spawn(const int16_t *p, uint32_t type, uint32_t fade, uint32_t spread, uint32_t flags); /* 0x442f70 */
void GAMEAPI dust_add(const int32_t *pos); /* 0x443100 */
void GAMEAPI bomblet_spawn(const int32_t *pos, const int32_t *vel, uint32_t kind, uint32_t owner); /* 0x46bd60 */
int32_t GAMEAPI crumble_setup(void); /* 0x412dd0 */
uint32_t GAMEAPI crumble_alloc(uint8_t *entry, uint32_t size); /* 0x412bd0 */
void GAMEAPI crumble_release_lines(uint8_t *object); /* 0x4038e0 */
int32_t GAMEAPI cos_table(int32_t angle); /* 0x480bc0 */
void GAMEAPI hud_set_mode(int32_t mode); /* 0x41b2d0 */
void GAMEAPI countdown_tick(void); /* 0x46a9a0 */
void GAMEAPI finish_messages(void); /* 0x464e10 */
int32_t GAMEAPI text_slide(uint32_t i); /* 0x479690 */
void GAMEAPI text_free(int32_t i); /* 0x479280 */
int32_t GAMEAPI text_create(const uint8_t *s, uint32_t owner, int32_t x, int32_t y, uint32_t flags, int32_t id, int32_t menu); /* 0x479060 */
void GAMEAPI race_go_banner(void); /* 0x4651f0 */
uint32_t GAMEAPI players_buttons(void); /* 0x41c230 */
int32_t GAMEAPI network_quit_menu(void); /* 0x45f920 */
void GAMEAPI pause_check(void); /* 0x4639b0 */
void GAMEAPI weather_update(void); /* 0x484080 */
void GAMEAPI weather_level_step(void); /* 0x486290 */
void GAMEAPI weather_emitter_place(uint8_t *emitter, int32_t restore, int32_t index); /* 0x484120 */
void GAMEAPI car_view(uint8_t **slot, int32_t *out); /* 0x40d090 */
void GAMEAPI weather_emitter_update(uint8_t *emitter); /* 0x4842e0 */
void GAMEAPI weather_emitter_indoors(int32_t indoors, uint8_t *emitter); /* 0x4846c0 */
void GAMEAPI weather_drops_spawn(const int16_t *min, const int16_t *max, uint8_t *emitter); /* 0x4846f0 */
void GAMEAPI weather_box(int16_t *min, int16_t *max, const uint8_t *emitter); /* 0x4848c0 */
int32_t GAMEAPI weather_indoors(const uint8_t *body); /* 0x4850f0 */
void GAMEAPI weather_thunder(void); /* 0x484e20 */
void GAMEAPI weather_lightning(int32_t outdoors); /* 0x484ef0 */
void GAMEAPI weather_thunder_clap(void); /* 0x484fd0 */
void GAMEAPI explosions_update(void); /* 0x405140 */
void GAMEAPI sprites_update(void); /* 0x42b990 */
void GAMEAPI flash_timer_step(void); /* 0x42c400 */
void GAMEAPI sprites_move(void); /* 0x42d1e0 */
void GAMEAPI sprite_move_smoke(uint8_t *s, int32_t life); /* 0x42cd80 */
void GAMEAPI sprite_move_rising(uint8_t *s, uint32_t life); /* 0x42ce50 */
void GAMEAPI sprite_move_drag(uint8_t *s, uint32_t life, int32_t slow_fade); /* 0x42ce70 */
void GAMEAPI sprite_move_spray(uint8_t *s, uint32_t life); /* 0x42cf40 */
void GAMEAPI sprite_move_swirl(uint8_t *s); /* 0x42d000 */
void GAMEAPI sprite_move_falling(uint8_t *s); /* 0x42d130 */
void GAMEAPI sprite_move_basic(uint8_t *s, int32_t behaviour); /* 0x42d400 */
uint32_t GAMEAPI sprite_colour(uint32_t behaviour, uint32_t life); /* 0x42d300 */
void GAMEAPI sprite_grow(uint8_t *s, int32_t behaviour, uint32_t life); /* 0x42d560 */
void GAMEAPI sprite_free(uint8_t *s); /* 0x42ad00 */
void GAMEAPI shadows_update(void); /* 0x46c300 */
void GAMEAPI shadow_probe(const uint8_t *car, uint8_t *slot); /* 0x46c520 */
int32_t GAMEAPI shadow_ground(uint8_t *car, int32_t *normal, int32_t *offset, int32_t time, const int32_t *hit); /* 0x46c450 */
void GAMEAPI shadow_corners(uint8_t *car, int32_t *n, const int32_t *offset); /* 0x46c580 */
int32_t GAMEAPI shadow_corner(const uint8_t *car, const int32_t *forward, const int32_t *side, int32_t *out, const int32_t *n, int32_t plane, uint8_t corner); /* 0x46c750 */
void GAMEAPI debris_update(void); /* 0x47c210 */
void GAMEAPI tyre_trails_update(void); /* 0x481f20 */
void GAMEAPI car_textures_update(void); /* 0x4751b0 */
void GAMEAPI car_texture_frames(int32_t car_index); /* 0x475430 */
void GAMEAPI rock_drop(int32_t *at); /* 0x458180 */
void GAMEAPI rock_update(void); /* 0x458010 */
void GAMEAPI hsv_to_rgb(int32_t hue, int32_t saturation, int32_t value, int32_t *red, int32_t *green, int32_t *blue); /* 0x467cf0 */
void GAMEAPI glow_levels(const uint8_t *car, uint8_t *glow); /* 0x47bae0 */
void GAMEAPI glow_vertices(uint8_t *glow, int32_t level, int32_t flash); /* 0x47bbd0 */
void GAMEAPI car_glow(uint8_t *car); /* 0x47b9a0 */
void GAMEAPI cars_glow(void); /* 0x47b8f0 */
void GAMEAPI piece_free(uint16_t piece, uint32_t index); /* 0x462490 */
void GAMEAPI piece_move(uint8_t *piece); /* 0x4629b0 */
void GAMEAPI gte_depth_cue(void); /* 0x427590 */
void GAMEAPI pieces_update(void); /* 0x462840 */
int32_t GAMEAPI view_count(void); /* 0x40d040 */
int32_t GAMEAPI direction_fit(int32_t *v); /* 0x407e90 */
int32_t GAMEAPI cell_has_surface(uint32_t surface, const int32_t *at); /* 0x407f30 */
void GAMEAPI body_side_axis(int32_t *out, const uint8_t *body, int32_t positive); /* 0x4081f0 */
int32_t GAMEAPI car_is_upside_down(const uint8_t *car); /* 0x408240 */
uint32_t GAMEAPI surface_at(const int32_t *at); /* 0x40a6e0 */
void GAMEAPI camera_spot_nearest(const int32_t *at, int32_t kind, uint8_t **out, int32_t range); /* 0x40a570 */
int32_t GAMEAPI camera_probe(const uint8_t *body, const uint8_t *at); /* 0x40a8f0 */
void GAMEAPI camera_look_or_keep(const void *from, const void *to, uint32_t *matrix, int32_t view); /* 0x40a940 */
void GAMEAPI camera_choose_shot(const void *unused, uint32_t view); /* 0x40a9b0 */
int32_t GAMEAPI camera_zoom_for(const int32_t *a, const int32_t *b, uint32_t divisor, int32_t limit); /* 0x40add0 */
void GAMEAPI camera_speed_zoom(uint8_t *slot, int32_t *at); /* 0x40b000 */
int32_t GAMEAPI point_line_distance(const int32_t *point, const int32_t *a, const int32_t *b, int32_t *along); /* 0x40b180 */
void GAMEAPI nearest_car(const int32_t *at, int32_t *index); /* 0x40b4f0 */
int32_t GAMEAPI camera_find_trackside(uint16_t *car_index, uint8_t **spot, int32_t view); /* 0x40b310 */
void GAMEAPI factorials_init(void); /* 0x40c940 */
int32_t GAMEAPI fix_pow(int32_t base, uint32_t power); /* 0x40c970 */
void GAMEAPI bezier_point(const int32_t *points, int32_t count, int32_t *out, int32_t t); /* 0x40c9e0 */
void GAMEAPI points_scale_for_mode(uint8_t *points, int32_t count); /* 0x40cad0 */
void GAMEAPI camera_check_events(int32_t view); /* 0x407fa0 */
int32_t GAMEAPI texture_anim_active(void); /* 0x46e3c0 */
int32_t GAMEAPI snatch_active(void); /* 0x4906c0 */
void GAMEAPI race_early_ticks(void); /* 0x46a9c0 */
void GAMEAPI pad_effect_request(uint8_t *pad, uint32_t strength, uint32_t on, uint32_t kind, uint32_t off_value, uint32_t priority); /* 0x41b4d0 */
void GAMEAPI car_clear_item(uint8_t *car, uint32_t item); /* 0x467c90 */
void GAMEAPI ai_grudge_reduce(uint8_t *a, const uint8_t *b, uint32_t amount); /* 0x40fe00 */
void GAMEAPI ai_grudge_add(uint8_t *a, const uint8_t *b, int32_t amount); /* 0x40feb0 */
void GAMEAPI ai_grudge_snatched(uint8_t *snatch); /* 0x4102b0 */
void GAMEAPI car_kick_forward(uint8_t *car, int32_t amount); /* 0x47a860 */
void GAMEAPI matrix_from_forward(int32_t *forward, int16_t *m); /* 0x458f50 */
void GAMEAPI car_facing(const uint8_t *car, int32_t *out); /* 0x48eb30 */
void GAMEAPI vec_clamp(int32_t *v, int32_t limit); /* 0x48ebf0 */
void GAMEAPI body_pull_toward(const int32_t *point, int32_t *body); /* 0x48eb80 */
void GAMEAPI snatch_pick_target(uint8_t *owner, uint8_t *snatch); /* 0x48db80 */
void GAMEAPI snatch_aim_point(const uint8_t *snatch, int32_t *out); /* 0x48ea80 */
void GAMEAPI snatch_to_target(int32_t *out, const uint8_t *snatch); /* 0x48ea40 */
int32_t GAMEAPI snatch_side(const uint8_t *snatch); /* 0x48ea10 */
void GAMEAPI snatch_save_matrix(uint8_t *snatch); /* 0x48e970 */
void GAMEAPI snatch_drop_point(const uint8_t *snatch, int32_t *out); /* 0x48e610 */
void GAMEAPI snatch_copy_motion(uint8_t *snatch); /* 0x48e6a0 */
void GAMEAPI snatch_hide_sprites(uint8_t *snatch); /* 0x48ee70 */
void GAMEAPI snatch_ring_sprite(const int32_t *a, const int32_t *b, int32_t radius, int32_t i, int32_t *out, uint8_t *colour); /* 0x48f130 */
void GAMEAPI snatch_ring2_sprite(const int32_t *a, const int32_t *b, int32_t radius, int32_t i, int32_t *out, uint8_t *colour); /* 0x48f2b0 */
void GAMEAPI snatch_spark_reset(int32_t *spark); /* 0x48f430 */
void GAMEAPI snatch_spark_sprite(const int32_t *nose, const int32_t *a, const int32_t *b, uint8_t *snatch, int32_t radius, int32_t i, int32_t unused1, int32_t unused2); /* 0x48f4f0 */
void GAMEAPI snatch_effects(uint8_t *snatch); /* 0x48eef0 */
void GAMEAPI snatch_launch(uint8_t *snatch); /* 0x48dea0 */
int32_t GAMEAPI snatch_fly(uint8_t *snatch); /* 0x48e0c0 */
void GAMEAPI snatch_finish(uint8_t *snatch, int32_t explode); /* 0x48ddd0 */
void GAMEAPI snatch_grab(uint8_t *snatch); /* 0x48e290 */
void GAMEAPI snatch_swap(uint8_t *snatch); /* 0x48e470 */
void GAMEAPI snatch_hold(uint8_t *snatch); /* 0x48e3c0 */
void GAMEAPI snatch_step(uint8_t *snatch); /* 0x48dc60 */
void GAMEAPI snatch_update(void); /* 0x48dc40 */
/* post_f.c: attached points, object grid, slot pool, fallen bodies, lap counting */
uint8_t *GAMEAPI grid_record(uint32_t link); /* 0x4613e0 */
uint32_t GAMEAPI grid_index(const uint8_t *record); /* 0x4613c0 */
void GAMEAPI object_grid_unlink(uint8_t *object); /* 0x4611d0 */
void GAMEAPI object_grid_unlink2(uint8_t *object); /* 0x4612a0 */
void GAMEAPI object_grid_insert(uint8_t *object); /* 0x461170 */
void GAMEAPI object_grid_insert2(uint8_t *object); /* 0x461240 */
void GAMEAPI object_grid_link(uint8_t *object); /* 0x460fb0 */
void GAMEAPI object_grid_link2_at(uint8_t *object, int32_t x, int32_t y, int32_t z); /* 0x460ff0 */
void GAMEAPI body_points_update(uint8_t *body); /* 0x442940 */
void GAMEAPI bodies_points_update(void); /* 0x442840 */
uint32_t GAMEAPI slot_take(void); /* 0x4190a0 */
void GAMEAPI slot_give(uint32_t slot); /* 0x4190f0 */
void GAMEAPI body_list_remove(uint8_t *body); /* 0x45c040 */
void GAMEAPI body_retire(uint8_t *body); /* 0x419300 */
uint8_t *GAMEAPI particle_take(void); /* 0x42ac30 */
void GAMEAPI particle_fade(uint8_t *p, int32_t level, int32_t set); /* 0x42cd40 */
void GAMEAPI spark_spawn(const int32_t *at, const int32_t *dir, int32_t grey, int32_t alt); /* 0x42b700 */
void GAMEAPI spark_spawn_plain(const int32_t *at, const int32_t *dir, int32_t grey); /* 0x42b8b0 */
void GAMEAPI explosion_effects(const int32_t *at, const int32_t *dir, int32_t size, uint32_t what, uint32_t sound); /* 0x42aa30 */
void GAMEAPI pickup_fallen(uint8_t *body); /* 0x458320 */
void GAMEAPI dropped_fallen(uint8_t *item); /* 0x46c160 */
void GAMEAPI cars_check_fallen(void); /* 0x469810 */
void GAMEAPI quat_facing(int32_t *q, int32_t *dir); /* 0x40ee60 */
void GAMEAPI car_camera_cut(const uint8_t *car); /* 0x406e00 */
void GAMEAPI car_respawn_sparks(uint8_t *car, int32_t mode); /* 0x468f80 */
void GAMEAPI patch_gather(uint8_t *slot, const uint16_t *patch, uint8_t *body, int32_t dt, const int16_t *point, int32_t radius2); /* 0x469040 */
int32_t GAMEAPI respawn_blocked(uint8_t *test, const uint8_t *body, int32_t main_only); /* 0x468d80 */
int32_t GAMEAPI respawn_try(uint8_t *body, const int32_t *position, const int32_t *q, int16_t *matrix, int32_t mode, uint32_t *key); /* 0x4687f0 */
void GAMEAPI respawn_place(uint8_t *body, const int32_t *position, const int32_t *orientation, const int16_t *matrix, uint8_t *line, uint32_t node, uint32_t key); /* 0x468bb0 */
void GAMEAPI slot_lap_line(uint8_t *slot); /* 0x43e000 */
void GAMEAPI slot_lap_done(uint8_t *slot); /* 0x43d460 */
int32_t GAMEAPI slot_checkpoints(uint8_t *slot, uint32_t probe); /* 0x43d8e0 */
void GAMEAPI slot_checkpoints_reset(uint8_t *slot); /* 0x43de60 */
void GAMEAPI fallen_bodies_update(void); /* 0x468450 */
void GAMEAPI ai_press_rear_fire(uint8_t *car);                                   /* 0x47f330 */
void GAMEAPI ai_press_fire_a(uint8_t *car);                                      /* 0x47f350 */
void GAMEAPI ai_press_fire_b(uint8_t *car);                                      /* 0x47f370 */
int32_t GAMEAPI line_node_ahead(uint32_t node, const uint8_t *line, uint32_t count, uint16_t *out); /* 0x47f550 */
int32_t GAMEAPI ai_corner_ahead(const uint8_t *car);                             /* 0x402e50 */
void GAMEAPI scenery_position(const int32_t *entry, int16_t *out);               /* 0x402750 */
int32_t GAMEAPI ai_weapon0_ready(const uint8_t *car, int32_t last_laps);         /* 0x401ca0 */
int32_t GAMEAPI ai_weapon1_ready(uint8_t *car, int32_t time);                    /* 0x4024a0 */
int32_t GAMEAPI ai_weapon2_ready(uint8_t *car, int32_t time);                    /* 0x402830 */
int32_t GAMEAPI ai_weapon3_ready(uint8_t *car, int32_t last_lap, int32_t time);  /* 0x401ea0 */
int32_t GAMEAPI ai_weapon4_ready(uint8_t *car, int32_t last_laps);               /* 0x402ed0 */
int32_t GAMEAPI ai_weapon5_ready(const uint8_t *car);                            /* 0x401c90 */
int32_t GAMEAPI ai_weapon6_ready(const uint8_t *car, int32_t time);              /* 0x401c70 */
int32_t GAMEAPI ai_weapon7_ready(uint8_t *car, int32_t last_lap, int32_t time);  /* 0x402190 */
void GAMEAPI ai_weapons(uint8_t *car);                                           /* 0x401780 */
int32_t GAMEAPI svec_dot(const int16_t *a, const int16_t *b);                                  /* 0x47f3a0 */
int32_t GAMEAPI ai_hazard_cost(const int16_t *from, const int16_t *d, uint32_t idx, int32_t margin, int32_t speed,
                               int32_t extra, int32_t len2, int32_t *len, int32_t *inv);       /* 0x403430 */
int32_t GAMEAPI ai_hazards_cost(const uint8_t *car, const int16_t *from, const int16_t *to, int32_t margin,
                                int32_t speed, int32_t extra);                                  /* 0x403350 */
int32_t GAMEAPI ai_obstacle_hit(const int16_t *a, const int16_t *b, const uint8_t *obj, int32_t margin,
                                uint32_t idx);                                                  /* 0x403d90 */
int32_t GAMEAPI ai_obstacle_cost(const int16_t *a, const int16_t *b, uint32_t idx, int32_t margin, int32_t speed,
                                 int32_t extra);                                                /* 0x403c00 */
int32_t GAMEAPI ai_obstacles_cost(const int16_t *a, const int16_t *b, int32_t margin, int32_t speed,
                                  int32_t extra);                                               /* 0x403ba0 */
int32_t GAMEAPI ai_target_danger(const uint8_t *car, int32_t k, int32_t hazards, int32_t obstacles); /* 0x4117d0 */
int32_t GAMEAPI ai_target_route_danger(const int16_t *target, int32_t len2, int32_t k, const uint8_t *car); /* 0x42a1a0 */
int32_t GAMEAPI ai_player_weight(const uint8_t *car);                                           /* 0x4297a0 */
int32_t GAMEAPI ai_player_takes_line(int32_t player, uint32_t line);                            /* 0x429ad0 */
int32_t GAMEAPI ai_route_branch_score(const uint8_t *car, int32_t weight, int32_t k);           /* 0x4298e0 */
void GAMEAPI ai_route_branches(uint8_t *car, int32_t n);                                        /* 0x429680 */
void GAMEAPI ai_route_add_pickups(uint8_t *car, int32_t *n);                                    /* 0x429ea0 */
void GAMEAPI ai_route_add_items(uint8_t *car, int32_t *n);                                      /* 0x429b50 */
void GAMEAPI ai_route_add_markers(uint8_t *car, int32_t *n);                                    /* 0x429380 */
void GAMEAPI ai_route_targets(uint8_t *car, int32_t *n);                                        /* 0x429350 */
void GAMEAPI link_queue_reset(void);
void GAMEAPI link_queue_push(uint32_t line, uint32_t node);
void GAMEAPI link_queue_pop(uint16_t *line, uint16_t *node);
int32_t GAMEAPI link_queue_empty(void);
int32_t GAMEAPI u16_list_contains(uint32_t value, const uint16_t *list, uint32_t n);
void GAMEAPI body_side(int32_t *out, const uint8_t *body);
void GAMEAPI slot_plan_direction(int32_t *out, const uint8_t *slot);
int32_t GAMEAPI line_bend(uint32_t line_index, uint32_t node, int32_t *pos, int32_t *back, int32_t *ahead);
int32_t GAMEAPI approach_rate(int32_t closing, int32_t along);
int32_t GAMEAPI line_side(uint32_t line_index, uint32_t node, uint32_t backwards, const int32_t *pos);
int32_t GAMEAPI ai_route_crosses(uint8_t *car);
void GAMEAPI ai_dodge_scores(const uint8_t *car, const uint8_t *other, const int32_t *d, int32_t dist, int32_t rate, uint32_t line);
void GAMEAPI ai_follow_target(const uint8_t *my_slot, const uint8_t *other_slot, const int32_t *d, int32_t dist, int32_t width, int32_t along, int32_t rate, int32_t *slots);
void GAMEAPI ai_overtake_target(const int32_t *d, int32_t dist, int32_t rate, const uint8_t *my_slot, const uint8_t *other_slot, int32_t along, int32_t *slots);
void GAMEAPI ai_react_to_cars(uint8_t *car, int32_t *slots);
void GAMEAPI ai_candidates_clear(void);                                                      /* 0x4117a0 */
int32_t GAMEAPI car_has_item4(const uint8_t *car);                                           /* 0x47fee0 */
int32_t GAMEAPI line_node_near_end(uint32_t line_index, uint32_t node, uint32_t k);          /* 0x47ff70 */
int32_t GAMEAPI point_between(const int32_t *p, const int32_t *a, const int32_t *b, const int32_t *c,
                              int32_t sign);                                                 /* 0x411cb0 */
int32_t GAMEAPI ai_link_cost(uint32_t line, uint32_t node);                                  /* 0x411870 */
void GAMEAPI ai_candidate_measure(int32_t index, const int32_t *pos, const int32_t *travel, int32_t speed); /* 0x4119a0 */
void GAMEAPI ai_mark_inside_curve(uint8_t *car, uint32_t line, uint32_t node);               /* 0x411b90 */
void GAMEAPI ai_score_line_ends(uint8_t *car, int32_t n);                                    /* 0x411d80 */
void GAMEAPI ai_score_line_bias(uint8_t *car, int32_t n);                                    /* 0x411e90 */
void GAMEAPI ai_candidates_along(uint32_t line_index, uint32_t node, int32_t *n, uint32_t level,
                                 uint32_t skill);                                            /* 0x4120b0 */
int32_t GAMEAPI ai_collect_candidates(uint8_t *car, uint32_t line, uint32_t node);           /* 0x411f00 */
void GAMEAPI ai_find_objects_ahead(uint8_t *car);                                            /* 0x403200 */
void GAMEAPI ai_find_bodies_ahead(uint8_t *car);                                             /* 0x403a40 */
int32_t GAMEAPI ai_line_cost(uint8_t *car, uint32_t line_index, uint32_t node, const int16_t *from,
                             uint32_t section, int32_t speed, uint32_t count);               /* 0x403620 */
void GAMEAPI ai_score_hazards(uint8_t *car, int32_t n);                                      /* 0x42a260 */
void GAMEAPI ai_choose_target(uint8_t *car);                                                 /* 0x412290 */
void GAMEAPI ai_think(uint8_t *car); /* 0x4015e0 */
void GAMEAPI ai_maybe_full_throttle(uint8_t *car); /* 0x40f900 */
uint32_t GAMEAPI ai_start_delay(const uint8_t *car); /* 0x40f980 */
void GAMEAPI ai_maybe_fire(uint8_t *car); /* 0x410d70 */
void GAMEAPI ai_state_full_throttle(uint8_t *car); /* 0x424d80 */
int32_t GAMEAPI ai_state_coast(uint8_t *car); /* 0x424dd0 */
void GAMEAPI ai_state_start(uint8_t *car); /* 0x424e10 */
void GAMEAPI ai_state_rejoin(uint8_t *car); /* 0x424fe0 */
void GAMEAPI ai_clear_buttons(uint8_t *car); /* 0x4255a0 */
int32_t GAMEAPI ai_state_swerve(uint8_t *car); /* 0x425610 */
void GAMEAPI ai_markers_update(void); /* 0x42a510 */
void GAMEAPI ai_check_route(uint8_t *car); /* 0x46ac80 */
int32_t GAMEAPI route_reaches(uint32_t from, uint32_t target_line, uint32_t target_node); /* 0x46ad00 */
void GAMEAPI ai_stuck_check(uint8_t *car); /* 0x46af10 */
void GAMEAPI ai_state_unstick(uint8_t *car); /* 0x46afb0 */
void GAMEAPI ai_rejoin_plan(const uint8_t *slot); /* 0x47fbb0 */
int32_t GAMEAPI ai_fire_kind4(uint8_t *car); /* 0x47ff00 */
int32_t GAMEAPI ai_may_attack(const uint8_t *slot); /* 0x480030 */
void GAMEAPI ai_replan(uint8_t *car, int32_t refresh);
uint32_t GAMEAPI ai_chance(uint32_t odds);
void GAMEAPI ai_nearest_player(uint8_t *car);
void GAMEAPI ai_set_skill(uint8_t *car);
void GAMEAPI ai_roll_flag1(uint8_t *rec);
void GAMEAPI ai_roll_flag2(uint8_t *car);
void GAMEAPI ai_roll_flag4(uint8_t *rec);
void GAMEAPI ai_roll_flag8(uint8_t *rec);
void GAMEAPI ai_roll_flag10(uint8_t *rec);
void GAMEAPI ai_roll_flag20(uint8_t *rec);
void GAMEAPI ai_roll_flag40(uint8_t *rec);
void GAMEAPI ai_roll_flags80(uint8_t *rec);
void GAMEAPI ai_roll_flag200(uint8_t *car);
void GAMEAPI ai_roll_flag400(uint8_t *car, int32_t refresh);
void GAMEAPI ai_roll_flag800(uint8_t *rec);
void GAMEAPI ai_roll_aggression(uint8_t *rec);
void GAMEAPI ai_set_gap_level(uint8_t *car, int32_t refresh);                                        /* 0x406e30 */
void GAMEAPI wall_touched(const int32_t *contact, const int16_t *vertex);
void GAMEAPI track_edge_hit(uint8_t *slot, int32_t *out, int32_t *best, int32_t *state, const uint8_t *entry,
                            const uint8_t *next, const uint8_t *list);
uint32_t GAMEAPI slot_track_sweep(uint8_t *body, uint8_t *slot, int32_t *out, uint32_t limit);
int32_t GAMEAPI object_contacts(uint8_t *body, uint8_t *object, uint8_t *slot, int32_t dt);
void GAMEAPI object_polygon_test(uint8_t *body, uint8_t *object, const uint8_t *polygon, uint8_t *slot,
                                 int32_t *out, int32_t dt);
int32_t GAMEAPI quad_root(int32_t a, int32_t b, int32_t c);
int32_t GAMEAPI vec_normalise_shifted(int32_t *v, int32_t *out);
int32_t GAMEAPI sphere_sweep_point(const uint8_t *slot, const int32_t *point, int32_t *out, int32_t dt);
int32_t GAMEAPI sphere_sweep_edge(const uint8_t *slot, const int16_t *a, const int16_t *b, const int32_t *n,
                                  int32_t *out, int32_t extra, int32_t dt);
int32_t GAMEAPI plane_test(uint8_t *slot, const int16_t *vertex, uint32_t plane, int32_t dt);
void GAMEAPI polygon_contacts(uint8_t *slot, const uint8_t *polygons, uint8_t *body, int32_t dt,
                              const int16_t *centre, int32_t reach);                  /* 0x419820 */

/* collide.c */
void GAMEAPI body_update_spin(uint8_t *body); /* shared tail of the impulses and 0x465740 */
void GAMEAPI collision_resolve(uint8_t *event, int32_t dt, int32_t step);             /* 0x4615e0 */
void GAMEAPI contact_to_body_frame(uint8_t *body, uint8_t *contact);                  /* 0x4622f0 */
void GAMEAPI contact_push_out(int32_t *out, uint8_t *body, uint8_t *contact);         /* 0x43be60 */
void GAMEAPI body_translate(uint8_t *body, const int32_t *delta);                     /* 0x43bf00 */
void GAMEAPI body_contact_kind_16(uint8_t *body, int32_t step);                       /* 0x468400 */
void GAMEAPI contact_point_velocity(uint8_t *body, uint8_t *contact, int32_t *out);   /* 0x4660d0 */
void GAMEAPI body_set_velocity(uint8_t *body, const int32_t *v);                      /* 0x4663c0 */
void GAMEAPI vec_split(const int32_t *n, const int32_t *v, int32_t *along, int32_t *across); /* 0x4661a0 */
void GAMEAPI body_keep_normal_spin(uint8_t *body, uint8_t *contact);                  /* 0x466440 */
void GAMEAPI body_stop(uint8_t *body);                                                /* 0x466ae0 */
void GAMEAPI quat_mul(const int32_t *a, const int32_t *b, int32_t *out);              /* 0x4809d0 */
void GAMEAPI contact_advance(uint8_t *body, uint8_t *contact, int32_t t);             /* 0x461d00 */
void GAMEAPI contact_gravity_torque(uint8_t *body, uint8_t *contact);                 /* 0x4662b0 */
void GAMEAPI contact_save(uint8_t *body, uint8_t *contact);                           /* 0x462290 */
void GAMEAPI contact_share_normal_velocity(uint8_t *event, uint8_t *contact);         /* 0x461d70 */
void GAMEAPI impulse_track(uint8_t *body, uint8_t *contact, const int32_t *point_velocity); /* 0x43b870 */
void GAMEAPI impulse_bodies(uint8_t *a, uint8_t *contact_a, const int32_t *relative_velocity, uint8_t *b,
                            uint8_t *contact_b);                                      /* 0x43ba50 */

#endif
