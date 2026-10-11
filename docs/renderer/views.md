# Views: cameras, projection, viewports, HUD, front end, present

Survey for the Vulkan renderer (higher resolutions, ultrawide). Covers how the original sets up
each view, what 2D it draws on top, the front end's drawing, and frame/present timing. The draw
pipeline itself (ordering tables, primitive packets, track/model drawing, D3D back end) is in
`pipeline.md`; cars and effects are in `objects.md`. Addresses are Rollcage.exe virtual addresses.
"(?)" marks things inferred but not confirmed.

Units used below: positions are 16.16 fixed point world units (the integer part is a "world
unit"); matrices are int16 with 4096 = 1.0; `W`, `H` are the screen size (`0x4acc38`, `0x4acc3c`,
int32, chosen in the launcher's video mode list 0x488470, default 640x480x16).

---------------------------------------------------------------------------------------------------

## 1. Camera and projection

### 1.1 Where the camera lives

Each car slot (`0x6bfb80 + slot*0x274`) has:

| slot off | meaning |
|---|---|
| +0x00 | car object |
| +0x08..+0x14 | view rectangle, inner (x, y, w, h as int32, pixels) |
| +0x18..+0x24 | the same grown by the split-screen border (cleared area) |
| +0x28 | chase camera record pointer (0x5c bytes, `0x6bf880 + slot*0x5c`); its +0 is the camera object |

The **camera object** is an ordinary world grid object (same layout as a car body):

| obj off | meaning |
|---|---|
| +0x0c | grid cell pointer (low bit a flag); moving it needs `grid_unlink` 0x4611d0 + `grid_link` 0x460fb0 |
| +0x34 | position, 3 x int32 16.16 |
| +0x68 | velocity, 3 x int32 (used by the sound listener 0x472050, not by drawing) |
| +0xc4 | **view matrix**, 3x3 int16 + pad (0x14 bytes copied by set_view_matrix): rows = camera side, up, forward (built by `matrix_look` 0x407220), i.e. it is already the world->view rotation |

The chase camera record (+0x28, see `src/game/camera.c` header) holds the follow logic state
(look direction +8, wanted up +0x14, offset +0x20/+0x2c, view 0..2 at +0x40, distance/height
+0x44/+0x46 from tables 0x4a77e4/0x4a77ec, fade +0x58/+0x5a). None of it is projection state:
**there is no per-camera FOV field**. The field of view is global ("zoom", §1.3) and is set per
view at draw time.

TV / replay / showcase cameras: 4 records of 0x150 bytes at `0x6cf6a0` (one per view, driven by
the director 0x408360, documented in formats.md "Camera viewports"). +0x24 is that view's zoom,
+0x40 a camera body (0xd8 bytes = an object from +0x34). `tv_camera_begin` 0x408270(view) saves the
real camera object +0x34..+0x10b to 0x4b616c, copies the TV body over it (relinking), saves the
current zoom (`__ftol(*0x6cce6c)` -> 0x4b6244) and sets the TV zoom; `tv_camera_end` 0x40ad60 undoes
both. Render flags at `0x6cce40` (see §1.6) say which mode is active.

### 1.2 View transform (per view)

draw_view 0x479be0 (per view, after race_draw has set zoom and rectangle):

1. `0x466f70(cam+0x34, car)` -> `0x423ce0`: camera translation `0x6ce5c0 = -(pos >> 16)` (int32,
   **truncated to whole world units**). If the car's shake countdown (car +0x6a8, set to 0x10 on a
   hit by forces.c, strength +0x6aa) is running, a random offset (private RNG 0x4b16bc) is added
   first; in TV mode the TV record's +0x148/+0x14c drive it instead.
2. `set_view_matrix` 0x423d10: copies cam +0xc4 (5 dwords) to `0x6ce5a0`.
3. `0x479940`: time-warp weapon wobble: while `0x5d9b20` is set (and this is not the owner's
   view, `0x5d9b24`) the off-diagonal int16 elements of the **screen matrix** `0x6cd480` are
   driven by sines (0x479a80, table 0x4b24f8, scaled by `0x10000 - 0x5d9b2c`); otherwise they are
   zeroed.
4. `0x423d30`: `0x6cd480[0] = 0x2000`, or `0xe000` (-2.0) when `0x6cce94` is set (mirror flip),
   then `0x423d70(0x6ce5d0, 0x6ce5a0, 0x6ce5c0)`:
   - `R' = S * R` with `S = 0x6cd480` (diag(+-2, 2, 2) plus wobble; 0x426e90, >>12 products),
   - `t = R' * (-c)` (0x426e10), stored at +0x14..+0x1c of 0x6ce5d0,
   - then `0x423df0(0x6ce5d0)` loads it as the GTE rotation (int16 `0x6ccbe0`, float copy
     `0x6ccd1c`, rows 0x10 apart, x 1/4096) and translation (int `0x6ccbf4`, float `0x6ccd0c`).

