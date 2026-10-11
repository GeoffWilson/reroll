# Renderer survey: dynamic objects and effects

Everything dynamic that `draw_view` 0x479be0 draws for one viewport (it is called per viewport from `race_draw`
0x443b50): cars, shadows, effects, weapons, weather, skid marks and the object/sprite passes. For each item this
document lists the draw function, the game state it reads, whether its corners are world-space or built on screen, and
the packet code / blend it produces. The goal is that a new (Vulkan) renderer can draw all of these from game state at
any resolution and aspect.

Companion documents: `pipeline.md` (ordering tables, packet formats, Direct3D back end, track and sky),
`views.md` (camera, projection, viewports, HUD and 2D). Data layouts from the game logic are in `../formats.md`.
Sources: Ghidra decompilation and disassembly of Rollcage.exe; nothing was run. "Sure" claims come from reading the
code; guesses are marked (guess) or **unsure**.

## Contents

1. Conventions shared by all drawers (projection, packets, depth order, flags)
2. Summary table
3. Cars
4. Bodies, track objects and the per-cell sprite / particle pass
5. Weather, skid marks, crumbling, TV, water
6. Effects and weapons
7. Screen-space effects (full-screen flash, time warp, respawn sparkle)
8. Things with no draw function of their own (lights, freezer, water, scorch)
9. Resolution / screen-space caveats
10. Draw-time side effects on game state
11. Open questions

## 1. Conventions

### draw_view order

After the camera set-up (0x466f70, `set_view_matrix` 0x423d10, 0x423d30) draw_view calls, in this order:
0x47bd20 car glow UV animation, 0x478df0 animated-texture clock, 0x490430 respawn sparkle, 0x42be20 blast flash,
0x47ea00 light glare, 0x479b30 submerged tint, 0x42c0a0 full-screen flash, 0x484980 weather, 0x438760 3D-anchored HUD,
0x433f00 wall ripples, 0x42b9a0 smoke rings, 0x42c950 shock waves, 0x42c420 blast ring, 0x4345c0 beams,
0x467ec0 pickup sparkles, 0x483210 projectile trails, 0x48fdd0 snatch, 0x46c840 shadows, 0x438310 HUD,
0x47a030 track visibility/lighting, 0x477940 track mesh, 0x479f60 OT state markers, 0x404f50 / 0x4049b0 sky
(far plane raised to 65536 around them), 0x47dd70 per-cell sprite/particle pass, 0x459b20 bodies, 0x46dc50 shields,
0x421ef0 cars, 0x4821d0 speed streaks, 0x442060 lens flares, 0x46f230 skid marks, 0x47db20 time warp, 0x46a9e0
countdown overlay (unless 0x6cce40 & 0x12), 0x41b2f0 TV screen. Call order does not matter much: everything is sorted
by the ordering tables afterwards.

### Projection (see pipeline.md 2.3 and 5, views.md)

Every 3D point goes through the GTE emulation: the caller writes int16 x, y, z to 0x6ccc64 / 0x6ccc66 / 0x6ccc68
(world coordinates in whole track units, i.e. the high halfword of the 16.16 positions; or model coordinates after an
object matrix was loaded), then calls `*0x6ccd78(rec)` (0x426a20, or 3DNow! 0x4268c0) and `*0x6ccd80()` (0x426b00).
`*0x6ccd7c` (0x426be0) is the variant used for sprite centres. The result is the 0x18-byte vertex record
`{flags, z, sx, sy, x, y}`: camera-space x, y, z (floats) plus screen pixels sx = x * H / z + 0x6ccd5c,
sy = y * H / z + 0x6ccd60, with H = 0x6ccd00 = zoom * W / 320, and outcodes 1/2/4/8 against the viewport clip rect
0x6ccd64..0x6ccd70, 0x10 nearer than 0x6ccd04 (2.0), 0x20 beyond 0x6ccd08. The matrix is the float copy 0x6ccd1c.. of
the int16 GTE matrix 0x6ccbe0. When no object matrix is loaded (sprites, particles, weather, skid marks, effects) it is
the view matrix 0x6ce5d0 (int16 3x3, 4096 = 1.0, translation 0x6ce5e4..0x6ce5ec). For model bodies 0x459e80(body)
loads view x body matrix (body +0xc4, int16) with translation view * (body +0x34 position >> 16) + view translation.

So every 3D packet carries camera-space coordinates as well as screen pixels: **the world-space inputs in this
document are what a new renderer needs; the screen numbers in packets can be ignored**, except for the items flagged
screen-space below, whose corners are built in pixels from one projected centre.

### Packets (pipeline.md 2.4)

PS1-style records: +0 tag/link, +4 r, g, b, code. Codes seen here (bit 1 = semi-transparent, blend pair chosen by the
texture page word's bits 6-7, pipeline.md 3.3):

| code | kind | size | used by |
|---|---|---|---|
| 0x8c / 0x8e | textured Gouraud quad (GT4) | 0x84 | model quads, shadow, speed cone, streaks, skid marks, snatch tube |
| 0x9c / 0x9e | textured Gouraud triangle (GT3) | 0x64 | model triangles, engine glow tris, shield bubble |
| 0x98 / 0x9a | textured flat quad (FT4) | 0x78 | all billboards / sprites, smoke and shock rings, beams, flares, snow, flashes |
| 0x96 | textured flat triangle (FT3) | 0x60 | snatch funnel |
| 0xaa | untextured flat quad (F4), semi-transparent | 0x68 | wall-impact ripple rings |
| 0xac / 0xae | line (G2), drawn as a thin quad of width 0x6cd41c | 0x3c | rain, shrapnel |

Vertex 0's flag word is preset to 0x40 in sprite packets ("force the screen-space clipper", pipeline.md 2.3).
The fade/alpha of a blended packet is either multiplied into the rgb or written to the alpha byte +0x22, depending on
the device-capability bits `0x6ccd84` (bit 0 set: rgb full, fade in +0x22; bit 1: shadows/skids allowed; bit 2: a
reduced-effects path that skips the sun flare, env map, speed cone, snatch funnel and full-screen flashes). These are
capability switches, not game state: a new renderer should treat "fade" as alpha/intensity and pick one blend path.

### Depth order (pipeline.md 2.1)

bucket = (int)z >> 5 (z = camera depth of a chosen vertex, often the farthest), then > 0x100: /2 + 0x80,
> 0x180: /2 + 0xc0, clamped 1..0x1ff, plus a per-effect bias. The packet is pushed on `0x6cd510[bucket]` (OT of view
record 0x6cd4a0) or `0x6cdd80[bucket]` (view record 0x6cdd10), drawn far to near. Bucket 1 (0x6cd514 / 0x6cdd84) is
"front" (drawn last), 0x1fe / 0x1ff the sky. `0x6cce84` is the z-buffer mode: opaque model faces then go into
`0x6cd518 + (tpage & 0x3f) * 8` (sorted by texture), while the car code sends its blended parts by depth to
`0x6cdd80` (which is flushed after 0x6cd510, so translucent after opaque; this reading is a guess consistent with
pipeline.md's flush order). The overlay OT 0x6ce580 (entry 0x6ce584) is flushed after both views with fog off
(screen flakes). Many records keep two packet sets selected by `0x6ce60c == 0x6cd4a0`; this is PS1 double buffering
and only storage for a new renderer.

### Other globals used throughout

- Car slots 0x6bfb80, 0x274 bytes each (+0 car pointer, +8.. view rect, +0x28 camera record), count race +0x54d
  (race = `*0x4b1960`). Race tick 0x6c0a50; paused flag 0x4d1198; effect RNG 0x4b11d8.
- Texture records `*0x6c1e14`, 16 bytes; "record base + n" means n relative to `U16 *0x6c1eb0 + 0x2e`, the track's
  first record (formats.md setup_t/u/v). A record gives the 4 corners' u, v, CLUT and texture page.
- Sine table 0x4b24f8 (int16, 4096 = 1.0, 256 steps per turn), cosine 0x4b2578.
- Sprite size scale `0x6cce44` = (zoom * 256 / 0x68) * W / 320 (0x423e10), i.e. 256 * H / 0x68; `0x6cd40c` =
  W * 256 / 320; `0x6cd41c` = line width / sprite scale (W / 640 based); `0x6cd404` = zoom * 256 / 0x68 (LOD, fade);
  `0x6cd400` = 0x6800 / zoom. All W-based scales follow the screen width (Vert- on wide screens, views.md 5.4).
- `0x6cce94` mirror flag (flips back-face tests). `0x6cce40` camera mode bits (0x10 TV/replay camera, 0x40 in-car).
- Options `*0x4b2198`: +0x24 car env map, +0x29 time warp effect, +0x2a animated shield/snatch textures, +0x2c skid
  marks, +0x30 speed cone, +0x31 detail (LOD distances x2). Config +0x68 smoke amount.

## 2. Summary table

Space: **world** = all corners are world/model points projected by the GTE; **billboard** = a world centre projected,
corners built in pixels around it (redo as a camera-facing quad); **screen** = positioned in pixels, no world anchor.

| thing | draw fn | state read | space | primitive / blend |
|---|---|---|---|---|
| Car culling / depth | 0x4231f0 | car +0x34 pos; writes +0x646 visible, +0x14b4 depth | world centre | none |
| Car body lift | 0x422140 | wheel flags car +0x324 + i*0xc4, up column +0xc6/+0xcc/+0xd2; writes +0x14bc..+0x14c8 | world offset | none |
| Car body (and other kind-3 bodies) | 0x422440 -> 0x4226a0 -> 0x45b400 + 0x4226d0 | car +0x10 model, +0x24 vertices, +0x28/+0x2c/+0x30 LOD faces, +0x34 pos, +0xc4 matrix, +0x11c cell, +0x12c ambient, lights 0x5d8000 | model | GT4 0x8c / GT3 0x9c, +2 per translucent face, lit |
| Wheels (4) | 0x4224a0 -> 0x4226a0 | car +0x324 + i*0xc4: +0x6c wheel body, +0x84..+0x98 extents | model | as body |
| Per-car layer record | 0x421e60, 0x422100 | car +0x1408 (3 x 0x38), +0x14b0 | n/a | one OT entry per car holding 10 sub-lists |
| Engine glow mesh | 0x422540, 0x47b470; UV anim 0x47bd20 | car +0x1108 glow record, colour +0x13f8..+0x13fe, texture records 0x91..0x95 | model | GT3 0x9e / GT4 0x8e, flat colour, translucent |
| Brake / reverse halos | 0x422f50 | model light points (model + u16 +0x4c), car +0x63e brake, +0x640 throttle | billboard (pixel size, max 0x80) | FT4 0x9a, packets car +0x2290 / +0x2380 |
| Chrome env-map overlay | 0x46e4b0 -> 0x46e540 | option +0x24, car +0x10b4 visible faces, normals model +0x48, car matrix, x/z position | model (reuses body vertices) | 0x9c-byte packets at car +0x10ac / +0x10b0, face code \| 2, grey |
| Speed cone | 0x475580 (+0x475290) | option +0x30, slot +0x265, car +0x68 velocity, +0x654 speed, rings 0x5f72c0, anim 0x5f7400 + i*0x1088 | model (velocity frame) | GT4 0x8e, records 0x86..0x8e |
| Shield bubble | 0x46dc50 -> 0x46dd20; linked by 0x46dff0 | car +0x6ac -> 0x6002e0 + i*0x84 (+0x70 vertex rgba), mesh `*0x4d44a4`, car matrix + lift | model (car frame) | GT3 0x9e, page `*0x4b24a0` or records 0x4e + (tick & 7) |
| Shadow | 0x46c840 -> 0x46c890, 0x46c9d0 (0x46caa0) | car +0x720: 4 corners int16 + alpha byte; record 0x41 | world | GT4 0x8e, tpage abr 2 |
| Mirror cars (reflective boxes) | 0x422c80 -> 0x422d30, drawn by 0x422440 | race +0x54e, track info +0xd4 boxes (count +0xff), slot +0x260 mirror body, +0x264 | world (mirrored) | as body |
| Side speed streaks | 0x4821d0 | car +0x14cc + i*0x6d4 (2 ribbons x 12 points), +0x654, +0x68, matrix, width table 0x4b2aac | world | GT4 0x8e, records 0x47..0x4c |
| 3D-anchored HUD (car arrows, names) | 0x438760 | car +0x34 with y - 16 | screen at a projected point | views.md (HUD) |
| Bodies / track objects / pickups / weapon bodies / wheels | 0x459b20 -> 0x45a260 -> 0x459e80, 0x45b400, 0x45a430 / 0x45ab20 | body +0x10 model, +0x24 vertices, +0x34 pos, +0xc4 matrix, LOD lists, light word, +0x11c cell, +0x180 baked runs | model | GT4 0x8c / GT3 0x9c, +2 translucent |
| Animated textures | clock 0x478df0, applied in 0x45a430 | 0x6bfb44 (0x18 each, count 0x6bfb48), frames `*0x6bfb4c` | n/a | UV swap + corner rotation 0x45a2f0 |
| Crumbling buildings | as bodies | body +0x24 -> sinking vertex copy (`*0x6d04a4` entry +0x98) | model | as bodies |
| TV screen | 0x41b2f0 (UVs 0x41b000) | 0x6ce704 state, `*0x6ce708` body, 0x6ce710 face indices, 0x6ce734 colour | model | patches 4 queued GT4 packets |
| Track flare sprites (kind 2) | 0x47e1b0 | sprite object +0x10 pos, +0x16 size, +0x17 fade | billboard | FT4 0x9a, half height, fog colour |
| Light glow sprites (kind 0xd) | 0x47e220 | +0x10 pos, +0x16 size, +0x108 rgb, +0x10b intensity | billboard, spins with screen position | FT4 0x98 |
| Particles: smoke, dust, sparks, glints, spray | 0x47dd70 -> 0x47eaa0 / 0x47e340 -> 0x47e5f0, 0x47e4c0, 0x47ef30 | pool `*0x6ccb5c`, 0x100 each: +0x88 pos, +0x95 size, +0x98 rgba, +0xa2 behaviour, +0xa4 texture/frame | billboard (streaks along camera right) | FT4 0x9a / 0x98 |
| Debris (kind 0xe) | 0x47e2a0 | `*0x5d9290`, 64 x 0x180: +0x10 pos, +0x2d size | billboard | FT4 0x98, grey 0x50, record 0x4d |
| Shrapnel (kind 5) | 0x47e020 | `*0x6b983c`, 0x200 x 0x80: +0x4c pos, +0x64 next point, colours in packet | world line | G2 0xac / 0xae |
| Rain | 0x484980 -> 0x484a10 | emitter `*0x5d6360[i]`: +0x14 400 drops, +0xc98.. drift; camera velocity | world line per drop | G2 0xae, `*0x5d6340` |
| Snow | 0x484980 -> 0x484c20 | same drops | billboard, max 12 px | FT4 0x9a, record 0x66 |
| Screen flakes / lens drops | 0x485d00 (0x485140 spawn, 0x4857f0 move) | `*0x4d5898`, 32 x 0xa0 | **screen** | FT4 0x9a, overlay OT 0x6ce584 |
| Skid marks | 0x46f230 | `*0x6002d4`, count 0x4b1c68, 0x124 each | world | GT4 0x8c family, record 0x8f, end fades |
| Wall-impact ripple rings | 0x433f00 (spawn 0x433d90) | 0x6c8020, 8 x 0x18 | world | F4 0xaa, 16 per ring |
| Ground smoke rings | 0x42b9a0 | `*0x6ccb54`, 5 x 0x1e2c | world (XZ annulus) | FT4 0x9a, 32 per ring |
| Shock waves | 0x42c950 | `*0x6cc800`, 3 x 0xf14 | world (cylinder band) | FT4 0x9a, records 0x3a..0x3e |
| Blast flash ring | 0x42c420 | 0x4be180 centre, 0x4be1a8 counter | billboard, ring radii in pixels | FT4 0x9a, records 0x47..0x4c |
| Sun and lens flares, projectile head flares | 0x442060 (queue 0x441d80) | 0x4beb90, 10 x 0x4c0, count 0x4beb88 | **screen** chain from a projected point | FT4 0x9a |
| Projectile trails + heads | 0x483210 -> 0x483240 | 24 weapon bodies 0x6b9ce0: +0x1b8 trail ring, +0x5dc packets, +0x22ec head | world (heads: billboard / flare) | FT4 0x9a, records 0x47..0x4c, 0x58, 0x3f |
| Pickup sparkles / collect flash | 0x467ec0 -> 0x467ff0 / 0x467f30 | `*0x6b8ce0`, count 0x6b8ce4: +0x34, +0x180, +0x182, +0x552 | billboard | FT4 0x9a |
| Lightning beams | 0x4345c0 | 0x6c8100, 4 x 0x18; jitter `*0x4be37c` | world | FT4 0x98, records 0x14 / 0x15 |
| Snatch / wormhole | 0x48fdd0 -> 0x48fdf0 | body `*0x5d1fc0`: +0x344 72 points, +0x6a4 centre, +0x2a1c colours | world | GT4 0x8e tube + FT3 0x96 funnel on page `*0x4b24a0` |
| Full-screen flash / tint | 0x42c0a0 (+ 0x42be20, 0x479b30, 0x47ea00, 0x47db20) | 0x4be134 colour, 0x4be11c.. uv, 0x4ac72c page | **screen** (viewport rect) | FT4 0x9a, front bucket |
| Time warp tunnel | 0x47db20; texture by 0x47d5a0 | 0x4d57f0, 0x4d57f4, page `*0x4b24a0` | **screen** + CPU texture | via full-screen flash |
| Respawn / snatched sparkle | 0x490430 | car +0x689, +0x68c -> 0x5cf860 + i*0x690 | **screen**, random | FT4 0x9a, front bucket |
| Countdown overlay | 0x46a9e0 | 0x4d3da8, 0x4d3dac, record 0x7e | **screen** | views.md (HUD) |
| Dynamic lights, freezer, water, ripples, scorch | none (vertex colours) | 0x5d8000, 0x6c3080, 0x5d6378, 0x6c1eb8 | world (track/body vertices) | track / body packets |
| Sky clouds / backdrop | 0x404f50 / 0x4049b0 | 0x6d0a00.., 0x6d0a40 | world, far | pipeline.md 4.5 |
| OT state markers | 0x479f60 | 0x5d9b00/08/10 | n/a | pipeline.md |

## 3. Cars

Cars are drawn by 0x421ef0: first the kind-3 bodies collected by the body pass 0x459b20 from visible cells (pointers
0x6ce640, light keys 0x6ce620), then every race car whose +0x646 is set (light key 0), then mirror cars. A car's body
position for drawing is `car +0x34.. + car +0x14bc..` (lift, below), not the physics position.

### Car visibility and depth (0x4231f0)

For each race car (slot array 0x6bfb80, 0x274 bytes per slot, +0 car pointer, count race +0x54d): projects the car centre (int16 high halves of +0x34/+0x38/+0x3c), stores rounded view z at car +0x14b4, and sets car +0x646 = 1 when z >= -80, the car is not the hidden in-car-view car (0x6cce40 bit 0x40, slot = (0x6cce40 >> 7) & 0xf), and either the point is off-near/far or a screen circle of radius (0x6ccd00 / z) * *0x4a334c overlaps the viewport. All later per-car draws test `+0x14b4 > -0x50` (or -100). New renderer: frustum-cull a bounding sphere round car +0x34; honour the in-car hidden-car rule.

### Car body lift (0x422140)

car +0x14c8 eases (half the gap if > 0x18, then +-1 a frame) towards 0x18 when any wheel record (car +0x324 + i*0xc4) has bit 0 set (0 if none; -0x30 + 0x18 = -0x18 when car +0x63f is set); then (unless replay flag 0x4d1198) car +0x14bc/+0x14c0/+0x14c4 = up column (int16 +0xc6, +0xcc, +0xd2) * +0x14c8 * 8. The body, glow, shield, cone and shadow probe are all drawn at car position + this offset (16.16). So the drawn body position is `+0x34.. + +0x14bc..`, not the physics position.

### Per-car layer record (car +0x1408, pointer +0x14b0)

Three 0x38-byte records at car +0x1408 (formats.md calls them "skid packets"; they are not). 0x422100 rotates +0x14b0 to the next one each frame and 0x427a40 makes it 14 linked empty list heads. Offsets inside it are per-car sub-lists drawn in this order (earlier = further back): +0x8 shadow, +0xc shield back faces / cone, +0x10 far wheels, +0x14 near wheels (outer face hidden), +0x18 glow (back-facing), +0x1c body, +0x20 env-map overlay, +0x24 glow (front-facing), +0x28 near wheels (outer face visible), +0x2c shield front faces / cone front. 0x421e60 links the whole record into the depth OT at the car's bucket ((z - 2 * model u16 +0x32) >> 5) - 2 + bias (bias 0 normal, 0x28 for the mirror car), as a sub-list (pointer | 0x80000000). Only when 0x6cce84 == 0; in the other mode each part inserts itself into 0x6cdd80 by its own depth. New renderer: with a depth buffer this ordering only matters for the semi-transparent layers (glow, env map, shield, shadow, halos, cone).

### Car body and other kind-3 bodies (0x422440, 0x45b400, 0x4226d0)

Who: 0x421ef0 first draws kind-3 bodies collected by 0x459b20 from visible grid cells into 0x6ce640 (pointers, 0-terminated, read up to 6) with their cell light keys in 0x6ce620, then every race car whose +0x646 is still set, using the light key 0 (no dynamic lights). After drawing, +0x646 is cleared. The mirror car (slot +0x260) follows with bias 0x28 when slot +0x264 is set.

0x422440(car, lights, bias): z = 0x422650 (centre of model bounding point model +0x28/+0x2c transformed with the body matrix at position + lift) -> car +0x14b4; if z > 0: body mesh, glow verts (0x422540), wheels (0x4224a0), OT insert (0x421e60).

Lighting (0x45b400(body, lights[3])):
- Light key (u32): three 10-bit indices (bits 0-9, 10-19, 20-29) into light records 0x5d8000 (0x40 bytes; see formats group X/GC: +0 x,+4 y,+8 z 16.16, +0xc colour, +0x10 radius).
- Light colour matrix (0x428940 from 0x459cb0): channel = (byte * 0x40) clamped 0xfff, from each light's +0xc colour (r = byte 0, g = bits 2..15 >> 2 range-limited, b = bits 10..23; read as written in 0x459cb0).
- Light direction rows (0x45ba30): d = body pos - light pos (integer units); rejected (zero) if any |component| > 0x4000; falloff = table *0x4d4b9c[(|d|^2 summed) >> light +0x10 byte]; d normalised and scaled by falloff * 4 (max 0x1fffc) >> 15. The 3x3 is then rotated into model space by the body matrix (light-local matrix 0x6ccc00.., float copy 0x6ccdd0).
- Ambient/back colour 0x6ccc14..0x6ccc1c (float 0x6cce30..38): only when body flags bit 0x800. Body +0x11c = track cell (byte 0 section, byte 1 column, high word row; -1 = none -> 0x400 grey). Colour = palette (0x486350() returns the palette; 0x400 entries) [vertex colour word >> 22] of the section vertex grid (*0x6bfac0[section] +0x20, (width+1)*row + column, 8-byte vertices, +4 colour word); entries >= 0x3f0 give 0x202020. Eased per frame at body +0x12c (0x45b390: each channel moves at most 0x18 a frame); ambient = channel << 4 | 0xf.
- Per-vertex colour (0x427400): c = BK + LCM * max(0, LLM * normal), normals at model +0x48 (int16 x,y,z,pad, 4096 = 1.0), clamped to 0xfff, >> 4 -> bytes.

Mesh (0x4226d0(body, sublist, z)); all offsets are body offsets, the car being its own body:
- Body +0 kind (low 9 bits; 3 = car), +0x10 model header (u), +0x24 vertex list (int16 x,y,z,pad), +0x28 / +0x2c / +0x30 face lists for LOD 0 / 1 / 2, +0x10d / +0x162 / +0x163 vertex counts - 1 per LOD, packet sets (2 each, by draw buffer) +0x14/+0x18 (LOD 0), +0x1c/+0x20 (LOD 1), +0x14c/+0x150 (LOD 2). LOD 1 when z > *0x6cd404 * 0x5dc >> 8, LOD 2 when z > *0x6cd404 * 0xea6 >> 8 (model header += u16 +0x1c / +0x1e for the LOD's header). *0x6cd404 is a detail scale (not checked here).
- Face list entries are 8 bytes: corner indices bytes 0..3 (byte 3 = 0 for a triangle), dword +4 bit 31 = semi-transparent. Every face has a prebuilt 0x84-byte packet (setup 0x460dc0 copies UV/CLUT/tpage words from the model's 0x28-byte face records: +0x20 uv0+clut, +0x40 uv1+tpage(+0x42), +0x60 uv2, +0x80 uv3; colours +4, +0x24, +0x44, +0x64; vertices +8, +0x28, +0x48, +0x68). Code byte +7 = 0x9c triangle / 0x8c quad, | 2 if semi-transparent.
- Back-face culled in screen space (0x4278f0 signed area, sign flipped by 0x6cce94) and rejected when all corners share an outcode.
- Faces come in groups (packet +0x82 u16 carries group size and flags); the visible groups are linked into the sub-list in an order taken from a precomputed table (model +0x18 [mask of facing bits], 4-bit group numbers; second table model +0x44): a per-model painter's order. With 0x6cce84 set, groups go instead into 0x6cd518[tpage & 0x3f] (i.e. bucketed by texture page).
- For kind 3 at LOD 0 it also records the visible face indices as bytes at car +0x10b4 (end pointer stored at +0x10fc, limit +0x10fc), packet base at +0x1100, and sets +0x1104 = 1: the env-map pass uses them.
- New renderer inputs: model (vertices, faces with UV/CLUT/tpage, normals, LOD headers), body matrix +0xc4, position +0x34 + lift +0x14bc, light key, cell +0x11c for ambient. Car textures/colours are only whatever the per-kind model's faces reference (models per car kind: body slot 0x4a7b68[kind], glow slot 0x4a7b78[kind] in the track header +0x58 slot table). No per-player palette swap was seen in the draw path.

### Wheels (0x4224a0, 0x4225c0)

Four wheel records at car +0x324 (0xc4 each): +0x6c wheel body (model slot 8, placed each step by car_wheels_place 0x40de60, which writes its +0x34 position and +0xc4 matrix), +0x84..+0x98 int16 extents (3 points read as pairs from +0x84/+0x88, +0x8c/+0x90, +0x94/+0x98). Each wheel: depth with the same lift; 0x4225c0 transforms the three extent points with the current matrix and tests their winding (outer hub face visible?). Layer: +0x10 if the wheel is further than the car body, else +0x14 (hub hidden) or +0x28 (hub visible). Drawn with 0x4226a0 using the same light key as the car. Wheels are ordinary model bodies: world matrix + position are all that is needed.

### Engine glow (0x422540, 0x47b470, 0x47bd20, 0x422f50)

Glow record car +0x1108 (0x300 bytes): +0 drawn flag (set by 0x422540, cleared by 0x47b470), +1 index of a face whose facing picks the layer, +4 glow body (+0x110c; model = glow slot per car kind, vertices offset at setup by 0x4b2480[kind]), +0xa u16 frame 0..4, +0xc.. projected glow vertices (0x18 each from +0x1114, 1-based so vertex n at +0x10fc + n*0x18), +0x2dc/+0x2e0 picked vertices, +0x2ec size, +0x2f4 random phase, +0x2f6 up to 8 face indices (-1 ends), and colour fields car +0x13f8/+0x13f9/+0x13fa (r,g,b; the same as the head light kind 8 light), +0x13fc pulse index into sine 0x4b24f8, +0x13fd flash (odd = -150 brightness), +0x13fe up to 8 faces drawn at half colour.
- 0x47bd20 (start of every view): frame = (+0xa + 1) mod 5; texture record = track base + 0x91 + frame; writes UV words +0x20, +0x40, +0x60, +0x80 and tpage +0x42 into the +0x2f6 faces' 0x84-byte packets of the current buffer (glow body +0x14/+0x18).
- 0x422540: projects the glow body's vertices (glow body +0x24, count +0x10d + 1) with the current (car) matrix.
- 0x47b470: colour = max channel scaled pulse: base rgb + (sin(+0x13fc) >> 9) * max(r,g,b) >> 8, clamped; then "boosted" so the brightest channel is 0xff, alpha byte at +7 of the colour = brightness (with detail flag 0x6ccd84 bit 0 clear, rgb are premultiplied by it). Flat colour on all corners, code 0x9e (tri) / 0x8e (quad) = semi-transparent, so additive-looking glow. Layer +0x24 if the +1 face is front-facing (area >= *0x4a3318) else +0x18; returns that list head, stored in car +0x228c.
- 0x422f50 (brake/reverse halos): up to two 8-byte light points at model + u16 model+0x4c (int16 x,y,z, byte +6 type must be 2; stops at the first other type), transformed by the car matrix; size = (0x6cce44 << 11 >> 8) / (z - *0x4a3348), max 0x80 pixels, skipped if z <= 0x20 or off screen (packed 16-bit compare against 0x6cd478). A screen-aligned square of +-size pixels round the projected point, all four z = that z. Colour: brake (car +0x63e >= 0x21) 0x0000ff r..; else throttle +0x640 < 0 (reversing) 0xffffff, otherwise 0x0000a0 (rgb bytes in r,g,b order: brake = bright red, idle = dim red, reverse = white), scaled by 0xdc unless detail bit 0; packet code byte 0x9a, alpha byte +0x22 = 0xdc. Packets: car +0x2290 (buffer 0) / +0x2380 (buffer 1), 2 x 0x78 (texture set at setup). Inserted into the glow layer (+0x228c) or 0x6cdd80 by depth.
- New renderer inputs: glow model + car matrix/position + lift, colour +0x13f8..+0x13fd, frame +0x1112, light points, brake/throttle. The halo size is in pixels (section 9).

### Chrome / environment map overlay (0x46e4b0, 0x46e540, 0x46e450, 0x46ea10)

Runs when option byte *0x4b2198 +0x24 is set, detail 0x6ccd84 bit 2 clear, and car +0x1104 (body was drawn at LOD 0). Env texture: track base + 0x56 outdoors, + 0x57 when the section under the car (*(*0x6c1e30)[cell & 0xff] +4 bit 3) is flagged (indoors guess); 0x46e450 writes its CLUT/tpage into all 0x78 overlay packets and the base u,v into car +0x1106/+0x1107. Intensity per car kind: model matched against slot table 0x4b1c50 (7 u16 slots) -> 0x4b1c34[kind], randomly flickered (0x486270), grey rgb, code 0x8e base, final code = face's code | 2.
UV per vertex (0x46ea10): rotate the vertex normal (model +0x48) and the vertex by the car matrix only (no view), translation = ((pos >> 20) & 0x7f) - 0x40 in x/z (so the map slides as the car moves); u = base_u + clamp((n.x >> 10) + 0x40 + (v.x >> 1), 0..0x7f), v = base_v + clamp((n.z >> 10) + 0x40 + (v.z >> 1)). For each visible face in car +0x10b4..*(+0x10fc) (max 60) whose rotated vertices all have y < 1 (upward-facing part only, guess: y up negative), copies the body face packet's projected vertices (+8/+0x28/+0x48/+0x68) into an overlay packet (0x9c bytes, buffer car +0x10ac / +0x10b0; formats.md calls this the tyre trail buffer, it is not) and links it into the +0x20 layer. New renderer: a second, additive/blended pass over the body's upward faces with world-normal planar env mapping (inputs: normals, car matrix, car x/z position, base UV of record 0x56/0x57, kind intensity).

### Speed cone (0x475580, 0x475290, data 0x5f72c0 / 0x5f7400)

Option *0x4b2198 +0x30, detail bit 2 clear, slot +0x265 != -1 (set by car_textures_update 0x4751b0 for up to 6 cars faster than the threshold, speed +0x654 > 0x3a0000; index into 0x5f7400 + i*0x1088). Frame: matrix from the car's velocity (+0x68..+0x70, normalised 0x407e90) and up column (0x407220 look matrix), origin = car pos + lift/2. Geometry: 5 rings x 8 points at 0x5f72c0 (point (r,c) at +r*8 + c*0x28: int16 x, y, z), ring radius 16r, z = 0x50 - (6r)^2/16; 0x475290 rewrites each ring's z (0x46 - (6r)^2/16, pushed out at rings 3/4 by speed, jittered by the RNG 0x4b11d8) and colour ((0xff - r*0xc0/5) * (rnd + speed)) >> 8, squared for g/b, code 0x8e. 4 x 8 quads of 0x84 bytes at 0x5f7400 + i*0x1088 (+0x40 first UV word), UVs from car_texture_frames 0x475430 (9-frame animation, records base + 0x86 + frame, stepped every other tick, v spread down the strip). Quads split into front-facing / back-facing lists inserted into layer +0x2c and +0xc (or 0x6cdd80 by car depth). Semi-transparent, textured, Gouraud. Purpose not confirmed in game (looks like a turbo/speed bow wave in front of fast cars).

### Shield ripple bubble (0x46dc50, 0x46dd20, 0x46dff0)

Car +0x6ac -> record (6 at 0x6002e0, 0x84 bytes; -1 none): +0 active, +0x64 the car, +0x68/+0x6c packet lists for the two buffers (0x64 bytes per triangle: tag, colour +4, vertices +8/+0x28/+0x48 (0x18 each + colour/UV tail), UV words +0x20/+0x40/+0x60, tpage +0x42 (+0x22 alpha byte, from colour byte 3)), +0x70 per-vertex colour dwords (built by game logic from ripple heights; byte 3 used as alpha), +0x74..+0x80 front head/tail, back head/tail (reset to -1 each view by 0x46dc50). Mesh *0x4d44a4 (+4 vertex count - 1, +6 triangle count, +0xc triangles of 8 bytes (three 0-based? indices as used: byte n * 0x18 from a -1 based array, so 1-based), +0x40 vertices int16 x,y,z,pad). Transformed with the car matrix at car pos + lift. Unless animated textures (option +0x2a) are on with snatch page *0x4b24a0 != -1, UVs are overwritten each frame from texture record base + 0x4e + (tick 0x6c0a50 & 7) (8-frame animation). Code 0x9e. Back faces -> layer +0xc, front faces -> +0x2c (0x46dff0; in 0x6cce84 mode both into 0x6cdd80 at car depth). Only drawn if the view-space origin is in front (0x4266d0 z > 0). New renderer inputs: shield mesh, car matrix/position/lift, per-vertex colours/alphas at record +0x70, active flag, frame.

### Shadow (0x46c840, 0x46c890, 0x46c9d0, 0x46caa0; init 0x46c220; update post_j 0x46c300)

Shadow record = car +0x720 (registered in list 0x4d3de4, count 0x4d3dfc; only cars use it as far as seen):
- +0 4 corners x 8 bytes: int16 x, y, z (world integer units, i.e. 16.16 >> 16) and alpha byte +7; computed by shadow_corners (post_j.c): the car's side/forward axes * 0x380, dropped onto the ground plane under the car, lifted 4 or 6 units along the normal, alpha = (0x80 - 2*height_above) * |n.y|.
- +6 drawn flag (car +0x726), +0x20 + buf*0x84: two 0x84 quads (texture record base + 0x41, tpage word has bit 0x80 set and 0x40 cleared in its high byte -> semi-transparency mode 2, code 0x8e, tag size 0xc), +0x128 packet chosen this frame, +0x12c 16 sub-quads (0x84 each), +0x96c depth bucket, +0x970..+0x978 cleared at init, +0x97c two tpage words (0x427ad0).
- 0x46c890: projects the 4 corners, colour per corner = grey (alpha*4 clamped 0xff), bucket from the nearest corner (>> 5) - 4.
- 0x46c9d0 (needs 0x6ccd84 bit 1 = shadows on): normal mode links the quad into layer +0x8 (drawn first, under the car). In 0x6cce84 mode, 0x46caa0 splits it into a 4x4 grid of quads by interpolating the projected screen positions, z and colours linearly (0.25 steps, *0x4a33d4), then inserts all 16 at the car's depth into 0x6cdd80.
- New renderer: a blended (darkening) textured quad from the 4 world corners and per-corner alpha. The subdivision is a screen-space artefact; with perspective-correct interpolation it is unnecessary.

### Car reflections (0x422c80, 0x422d30, 0x422eb0)

Only when race +0x54e is set. For each car, 0x422eb0 searches track info (*0x6c1eb0) +0xd4 (count byte +0xff) 8-byte boxes: int16 x, y, z, half-size; if the car centre is inside a box, slot +0x264 = 1 and 0x422d30 fills the mirror body (slot +0x260): matrix with entries 0,4,5,6 negated (0x422e30: a y-mirror), position y' = 2 * box.y - y, same for its 4 wheel bodies (car +0x390 + i*0xc4 i.e. wheel record +0x6c), lift negated, and the whole glow record (0x300 bytes) copied, keeping its own glow body. Drawn by 0x421ef0 (bias 0x28 after the real car), plus glow (0x47b470) but not shadow/cone/env. New renderer: a mirrored draw of the car about the plane y = box.y (water/ice reflection), clipped to the box area.

### Side speed streaks (0x4821d0; update 0x481f20)

Two trails per car at car +0x14cc + i*0x6d4: +0 next index, +4 points in use (max 12), +8 int16 texture scroll, +0xa byte angle, +0x14 12 points of 3 x int32 (16.16 world, beside each rear wheel body +0x518 + i*0xc4, offset (+-8/9, +-6, 0) in the car frame), +0xa4 packets (0x84 each, 12). Drawn when speed s = (+0x654 - 0x370000) * 0x80 / 0x1e0000 > 1 (max 0x96), the car is moving forward (dot(forward column +0xc8/+0xce/+0xd4, velocity dir) > 0xea5), and velocity is not tiny. For each point (newest backwards) two world points: point +- side axis (column +0xc4.., negated for trail 1) * width, where width = 0x4b2aac[k] +- an index-dependent value, rotated by sin/cos of the +0xa angle (decreasing by 0x14 per point) using the up column too, i.e. a twisting ribbon. Brightness per point from s fading along the trail plus sin(phase) wobble (phase = +8 byte, minus 0x28 per point). Quads between consecutive points, texture records base + 0x47 + (index % 6), colour 0x8effa0a0 (code 0x8e, b=0xff, g=r=0xa0 in byte order 0xa0,0xa0,0xff) times brightness unless detail bit 0, alpha bytes +0x22/+0x43 etc = brightness. Depth from the further corner. New renderer: a world-space ribbon from the 12 stored points; everything else is derived from car state. (Name in post_j.c is "tyre trails"; visually these are the blue speed streaks; unconfirmed.)

### 0x438760 / 0x438310 / 0x46a9e0

- 0x438760: 3D-anchored HUD. 0x439400 (game modes race +0x551 = 3, 5..8) places an arrow sprite (texture record base + 0xa3) above each other car at the screen point of (car pos with y - 0x10) from 0x43a3e0 (returns 0 if z outside [*0x4a3334, *0x4a339c]; screen coords made viewport-relative with 0x6ccd54/0x6ccd58); mode 8 also an off-screen direction arrow from the car-to-car vector. 0x43a000 places name/position text above cars the same way (offset -(0x6cd410 * 0x16 >> 8)). 0x45cfe0 when race +0x552 == 0. Sizes scale with 0x4acc38/*0x4acff0 and 0x6cd410 (resolution-relative). The drawing is in views.md (HUD); the only world input is the car position.
- 0x438310: pure HUD (overlay OT *(0x6ce608+4)), not covered here.
- 0x46a9e0: screen-space overlay sprite (texture record base + 0x7e, frames 0..9 from 0x4d3da8, fade by 0x4d3dac, size from race +0xa82 * 0xff / 0x280), only while race +0x57c < 4 and 0x6cce40 & 0x12 == 0: the start countdown (guess). views.md.

## 4. Bodies, track objects and the per-cell sprite / particle pass

### Bodies and track objects: 0x459b20 -> 0x45a260

- **Per view.** Picks the per-view shared packet buffer: 0x6c1e18 = *0x4beb84 or *0x6c1e1c, limit 0x6c1e24 =
  *0x6c1e34 / *0x6c1e38. For each static-object list entry from 0x47dd70 (0x5d7040 index, 0x5d6ac0 light word):
  - **Light word.** Three 10-bit light indices into 0x5d8000 (0x40-byte light records, setup_x formats). 0x459cb0
    builds the GTE light colour matrix 0x6ccc20 (via 0x428940): each column is a light's rgb * 0x40, capped 0xfff.
  - **Which objects.** It walks the objects linked from that list. Only those with kind bit 0x400 are drawn. Kind
    (low 9 bits) 3 = car: deferred to 0x6ce640 (light word in 0x6ce620) and drawn by 0x421ef0 (see Cars).
  - **Triggers (side effect).** Any drawn object with trigger actions (+0x16a != 0) and the game not paused is
    appended to the trigger queue 0x6b9ba0 (count 0x6b9cb8, max 70). **This is a game-logic side effect of drawing:
    triggers fire when the object is drawn in some view.** A new renderer must keep this (see caveats).
  - Kind words with 0x400 seen in setup: 0xc00 wheel, 0x1c03 car, 0x1c07 bomblets, 0x8c0a pickup, 0x8c0b wheel,
    0x1c0c homer / leader, 0x8c0c driller parts, 0x8e1a dropper, plus the track object records' own kind words. Kinds
    without 0x400 (0x816 snatch, 0x811 driller head, 0xa00f light carrier, 0x821 rock, 0x8013) are not drawn by this
    pass. Their drawing is elsewhere (snatch: 0x48fdf0, see Weapons) or they are invisible.
- **0x45a2a0 -> 0x459e80(obj): the object matrix.**
  - GTE rotation = view 0x6ce5d0 x object matrix (int16 3x3 at obj+0xc4, rows +0xc4/+0xca/+0xd0, 4096 = 1.0).
  - Translation = view * (obj+0x36, +0x3a, +0x3e, the high halfwords of the 16.16 position at +0x34) + view
    translation 0x6ce5e4.
  - The float copies 0x6ccd1c.. are refreshed. Model space -> view space.
- **Depth.** The model's bounding centre (model +0x28 / +0x2c int16) is projected. Drawn only if z > -1001.
- **0x45b400(obj, lights): GTE lighting.**
  - For each of the 3 lights: vector = light pos (16.16 >> 16) - object pos. Ignored if > 0x4000 in any axis.
    Attenuation = *0x4d4b9c[(dx² + dy² + dz²) >> light+0x10], 0x400 entries, a falloff table. It is normalised and
    scaled, so the light matrix rows are light direction * intensity.
  - The rows are rotated into object space (L * objM) -> 0x6ccc00 (LLM) and float 0x6ccdd0..
  - **Ambient (BK 0x6ccc14..1c).**
    - Objects with kind bit 0x800: the colour under the object is the track vertex colour index at cell obj+0x11c
      (section byte, column byte, row word), looked up in the current palette (0x486350, which returns the
      lightning / weather-tinted copy when active).
    - It is smoothed by 0x45b390: each channel moves at most 0x18 per draw towards it, state in obj+0x12c.
    - obj+0x11c == -1 gives 0x400 (mid grey).
- **0x45a430 (flat per face) / 0x45ab20 (model +0x48 vertex normals present: per-vertex lighting): draw.**
  - **LOD.** By centre z vs 6000 and 15000 * 0x6cd404 >> 8 (both doubled when options+0x31 == 1):

    | LOD | vertices | vertex count | per-view face packets |
    |---|---|---|---|
    | 0 | obj+0x24 (8 bytes: int16 x,y,z,pad) | obj+0x10d | obj+0x14 / +0x18 |
    | 1 (model + U16 model+0x1c) | — | obj+0x162 | obj+0x1c / +0x20 |
    | 2 (model + U16 +0x1e) | — | obj+0x163 | obj+0x14c / +0x150 |

  - All vertices are projected first (stack array of 0x18 records).
  - **Faces.** 8 bytes from the model: vertex numbers a, b, c, d in bytes 0-3 (1-based; d = 0 means triangle), then a
    dword normal packed 10:10:10 (x bits 20-29, y 10-19, z 0-9, 1.0 = 512). Bit 31 = semi-transparent, becoming
    0x2000000 in the code word.
  - **Back-face cull.** 0x4278f0 screen-space cross product, sign flipped in mirror mode 0x6cce94. Skipped for
    two-sided faces (obj+8 bit pattern shifted per face group). Faces with any behind-near vertex are kept and marked.
  - **Colour.** The normal goes through `*0x6cce3c` (NormalColor: 0x427400, or 3DNow! 0x427250) -> 0x6cccbc. The
    same colour is used on all vertices in 0x45a430.
  - **Packet.** 0x84 record: vertex blocks at +8/+0x28/+0x48/+0x68, colours +4/+0x24/+0x44/+0x64 (| 0xff000000),
    +0x22 = 0xff. Code 0x8c (quad, GT4) or 0x9c (triangle, GT3), | 0x2000000 when translucent (code +2).
  - **Bucket.** The farthest vertex z (max). Translucent faces go to the depth OT. Opaque faces go to the depth OT
    when 0x6cce84 == 0, else to `0x6cd518[(tpage & 0x3f) * 2]` (texture-page bins, per the code). See "Depth ordering" above.
    (z-buffer mode).
  - **Two face sources.**
    - **Kind bit 0x800 objects (dynamic bodies):** the object's own prebuilt face packets. UVs were copied from the
      model's 0x28-byte draw faces at creation (model_instance_init 0x460dc0).
    - **Others (shared track objects, kind word from the object record):** faces from the per-LOD lit-face list
      0x6bfb58 + U16 obj+0x1a0.. Per-face back colour comes from a run list obj+0x180.. (u16: low byte run length,
      high byte palette index -> BK), which is baked track lighting. Packets come from the shared buffer 0x6c1e18
      (stops at the limit 0x6c1e24).
    - The texture comes from the face's texture word: if its u16 +2 != 0xffff, it is an **animated texture**.
      0x6bfb44[low byte] (0x18 records) -> current frame u16 at +6 -> frames *0x6bfb4c (6 bytes: u16 texture
      record, u16, s16 duration; duration 0 = jump to frame index) -> texture record *0x6c1e14 + idx*0x10, with an
      orientation variant 1..7 (high byte) applied by 0x45a2f0, which permutes the 4 corner UVs.
- **Animated texture clock: 0x478df0.** Once per race tick (0x6c0a50 change) each 0x6bfb44 record counts down +0;
  at 0 it steps to the next frame (+6), copying the frame's two words to +0x10 / +0x12.
- **New renderer inputs per drawable object.**
  - Model pointer obj+0x10. Vertex list obj+0x24, which can be a crumble copy. Position obj+0x34 (16.16) and matrix
    obj+0xc4.
  - LOD models via model +0x1c / +0x1e. Faces and normals from the model.
  - UVs: from the object's packets (0x800) or the model's faces + animated-texture state 0x6bfb44.
  - Up to 3 lights from the cell's light word. Ambient from the ground cell (0x800) or baked runs obj+0x180.
  - Translucency bit per face.
  - Everything is model space; no screen-space dependence except back-face culling and LOD thresholds (which use
    the zoom 0x6cd404, not resolution).

### Crumbling buildings (`*0x6d04a4`, 4 entries of 0xa8; post_k.c crumble_update 0x405f70)

No separate draw. crumble_setup 0x412dd0:

- copies the object's vertex list into the entry's scratch (+0x98, from the 0x800-byte buffer at +0x8c)
- points obj+0x24 at the copy
- saves the matrix in entry+8..

crumble_update then sinks the vertices face group by face group, spawning debris, dust and bomblets (drawn as effects
above). The building is drawn by 0x459b20 / 0x45a430 like any object, from the moving copy. Inputs: obj+0x24 per
frame. Whether faces are hidden as they sink below the level is **not checked** (the update may clear face flags).


### Grid object pass: 0x47dd70(visible span list)

The span list comes from 0x47a030: track visibility, per view, double-buffered in 0x5d9454..0x5d9460. It holds
section, first row, count and LOD; it is covered by pipeline.md 4.3-4.4, as are 0x47a500 / 0x47c420 / 0x47c730 and the track
mesh 0x477940 -> 0x4779f0.

0x47dd70 first calls 0x47ee90. It takes the view matrix's first row, horizontal part (m00, 0, m02), normalises it
and stores it in 0x5d6de0..e4 (the camera right vector in world xz). Flag 0x5d65e0 = it was long enough.

For each visible row of each span it walks the row's grid cells:

- section model (*0x6bfac0)[sec] + 0x48 -> u16 per row
- -> cell indices in *0x6bfae4
- -> cell records *0x6bfaf8 + i*0xc
- dword +8 of the cell record: high 16 = first dynamic grid link, low 16 = static object list

Static object list: pushed to 0x5d7040 (u16 list), with that row's light word (from 0x5d7540, built by 0x47c420) in
0x5d6ac0. These are drawn by 0x459b20 below.

Dynamic links: walked with 0x4613e0(index) -> link (+0 kind dword, +4 u16 next). Dispatch on kind:

| kind | what | handler |
|---|---|---|
| 2 | track sprite / flare marker (sprite object 0x120, sprite_object_create 0x47dcc0) | 0x47e1b0 |
| 5 | shrapnel piece | 0x47e020 |
| 8, 9, 0x22 | effect sprites: smoke puff, plain effect, spray | queued to 0x5d6600 (max ~0x12c), count -> 0x4d5804, drawn by 0x47eaa0 at the end |
| 0xd | light glow sprite (object type 0xd; light sprites by number at 0x5d6e00) | 0x47e220 |
| 0xe | debris piece | 0x47e2a0 |
| 0x17, 0x1b, 0x1c, 0x1e | effect sprites: swirl/spark, rising glint, drag glint, falling spark | 0x47e340 |

Only objects in visible cells are drawn. Visibility is per track row.

#### World billboard: 0x47e5f0(pos16 *p, size, packet, flags_lo|rot_hi, mode, owner)

- **Projection.** The centre p (int16 x,y,z track units) with `*0x6ccd7c`. Rejected if z < 0.
- **Size.** Half-width w = ((0x6cce44 * size) >> 8) / (z + bias), where bias = -(flags & 0xffff) * 0x20. w = 0 is
  rejected.
  - 0x6cce44 is set by 0x423e10(zoom): fix_div8(zoom << 8, 0x6800) * screen_width(0x4acc38) / 320.0. Since the focal
    length is H = 0x6ccd00 = zoom * W / 320 (views.md), this reading gives 0x6cce44 = 256 * H / 0x68, i.e. sprite
    sizes are proportional to H, and world half-size = size / 0x68 units. Checked in the disassembly (0x423e67..0x423e91: fild 0x4acc38,
    fdiv *0x4a3350 (presumably 320.0), fmul, __ftol): the width factor is there, so views.md's "zoom / 104 (8.8)" omits it.
- **Fade.** The intensity is 0xfff - (z + bias) / 4. With an owner it is a curve using owner+0x17 (fade byte).
  0x6cd404 > 0x100 (zoomed) scales it up. Drawn only if > 0x31. So sprites fade out with distance.
- **Aspect.** Half-height h = w >> mode_lo (or << -mode_lo when negative). mode = 1 gives a sprite half as tall.
- **Rotation.** If flags has any high bits, the quad is rotated (0x47e980) by angle =
  ((sx - clipx) + (sy - clipy)) * (65536 / screen_width) >> 8, in 256 units per turn.
  - So **the rotation is derived from the sprite's screen position**: glows twist as they cross the screen.
  - It is normalised by width, but sy is also divided by width, so the look changes with aspect ratio.
  - Resolution-specific; reproduce as angle = 256 * (sx + sy) / width.
- **Rejection.** Trivially rejected against the viewport rect 0x6cd478.
- **Packet.** Code 0x98 | (mode >> 30) (0x9a when bit 31: semi-transparent). The colour comes from 0x47e550 with
  that intensity. vertex 0 flags = 0x40 (pipeline.md 2.3: force the screen-space clipper), all four z = the projected z. Bucket = (z + bias) >> 5 compressed, current
  view list.
- **New renderer.** A camera-facing quad at the world centre, world half-size ~ size / 256 (if 0x6cce44 / 256 is the
  focal length), aspect 1 : 2^-mode, distance fade, optional screen-position spin, colour x intensity.

#### Track flare sprite, kind 2: 0x47e1b0

- Far colour = the fog colour *0x4af4b8 (r, g, b bytes << 4), intensity 0xff0.
- 0x47e5f0(obj+0x10 pos, (obj+0x16) << 10, obj+0x18 packet, bias 10, mode 0x80000001 (0x9a, half height), owner obj
  (fade by obj+0x17)).
- **Texture.** Fixed at creation (corners +0x38/+0x54/+0x70/+0x8c from texture record). Positions are also in
  0x5d6400 (max 40, count 0x4d5800); 0x47ea00 uses those for a proximity screen tint, which is screen-space (see
  caveats).

#### Light glow sprite, kind 0xd: 0x47e220

- Colour obj+0x108 (rgb), intensity obj+0x10b.
- 0x47e5f0(obj+0x10, (obj+0x16) << 8, obj+0x18, 0x10006 (bias 6, rotate on), 0x80000000 (code 0x98), no owner).
- Turned on and off by triggers (gl_gc formats: action 2 writes +0x108, +0x10c on). The draw does not test +0x10c;
  presumably off = colour 0 (**unsure**).

#### Debris, kind 0xe: 0x47e2a0

- Colour 0x50 grey (far colour 0x500 x3, intensity 0x500).
- Position: high halfwords of obj+0x10/+0x14/+0x18 (16.16).
- 0x47e5f0(pos, (obj+0x2d) << 8, obj+0x30 packet, bias 2, 0x80000000).
- Texture record base+0x4d set at debris_init. The record has a second packet at +0xa8 that this draw does not use.

#### Shrapnel, kind 5: 0x47e020

- **Piece** (*0x6b983c, 0x200 x 0x80; post_i.c / post_k.c):
  - +8 life
  - +0x10 line packet (0x3c): +0x14 colour 0 with code (mode 0xae...), v0 record +0x18, +0x30 colour 1, v1 record +0x34
  - +0x4c/+0x50/+0x54 position 16.16
  - +0x58.. velocity
  - +0x64/+0x66/+0x68 int16 = position + one step of velocity
  - +0x6c glow flag
- **Draw.** A 2-point line from (pos >> 16) to the +0x64 point, with the guard band widened by 0x6cd418. Bucket from
  the first z >> 5 (needs > 1). Skipped if both points are outside the same edge.
- **Colours.** Per spawn kind (post_k.c 0x11..0x19 table), faded by pieces_update over the last 11 steps (depth-cue
  towards the far colour).
- pieces_init sets up a second line packet at +0x4c, but that overlaps the position fields; only +0x10 is used
  (**sic**).
- **New renderer.** World-space line (spark streak), semi-transparent. Drawn as a thin quad of width 0x6cd41c pixels (scales with
  screen width, pipeline.md 2.4).

#### Effect sprites: 0x47e340 (kinds 8, 9, 0x17, 0x1b, 0x1c, 0x1e, 0x22)

Pool `*0x6ccb5c`..`*0x6ccb84`, 0x100 bytes each (smoke_pool_init 0x42a710; config+0x68 smoke amount sets the count).
Moved by sprites_move 0x42d1e0 (post_j.c).

| offset | contents |
|---|---|
| +0 | kind |
| +4 | grid link |
| +0x10 | 0x78 quad packet: texture words written per draw at +0x30, +0x4c, +0x68, +0x84 |
| +0x88 / +0x8c / +0x90 | position 16.16 |
| +0x94 | in use |
| +0x95 | size |
| +0x96 | life |
| +0x98 | rgb |
| +0x9b | intensity / alpha |
| +0x9c..+0xa0 | int16 velocity |
| +0xa2 | u16 behaviour |
| +0xa4 | texture: u16 base record, u8 current frame +0xa6, u8 frame count +0xa7 |
| +0xa8 / +0xac | list links |

- **Texture animation inside the draw.** On odd race ticks (0x6c0a50 & 1) when not paused (0x4d1198 == 0), the frame
  +0xa6 advances, wrapping at +0xa7. The texture record ((+0xa4 & 0xffff) + frame) is copied into the packet.
  **sic**: this happens per draw, i.e. per view, so it advances twice per tick in split screen.
- **Kind 8 (smoke puff).** Uses the quad 0x47eaa0 precomputed in screen space (bucket at +0x6a, depth at +0x86).
  0x47e4c0 applies intensity 0xfff - (depth >> 2) (scaled by 0x6cd400) with code 0x9a. Skipped if the depth term is
  > 4000.
- **Behaviour 0x10 (still).** 0x47ef30 draws a streak quad. It projects the centre, the centre + 0x5d6de0 *
  0x8000 >> 21 (along the camera-right vector) and the centre lifted by (0x8000 >> shift) >> 9 in y, and builds a
  parallelogram from the two screen offsets. Requires z > 0xe0. Code 0x9a.
- **Others.** 0x47e5f0(pos >> 16, size << 8, packet, bias 3 (1 when behaviour == 0x8000), mode 0x80000000 (0x9a)
  | rotate flag when behaviour == 0x10..., no owner).
- **Kind 8/9/0x22 overdraw throttle in 0x47eaa0.**
  - For each queued sprite it projects the centre (0x426cd0/0x426dc0). Half-size = size * 0x6cce44 / z, needs
    z > 0x60, and is capped at 0x6cd40c * 5 >> 5 (kind 8) or 0x6cd40c * 15 >> 6 pixels. It builds the screen quad
    into the packet (z - 0x40) and sums the on-screen area in pixels (clipped to race+0xa82 x race+0xa80).
  - The budget is (0x6cd40c * 0x4b & 0x7ffff) * 0x20 * clamp(config+0x68 * 0.4 * 256, 0x80, 0x300) >> 8. If the area
    exceeds it, a fraction 0xff - budget * 256 / area of the sprites is skipped (every n-th).
  - **Resolution-dependent.** The area grows with width^2, the budget with width, so higher resolutions thin the
    smoke more. A new renderer should define its own budget, or use the 640x480 numbers in normalised units.
- **New renderer inputs.** Kind, position, size byte, rgb + intensity, texture record + frame, behaviour (0x10 =
  streak along camera-right). All are world space except the kind 8 size cap and the throttle.


## 5. Weather, skid marks and the TV screen

### Weather

#### Rain: 0x484a10(emitter index)

- **Selection.** Called by 0x484980(slot) when race+0x553 (weather) has bit 0 set. Emitter index: the slot's position
  among the car slots (0x6bfb80, stride 0x274), capped at `*0x5d6248 - 2`. The last index is used when 0x6cce40
  bit 1 is set, and `*0x5d6248 - 1` when bits 4 and 3 are both set (TV / replay cameras).
- **Emitter.** `E = *0x5d6360[i]` (0xca4 bytes, post_j.c / formats setup_v). The drops are at E+0x14, 8 bytes each:
  int16 x, y, z, alive. There are 400; only alive != 0 are drawn. **sic**: the loop starts at E+0x14+i*8 (offset
  by the emitter index) and still reads 400 entries.
- **Points.** Per drop: P0 = the drop (track units). P1 = P0 + (E+0xc98 - (cam+0x68 >> 16), E+0xc9a - (cam+0x6c >> 16),
  E+0xc9c - (cam+0x70 >> 16)). cam = E+0x10, the camera body the emitter is parked on, so the streak is the drop's
  motion relative to the camera (a motion-blur streak).
- **Clip.** The clip rect is widened by 0x6cd418 (1.5 x FOV scale) on every side during the draw (guard band). A
  drop is drawn if both points are not outside the same edge and z >> 5 > 0.
- **Bucket.** z of P0 >> 5 through the usual compression, then -0x30 (drawn nearer than its depth), minimum 1, into
  the current view's OT.
- **Packet.** `*0x5d6340`, 0xa0 records of 0x3c: +4 colour 0x2f0000 code 0xae, P0 record at +8, +0x20 colour
  0x3f3f3f, P1 record at +0x24. It stops after 0xa0 drawn.
- **New renderer.** A world-space line list: P0 and P1 per live drop, colours 0x2f0000 (head?) -> 0x3f3f3f,
  semi-transparent. The back end draws lines as thin quads of width 0x6cd41c (= screen width * k / 640, at least 0.6 px;
  pipeline.md 2.4), so the width already scales with horizontal resolution; widths are in pixels, not world units.

#### Snow: 0x484c20(emitter index)

- Uses the same emitter and drops, with the same index **sic**.
- Each drop is projected with `*0x6ccd7c` (no clip flags) and skipped if behind (z < 0) or flag 0x10.
- **Half-size in pixels.** s = min(12, (0xc00 << *0x6cce48) / z).
  - 0x6cce48 is a shift picked from a table by 0x6cce44 >> 8, in 0x423e10. 0x6cce44 = zoom-based scale x screen
    width / 320.
  - So the size is in pixels with a **hard 12-pixel cap**: resolution-specific.
- **Quad.** Screen-aligned, centre +- s, z = projected z for all 4 corners. Trivially rejected against the viewport
  rect 0x6cd478 (h << 16 | w) relative to the clip origin 0x6ccd64/0x6ccd6c.
- **Bucket.** As rain (-0x30).
- **Packet.** `*0x5d6244`, max 0xaf of 0x78: colour 0x606060 code 0x9a. Texture words from texture record
  base+0x66 (setup_v; base = U16 *0x6c1eb0+0x2e).
- **New renderer.** A world-space point-sprite per live drop. Size ~ 0xc00 << shift / z px, capped. The intended
  look is a fixed small flake, roughly constant-size on screen for near flakes.

#### Screen flakes: 0x485d00 (called by 0x484980 unless 0x6cce40 bit 1)

- Only in single-view mode (race+0x552 == 0). 32 records (rain / snow) or 8 (no weather or snow?: `local_14 = 8`
  when race+0x553 is 0 or 2, so 32 only for rain).
- **Records.** `*0x4d5898`, 0xa0 each:
  - +0 live
  - +0x7c / +0x80 centre in **screen pixels**. 0x485140 spawns them inside race+0xa82 x race+0xa80, the screen
    width / height.
  - +0x84 size, +0x98 grey level, +0x9c angle (sin table 0x4b24f8 / cos 0x4b2578)
  - +0x8c.. +0x94 motion (0x4857f0, not decompiled cleanly here; post_j.c says it is the screen-space flake mover,
    x87)
- **Scale.** f = 0x6cd40c / 512 = width / 640.
- **Shape by weather:**
  - weather 0: a fixed corner shape (0x4b2c68..84 = (-10,10,-10,10) / (-30,-30,30,30)) rotated by the angle;
    "dry-weather flakes", probably leaves or dust.
  - weather 1: axis-aligned square of half-size +0x84 * f (rain drops on the lens).
  - weather 2: square rotated by angle - 0x20.
- **Packet.** z fixed at 3.0, colour grey x3, code 0x9a. Pushed on the fixed OT entry 0x6ce584. That is 4 bytes past
  the second view's buckets: the 2-entry overlay OT 0x6ce580, flushed after both view OTs with fog off
  (pipeline.md 1.3).
- Texture words: the 0x4d5898 records' +0x24.. (setup_v: records 0xa0 / 0xa1, alternating).
- **Entirely 2D overlay in screen pixels, 640-scaled. Must be redone as a 2D effect.**

#### Related weather effects (not drawn here)

- **Lightning.** 0x4d58f4 flash makes 0x486350 return a white palette (0xff,0xff,0xf0) for object ambient colour, and
  0x47c730 brightens track lighting.
- **Thunder.** Clap screen flash 0x6ccb60 (0x484fd0), drawn by 0x42be20 (see Full-screen flash).

### Skid marks: 0x46f230

- **Gate.** 0x6ccd84 bit 1 and options (*0x4b2198)+0x2c.
- **Segments.** `*0x6002d4`, count 0x4b1c68 (150 or 300), 0x124 bytes (gl_gc formats):

  | offset | contents |
  |---|---|
  | +0 | old point int16 x,y,z |
  | +6 | high bits of the packed side for the old point |
  | +8 | dword: bit 15 = segment live; packed side vector (5-bit signed fields: x = bits 0-4, y = bits 5-9, z = bits 10-14, each *4) |
  | +0xc | new point int16 x,y,z |
  | +0x14 | its packed side |
  | +0x18 | u16 start flag (0 while still marking) |
  | +0x1a | u16 end flag |
  | +0x1c | packet for view 1 (0x6cdd10) |
  | +0xa0 | packet for view 0 (0x6cd4a0) (local_c = 0x84 when the current view is 0x6cd4a0) |

- **Corners.** P, P + side for the old point, then the same for the new point: 4 world points (track units). The
  segment is skipped if any projected z is outside [-250, 5000] (0x4a33e0 / 0x4a33dc).
- **Packet.** 0x84 (4 x 0x20 vertices).
  - Colour words: `*0x4d44b8` (opaque end) or `*0x4d44bc`, chosen per pair by the flags. The alpha byte is set to
    0xff or 0 at +0x22 / +0x27 / +0x47 / +0x67 so the strip fades in at its start and out at its end.
  - Code 0x8c family (setup_t: 0x8c skid marks; texture record base+0x8f).
- **Bucket.** z of the second vertex (capped 1048576), b - 2 (b < 0xd) or b - 3. Decal bias: slightly nearer.
- **New renderer.** A world-space decal quad strip on the track, with per-end alpha. Needs depth bias or polygon
  offset. Gl_gc.c (skid update 0x46ed80 tree) maintains the segments; nothing is screen-space.
- **"Blast marks" on cars.** blast_mark 0x46d760 / damage record car+0x6ac are car-side (the shield / ripple record
  see Shield bubble under Cars). Blast scorching of the track is 0x42c760 scorch_track, which darkens track
  vertex colour indices (track mesh data, drawn by the track code).


### TV screen: 0x41b2f0 (last call in draw_view), with 0x41b000 screen_update (pre-physics)

- Off in race modes race+0x551 == 3 or 8, and while 0x6ce704 (state) == 0.
- **Draw.** For the 4 polygon indices 0x6ce710[0..3] of the screen body's per-view face packets (*0x6ce708 + 0x14
  view 0 / +0x18 view 1, 0x84 each), it overwrites the 4 vertex colours (+4, +0x24, +0x44, +0x64) with 0x6ce734
  (rgb), keeping the code byte.
- This runs after 0x459b20 queued them, so it overrides the lighting.
- **UVs.** screen_update rewrites the same packets' UV bytes (+0x20, +0x40, +0x60, +0x80) per state: a picture strip
  stepping through rows, flicker colour ramps (states 4/5). The screen record is at 0x6ce700 (setup_v formats).
- **New renderer.** Screen body + those 4 faces with UVs and colour override from 0x6ce704..0x6ce734.


## 6. Effects and weapons

Weapon bodies (homing missiles, drillers, leaders, pickups, bomblets, dropper) are ordinary bodies drawn by 0x459b20 (section 4); this section covers their extra effects and the pooled effects.

### Wall-impact ripple rings (0x433f00)

Spawner 0x433d90(point 0x6c80f0 (3 x i32 16.16), vertex 0x6c80e0 (3 x s16), normal 0x6c8010 (3 x i32)) is called
from the draw when the wall-contact frame 0x6c8004 equals the race tick 0x6c0a50 (wall_touched 0x416cd0, trackhit.c),
at most every 5 ticks (0x4be380 = tick of last spawn), into ring slot 0x6c80fc (0..7, round robin).

Ring record, 0x6c8020 + i*0x18:

| off | type | meaning |
|---|---|---|
| +0 | 3 x s16 | centre (point >> 8, i.e. 1/256 units? see note) |
| +6 | u16 | age, 0..0x1f; incremented by the DRAW; at 0x20 the ring is switched off |
| +8 | 3 x s16 | unit direction point -> vertex (16.16 normalised, >> 2: 0x4000 = 1) |
| +0xe | u16 | active |
| +0x10 | 3 x s16 | cross(direction, normal) >> 2 |

Note: the centre is stored as `point >> 8` but used as whole units when fed to the GTE vector; I did not resolve
whether 0x6c80f0 is 24.8 or 16.16 (check trackhit.c wall_touched). Geometry: 16 points on a circle in the plane
(dir, cross) from the cos/sin table 0x4acb94 (8 pairs, 16.16, 0..157.5 degrees, used with +/- for the full
circle), inner radius age*4, outer radius age*4 + 16 world units. 16 quads (inner i, inner i+1, outer i,
outer i+1), each 0x68 bytes (4 copied 0x18-byte projected records from +8), code 0xaa, no texture.
Colour bytes: +4 = 0x40 - 4*|age-15|, +6 = 0x80 - 4*age (+5 stays 0 from init: a blue-violet fading ring).
Bucket = (z of one vertex >> 5) + 0x10, compressed. Pool *0x4be378: 8 rings x 16 x 0x68 per view, second view
at +0x3400. State mutation in draw: age++ per draw call (so twice per tick in split screen; sic).

New renderer inputs: centre, dir, cross, age; build a 16-segment translucent annulus.

### Ground smoke rings (0x42b9a0)

Records at *0x6ccb54, 5 x 0x1e2c (started by smoke_cloud_start 0x42b3b0):

| off | meaning |
|---|---|
| +0, +4, +8 | i32 16.16 centre x, y, z |
| +0xc | remaining life; DECREMENTED BY THE DRAW (per view, sic); 0 = free |
| +0x10 | current animation frame, advanced by the draw, wraps at +0x1c |
| +0x14 | colour rgb (code 0x9a ORed in) |
| +0x18 | first texture record (relative to the track base *(u16*)(*0x6c1eb0+0x2e)); 0x86 and 0x91 have their UVs rotated (corner bytes swapped) |
| +0x1c | frame count |
| +0x20 | fade-out length: fade = 0xff/len * life while life < len, else 0xff |
| +0x24 | total life |
| +0x28 | radius step per tick (16.16 sine scale) |
| +0x2c / +0xf2c | 32 packets (0x78) for view 0 / view 1 |

Geometry: flat annulus in the XZ plane at height y: 32 segments (angle step 0x3243 in the 0x480bc0/sin_table units,
2*pi = 0x6487f), inner radius (total - life + 2) * step, outer (total - life + 5) * step (radius grows linearly).
Segment quads skipped when the sum of the 4 vertex z's is not above 0x4a3318 (near cull) or bucket > 0x1fb. Bucket
= (z >> 7) - 1 (z of one vertex, compressed). Texture = record base + frame. Fade into RGB or alpha per 0x6ccd84 bit 0.

### Shock waves (0x42c950)

Records *0x6cc800, 3 x 0xf14 (shockwave_start 0x42c360 sets +0..+8 = 16.16 position, +0xc = 0x18, +0x10 = 0):
+0xc life (decremented by the draw), +0x10 frame 0..4 (advanced by the draw; texture records 0x3a + frame),
+0x14 / +0x794 16 packets per view. Geometry: a vertical band (cylinder) of 16 segments: radius = sine table
0x4b24f8[(24-life)*64/20, max 64] >> 7 (eases out), bottom at y - (life^2/4 + 0x20), top 0x14 units above
(remember y is down: the band rises and slows). Brightness grey 0x15*life below 12 else 0xff, colour word =
0x9a<<24 | grey*0x10101. Same z-sum cull and bucket formula as smoke.

### Blast flash ring (0x42c420)

State: blast_point_set 0x42c3c0 sets 0x4be180/2/4 = s16 x,y,z and 0x4be1a8 = 1; flash_timer_step 0x42c400
(sim step) counts it to 0x14 then 0. Packets *0x4be1a0 (view 0) / *0x4be1a4 (view 1), 32 used x 0x78,
initialised with colour 0x80808080 and blend bits 2 at +0x3e (flares_init 0x42c200). Draw: project the centre;
d = (z >> 5) - 2 compressed (the OT bucket value, also used as the size divisor); inner radius = table
0x4be156[n] / d, outer = 0x4be158[min(n+10, 0x13)] / d (table of sqrt steps, 0x4be158..0x4be17e); 32 points from
sin table 0x4b24f8 step 8, offsets added to the projected sx/sy: THE RING IS BUILT IN SCREEN PIXELS, with
the OT bucket as a coarse 1/z. Alpha 0x60, fading over the last 10 counts. Textures cycle records 0x47..0x4c.
All vertices get the centre's z. Resolution note: the radius is in screen pixels at 640-wide-ish scale and is
not multiplied by any screen scale; a new renderer should derive it from world size / depth.

### Sun and lens flares (0x442060)

Queue: 0x441d80(pos 16.16, kind, colour) appends to 0x4beb90 + n*0x4c0 (max 10, count 0x4beb88, emptied by the
draw each view: entries queued once per frame are drawn in the first view only; check for split screen). Entry:
+0 kind, +4 s16 x,y,z, +0xc colour, +0x10 packets (0x78 each, up to 10). Not drawn when race +0x57c >= 7.
Callers: the sky 0x4049b0 (kind 2, the sun, colour 0x6fff?) and projectile heads 0x483240 (kind 4). Kinds 0/1
(single glow, 0x20/0x40 size) have no caller found.

- size unit: local_10 = 0x6cd40c * 0x4a3338 * 0x4a332c >> 8, i.e. proportional to screen width.
- kind 0/1: project; if visible and z <= 0x4a33ac draw one quad, texture 0x3f, alpha 0x80.
- kind 2 (sun): only if 0x6ccd84 bit 2 clear. Intensity from the weather level ((0x4b2c60 - 0x4b2c58) * 0x10000
  / (0x100 - 0x4b2c58)) * 0x4af570; glow quad (texture 0x4af548, size 0x4af520) in the FAR bucket (drawn behind
  the scene); if fully on screen, 9 more quads along the line from the sun's screen position through the screen
  centre (0x6ccd5c/0x6ccd60), spaced by 1/0x4a33a4 of that line, textures 0x4af54c.., sizes 0x4af524.., dimmed by
  distance of the sun from the centre relative to the screen height (race +0xa82), front bucket. A classic
  screen-space lens flare.
- kind 4 (projectile head): flare relative to the camera body (param = camera object: +0x34 position, +0xc4
  matrix): direction from camera to head normalised (0x407e90), a look matrix (0x407220), rotated into the view
  (0x426e90, 0x426e10) to get a screen offset, 5 quads along it (textures 0x4af4f8.., sizes 0x4af4e4..,
  brightness 0x4af50c.. falling with the offset), depth-bucketed at the head.
- Placement 0x441de0(sx, sy, z, alpha, size, packet, mode): axis-aligned square of half-size 2*size pixels;
  mode 1 far bucket, mode 2 depth bucket ((z-10) >> 5), otherwise front bucket 1. Texture/colour by 0x441fa0
  (texture record + track base, code 0x9a, colour * alpha or alpha byte).

All flare geometry is screen space and sized from screen width: inherently 2D, aspect-dependent. On ultrawide
the size follows width, so flares get relatively bigger vertically; base it on height or the projection scale.

### Projectile trails and head glow (0x483210 -> 0x483240)

The 24 weapon bodies at 0x6b9ce0 (weapon_bodies_create 0x4583e0: 4 homing missiles, 18 driller parts at
0x6b9cf0.., 2 leaders at 0x6b9d38); drawn when +0x188 (state) != 0. Fields used:

| off | meaning |
|---|---|
| +0x180 | sub-kind index into colour tables 0x4b2b3c / 0x4b2b10 / 0x4b2ae4 (state 1) |
| +0x184 | kind: 0 leader, 1 homing missile, 2 driller part |
| +0x188 | state (bits 0x1c3 = head visible; 1 = flying straight?; 3 bits: fading) |
| +0x1b4 | colour chosen this frame (written by the draw) |
| +0x1b8 + i*0x20 | trail ring of 32 entries: 4 points of s16 x,y,z,pad (two pairs: the trail's cross-section) |
| +0x23dc | s8 head offset; +0x23de s16 valid entries (newest = (+0x23dc - 1 + +0x23de) & 31) |
| +0x5ba + i | per-entry brightness byte |
| +0x5dc + i*0xf0 | 2 packets (0x78, code 0x9a) per trail entry; textures 0x47..0x4c by entry (0x58 for drillers), last entry 0x3f tinted (0xfe,100,200) |
| +0x22ec | head billboard packet (drillers) |

Head: centre of the newest entry's first pair. Colours: state 1 from the tables above, else kind 0 0x70f040,
kind 2 0xf07040, kind 1 0x60a0f0. Kind 2 (driller): billboard 0x47e5f0(size 0x3000); kinds 0/1: queued as a
kind-4 flare (0x441d80) with colour 0xc0ffc0 (kind 0) or 0x0000c0ff (others; the sum overflows, sic).
Trail: for each older entry, 4 projected points; the oldest point is pulled 30 units in (interpolated) so the
tail is closed; 2 quads per entry between consecutive entries, colour 0xf4dcc4 scaled by brightness =
+0x5ba byte + entry*6 + ..., code byte from packet +7, bucket min z >> 5. Only the first quad of each pair is
drawn when config smoke amount +0x68 < 0x4a3344 and kind == 0 (less smoke).
STATE MUTATION IN DRAW (not when paused 0x4d1198): kind 1 trail points get random jitter from RNG 0x4b11d8,
kind 2 random shake, others drift up 2 units per draw. This consumes the effect RNG 0x4b11d8 per view and
changes the trail data, so the game's look (and RNG sequence) depends on draw calls; a port must move this
into the step or keep calling it.

### Pickup sparkles and collect flash (0x467ec0)

Pickups: *0x6b8ce0 (count 0x6b8ce4), pickup body +0x182 u16 state: bit 0x80 visible; low byte 1 = sitting
(sparkles), 3 = collected (flash + sparkles), 2/4 nothing. Sparkles 0x467ff0: three billboards orbiting the
body +0x34 (whole units), one in each of the XZ, XY, YZ planes, radius ~0x7f units (high byte of sin/cos tables
0x4b24f8/0x4b2578), angle (tick*16, *20, *24)/2 + phase +0x552; colour blue (0x3f,0x3f,0xff), size 0x3000,
mode 3 (quarter height?), packets +0x280 + k*0xf0 (view 0) / +0x2f8 + k*0xf0 (view 1). Flash 0x467f30:
billboard 10 units above, colour 0x7f0f7f, size grows with timer +0x180 (t/2 below 0x12, then 0x1a - t),
packet +0x190 / +0x208. The pickup model itself (and its texture records 0x3f/0x40, beams) is drawn by 0x459b20 (Bodies and track objects) /
setup_u.

### Lightning beams (0x4345c0)

4 beams at 0x6c8100 + i*0x18 (beam_add 0x4344e0, beam_move 0x434540, beam_remove 0x4345a0; used by the pickup
dropper in post_h around the drop, and probably by the freezer: 0x6c3e96 holds two beam indices):

| off | meaning |
|---|---|
| +0 | s16 life (decremented by the DRAW) |
| +2 | s16 phase (incremented by the draw, or re-randomised) |
| +4 | u16 random shape bits (frequency per axis: bits 0-1, 2-3, 4-5) |
| +6 | u16 brightness (0xff, -0x40 per draw) |
| +8 / +0x10 | s16 x,y,z from / to |

Per-beam jitter table *0x4be37c + i*0x110 (17 entries x 0x10: two s16 offset triples) refreshed by the draw from
the sine table and RNG 0x4b11d8. 17 points interpolated from -> to, each displaced by random noise and the
jitter; quads between successive points (point and point + offset), 16 per beam, packets *0x4be374 + i*0xf00
(+0x78 for view 1), textures 0x14/0x15 alternating (UV words swapped at init), colour grey = brightness.
STATE MUTATION IN DRAW: life/phase/brightness change, RNG 0x4b11d8 consumed, and at random points it SPAWNS
PARTICLES (0x462690 particle_spawn with random velocity). Needs moving to the step for a renderer port.

### Snatch / wormhole (0x48fdf0)

Body *0x5d1fc0 (snatch_create 0x48d880, logic post_g.c), drawn while +0x188 != 0:
+0x344 72 world points (8 bytes: s16 x,y,z,pad; 6 rings of 12), +0x6a4 centre (s16 x3), +0x340 s8 texture frame
(0..3, texture records 0x5c..0x5f), +0x2a1c 6 ring colours (u32 rgb + alpha byte), +0x180 timer, +0x2a37 grab
level byte. Projection scratch 0x4d9af8 (72 x 0x18) and 0x4da1e8 (centre).
Tube: 5 bands x 12 quads between consecutive rings, 0x84-byte gouraud textured quads (code 0x8e, colour per ring
with alpha byte), at +0x6ac (60 x 0x84), cull when max z <= 0x4a34b8, bucket (sum of 3 z / 0x4a3344) >> 5.
Funnel: when config +0x28 (animated textures) and page *0x4b24a0 exist and 0x6ccd84 bit 2 clear, 12 triangles
from ring 0 to the centre (0x60 bytes, code 0x96, at +0x259c), grey alpha = 0x30 growing with +0x180 up to 0x28
steps, or +0x2a37*4 + 0x30; texture = the snatch page (setup_u: UV corners 0x8000/0x807f/0xff00/0xff7f, a
128x128 plasma image at v 0x80..0xff) which sim_step regenerates every step on the CPU (0x486540 -> 0x486600,
tables from 0x4867a0, palette 0x486450). New renderer: world-space mesh; the plasma texture is a procedural
128x128 animation best redone as a shader.

## 7. Screen-space effects

### Full-screen flash (0x42c0a0) and its sources

One accumulator per frame: 0x42bfc0(rgb) adds (saturating) into 0x4be134 and sets 0x4be138;
0x42c040(priority, page/blend, uv0, uv1, uv2, uv3) selects a texture when priority <= 0x4ac72e (lower wins).
0x42c0a0 (not when 0x6ccd84 bit 2): if 0x4be138 and 0x4be134, fill packet 0x6cc960 (view 0) or 0x6cc9d8
(view 1) (0x6cca60/0x6ccad8 when 0x479ae0 returns non-zero; 0x479ae0 is a 3-byte stub, so never):
UVs 0x4be122/0x4be120/0x4be11e/0x4be11c, colour 0x4be134, +0x3e = 0x4ac72c & 0x3f, vertices = the VIEW CLIP
RECT (0x6ccd64, 0x6ccd68, 0x6ccd6c, 0x6ccd70), z 2.01 (0x4000a3d7), code 0x9a, front bucket; then resets
0x4be134/0x4be138 and the priority 99 / page 0xffff. Sources (0x42be20, 0x47ea00 and 0x479b30 run before 0x42c0a0 in draw_view; 0x47db20 runs near the end of draw_view, after it, so the time-warp tint is drawn by the next 0x42c0a0 call, i.e. the next view or frame):

- 0x42be20 blast flash: 0x6ccb60 on, 0x6ccb64 countdown (decremented by the draw), 0x6ccb68 strength,
  0x6ccb6c/70/74 centre 16.16 (blast_flash_set 0x42b330); brightness = countdown*0x48000 / (dist^2 *
  (12 - strength)) - 0x6c, clamped, colour (v, v, 3v/8). With weather (race +0x553) and 0x6ccb62 == 1 it
  computes a value into a dead local and adds nothing (sic: lightning flash disabled?).
- 0x479b30 submerged: camera car (slot +0) surface_kind 0x40d6f0: 1 (below 0x3d0: water) or 2 (lava?):
  fade byte slot +0x256 (+8 / -8 per draw, sic per view), kind byte +0x257; tint (0x40,0x40,0xa0)*f for
  kind 1 plus texture (0x47db80: page *0x4b24a0, UV u 0x81..0xff v 0x01..0x7f = the ICE.BMP half, priority
  3, config +0x32), (0xa0,0x40,0x40)*f for kind 2. Skipped when 0x6cce40 & 0x10.
- 0x47ea00 light glare: nearest type-2 sprite (0x5d6400, 3 x i32, count 0x4d5800) to the camera within 4 units
  (>>0x12 grid): ambient 0x6ccd90 * 2 * (1 - d/0x200).
- 0x47db20 time warp: config +0x29, 0x4d57f0 on, page *0x4b24a0 valid: add 0x4d57f4 (grey = level*3/8), texture
  UV u 0x01..0x7f v 0x01..0x7f (the tunnel half), priority 1.

Time warp texture: 0x47d980 (race setup) loads one of TW1..TW8.BMP (random) as a 128x128 8-bit image and builds a
radius/angle lookup 0x4d57ec (0x47d400); time_warp_update 0x479740 (post_h; states 2 in, 1 on 100 steps, 4 out)
calls 0x47d5a0(level) each step, which re-renders the tunnel into the page with scrolling offsets 0x4d57f8/
0x4d57f9 (0x47d6d0), copies the palette 0x4d4ba0 and sets 0x4d57f4. 0x47d7f0 loads ICE.BMP into the other half.
0x5d9b2c (warp scale) is read by 0x479940 for the warp sound pitch only; the warping car (0x5d9b24) sees nothing.
Also probably used by the camera/FOV (not checked here).

New renderer: the flash is a viewport-sized translucent quad (screen space by nature; draw it as a full-viewport
pass at any resolution). The time-warp tunnel is a CPU-written 128x128 texture stretched over the viewport:
redo as a shader (polar lookup of a TWn image with scrolling) or upload the same texture; it only depends on
the viewport, not the resolution.

### Respawn / snatched screen sparkle (0x490430)

From the camera record (param = slot +0x28 camera; +4 its car): if the car is a body of kind 3 and car +0x689
(countdown from 0x1f set by car_respawn_sparks 0x468f80 and the snatch drop in post_g; decremented by 0x4906a0)
is non-zero and 0x6cce40 & 0x10 is clear: header packet at *(car +0x68c) (= 0x5cf860 + car*0x690) gets colour
(5n, 7n, 7n) and goes in the front bucket; then 13 squares (texture record 0x40) at random screen positions
(RNG 0x4b2d1c) on a grid of 32-pixel cells of the screen size (race +0xa80 width, +0xa82 height), size random*4,
grey random*n/16, z 3.0, front bucket. Pure 2D overlay in pixels: at higher resolution the squares stay 32-px
scale and cover less; scale by the viewport. I did not confirm how the header quad gets its corners (glow_prim_init
at setup; possibly the flash-packet layout without positions).


## 8. Things with no draw function of their own

### Freezer ray (no draw function)

freezer_update 0x43aba0 / freeze_row_toggle 0x43b190 / freeze_vertex_colour 0x43b2a0 (post_h): rows of track
vertices ahead of the firing car have their colour word (vertex +6, bits 6-15 palette index) replaced by an
icy copy allocated downwards from palette entry 0x3cf (map *0x6c3080, 0x400 u16; free counter 0x6c3e94;
palette = track +0x30 (*0x6c1e30); colour + (0x50, 0x50, 200) clamped); originals saved in 0x88-byte records
(count 0x6c3e90). The renderer sees it only as changed track vertex colours and palette entries: the track
renderer must re-read vertex colour words and the palette every frame (or on 0x6c3e90 change).

### Dynamic lights (no draw function)

Light records 0x5d8000 (0x40 bytes, formats.md setup_x / gl_gc): +0 i32 16.16 x,y,z, +0xc colour, +0x10 radius,
+0x12 countdown, +0x24 kind, +0x30 carrier object. They are consumed as vertex lighting: track cells through the
light grid 0x5d7de0 in the track lighting 0x47c420/0x47c730 (0x47a030 tree), and bodies through a packed key of up
to three light indices (10 bits each) decoded by 0x459cb0 in the object drawer (see Bodies and track objects). The
visible glow of a light is a separate light sprite (kind 0xd above) or flare.

### Water and ripples (no own draw)

All of these mutate the track's section vertex grids (*0x6bfac0[i] +0x20: 8-byte vertices, int16 x,y,z + u16 colour
word, bits 6-15 palette index). The track mesh drawer 0x477940 -> 0x4779f0 (pipeline.md 4.4) draws them:

- **Water meshes.** Records 0x5d6378 (0x1c, count 0x5d637c): water_ripples_update 0x482df0 moves heights (y at
  vertex +2) and edge vertices along a sine wave, only near a camera.
- **Rippling vertices.** 0x6c1eb8 (0x18, count 0x6c1eb4): ripple_update 0x43e270 moves the height and shade bits of
  the colour word. The palette ramp 0x3e0..0x3ff is rewritten at setup.
- **Scorch.** 0x42c760 darkens colour indices near blasts.

For a new renderer: re-upload or patch those vertices per frame (dynamic vertex buffer for water sections). The "water
reflections" option (fe_n / fe_o) and the mirror flag 0x6cce94 (view x scale 0xe000 in 0x423d30) are covered by
views.md; the mirrored cars are in section 3 (Car reflections).

### Track rows' dynamic light colours (inside 0x47a030; pipeline.md 4.3-4.4)

0x47c420 builds per visible row the lights touching it. It uses the light grid 0x5d7de0[section] (rows of 8 keys:
light index bits 22-31, column range) into 0x5d7540, up to 3 per row (10 bits each), and registers short-lived ones
in 0x5d71e0. 0x47c730(view) lights the track vertices with them (GTE 0x6ccc14.., lightning 0x4d58f4). Dynamic
lights (gl_gc: car headlights, explosions) therefore show up only as track and object vertex colours. There is no
light primitive except the glow sprites.

### Sky (pipeline.md 4.5)

- **Clouds: 0x404f50.** Skipped when 0x6ccd84 bit 2 is set. *0x6d0a00 cloud quads, vertices *0x6d0a38 (4 x int16
  x,y,z each, world-ish), packets 0x4b5fc0 (0x78 each), pushed onto a fixed back OT entry (0x6cd6f8 / 0x6cdd08).
- **Backdrop: 0x4049b0.**
- draw_view sets the far plane to 65536.0 (0x428b90) around both and restores it.

## 9. Resolution and screen-space caveats

Everything not listed here has world-space inputs and can be drawn at any resolution from the state named above.

**Inherently screen-space (must be redone as 2D / full-viewport passes):**

- Full-screen flash / tint 0x42c0a0 and its sources (blast flash 0x42be20, submerged water/lava tint 0x479b30, light
  glare 0x47ea00, time warp 0x47db20): one quad over the viewport clip rect. Resolution-independent if drawn as a
  viewport pass.
- Time warp tunnel: a 128x128 8-bit texture re-rendered on the CPU every step (0x47d5a0 from TW1..TW8.BMP, polar
  lookup 0x4d57ec, scroll 0x4d57f8/9, palette 0x4d4ba0) and stretched over the viewport. Redo as a shader or upload the
  same texture.
- Screen flakes / lens drops 0x485d00: pixel positions spawned inside race +0xa82 x +0xa80, sizes scaled by W / 640,
  overlay OT 0x6ce584.
- Respawn / snatched sparkle 0x490430: random squares on a 32-pixel grid of the screen size.
- Sun lens flare chain and projectile-head flares 0x442060: positioned along the line from the projected point through
  the screen centre, sized by 0x6cd40c (W / 320).
- 3D-anchored HUD 0x438760 and the countdown 0x46a9e0: views.md.

**Billboards whose size or look is defined in pixels:**

- Shared billboard 0x47e5f0 (all sprites, particles, debris, pickup sparkles, driller heads): half-size =
  size * 0x6cce44 / z. Since 0x6cce44 = 256 * H / 0x68, w = H * size / (0x68 * z), a true perspective size with
  world half-size = size / 0x68 units (size is the caller's argument, e.g. size byte << 8), so it is
  resolution-correct as long as H is; it follows W, so
  on ultrawide it should follow the new focal length (views.md recommends H from the height).
- Its optional spin angle comes from the sprite's screen position normalised by the screen width only
  (angle = 256 * ((sx - clipx) + (sy - clipy)) / W): reproduce with normalised coordinates; the look changes with
  aspect.
- Brightness of billboards falls with camera z (0xfff - z/4), not world distance; fine at any resolution.
- Particle size caps (0x6cd40c * 5 >> 5 for smoke, * 15 >> 6 otherwise) and the smoke overdraw limiter in 0x47eaa0
  (screen area in pixels against a budget linear in W): at higher resolution area grows with W^2 so more smoke is
  culled. Use a resolution-independent budget (e.g. the 640x480 numbers in normalised units).
- Snow: half-size min(12, (0xc00 << 0x6cce48) / z) pixels: a hard 12-pixel cap.
- Brake/reverse halos 0x422f50: size from 0x6cce44, clamped to 0x80 pixels, screen-aligned square.
- Blast flash ring 0x42c420: radii from a pixel table divided by the OT bucket (a coarse 1/z), not scaled by any
  screen factor: smaller on screen at higher resolutions. Derive from a world size instead.
- Lines (rain, shrapnel) are thin quads 0x6cd41c pixels wide (W / 640 based, at least 0.6): widths are screen-sized.

**Other screen artefacts:**

- Shadow subdivision 0x46caa0 (z-buffer mode) interpolates in screen space only to hide affine warping: not needed
  with perspective-correct texturing.
- Trivial rejects use the packed 16-bit viewport size 0x6cd478.
- All packet screen coordinates are already in viewport pixels for the original mode (0x4acc38 x 0x4acc3c); never
  reuse them, re-project from the camera-space x, y, z in the vertex records or (better) from the world inputs.

## 10. Draw-time side effects on game state

Several draw functions change game state, some once per view (so twice per frame in split screen) and some consume
the effect RNG 0x4b11d8. A replacement renderer must either keep calling them (e.g. run the original function but
discard its packets) or move the logic into the simulation step and accept a timing change:

- 0x459b20 queues every drawn object with trigger actions (+0x16a) into the trigger queue 0x6b9ba0 (count 0x6b9cb8,
  max 70): **triggers depend on visibility**.
- 0x47bd20 advances the car glow frame (car glow +0xa); 0x478df0 the animated textures (guarded by the race tick).
- 0x47e340 advances particle texture frames on odd ticks, per view.
- 0x45b390 eases body ambient colour (body +0x12c), per view; 0x422140 eases the car lift +0x14c8.
- 0x433f00 ages wall ripples and spawns new ones from the wall-contact state; 0x42b9a0 / 0x42c950 decrement smoke-ring
  and shock-wave life and advance their frames; 0x42be20 counts down the blast flash 0x6ccb64; 0x479b30 steps the
  submerged fade (slot +0x256).
- 0x4345c0 changes beam life / phase / brightness, refreshes the jitter table from the RNG and spawns particles
  (0x462690).
- 0x483240 jitters projectile trail points (RNG) or drifts them up, per view, unless paused.
- 0x490430 uses its own RNG 0x4b2d1c; 0x442060 empties the flare queue (so queued flares are drawn in the first view
  only, **unsure** for split screen).
- 0x46dc50 resets shield chain heads; 0x4231f0 / 0x421ef0 set and clear car +0x646, +0x14b4.
- Visibility itself is per track row from 0x47a030 (LOD distances compared with the far plane): effects and objects in
  rows the original considers invisible are never drawn, and their triggers never fire.

## 11. Open questions

- Z-buffer mode 0x6cce84: which drawers keep depth buckets and whether 0x6cdd80 really serves as the translucent list
  in that mode (the car code sends blended parts there); and how 0x6cdd80 then coexists with a second split-screen
  view (z-buffer mode may be single-view only: unchecked).
- The face group word at packet +0x82 and the per-model order tables (model +0x18 / +0x44): a painter's order within a
  car; a z-buffered renderer can ignore them for opaque faces, but translucent faces may rely on them.
- Env-map face test (rotated vertex y < 1 for all corners) is read as "upward-facing"; unconfirmed.
- Speed cone (0x475580) purpose and the side streaks (0x4821d0, called "tyre trails" in post_j.c): both identified
  from code only, not seen in game.
- No visible car damage (deformation or texture swap) was found in the draw path; car +0x6ac is drawn as the shield
  bubble with per-vertex colours driven by game logic (gl_gc calls it a damage record).
- Line-up name panels (`*0x6b8cd8`) are not drawn from draw_view.
- Wall ripple centre: stored as point >> 8 but used as whole units; check trackhit.c wall_touched for the scale.
- Glow queue kinds 0 / 1 (0x441d80) have no callers found.
- surface_kind 1 / 2 in 0x479b30 read as water / lava from the tint colours (guess).
- How the header quad of 0x490430 gets its corners; how 0x4857f0 moves screen flakes (did not decompile cleanly).
- Light sprites (kind 0xd): whether the on flag +0x10c is tested anywhere at draw time (the draw does not test it).
- Kinds without bit 0x400 (driller head 0x811, rock 0x821, 0x8013, light carrier 0xa00f) are not drawn by the body
  pass; snatch 0x816 is drawn by 0x48fdf0; the others are probably invisible or drawn through their trail / head
  effects.
- Mistakes found in formats.md: car +0x1408 is the per-car layer records (not "3 skid packets"); car +0x10ac is the
  env-map overlay packets (not the tyre trail buffer); car +0x2290 holds the brake-halo sprites. views.md lists
  0x6cce44 as zoom / 104 without the W / 320 factor that 0x423e10 applies.