So for a world point `p` (integer world units):

    v = S * R * (p - c)          // S = 2 * I normally, x negated when mirrored
    X, Y, Z = v                   // Y is down (PlayStation convention), Z forward

`0x6cce94` = race +0x58b normally (mirrored-track mode flips the whole world) and is toggled for
the rear-view mirror pass (§1.5). A negative `S[0]` reverses triangle winding: culling must flip
with it.

### 1.3 Projection (perspective divide)

The projection constants (set by 0x426260, called through `camera_fov_set` 0x423f00(zoom)):

| global | value |
|---|---|
| 0x6cd46a | zoom (int16, the 0x423f00 argument) |
| 0x6cce6c | zoom as float |
| **0x6ccd00** | **H = zoom * W / 320** (float, focal length in screen pixels); 0x6ccc48 = int |
| 0x6cd41c / 0x6cd418 | W * 0.5 / 640 and twice that (screen-space size scale for some sprites, read at 0x42114e, 0x462bb6, 0x47e026, 0x484a35; init 0x426090 uses max(W*0.6/640, 0.6) and x1.5) |
| 0x6cce44, 0x6cce48, 0x6cd400, 0x6cd404 | zoom / 104 (8.8), a 0..3 LOD class from it (table in 0x423e10), 0x6800/zoom, zoom*256/104 |
| 0x6ccd04 | near z = 2.0 (float, in the doubled view space = 1 world unit) |
| 0x6ccd08 | far z, `0x428b90(far)`: far = settings +0x64 (float, clamped 1024..65536, default 65536) |
| 0x6ccd98 / 0x6ccd9c / 0x6ccd94 | fog start / end / max bucket in z>>5 units: 0x4283d0(far*0.5>>5, far>>5, 0x1fb); 0x6ccda8 = 255/(end-start) |

Per view the centre and clip rectangle (0x428770 / 0x428850 -> 0x426200, 0x417ed0, 0x41f2d0):

| global | value |
|---|---|
| 0x6ccc40 / 0x6ccc44 | w/2, h/2 as 16.16 (integer projection path) |
| 0x6ccd4c / 0x6ccd50 | w/2, h/2 floats (viewport-relative centre) |
| 0x6ccd54 / 0x6ccd58 | viewport origin x, y (floats; the HUD adds these) |
| **0x6ccd5c / 0x6ccd60** | **absolute centre x + w/2, y + h/2** |
| 0x6ccd64 / 0x6ccd68 / 0x6ccd6c / 0x6ccd70 | clip rect left, right, top, bottom (clipped to the screen) |
| 0x4b7fa8..0x4b7fe4 | 4 frustum side planes from 0x417ed0(x, y, w, h, H): normals (cos, sin) of atan(2H/w) and atan(2H/h); 0x4b7fec = 0x40 if the rect leaves the screen |

The vertex projection (3DNow! version around 0x4269a3, x87 versions 0x426ade/0x426caf/0x426da9;
picked at init by 0x426090):

    if Z >= far(0x6ccd08)  outcode |= 0x20
    if Z >= near(0x6ccd04):
        sx = cx(0x6ccd5c) + X * H / Z
        sy = cy(0x6ccd60) + Y * H / Z
        outcodes vs the clip rect (1 left, 2 right, 4 top, 8 bottom)
    else outcode 0x10 (behind the near plane)

**The same H is used for x and y**: pixels are square, nothing is scaled by an aspect ratio.
The field of view therefore follows from H and the view rectangle:

    tan(hfov/2) = (w_view / 2) / H        tan(vfov/2) = (h_view / 2) / H
    H = zoom * W / 320

Because H is tied to the **screen width**, the original is **Vert-**: a wider screen keeps the
horizontal FOV and crops the vertical one. In split screen H still uses the full screen width, so
each view is a crop of the same pixel scale (two side-by-side 320x480 views at 640x480 each get
hfov 2*atan(160/H)).

Zoom values in use:

| who | zoom |
|---|---|
| default / views_setup 0x4238c0, rear mirror pass | 0x68 = 104 |
| chase camera, every draw: `camera_speed_zoom` 0x40b000 | `200 - (min(speed, 425) * 5 >> 4)`; with cheat settings +0x95: `200 - (min(speed, 450) * 6 >> 4)`; speed = car +0x658 (the speedometer number) |
| TV cameras (record +0x24) | 0x68 each step; 0x138 / 0xd0 by config +0x31; 0x7b event shots; kind 0xf: `(x*104)>>8`; kinds 1/7/0xe: the current zoom |
| front end, setup paths | 0x96 (st_sc.c 0x423f00(0x96)), `*0x4b1964` (setup_s.c) |

camera_speed_zoom also pulls the camera towards the car (`k = pull*256/0x68`, blended with the
camera's look direction, `ease = 0x1e0 - k`) and relinks it; race_draw saves all camera
positions first and restores them after drawing, so this is draw-only.

FOV examples (degrees, h x v). 4:3 = original at 640x480 (any 4:3 size is the same);
"UW Vert-" = original maths at 3440x1440; "UW Hor+" = the proposed H = zoom*Hs/240:

| zoom | 4:3 | UW Vert- | UW Hor+ |
|---|---|---|---|
| 200 (standing) | 77.3 x 61.9 | 77.3 x 37.0 | 110.2 x 61.9 |
| 169 (speed 100) | 86.9 x 70.8 | 86.9 x 43.2 | 119.0 x 70.8 |
| 138 (speed 200) | 98.4 x 82.0 | 98.4 x 51.8 | 128.6 x 82.0 |
| 107 (speed 300) | 112.5 x 96.6 | 112.5 x 64.1 | 139.1 x 96.6 |
| 104 (default/mirror) | 114.0 x 98.2 | 114.0 x 65.6 | 140.1 x 98.2 |
| 68 (speed cap) | 133.9 x 120.9 | 133.9 x 89.1 | 153.3 x 120.9 |

(The speed column assumes +0x658 reads like the HUD speedometer; check real top speeds in game.)

### 1.4 The GTE emulation (src/game/fixed.c)

fixed.c holds the bit-exact integer maths the physics uses (`gte_rotate` 0x428090: int16 matrix
0x6ccbe0 x int16 vector 0x6ccc64 >> 12 -> int32 0x6cccc8; `gte_outer_product` 0x427f50;
`gte_load_matrix` writes the int16 matrix and its float copy 0x6ccd1c). It has **no perspective
step**: projection is only in the renderer's own routines above (and an int16 screen-xy path at
0x426dc0, used by logic that needs screen positions, e.g. name labels 0x43a3e0 (?)). The new
renderer can ignore the GTE register file except as the source of the current view matrix.

### 1.5 Per-frame flow: race_draw 0x443b50

    if render enabled (0x4af5ac):
      save camera positions of all cars (camera object +0x34)
      for i in 0 .. view_count()-1          (0x40d040: race +0x552 -> 1,2,2,3,4 views)
        s = 0x6c0a40[i]                       (car slot shown by view i, from 0x45ed40)
        hud_update(slot s)                    (§2.2)
        camera_speed_zoom(slot s, cam pos)    (zoom + pull-in)
        copy slot s's rects (+8..+0x24) into slot i (restored after)
        if not TV mode (0x6cce40 & 4):
          network: sound listener from the local player's car
          draw_view(slot s); 0x423c40()       (0x423c40 = flush that view's lists, swap HUD list)
        else:
          tv_camera_begin(i); draw_view(slot i); 0x423c40(); tv_camera_end()
      rear-view mirror (settings +0x25, not in TV mode):
        rect x = (W - 2*(W/5))/2, y = (H - (H/8 & ~1)) >> 5, w = 2*(W/5), h = H/8 & ~1
        (640x480: 128, 13, 256, 60), on view 0's car only, full-screen coordinates
        0x428a80: save fog/zoom state, flags |= 0x42, player bits; 0x6cce94 = race+0x58b ^ 1
        camera_fov_set(0x68); flags |= 0x20
        0x40d090: camera object = car body copy, 7 units up (y - 0x70000), looking backwards
                  (matrix_look of the car's negated forward, up sign by car +0x63f)
        draw_view(slot); 0x423c40(); restore rect, camera object, flags; 0x428b00 restore fog/zoom
      picture-in-picture TV view (flag 8, toggled by button bit 31 in 0x41c7a0):
        w = h = W/4, x = W - w - 10, y = (H - w)/2   (a square in screen pixels)
        tv_camera_begin(0); draw_view(slot 0); 0x423c40(); tv_camera_end()
      restore camera positions (relink)

draw_view 0x479be0 per view (camera/viewport parts; the draw calls are pipeline.md's):

- rectangle from slot +8 (inner) and +0x18 (bordered). End-of-race closing bars: when race
  +0x57c > 6 and race +0x580 < 48, `0x6cd414 = H*256*t/48`, the view's y grows and h shrinks by
  that share until it closes (a vertical "iris" to black).
- normal views: `0x4284b0(bordered, inner, 0)` draws the 4 black border quads (z 4.0) between the
  inner and bordered rects, then `0x428770(inner, 0)` sets centre/frustum/D3D viewport.
- insets (flag 0x20: mirror, PiP): `0x428770(inner, 2)`: a 2-pixel border frame, the inner rect
  filled with the fog colour 0x6ccd8c (z 1.9), and when `0x6cce7c` is set an extra
  EndScene / Clear(z) / BeginScene so the inset gets its own depth.
- camera fade: `0x40cbe0` / `0x40cd00` temporarily move the camera when the chase camera's fade
  (+0x5a) is running (close-view switch), restoring it after.
- `0x479b30`: full-view colour tint when the car is in water/lava-like surfaces (slot +0x256
  strength 0..255, +8/-8 per draw).
- `0x438310(slot)` near the end links the HUD (§2).

### 1.6 Render flags 0x6cce40

| bit | meaning |
|---|---|
| 0x02 | mirror pass (with 0x40), set by 0x428a80 |
| 0x04 | TV-camera mode for every view (race end showcase, replays; `race_end_fade_set` 0x40afb0) |
| 0x08 | picture-in-picture TV inset on (debug/extra button) |
| 0x10 | a TV camera is active (set by tv_camera_begin) |
| 0x20 | inset pass (no split border, own clear) |
| 0x40, 0x780 | "player view" bit and player index (bits 7..10) for the current view |

### 1.7 120 fps interpolation (this project)

`src/game/frame.c` replaces race_frame 0x4440b0, present_frame 0x436a52 and hooks race_draw
0x443b50 through a trampoline (`race_draw_hook` in src/main.c: the first 11 bytes are moved so
`race_draw_original` stays callable; the hook skips draws while `holding_draws`).

- Physics steps come from QueryPerformanceCounter at the original 32 ms (16 ms if race +0x58d);
  a >250 ms gap resets; at most 4 steps per frame. Only single-player races (not front end, not
  network, not settings +0x92 "no wait").
- Draws happen at most once per display frame (`REROLL_MAX_FPS`, default 120, 0 = uncapped,
  minimum 31), via `interp_draw(race_draw_original, alpha, hud_tick)`.
- `src/game/interp.c` records after each step, for every race car: the car body, its 4 wheels
  (car +0x390 + i*0xc4 pointers), its camera object, every on-screen view's camera, and the 4 TV
  camera records (as pseudo-objects at `0x6cf6e0 + i*0x150 - 0x34`). It blends position (linear,
  16.16; > 0x4000000 on an axis = teleport, no blend) and the +0xc4 matrix (cars/wheels:
  per-row lerp + renormalise; cameras: plain element lerp), writes them in, relinks grid objects,
  draws, then puts the exact values back.
- Draw-time side effects it neutralises on the extra draws: HUD flash countdowns (car +0x65c,
  +0x65d) and the trackside trigger queue (0x6b9ba0..0x6b9cbb, 0x6b9b94).
- Not interpolated: every other object (projectiles, pickups, particles, debris, animated track
  parts, sky), the zoom (speed +0x658 is per step, so the FOV changes in 32 ms steps), the
  wobble, and the time-of-step HUD numbers.
- Draw-time state that still advances per draw (so runs up to ~4x faster at 120 fps; found in
  this survey, not handled): the camera shake countdown car +0x6a8 (0x466f70), the HUD fade-in
  level `0x6c7fdc` (+2 per draw in 0x439d70), the incoming-missile warning beep counter
  `0x6c7fe2 + car*4` (0x438310), the surface tint slot +0x256 (+-8 per draw, 0x479b30), and the
  random shake RNG 0x4b16bc (private to 0x466f70, harmless). The new renderer should either run
  these on the step clock or scale them by frame time.
- Paused: pause_frame 0x463b70 calls race_draw (exact state, no blend) and draws the pause menu
  every frame through the original present path.

Recommendation: the new renderer should take a snapshot of all drawable state after each physics
step and interpolate itself (including the projectile/particle positions), instead of patching
live objects and relinking the grid.

---------------------------------------------------------------------------------------------------

## 2. Viewports and split screen

`viewports_setup` 0x444080 -> `viewport_player_order` 0x45ed40 (fills 0x6c0a40[4]) ->
`viewports_layout` 0x423360 (C in setup_u.c). Screen size used: `0x6cd462` / `0x6cd464`
(u16, = W, H from views_setup 0x4238c0). Border `bx, by` from `viewport_border` 0x428b50: 0 in single view, 1 pixel when split.

| race +0x552 | views | layout (w x h before border) |
|---|---|---|
| 0 | 1 | full screen |
| 1 | 2 | stacked: W x H/2, top and bottom |
| 2 | 2 | side by side: W/2 x H |
| 3 | 3 | W/2 x H/2: one centred on top (its cleared rect spans the full width), two below |
| 4 | 4 | W/2 x H/2 quadrants |

It also sets the screen centre (0x6cd466/0x6cd468 = w/2, h/2 of one view; 0x426200), packs
`0x6cd478 = h<<16 | w`, copies the track background colour (track info +0x30..+0x32, x2) into
both view records (0x6cd4b9.., 0x6cdd29..), issues two PlayStation-VRAM clears of 320x256
(`screen_clear` with rect (0,0,0x140,0x100) and (0,0x100,0x140,0xff): PSX leftovers (?)) and
stores **race +0xa82 = view w, race +0xa80 = view h - 2*by**. Those two race fields are what the
HUD and messages lay out against (note gl_gb.c's `hud_layout` names them W/H the other way round;
the data says +0xa82 is the width).

View records (`0x6cd4a0` and `0x6cdd10`, 0x870 bytes apart, current at `0x6ce60c`): PSX-style
double-buffered draw environments: +0x5c rect, +0x64 a projection centre from race +0xa8c/+0xa8e
(`(x>>2)*60/100`, `y>>2`; PSX GTE offset leftover (?)), +0x70 a 512-entry ordering table
(0x6cd510 / 0x6cdd80). Projection on PC uses the 0x41f2d0 values, not +0x64.

Split-screen notes for the new renderer:

- Every view uses the same H (screen-width based), so side-by-side views are narrow tall crops
  (hfov 43.6 deg at zoom 200 at 640x480), stacked views wide short crops.
- HUD scale base switches to 550 in split screen (`0x4acff0 = 0x226`, §3.1) and is never set back
  to 400 (one-way write at 0x4377ec): after a split-screen race, single-player HUDs are smaller
  (?).
- The mirror and PiP are placed in full-screen coordinates and use view 0's car only.

---------------------------------------------------------------------------------------------------

## 3. HUD and 2D in races

### 3.1 Coordinate system and scales

All in-race 2D is **pre-transformed screen-pixel geometry** (D3D TL vertices: float x, y, z, rhw,
colour, uv), built by the game in pixel units of the current screen. There is no virtual
resolution; instead each subsystem scales its PlayStation-era constants by a **width-only**
factor:

| scale | value | used by |
|---|---|---|
| `u` (HUD) | `W*256 / *0x4acff0` (0x4acff0 = 400, or 550 after split screen) | race HUD item positions and sizes (0x4377c0, 0x4387d0, 0x4390c0, 0x4392e0, 0x439400) |
| `0x6cd410` "screen scale" | `W*256/640` | message text glyphs and positions (0x479060/0x4792a0), pause panel (0x463ca0, 0x464a20, 0x464c10), name labels |
| `0x6cd40c` | `W*256/320` | marker/sprite sizes (0x43a6c1, 0x442069, 0x47eb09, 0x483a96, 0x485d06) |
| countdown | `race+0xa82 * 255 / 640` | countdown numerals (0x46a9e0) |
| front end | `x*W/640, y*H/480` (and `x*W/512, y*H/256`) | menus, boxes, text boxes (§4) |

Positions: race HUD items are laid out **relative to the view rectangle's edges** (left/top
`T(n)`, right `w - T(n)`, bottom `h - T(n)`, centre `w/2`; 0x4377c0 `hud_layout`, gl_gb.c), then at
draw time offset by the view origin (`0x6ccd54/58`). Messages are positioned in view pixels with
helpers for centre (flag 4), bottom-centre (0x40), centring (2), right align (0x10).

### 3.2 HUD items (0x4377c0 layout, hud_update 0x4386f0, draw 0x438310)

0x43 items, two 0x78-byte quad packets each at `0x6c3ec0` (packet i at +i*0xf0; the second is a
per-draw copy offset by the view origin). Packet: header (+0 link, +4 colour, +7 code 0x98...),
4 vertices 0x1c apart with x/y floats at +0x10/+0x14, +0x2c/+0x30, +0x48/+0x4c, +0x64/+0x68 and
u/v bytes around +0x20/+0x3c/+0x58/+0x74 (texture page word near +0x22). Item positions: int16
pairs at `0x6c7dc8 + i*4`; texture index per item `0x6c7ed4 + i*2` (into the track's texture
list `*0x6c1e14` + track info +0x2e); shown flags `0x6c7f5a + i`. Textures are track texture
atlas cells (HUD art is in each track's texture set).

hud_update(slot) 0x4386f0, called by race_draw per view **before** the view is drawn, only
rewrites UVs/sizes (digits are UV offsets into a digit strip, 0x4387d0(item, digit, flash)):

| function | items | shows |
|---|---|---|
| 0x438df0 | 0x2d..0x2f | speed, 3 digits (car +0x658) |
| 0x438e60 | 4..7, 8, 0x34 | lap n / of laps (race +0x586); race position big digit and "st/nd/rd/th" (flashes in 1st); kills/score in kinds 3/8 |
| 0x4390c0 | 2, 3 | the two weapon icons (car +0x67c/+0x67d, icon table 0x4acff4; bit 0x40 flashing, 0x20 fast) |
| 0x439050 | 0x41 | a flashing indicator from slot +0x2f (?) |
| 0x438fb0 | 0xd.., 0x15.., 0x1d.., 0x25.. | four lap-time rows of 8 digits (slot +0x4c.., packed nibbles); row 3 flashes by car +0x65c |
| 0x439270 / 0x4392e0 | 0x42 | "lap" / "final lap" banner (texture +0x60 / +0x98), flash by car +0x65d |
| 0x439f20 | 0x6c7fa4 / 0x6c7fc0 | race kind 2: progress markers along the view edge |

The draw side, in draw_view via `0x438310(slot)` and `0x438760(slot)`: race-state banners,
`0x43a4a0` (positions board, single view, some race kinds), `0x483d20` (target marker), `0x458760`
+ `0x439d70` (incoming warning and the HUD fade `0x6c7fdc`), `0x469f80` (race kind 4), the 0x43
HUD packets (offset by the view origin and linked into the HUD list), `0x4792a0(slot, 0)`
(messages), and `0x439400` / `0x43a000` / `0x45cfe0` (labels over other cars and network names,
placed by projecting car positions, 0x43a3e0). `0x46a9e0`: countdown numeral (texture +0x7e,
digit by UV, size shrinking with the countdown tick 0x4d3dac, centred on the view centre;
skipped when flags & 0x12).

Lists: each view's HUD goes into a 2-entry list (`0x6ce5f0` / `0x6ce5f8`, current pointer
`0x6ce608`), drawn last for that view by 0x423c40 after the two 512-bucket ordering tables
(0x6cd510, 0x6cdd80) and the overlay list 0x6ce580; then the HUD list pointer swaps.

### 3.3 Text and fonts

- Font image: FONT.BMP (image record 0x4c2408, 16-wide cells from row 0x36; formats.md "FONT
  MAP"), uploaded to D3D textures by 0x4432d0 (original). `fonts_init` 0x42e0c0 builds fonts A
  (0x6c8180, big, cell height 0x2e), B (0x6c92a0, small, 0x10) and C (copy of A, 0x6ca3c0); the
  current font pointer for messages is `*0x5f6940`.
- Font record: +4 default scale (u16, 0x100 = 1.0), +0x10 space width, +0x14 gap between glyphs,
  glyph `g` at +0x20 + g*0x10 - 2: +0x1e y offset, +0x20 width, +0x22 height, +0x24 texture page,
  +0x26 u, +0x28 v; character map at +0x101c (byte per character, 0xff none: A-Z, a-z, 0-9, then
  the extra characters of 0x4ac86c).
- Messages: `text_create` 0x479060 (C, post_k.c): 0x1c records of 0x50 bytes at 0x5f6944 (menu
  list: 8 at 0x6b95a4): +0 owner car (0 = all views), +4/+6 x/y (pixels), +8 length, +9 flags,
  +0xa width (pixels), +0xc id, +0xe scale, +0x10 text. Width is computed in pixels with
  `0x6cd410`, so a message's layout is resolution-dependent at creation.
- Drawing: 0x4792a0(owner, menu): one 0x84-byte quad per glyph into the buffer 0x5d9b40..0x5f698b
  (stops when full), at x + view origin, glyph size `(w*scale>>8) * 0x6cd410 >> 8`, top colour
  `c`, bottom `c>>2` (vertical gradient), flags 1/0x20 blink by the race tick. Race messages go
  in the HUD list; menu texts (pause) are drawn immediately (0x428460).
- The front end has its own text items (0x448a50, 0x445f50) and the multi-line text box
  0x431780 (x87, 4254 bytes; args (list, text, colour, x, y, w, h) in 640x480 units, scaled by
  `x*W/640`, `y*H/480`).

### 3.4 Countdown, messages, pause, fades

- Countdown: state from `race_progress` 0x46a530 (logic); numeral sprites set up by
  `countdown_init` 0x46a940 (two packets at 0x4d3cb8 / 0x4d3d30, one per view record), drawn by
  0x46a9e0. "Go" banner and finish messages are text messages (0x4651f0, 0x464e10) that slide
  (`text_slide` 0x479690, 0x20 px per step).
- Pause: `pause_open` 0x463ca0 builds 9 panel sprites (0x6b8d8c, 0x78 bytes; table 0x4b15f0:
  s16 x, y, u8 w, h, texture) at `(x * 0x6cd410 >> 8) + layout_w/2` (uniform W/640 scale,
  centred on the screen; `pause_layout` 0x423f20 returns the screen w/h and in split screen
  resets the view to full screen). 0x464a20 adds icon sprites (table 0x4b1638, 8 bytes: s16 x,
  y, -, flip, texture), 0x464c10 rotates a slider knob (radius 0x13, sine tables 0x4b24f8 /
  0x4b2578). `pause_draw` 0x463f90 draws them immediately (0x428460) with the highlight colour,
  then the menu texts. Each paused frame: race_draw (exact state), pause_layout, pause_draw,
  present.
- Fades: the front end fade level `0x4ac990` (0 clear .. 0x100 black), stepped 0x15 per 16 ms
  timer tick by `fade_step` 0x430190 (direction 0x4be330), is not a full-screen quad: it
  **multiplies colours** (0x432820: colour*(0x100-fade)>>8; 0x4328a0: value*fade>>8 for slide
  offsets) in the front end. Races use the per-view closing bars (§1.5), the camera fade and the
  surface tint; the attract overlay uses 0x490ec0/0x490ed0.

---------------------------------------------------------------------------------------------------

## 4. Front end drawing

`frontend_frame` 0x4302d0 (C, fe_l.c) per frame, drawing calls in order:

1. clear the ordering tables (0x427a70 on 0x6cd510 / 0x6cdd80, 0x200 each).
2. backdrop colour: `0x4530d0(list, page, colour)` (32 packets at 0x4cd950, colour faded by
   0x432820 and darkened while the attract demo fades in).
3. menu box: `0x453220(list, page, left, top)` (quads at 0x4cf7c8.., x in **512-wide units**
   `x*W/512`, y in **256-high units** `y*H/256`, z 4500/4000), then `menu_layout` 0x445790
   (C) places the items.
4. main menu extra: `0x4535b0` (screen 4 with config +0x11; z 2000; also 512x256 units).
5. attract mode: `menu_water_update` 0x48d630 (logic, fe_z.c): a 30x30 height grid
   (0x5d1fe0: 1500x1500 world units, 50 per cell, from 0x48d530) and `0x490ed0`, which builds
   its quads (code 0x8e, z 5000) through 0x4908a0's projection - the water is a 3D mesh, so it
   follows the projection, not the 2D scaling.
6. flush the lists (0x428470 = 0x421860) on 0x6cdd0c and 0x6ce57c.
7. 3D scenes (each sets its own view rectangle with `view_rect_set` 0x428850(x, y, w, h), where
   x,w are `n*W/640` and y,h `n*H/480`, draws, flushes, and restores 0x428850(0,0,W,H)):
   - `0x455e90`: the selected menu item's 3D model (`0x4af660`, menu model records 0x4cba80,
     100 x 0x48 bytes), in a box at `(left+0x154)*W/512` ...; spins via 0x426f80 (angles to
     matrix) and lights via 0x427fc0 (light vectors rotated by the GTE matrix).
   - `0x456800`: a second model (`0x4af664`) in a 220x300 box right of the menu.
   - `0x457020` -> `0x453a00` (9351 bytes): the car/track showroom scene (7 kinds keyed by
     0x4af660 in 0x4af614, distance from 0x4af630), with 3 rotating light vectors, in a 350x350
     box; then two info text boxes (0x431780) and stat bars.
   - `0x4538e0(model, matrix, pos, ...)` draws a menu model: copies matrix/position into its
     body and calls the model renderer (0x45a2a0, 0x45b200, 0x45b400, then 0x4226d0 into an
     ordering-table bucket by depth, or 0x45a430).
8. `0x4579a0`, `0x462bb0(1)` (corner sparkles from 0x462ae0, sizes from 0x6cd418).
9. menu text box `0x446200` -> 0x431780, item rows/highlights `0x446300` (x `n*W/640`,
   y `n*H/480` as floats), scroll arrows/extras `0x446ea0`, sprite list `0x42f3c0` (0x4be310,
   x `n*W/640`).
10. flush, `0x472040` (mixer thunk).

All front-end 2D therefore maps a 640x480 (or 512x256) virtual screen onto W x H with
**independent x and y scales** - stretched on non-4:3 screens. The 3D boxes are placed the same
way but projected with the isotropic H = zoom*W/320 (zoom 0x96 for the front end (?)), so their
contents are not stretched but their framing changes.

Movies: `movie_play` 0x42d7e0 runs the original FMV player (0x493210) with its own DirectDraw
use (surfaces 0x4d5f40/0x4d5f48); startup movies switch the display to 640x480x16 (0x4885d0)
first and restore the size after. Left original; a new renderer must release/restore the
display around them (or replace the player).

Loading screens: `loading_screen_show` 0x436b9d clears and flips twice, then `0x436c2c` loads
`LOAD%d.BMP` (1..8: 1/2 by state, 3..7 by settings +0x36 for races, 8 with settings +0x96) and
**stretch-blits it to (0, 0, W, H)** (0x4203a0).

---------------------------------------------------------------------------------------------------

## 5. Present and frame timing

Original `present_frame` 0x436a52 (now C in frame.c, same calls):

    0x41e190  IDirect3DDevice2::EndScene (vtable +0x2c)
    0x429070  -> 0x420090: flush queued texture uploads (texture Load for each pending page)
    busy-wait until timeGetTime() >= last_present + 32 ms (16 ms if race +0x58d; none if
              settings +0x92 "no wait"; races and front end alike)
    0x4876f0  IDirectDrawSurface::Flip(0, DDFLIP_WAIT) fullscreen (Restore on DDERR_SURFACELOST),
              or a Blt to the window (0x4878d0) when windowed (0x4d5f80 == 0)
    0x405c10  if 0x4be49c: screenshot to "\RollCage Dumps\DumpN.bmp" (reads the 16-bit surface)
    0x41f2d0(0, 0, W, H)  full-screen viewport
    0x41e040(0)  IDirect3DViewport2::Clear (target + z)
    0x41e170  IDirect3DDevice2::BeginScene (vtable +0x28)
    stats 0x4be494 / 0x4be498

So drawing happens between a BeginScene at the end of the previous present and the EndScene at
the start of the next. The original is effectively locked to **31.25 fps** (one physics step per
frame in single player; network races use `sim_steps_due` 0x436958 with 33 ms steps and draw
once per batch). A 16 ms multimedia timer (`timer_start` 0x476fb0, callback 0x4365ed, ticks
0x4be3e8) drives front-end fades/animations independently of the frame rate.

With this project's frame.c: in paced races, present skips the wait for frames it drew
(FRAME_DRAWN), and does not present at all when nothing was drawn (FRAME_NOTHING, `Sleep(1)`).
Menus, network and pause still use the original wait.

For a new renderer: own the swapchain (FIFO/mailbox) and frame pacing; keep the physics on its
32/16 ms clock; treat the per-draw counters listed in §1.7 as step-rate state; handle the
pause path (draw from exact state each frame) and the screenshot request (0x4be49c).

---------------------------------------------------------------------------------------------------

## 6. Resolution and aspect assumptions, and how to remove them

Hard-coded 640x480 / 4:3 / width-based items:

| where | assumption | fix |
|---|---|---|
| 0x426260 `H = zoom*W/320` | horizontal FOV fixed (Vert-) | `H = zoom * Hs / 240` (Hor+); identical on 4:3 |
| 0x426260 `0x6cd41c = W*0.5/640` | sprite size scale by width | use `Hs*0.5/480` (or derive from H) |
| `0x6cd40c = W*256/320`, `0x6cd410 = W*256/640` (0x4238c0) | 2D/text scale by width | use `min(W/640, Hs/480)` (= Hs/480 on wide screens) |
| HUD `u = W*256/0x4acff0` | HUD scale by width; base 400 (550 split, sticky) | `u = (Hs*4/3)*256/base`; items already anchor to view edges |
| countdown `race+0xa82*255/640` | width | use view height * 4/3 |
| mirror rect (0.3W, H/32, 0.4W, H/8) | 4:3 | width `0.4 * Hs*4/3`, centred |
| PiP inset `W/4` square | 4:3 | `Hs/3` square, `W - size - 10` |
| pause panel `x*W/640` both axes, centred | 4:3 | uniform `min` scale, centred |
| front end `x*W/640`, `y*H/480`, `x*W/512`, `y*H/256` | stretched 4:3 | map the virtual screen into a centred 4:3 rect (`Hs*4/3` wide) and fill the sides (backdrop colour, or widen the water mesh / 3D backdrop); 3D boxes use the same rect |
| loading pictures stretched to W x H | 4:3 images | pillarbox |
| movies | 640x480 display mode | play in a 4:3 rect |
| `text_create` widths in pixels at creation | layout fixed per resolution | fine if the scale is consistent |
| camera position `-(pos >> 16)` | integer world units | use full 16.16 (plus interpolation) to avoid sub-unit judder at high fps |
| 16-bit display modes (launcher list, 0x405c10 dump) | colour depth | n/a for Vulkan |

### Hor+ FOV for ultrawide (the maths)

Keep the original's **vertical** framing for a 4:3 screen of the same height and widen
horizontally:

    H_px      = zoom * Hs / 240                 // focal length in pixels (original: zoom * W / 320)
    tan(v/2)  = (h_view/2) / H_px               // equals the 4:3 original
    tan(h/2)  = (w_view/2) / H_px = aspect_view * tan(v/2)

Vulkan projection for a view rectangle (x0, y0, w, h) in pixels, from the doubled view space
(X, Y, Z) above (Y down, Z forward):

    x_ndc = (2*H_px / w) * X / Z + (2*(cx - x0)/w - 1)     // cx = x0 + w/2 -> offset 0
    y_ndc = (2*H_px / h) * Y / Z                           // Vulkan NDC y is down, like PSX
    near  = 2.0, far = settings+0x64 (both in the doubled space); reversed Z recommended

Frustum culling (0x417ed0 equivalent) must use the same w, h, H_px: side planes at
`atan(2*H_px/w)` and `atan(2*H_px/h)` from the view axis. Split screen keeps one H_px for all
views, as the original does.

Optional limit: at 3440x1440 and high speed the Hor+ horizontal FOV reaches ~140-153 deg (table
in §1.3). Offer a cap (e.g. clamp `tan(h/2)` to `tan(75 deg)` by raising H_px, which narrows the
vertical slightly) or a "4:3 locked" option.

HUD for ultrawide: scale by height (`u = Hs*4/3*256/400`) and keep the existing edge anchoring;
optionally clamp the anchors to a 16:9 safe area so the gauges are not at the far corners.

---------------------------------------------------------------------------------------------------

## 7. Open questions

- Is anything culled by a precomputed visibility that assumes the 4:3 FOV (track section
  visibility, object draw distance tables, LOD class 0x6cce48)? If so, Hor+ will pop at the
  sides; pipeline.md should say. 0x417ed0's planes follow H and the rect, so the frustum part
  is fine.
- Real range of car +0x658 (speedometer) and so the real chase FOV range; confirm in game.
- `0x4acff0` never returning to 400 after split screen: confirm the single-player HUD really
  shrinks (could be reset by reloading data).
- race +0xa8c/+0xa8e projection centre in the view records: confirm it is unused on PC.
- Item 0x41 (0x439050) and the exact meaning of the HUD extras 0x43a4a0, 0x439400, 0x483d20.
- 0x453a00's scene kinds (car showroom vs track preview) and the front-end zoom value.
- The per-draw counters in §1.7 (shake +0x6a8, HUD fade 0x6c7fdc, warning beep 0x6c7fe2,
  tint +0x256) are a behaviour change of the current 120 fps mode, not only a renderer concern.
