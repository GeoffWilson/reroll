# Render pipeline back end and track/world drawing

A read-only survey of how Rollcage.exe (Direct3D build) draws a race frame,
written for the planned Vulkan renderer. Addresses are in the original exe.
"Sure" means read in the disassembly; "probably" or "unsure" means inferred.
Related surveys cover cars, shadows, effects, particles, sprites and weather
(worker a), and camera, projection set-up, viewports, HUD, 2D text and the
front end (worker b). This document only points at their areas.

Summary: the PC port keeps the PlayStation design. The game code fills
PS1-style GPU packets and links them into **ordering tables** (OTs, 512 depth
buckets). The back end walks each OT once per view and turns every packet into
**immediate `DrawPrimitive` calls on an `IDirect3DDevice2`**, using
pre-transformed `D3DTLVERTEX` vertices, with no transform/lighting by D3D and
no batching. Unlike the PS1 version, the 3D packets carry **float vertex
records that include the camera-space x, y, z** as well as the projected
screen x, y. That is the best place for a new renderer to start.

---

## 1. Frame flow

### 1.1 Call order

```
race_frame 0x4440b0 (C, frame.c)
  sim_step 0x444120 ... last step calls race_draw
  race_draw 0x443b50 (C wrapper -> original via trampoline)
    for each view (count 0x40d040(), order table 0x6c0a40[]):
      hud_update 0x4386f0, camera_speed_zoom 0x40b000           (worker b)
      draw_view 0x479be0(slot)          build this view's OTs
      flush_ots 0x423c40                walk the OTs -> D3D, then clear them
    rear-view mirror (settings +0x25): FOV 0x68, rect W/5.., draw_view + 0x423c40
    TV-camera inset (0x6cce40 & 8): draw_view + 0x423c40
present_frame 0x436a52 (C, frame.c)
  0x41e190  IDirect3DDevice2::EndScene            (vtbl +0x2c)
  0x429070  deferred palette/texture re-uploads (0x4bde04 queued pages)
  (frame pacing wait)
  0x4876f0  flip: fullscreen -> primary(0x4d5f48)->Flip(0, DDFLIP_WAIT) (vtbl +0x2c),
            restores lost surfaces on DDERR_SURFACELOST; windowed -> blit 0x4878d0
  0x405c10  pending work when 0x4be49c is set
  0x41f2d0(0,0,W,H)  full-screen D3D viewport
  0x41e040(0)        IDirect3DViewport2::Clear(1 rect, D3DCLEAR_TARGET [| ZBUFFER when 0x4d5f98])
  0x41e170  IDirect3DDevice2::BeginScene          (vtbl +0x28)
```

All drawing therefore happens between the BeginScene at the end of one
present and the EndScene at the start of the next. Inside a frame,
`0x428770` (called by `draw_view` to set the view rectangle) also does
EndScene, Clear(ZBUFFER) and BeginScene when a z-buffer is in use
(`0x6cce7c`).

Note: frame.c names 0x41e170 `present_flip` and 0x41e190 `present_prepare_a`.
From the vtable offsets (`IDirect3DDevice2`: +0x28 BeginScene, +0x2c
EndScene, +0x5c SetRenderState, +0x74 DrawPrimitive, +0x78
DrawIndexedPrimitive) they are BeginScene and EndScene. The real flip is
`0x4876f0` (`present_after_wait`).

### 1.2 draw_view 0x479be0(slot)

`slot` is the 0x274-byte car/view slot at 0x6bfb80 + i*0x274. +8..+0x14 is
the view rect (s16 x, y, w, h), and `*(slot+0x28)` is the camera object (+0x34
position 16.16, +0xc4 view matrix). The calls below are in order. Callees
marked (a) or (b) belong to the other workers.

| call | what (sure / probable) |
|---|---|
| 0x4284b0 / 0x428770(rect, border) | viewport: 0x426200 (projection centre), 0x417ed0 (frustum side planes), 0x41f2d0 (D3D viewport and float clip rect), border quads through the packet interpreter |
| 0x40cbe0 | camera set-up (b) |
| 0x466f70(cam+0x34, slot) | camera translation with shake: 0x423ce0 stores **-(camera pos >> 16)** as integers at 0x6ce5c0/c4/c8 |
| set_view_matrix 0x423d10(cam+0xc4) | copies 5 dwords (3x3 s16 matrix, 4.12) to 0x6ce5a0 |
| 0x479940 | sound/camera bits (b) |
| 0x423d30 | builds the view RT at 0x6ce5d0 (0x423d70: R x 0x6cd480 tilt, T = R x 0x6ce5c0) and loads it into the GTE registers (0x423df0) |
| 0x47bd20, 0x478df0 | car engine glow (a); animated texture frame timers (0x6bfb44 list) |
| 0x490430, 0x42be20, 0x47ea00, 0x479b30, 0x42c0a0 | screen-space static/flash overlays and tints (a) |
| 0x484980 | weather: 0x485d00 rain streaks, 0x484a10 / 0x484c20 (a) |
| 0x438760, 0x433f00, 0x42b9a0, 0x42c950, 0x42c420, 0x4345c0, 0x467ec0, 0x483210, 0x48fdd0, 0x46c840, 0x438310 | HUD, markers, weapons, projectiles, effects (a/b) |
| **0x47a030(cam_cell)** | **track visibility: returns the visible-run list** (section 4.3) |
| **0x477940(list)** | **track drawing**: transform, project, cull and insert the track quads |
| 0x428bd0 / 0x479f60 | saves the far plane; inserts three 0xa0 "fog marker" packets (no effect in this build, section 3.6) |
| 0x428b90(65536.0) | far plane = 65536 for the sky |
| **0x404f50(cam)** | **cloud quads** |
| **0x4049b0(cam)** | **sky dome** |
| 0x428b90(saved) | far plane restored |
| **0x47dd70(list)** | **per visible cell: track lights (0x47c730 is called earlier by 0x47a030), static objects and object sprites** (section 4.6) |
| **0x459b20** | **world objects in the visible cells** (model drawing shared with cars, (a)) |
| 0x46dc50, 0x421ef0, 0x4821d0, 0x442060, 0x46f230, 0x47db20, 0x46a9e0, 0x41b2f0 | shields, cars and wheels, shadows, flares, skid marks, the snatch beam, screen object flicker (a) |

The fixed tint `0x6ccc34..3c` (from the track ambient colour 0x4af4b8, >>4,
|8) is set before and after the world is drawn.

### 1.3 flush_ots 0x423c40 (sure)

```
0x428250 -> 0x41de40  SetRenderState(FOGENABLE, *0x4bcdc0); 0x6cce4c = 1
0x428240 -> 0x41dd90  SetRenderState(TEXTUREMAG/MIN, LINEAR or LINEAR+1 by 0x6cce90)
DrawOTag(0x6cdd0c)    view-0 OT  (0x6cd510, 512 entries, walked from entry 511 down to 0)
DrawOTag(0x6ce57c)    view-1 OT  (0x6cdd80, 512 entries)
0x428260 -> 0x41de60  FOGENABLE off; 0x6cce4c = 0
DrawOTag(0x6ce584)    2-entry OT 0x6ce580 (screen-space overlays: rain 0x485d00, ...)
DrawOTag(*0x6ce608+4) 2-entry 2D OT, alternating 0x6ce5f0 / 0x6ce5f8 (HUD, (b))
swap 0x6ce608; ClearOTagR(0x6cd510,512), (0x6cdd80,512), (*0x6ce608,2), (0x6ce580,2)
```

`DrawOTag` = 0x428470 -> 0x421860, `ClearOTagR` = 0x427a70.

---

## 2. Ordering tables and packet formats

### 2.1 Ordering tables (sure)

* `ClearOTagR(ot, n)` 0x427a70: `ot[i] = &ot[i-1] | 0x80000000` for i >= 1,
  and `ot[0] = 0xffffffff` (the end).
* Insertion (inline everywhere): `pkt->tag = *e; *e = pkt` where
  `e = ot_entry & 0x7fffffff`. The full 32-bit next pointer overwrites the
  tag word, including the PS1 size byte that setup code writes (0x0c...).
  Within a bucket the order is last in, first out.
* `DrawOTag` 0x421860(start): restores lost surfaces first (0x4218e0 ->
  0x420150, re-Load + GetHandle), resets the cached state (0x420a30). Then it
  walks: a word without bit 31 is a packet pointer (drawn by 0x420be0, then
  next = its tag); a word with bit 31 is an OT link. It stops at 0xffffffff.
* **Depth bucket**: world drawers use `idx = (int)max_z >> 5`, where max_z is
  the largest camera-space z of the polygon's vertices. Then
  `idx < 1 -> 1`; `idx > 0x100 -> idx/2 + 0x80`; `> 0x180 -> idx/2 + 0xc0`;
  clamped to 0x1ff (track 0x4779f0, object sprites 0x47eaa0). DrawOTag starts
  at entry 0x1ff (the farthest), so this is a **painter's algorithm, far to
  near**, with 32-unit buckets near the camera and coarser ones further away.
  The track's overlay (decal) layer goes in `idx + 0x50` (drawn earlier). The
  sky and clouds use entries 0x1fe / 0x1ff (0x6cdd08 / 0x6cdd0c).
* **Z-buffer mode** (`0x6cce84`, set with `0x6cce80` by 0x428270 when
  `0x6cce7c` says a z-buffer exists; 0x428270 is called from 0x435aae). The
  world drawers then insert into `0x6cd518 + tpage*8`, i.e. OT entry
  `2 + 2*(tpage & 0x3f)`: **sorted by texture**, relying on the z-buffer. The
  sky uses the buckets for pages 0x3c / 0x3d (0x6cd6f8 / 0x6cd700); 0x490430
  uses entry 1 (0x6cd514 / 0x6cdd84). Unsure which other drawers keep depth
  buckets in this mode.
* The view structs 0x6cd4a0 and 0x6cdd10 (current one at `0x6ce60c`; a 0x5c
  copy at 0x4bde98) have their OT at +0x70 (0x6cd510 / 0x6cdd80). Several
  per-primitive buffers come in two copies selected by `0x6ce60c` (track
  records +0 / +0x84, object packet lists +0x14 / +0x18, visibility lists).
  This looks like PS1 double buffering left in place: on PC each view is
  flushed before the next is built.

### 2.2 Packet header (sure)

```
+0  u32  tag: next pointer once inserted (setup writes size<<24 | link)
+4  u8 r, g, b, u8 code      PS1 order: 0x00BBGGRR | code<<24
```

The back end dispatches on `code >> 2` through the jump table at
0x4217a4/0x4217dc. The low two code bits work as on the PS1:
**bit 1 = semi-transparent** and bit 0 = "raw texture". Together with the
packet's texture page word they select the blend mode (section 3.3).

### 2.3 The 3D vertex record (0x18 bytes) (sure)

Every 3D packet stores its corners as this record. The vertex transform
functions (section 5) produce it, and the back end and the clipper read it.

```
+0x00 u32   clip/outcode flags
              1 x < left, 2 x >= right, 4 y < top, 8 y >= bottom  (viewport 0x6ccd64/68/6c/70)
              0x10 z < near (0x6ccd04, 2.0)   0x20 z >= far (0x6ccd08)
              0x40 = force the screen-space clipper; 0x80 = backface test needed after clipping
+0x04 f32   z: camera-space depth (view units)
+0x08 f32   sx: screen x in pixels (float, already offset by the projection centre 0x6ccd5c)
+0x0c f32   sy: screen y in pixels
+0x10 f32   x: camera-space x
+0x14 f32   y: camera-space y
```

`sx = x * (H / z) + cx`, `sy = y * (H / z) + cy` (0x428900, 0x426b00,
0x4268c0). sx/sy are only valid when flag 0x10 is clear. **x, y, z are the
unprojected camera-space position**, so a packet can be re-projected at any
resolution.

### 2.4 3D packet formats (float vertices; sure unless marked)

Offsets are from the packet start. "v" = one 0x18-byte vertex record. "uv"
= u8 u, u8 v texel coordinates (0..255). "tpage" = u16: bits 0-5 texture
page id, bits 6-7 blend (abr), more bits unused here. "a" = the byte that
replaces the code byte as the D3D vertex alpha.

| code (>>2) | kind | size | layout |
|---|---|---|---|
| 0x8c-0x8f (0x23) | textured gouraud quad (GT4) | 0x84 | +4 rgb0, +8 v0, +0x20 uv0, +0x22 a (PS1 clut slot), +0x24 rgb1, +0x28 v1, +0x40 uv1, +0x42 tpage, +0x44 rgb2, +0x48 v2, +0x60 uv2, +0x64 rgb3, +0x68 v3, +0x80 uv3 |
| 0x9c-0x9f (0x27) | textured gouraud triangle (GT3) | 0x64 | as GT4 without corner 3 (+0x22 a, +0x42 tpage) |
| 0x98-0x9b (0x26) | textured flat quad (FT4) | 0x78 | +4 rgb (all corners), +8 v0, +0x20 uv0, +0x22 a, +0x24 v1, +0x3c uv1, +0x3e tpage, +0x40 v2, +0x58 uv2, +0x5c v3, +0x74 uv3 |
| 0x94-0x97 (0x25) | textured flat triangle (FT3) | 0x60 | as FT4 without corner 3; vertex alpha forced to 0xff |
| 0xa8-0xab (0x2a) | untextured flat quad (F4) | 0x68 | +4 rgb, +8 v0, +0x20 v1, +0x38 v2, +0x50 v3; alpha = r\|g\|b |
| 0xac-0xaf (0x2b) | line (G2) as a thin quad | 0x3c | +4 rgb0, +8 v0, +0x20 rgb1, +0x24 v1; alpha = (r+g+b)/2 * 10/16; width `0x6cd41c` (= W*k/640, at least 0.6, set in 0x426090) widened x1.4 along the minor axis; only drawn if neither end is clip-flagged (no clipping) |

Corner order is the PS1 one (0,1,2,3 = TL, TR, BL, BR). Quads become the
triangles (0,1,2) and (1,3,2): index list 0x4a80b0 = {0,1,2,1,3,2}, and the
clip path does the same through 0x418260.

### 2.5 2D / state packets (integer screen coordinates, PS1 layouts)

These use s16 x, y in **viewport pixels** (offset by 0x6ccd54/58) and are
always clipped in screen space (0x418170 -> 0x418290). They come from the HUD,
front end and 2D effects (b).

| code (>>2) | kind | layout |
|---|---|---|
| 0x2c-0x2f (0x0b) | POLY_FT4 | +4 rgb, +8 xy0, +0xc uv0, +0x10 xy1, +0x14 uv1, +0x16 tpage, +0x18 xy2, +0x1c uv2, +0x20 xy3, +0x24 uv3 (alpha 0xff) |
| 0x30-0x33 (0x0c) | POLY_G3 | +4 rgb0, +8 xy0, +0xc rgb1, +0x10 xy1, +0x14 rgb2, +0x18 xy2; untextured |
| 0x64-0x67 (0x19) | SPRT | +4 rgb, +8 xy, +0xc u, v, +0x10 w, h; texture page = the current one |
| 0x44-0x47 (0x11) | SPRT with its own page | as SPRT, +0xe tpage byte (sets the mode first) |
| 0xa0 (0x28) | "fog marker" | +4 = 0/1/2: writes 0x6cce4c / 0x6ccdb0, which nothing reads in this exe (no effect) |
| 0xb0 (0x2c) | filter | +4 != 0: bilinear (0x41dd90), 0: point (0x41ddd0) |
| 0xb4 (0x2d) | DR_TPAGE | +4 tpage byte: sets the texture/blend mode (built by packet_tpage_set 0x427ad0) |

### 2.6 Where 3D packets come from

The game keeps fixed packet buffers (built at race setup, see
docs/formats.md) and fills their vertex records each frame: the track
(section 4), sky and clouds, object models (0x45a430), cars, shadows and
trails (a). Each drawer copies its 0x18-byte vertex records into the packet
(`rep movs` of 6 dwords) and inserts the packet. Nothing is converted to
integers before the back end.

---

## 3. Direct3D back end

### 3.1 Objects (sure)

| global | object |
|---|---|
| 0x4b8380 | IDirect3D2 (released in 0x41d130) |
| 0x4b8384 | IDirect3DDevice2 |
| 0x4b8388 | IDirect3DViewport2 (Clear +0x30, SetViewport +0x14, SetBackground +0x20) |
| 0x4bddcc | background material (0x41ec40: SetMaterial colour = clear colour) |
| 0x4d5f40 | IDirectDraw2 (CreateSurface +0x18, CreatePalette +0x14) |
| 0x4d5f48 / 0x4d5f4c | primary / back buffer surfaces; 0x4d5fa0 z-buffer (0x4d5f98 = present) |
| 0x4bcd80 | D3DTLVERTEX scratch buffer, 0x1800 bytes (192 vertices), count 0x4bddd8; vertex i at +0x4a807c[i] (i*0x20) |
| 0x4ba6fc + s*0x24 | texture slots, 64 (section 3.4) |

Device init and default states: 0x41db00. Shutdown: 0x41d130. Capability
probe: 0x41f720 (draws test quads with 0x41f550 and reads pixels back with
0x41f400). It records driver quirks in settings +0x98..+0x9b: +0x9a ->
`0x4b86b8` (snap screen x/y to integers before every draw, 0x420600),
+0x9b -> `0x6cce60` (force colour-key mode), +0x99 (z-buffer broken -> it is
released, 0x6cce7c = 0). Blend capability flags: 0x41fda0 (0x4b86bc, 0x6ccd84).

### 3.2 Per-packet submission (sure)

* Fast path (no vertex has clip flags set): each corner becomes a
  `D3DTLVERTEX` (0x420a90, register arguments: edx = vertex record, eax =
  rgb|code, bl/bh = u/v):
  `sx, sy` copied; `sz = z * 2^-16`; `rhw = 1/z`;
  `color = A<<24 | R<<16 | G<<8 | B` (R/B swapped from the PS1 order, A = the
  packet's code byte or its replacement "a");
  `tu = utable[u]`, `tv = vtable[v]` (per-page float tables, section 3.4);
  **specular is never written**.
  Then `DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, buf, 3, 0x1c)`
  (0x4206a0) or `DrawIndexedPrimitive(TRIANGLELIST, TLVERTEX, buf, 4,
  {0,1,2,1,3,2}, 6, 0x1c)` (0x420720). The flags 0x1c are
  DONOTCLIP | DONOTUPDATEEXTENTS | DONOTLIGHT.
* Clip path: the corners go into a 0x30-byte clip vertex (0x4180c0): the
  0x18 record + `+0x18 f32 tu, +0x1c f32 tv, +0x20..+0x2c f32 r, g, b, a`, in
  the pool 0x6cce98 with pointer list 0x6cd25c and count 0x6cd258. Then
  0x417af0 (gouraud) or 0x417ce0 (flat) does Sutherland-Hodgman clipping in
  camera space against the 4 frustum side planes 0x4b7fa8 (4 x {d, nx, ny,
  nz}, built by 0x417ed0 from H and the viewport size; flag masks 0x4a7cf0 =
  {0x11, 0x12, 0x14, 0x18}), the far plane (0x417600 / 0x417950) and the near
  plane z = 2.0 (0x417450 / 0x4177b0). It re-projects (0x428900) and backface
  tests (2D cross product 0x4278f0, sign flipped when the mirror flag 0x6cce94
  is set). Viewports that go past the screen (0x4b7fec = 0x40) use the
  screen-space clipper 0x418290 instead. The result is drawn as a
  **TRIANGLEFAN** (0x420bb0 -> 0x420b10 per vertex -> 0x4206e0,
  DrawPrimitive type 6). There, colour alpha = 0xff unless 0x6ccd84 & 1, in
  which case it is the interpolated alpha.
* No batching: one DrawPrimitive per triangle or fan. The track alone is
  several thousand calls per view.

### 3.3 Render states and blend modes

Defaults (0x41db00; D3DRENDERSTATETYPE numbers): FILLMODE 0x4bcda8, SHADEMODE
0x4bcda4 (2 = Gouraud), DITHER 0x6cce8c, ANTIALIAS 0x6cce88, CULLMODE
0x4bcdbc (1 = NONE; culling is done in software), TEXTUREPERSPECTIVE on,
TEXTUREMAPBLEND MODULATEALPHA (4; MODULATE (2) when caps flag 0x4b8618 & 2),
TEXTUREADDRESS CLAMP, WRAPU/V off, ZFUNC LESSEQUAL, SUBPIXEL on, SPECULAR off,
FOGENABLE `0x4bcdc0`, FOGCOLOR 0x4bcdac (0x808080 or set by 0x41de10),
FOGTABLEMODE NONE, COLORKEYENABLE off, ALPHATEST off, ALPHABLENDENABLE off,
STIPPLEDALPHA off. Z: 0x41de80 sets ZENABLE, ZWRITEENABLE and LESSEQUAL;
0x41dec0 turns them off.

The mode word is cached in 0x6cce5c and applied by 0x420a30. Its argument is
in ebx: bl = tpage byte, bh = the packet code.

* `page = mode & 0x3f` -> slot `0x6cd300[page]` -> SetRenderState(TEXTUREHANDLE,
  slot handle) and the u/v tables 0x6ce660 / 0x6ce65c (0x4209c0). 0x3f = no
  texture (handle 0).
* `abr = (tpage >> 6) & 3 | 0x6cce60`, `tm = code & 3`. Applied by 0x420770:
  * abr bit 1 selects the SRCBLEND/DESTBLEND pair: 0x4bcd8c/0x4bcd90 (abr 0/1)
    or 0x4bcd94/0x4bcd98 (abr 2/3). 0x41fda0 fills the pairs from caps, for
    example 2/2 and 1/4 (ONE+ONE additive, ZERO+SRCCOLOR modulate), or 5/6 and
    6/5 (SRCALPHA/INVSRCALPHA and the reverse).
  * tm 0: opaque; COLORKEYENABLE = abr bit 0 (colour 0 transparent), always on
    when caps flag 0x4b86bc & 1.
  * tm 1: COLORKEYENABLE on, opaque.
  * tm 2/3 (semi-transparent): ALPHABLENDENABLE on (or STIPPLEDALPHA on
    stipple-only cards, 0x4b86bc & 8).
  * In z-buffer mode (0x6cce80) ZWRITEENABLE is on only for tm 0 and off for
    colour-keyed and blended primitives.

  In short: the four PS1 semi-transparency modes become two D3D blend pairs
  chosen by capability, plus colour-key transparency for texel 0. Vertex alpha
  is the packet's code byte, or the packet's +0x22 byte for GT3/GT4/FT4
  (game-set alpha).

### 3.4 Textures (sure unless marked)

* Upload: `texture_upload` 0x41e710(page, w, h, pixels, palette, flags),
  called by track_textures_load 0x428130 (setup_s.c) for every page of the
  track texture file (`.BT6`/`.BTP`). Page records there are 0xc bytes: +0 u16
  w, +2 u16 h, +4 u16 palette index (x0x400 bytes into the file's palette
  block), +8 pixel offset. It creates a system-memory DirectDraw surface
  (DDSD 0x21007, caps 0x401808, pixel format template 0x4bcce8, 16-bit). The
  8-bit pixels are converted through the 256-entry 16-bit LUT 0x4bcd7c built
  from the palette (0x41e1d0; flags & 1 = colour key / transparent texel 0).
  When 0x4bcd54 is set it uses a real 8-bit palettised surface
  (CreatePalette/SetPalette) instead, and flags & 2 means 16-bit source
  pixels (0x41e440). It then makes the video-memory texture surface,
  `IDirect3DTexture2::Load`, and `GetHandle` (0x41d940).
  (Unsure: track_textures_load advances the pixel offset by w*h*2 although
  the 8-bit path reads w*h bytes; check against the file.)
* Slot record 0x4ba6fc + s*0x24 (64 slots, 0x3f = none): +0 D3DTEXTUREHANDLE,
  +4 system surface, +8 video surface, +0xc IDirect3DTexture2, +0x10 width
  (power of 2), +0x14 height, +0x18 page id, +0x1c u table, +0x20 v table.
  `0x6cd300[page] = slot`. formats.md's "0x4ba700, +0 used, +c width, +10
  height, +14 page" is the same table based 4 bytes later.
* **Texture coordinates are 8-bit texel units (0..255) within a page of at
  most 256x256.** The float tables (0x41cfd0, 9 sizes 1..256 at
  0x4bdddc[log2]) map texel i to `tu = i*s + o` with `o = 1/(2.1*size)` and
  `s = 1/(size - o)` (as coded). So texel coordinates address texel corners
  with roughly a half-texel bias. A new renderer can use
  `(i + 0.5) / size`, or reproduce this.
* Palettes stay in RAM after loading: the track palette is track info +0x4c
  (0x400 entries x 4 bytes r, g, b, flags), also at 0x6c1e20 / 0x6bfaf0. The
  texture palettes live in the texture file block (+0x3c). Palette animation
  re-uploads pages through the queue 0x4bde04 / 0x4b836c, drained by 0x429070
  each present.
* Texture records (what packets copy UVs and tpage from): `*0x6c1e14`, 16
  bytes: u0, v0, clut(u16), u1, v1, tpage(u16), u2, v2, -, -, u3, v3 (see
  docs/formats.md, setup_u.c section).

### 3.5 Sorting and depth

* Default: painter's algorithm through the 512-bucket OT (2.1). No z-buffer
  is needed, but one is still cleared and used when present. `sz = z/65536`
  is a linear depth, with far = 65535 giving about 1.0. `rhw = 1/z`.
* Z-buffer mode: buckets by texture page (2.1), with ZWRITE off for
  colour-keyed and translucent primitives.
* Backface culling is done in software, on screen-space winding (0x4278f0)
  and only for polygons with no near-clipped corner. D3D culling is off.

### 3.6 Fog

D3D fog is set up as vertex fog (FOGTABLEMODE NONE, so the fog factor would
be the specular alpha). But **FOGENABLE takes `*0x4bcdc0`, which nothing in
the exe writes (BSS, so 0)**, and the vertex specular is never written. The
fog parameters 0x4283d0(near 0x96, far 400, 0x1fb) -> 0x6ccd98..0x6ccdac and
the 0xa0 marker packets (0x479f60) set variables nothing reads. **Conclusion
(fairly sure): this build draws no distance fog.** Distance hiding comes from
the far plane, the PVS distances (4.3) and the sky colour matching the clear
colour (the track info +0x30 colour, doubled, through 0x428390 -> 0x41ec40
background material). Worth confirming on screen.

---

## 4. Track and world data

### 4.1 Load path

`track_load` 0x43fa60 (src/game/gl_gb.c) is called from race setup
(setup_s.c ~line 780) with the track record 0x6bfac0 (`*0x6c1e30 = 0x6bfac0`).

* Track file (`.BL6`/`.BLP`) -> track info `*0x6c1eb0`, relative offsets made
  absolute (docs/formats.md, setup_s.c section).
* Render records: `n = info+8`; `n * 0x120` bytes -> `track+4` (0x6bfac4).
  Each record holds **two GT4 packets (code 0x8c, 0x84 bytes) at +0 and
  +0x84, one per view slot**. Colours start at 0xffffff and UVs/tpage come
  from the texture record (gl_gb.c 1243-1296: +0x42 tpage | 0x40, +0x22 clut
  & 0x7fff).
* Texture file (`.BT6`/`.BTP`) -> pages uploaded (3.4), sky packets
  (0x404550 sky_packets_build), objects.
* Track record fields used by the drawers (gl_gb.c 1301-1342): +0
  (0x6bfac0) section pointer list, +4 render records, +8 info, +0xc
  (0x6bfacc) **PVS lists** (info+0x20), +0x24 (0x6bfae4) cell object index
  list, +0x2c (0x6bfaec) fixed lights, +0x30 (0x6bfaf0) palette, +0x38
  (0x6bfaf8) **cell table** (0xc bytes per cell), +0x98/+0x9c (0x6bfb58 /
  0x6bfb5c) polygon lists for lit models.

### 4.2 Section (mesh piece) layout (`(*0x6bfac0)[i]`)

From 0x4779f0, 0x47a1d0, 0x47a500 and 0x47c730. Fairly sure for LOD 1; the
LOD n>1 tables are inferred.

```
+0x06 u8    flags (0x40 water direction; see formats.md)
+0x08 u32   width in quads (a row has width+1 vertices)
+0x0c u32   rows
+0x20 ptr   LOD1 vertex grid: (rows+1) x (width+1) x 8 bytes: s16 x, s16 y, s16 z,
            u16 colour (bits 6-15 = palette index, bits 0-5 other; bit 2 of the dword+4
            (0x40000) = quad diagonal flip, read per vertex)
+0x24+4n    LOD n quad lists (n = 2..4)
+0x20+4n    LOD n vertex-step tables: u16 count, then u16 byte steps between used vertices
+0x28       LOD1 (u16 list used for the T-junction fix at LOD borders)
+0x34+4n    LOD n: packed 4-bit flags per quad (bit 0 overlay, bit 2 flip)
+0x48 ptr   per-row index into the cell list 0x6bfae4 (u16 per row)
+0x4c+4n    LOD n row bound records (0x1c bytes, n >= 2)
+0x4c ptr   LOD1 row bound records, 0x1c bytes: +2 s16 radius, +4.. 4 x dword (s16 x,y), +0x14 4 x u16 z
+0x50 ptr   LOD1 quad list, 8 bytes per quad: +0 u16 render record index, +4 u32 texture
            record index (x16 into *0x6c1e14; bit 31 = has an overlay layer)
```

The track surface is a set of **regular height-field grids ("sections")**
with int16 vertices in world units, per-vertex palette colours and
per-quad texture records. Per frame only colours and vertex records change.

### 4.3 Visibility and LOD (sure for the flow)

* `0x47a030(cam_cell_word)`: visible-run lists are double-buffered per view
  (0x5d9454..0x5d9460).
* `0x47a0d0`: the PVS for the camera's cell is `*0x6bfacc + (cell & 0xffff)*2`.
  Entries: u16 (bits 0-10 first row, 11-15 byte count), u8 (bit 0 = last
  entry, bits 1-7 section), then bytes of 6-bit row-run length plus a 2-bit
  LOD (1..4); level-n rows are 2^(n-1) full rows. (Corrected from the
  disassembly: Ghidra's decompilation swaps section and first row.) The run
  builder is `0x47a1d0(list, section, first_row, length, lod)`. LOD n is skipped when its distance (700 / 1500 / 2500 / 7000 world
  units) is beyond the far plane.
* `0x47a1d0`: binary search over the run's rows. Each row's bound record is
  frustum-tested by `0x47a440`: 4 points through the vertex transform; a row
  fails when all z < -2*radius. Emits visible runs of 8 bytes: u16 section,
  u16 first row, u16 row count, u16 LOD.
* `0x47a500`: compares with last frame's list and writes texture words into
  the render records of rows that just became visible (UV/tpage only; overlay
  records get code 0x8c and colour 0x808080).
* `0x47c420`: per visible row, marks rows that need dynamic lighting (lights
  linked to cells through the light records at 0x5d8000, the tables
  0x5d7540 / 0x5d91c0 / 0x5d71e0).
* `0x47c730`: **vertex lighting**. For each visible row vertex: colour =
  `palette[vertex.colour >> 6]`. The palette comes from 0x486350: the track
  palette scaled by the lightning/darkness factor 0x486240, cached at
  *0x4d58fc. Up to 3 point lights per row are added (light record 0x40 bytes
  at 0x5d8000 + id*0x40; position +0..+0xb, colour +0xc, falloff shift +0x10;
  falloff table *0x4d4b9c, 0x400 entries). This goes through the GTE colour
  emulation 0x427100 (x87) / 0x427250 (3DNow!) with LLM 0x6ccc20 and BK
  0x6ccc14. The colours land in the GT4 packets' +4 / +0x24 / +0x44 / +0x64.

### 4.4 Track drawing 0x477940 -> 0x4779f0(section, first_row, rows, lod, seam_flags) (sure)

1. For each grid vertex of the run: V0 (0x6ccc64/66/68) = s16 x, y, z, then
   `(*0x6ccd78)(rec)` and `(*0x6ccd80)()`. This is the transform to camera
   space and the perspective (section 5), into a ring of 0x18-byte vertex
   records on the stack.
2. Seams: where a neighbouring run uses a coarser LOD (seam_flags bits 0/1),
   edge midpoints are replaced by the average of their neighbours and nudged
   +-0.5 pixel (T-junction crack fix, in screen space).
3. Per quad: trivial reject if all 4 outcodes share a bit. The diagonal is
   chosen by the flip bit. Two 2D cross products (backface), skipped when a
   corner is near-clipped (it then sets 0x80). The quad is dropped when both
   halves face away.
4. Bucket from max z (2.1). Copies the 4 vertex records into the record's
   packet (+8/+0x28/+0x48/+0x68), code 0x8c. If the quad has an overlay (flag
   bit 0), the next record's packet (+0x120) gets the same vertices, code 0x8c
   | 2 (semi-transparent), the base colours, and is inserted at bucket+0x50.
   When race byte +0x579 == 0x0c (meaning unknown), an overlaid quad's base
   gets code bit 0 (colour-keyed) and, in z-buffer mode, the bucket for page
   0x3f.
5. Insert into the OT (0x6cd510 + idx*4, or the texture bucket in z mode).

### 4.5 Sky and clouds

* `sky_set` 0x404480: sky record `*0x6d0a40` (from info+0x60). +2.. ring
  profile, 4 x s16 per ring point; +4 height; +0xd0..+0xd6 sun/moon position
  data. 0x4b5fac = height/4 as a float.
* `0x4049b0(cam)`: a dome of 32 segments x 3 rings centred on the camera's
  integer position (cam +0x34/+0x38/+0x3c >> 16). The profile is scaled by
  the FOV factor 0x6cd400 (0x404980). Vertices go through the same transform
  as the track, with far = 65536. Colours: horizon = track info +0x30..+0x32
  (the clear colour) and top = 0x4b5fd0, both scaled by the darkness factor
  0x486240. Packets: 32 quad pairs of 0x78 (FT4, code 0x98) at *0x4b5fb0
  (0xf0 apart) and 32 FT3 0x60 caps at *0x4b5fbc. 0x404e50 / 0x404ee0 insert
  them at OT 0x1fe / 0x1ff. The sun or moon flare is registered with 0x441d80
  (a) unless the camera is inside a "no sky" section (flags +4 & 8), there is
  weather, or info +4 & 4.
* `0x404f50`: cloud quads, `*0x6d0a00` of them (config +0x33 halves or drops
  them). Vertices are 4 x 8 bytes at *0x6d0a38 (s16 x, y, z). Packets FT4
  0x78 at *0x4b5fcc (aliased at 0x4b5fc0) go to OT 0x1fe. Skipped when
  0x6ccd84 & 4.
* Backdrop quads (`*0x6d0a78`, vertices *0x6d0a3c, packets *0x4b5fc8, built
  by 0x404550): **no drawer reads them in this exe** (unsure; maybe reached
  through a pointer copy 0x4b5fd8).

### 4.6 Scenery, objects and lights in visible cells

* `0x47dd70(list)`: for each visible run, the per-row cell indices (section
  +0x48) give cell entries `*0x6bfaf8 + cell*0xc`; +8 dword: high 16 bits =
  grid object chain (records via grid_record 0x4613e0), low 16 = static object
  list.
  * Dynamic objects by type: 2 sprite marker (0x47e1b0), 5 (0x47e020), 0xd
    light sprite (0x47e220), 0xe (0x47e2a0), 0x17/0x1b/0x1c/0x1e (0x47e340).
    Types 8/9/0x22 (light glows) are deferred to 0x5d6600 and drawn by 0x47eaa0
    as screen-aligned sprites sized by 1/z, with an overdraw budget that
    thins them (settings +0x68) (a).
  * Static lists: u16 into 0x5d7040 (0-terminated), with the row's light word
    in 0x5d6ac0.
* `0x459b20`: walks those cells' object chains. Objects with flag 0x400 go to
  0x45a260 (type 3 = cars -> list 0x6ce640): 0x45a2a0 (depth of the model
  centre) -> 0x45b400 (lighting from the cell's 3 lights, 0x459cb0 -> GTE
  light matrix) -> **0x45a430 model draw** (3 LODs at 6000/15000 x FOV factor
  0x6cd404, doubled by settings +0x31; packets per instance at object +0x14/+0x18
  (+0x1c/+0x20, +0x14c/+0x150 for LODs) per view; vertices through the same
  0x6ccd78/0x6ccd80). Models with +0x48 use 0x45ab20. Model data layouts are
  in formats.md. The draw code is shared with cars (a).
* Water: no separate water drawer was found in draw_view. Water is part of
  the section grids (bands of rows animated per frame by the ripple code,
  formats.md setup_w.c; vertex y and palette entries 0x3e0..0x3ff change).
  Unsure whether any packet gets a semi-transparent code for water; check the
  texture records' tpage abr bits.

---

## 5. Transform and projection (precision and screen assumptions)

### 5.1 GTE emulation registers (sure)

| address | PS1 GTE | notes |
|---|---|---|
| 0x6ccbe0..0x6ccbf0 | RT (3x3 s16, 4.12) | float copies x1/4096 at 0x6ccd1c..0x6ccd44 (0x426330) |
| 0x6ccbf4..0x6ccbfc | TR (3 x s32) | float copies 0x6ccd0c..0x6ccd14 (0x426530) |
| 0x6ccc64/66/68 | V0 (s16 x, y, z) | input of every 3D vertex |
| 0x6ccc88/8c/90 | IR1-3 | |
| 0x6cccc8..d0 | MAC1-3 | |
| 0x6cccac/b0 | SZ | |
| 0x6ccc9c | SXY (s16, clamped -1024..1023) | old integer RTPS 0x4266d0, still used by some 2D code |
| 0x6ccc40/44 | OFX/OFY (16.16) | 0x426200 |
| 0x6ccd00 | H (float) | 0x426260 |
| 0x6ccc20.. / 0x6ccc14.. / 0x6ccc7c | LLM / BK / RGB | 0x428940, 0x427100 |

`0x423df0(m)` = SetRotMatrix + SetTransMatrix of a 0x20-byte matrix (9 s16,
pad, 3 s32 at +0x14). Camera: 0x6ce5d0 (built by 0x423d70). Objects compose
their own matrix with the camera's (0x426e90 MulMatrix, 0x426e10 ApplyMatrix)
and load it the same way.

### 5.2 The vertex function pointers (sure)

Set by 0x426090 (at renderer init), selected by CPU (0x401110 = *0x4b5e44,
3DNow! present):

| | x87 | 3DNow! |
|---|---|---|
| `*0x6ccd78` transform | 0x426a20: rec.x, y, z = RTfloat * V0 + TRfloat; flags 0x10/0x20; returns H/z on the FPU stack | 0x4268c0: transform **and** project in one (PFRCP approximation) |
| `*0x6ccd80` project | 0x426b00: if not near-clipped, sx, sy = x*(H/z)+cx, y*(H/z)+cy and outcodes vs the viewport | 0x4291f0 (empty `ret`) |
| `*0x6ccd7c` | 0x426be0 (transform only) | 0x4268c0 |
| `*0x6cce3c` lighting | 0x427400 | 0x427250 |

**Every 3D vertex in the game (track, sky, clouds, objects, cars) goes
through `*0x6ccd78` then `*0x6ccd80`**, with V0 = model-space s16 and RT/TR =
the current model-to-camera matrix.

### 5.3 Precision

* Input positions are **int16 world (or model) units**. The world fixed point
  is 16.16 (object positions); the track and the camera translation use only
  the integer part. **The camera translation drops the fraction**
  (0x423ce0: `-pos >> 16`), so the camera snaps to whole world units. That is
  invisible at 640x480 but may show as sub-pixel swimming at 4K. A new
  renderer can use the full 16.16 camera position (cam object +0x34..+0x3c).
* Rotation is 4.12 fixed (s16), converted to float. The rest of the pipeline
  (camera space, projection, clipping, screen coordinates) is **float**. sx/sy
  are not rounded unless the driver-quirk flag 0x4b86b8 is set. The old
  integer GTE path (SXY clamp +-1024) is not used by 3D packets.
* Depth: near = 2.0, far = 65535 (0x426090), or 65536 for the sky. Bucket
  size 32 units near the camera.

### 5.4 Screen and FOV assumptions

* Screen size `0x4acc38` / `0x4acc3c` = 640 / 480 by default. It is written
  by the display-mode code (0x4359f1, 0x4370b2, 0x4745f0, 0x487e10,
  0x4885d0), so other resolutions exist (b). The view rect comes from the
  car slot (+8..+0x14) and race +0xa80 / +0xa82 (h, w).
* **H = (screen_W / 320) * fov** (0x426260; fov is the PS1-style s16 set by
  0x423f00, e.g. 0x68 for the mirror; 0x6cd400 = 0x6800/fov and 0x6cd404 =
  fov*256/0x68 are the FOV-relative scale factors used for LOD and sky). H
  follows the **width**, so the PS1 horizontal FOV is kept and the vertical
  FOV comes from the 4:3 height. On a wider internal screen the vertical FOV
  would shrink (Vert-). For ultrawide, H should be derived from the height
  (`H = (screen_H / 240) * fov`) to get Hor+.
* Frustum side planes (0x417ed0) are built from H and the view w, h. Outcodes
  use the float viewport rect 0x6ccd64..0x6ccd70 (0x41f2d0). Line width
  0x6cd41c scales with W/640.
* 2D packets are in viewport pixels of the 640x480 (or current mode) screen.

---

## 6. Interception recommendation

### 6.1 Candidates

| level | where | gives | cost / problems |
|---|---|---|---|
| A. D3D calls | 0x4206a0 / 0x4206e0 / 0x420720 | TL vertices in pixels | resolution-bound; useless for hi-res |
| **B. Packet interpreter** | **DrawOTag 0x421860 / per packet 0x420be0** | every primitive, in draw order, with camera-space float x, y, z per corner plus texel UVs, tpage, blend mode and colours | 2D packets are in pixels (scale them); culling already done for the 4:3 frustum |
| C. Vertex transform | `*0x6ccd78` / `*0x6ccd80` and the matrix loads 0x426330 / 0x426530 | model-space s16 vertices and the model-to-camera matrix | vertices are not tied to primitives; you would need every drawer's connectivity |
| D. Drawer level | replace 0x47a030 / 0x477940 (track), 0x4049b0 / 0x404f50 (sky), 0x45a430 (models) | world data: section grids, texture records, palette, model meshes, camera | most work; must reproduce lighting, LOD, seams and animation, or do them better |

### 6.2 Recommendation

**Intercept at the ordering-table walk (B): replace `DrawOTag` 0x421860
(and with it the whole back end 0x420be0 and below) by a Vulkan back end.
Read the camera-space `x, y, z` (vertex +0x10, +0x14, +0x04) of the float
packets and project them yourself.** Reasons:

1. All the game's 3D content (track, sky, objects, cars, effects) arrives
   there as already-built, already-lit primitives, in the same 12 packet
   formats. One decoder covers everything, and the "look and feel" (colours,
   blend modes, overlay layers, sprite sizes, draw order) is preserved by
   construction.
2. The coordinates are **unprojected floats in camera space**, so the new
   renderer can use any projection, resolution and aspect, and a real
   z-buffer instead of the 512 buckets. To go back to world space, multiply
   by the inverse of the camera matrix at 0x6ce5d0 / 0x6ce5a0 + 0x6ce5c0. The
   original screen x/y in the same record are available for checking.
3. Textures are already in RAM in their original 8-bit + palette form (3.4).
   Upload pages once with UVs as `u/size`, and drop the D3D-era quirks
   (colour-key emulation, stipple, the half-texel tables).
4. The sort order is still available (bucket index = OT position), so
   translucent primitives can be drawn back to front as the original does,
   and opaque ones can use depth testing.

Things that must change for higher resolution and **ultrawide**:

* **Culling is done for the game's viewport before packets reach the OT**:
  the track drawer drops quads whose outcodes share a bit (0x4779f0), the PVS
  row test (0x47a440) and the sky (0x404e50/ee0) do the same, and objects use
  the projected depth. To see more horizontally, the game's own view must be
  wider: set the internal screen/viewport (0x4acc38/3c, view rects,
  0x6ccd64..70, frustum planes 0x417ed0) to the target aspect, and make H
  follow the height (patch 0x426260, section 5.4). Because the projection is
  float, raising the internal size to the real resolution should "just work"
  for 3D. Check the 2D code that uses W/H (HUD (b), lines 0x6cd41c).
* Treat the 2D packet formats (0x2c, 0x30, 0x44, 0x64) as a separate 2D layer
  in 640x480 virtual pixels, scaled and positioned by the new renderer (b).
* The screen-space T-junction nudges (4.4 step 2) and the +-0.5 offsets are
  in pixels. At high resolution the seam fix still works but is
  sub-pixel-tuned for 640. Re-deriving the seam vertices in camera space
  would be cleaner, which leads to stage 2.

**Stage 2 (optional, for a native build):** replace the track drawer (D:
0x47a030 visibility, 0x47c730 lighting, 0x4779f0 drawing) with a GPU path
that uploads each section grid once as a static vertex/index buffer (int16
positions, per-quad texture record -> page + UVs), and updates only the
per-vertex colours each frame (from the 0x47c730 logic). The per-cell PVS
(0x6bfacc) can stay as the culling input. With a real frustum the
ultrawide culling problem disappears for the track. Objects and cars can
follow later through the model data (formats.md).

---

## 7. Open questions

1. Fog: is `*0x4bcdc0` really never set (no indirect write)? Confirm on
   screen that there is no distance fog in the D3D build.
2. Z-buffer mode (0x6cce84): when is it enabled in practice (0x435aae ->
   0x428270, depends on 0x6cce7c and the 0x41f720 probe)? Do all drawers use
   the per-texture buckets in that mode, or only some?
3. The texture pixel stride in track_textures_load (`w*h*2` per page) versus
   the 8-bit upload path. Are track pages 8-bit with 16-bit spacing, or
   16-bit (flags & 2)?
4. LOD n>1 section tables (+0x20+4n step tables, +0x24+4n quad lists,
   +0x34+4n flag nibbles, +0x4c+4n row bounds) are inferred from one reading
   of 0x4779f0 / 0x47a500. Verify against a real track file.
5. The overlay layer uses the *next* render record (+0x120) and the *next*
   texture record (+0x10). Confirm that the loader reserves those records (n
   counts them) and that the record +0x84 copy is the view-1 version.
6. Backdrop quads (*0x6d0a78 / *0x4b5fc8) seem never drawn.
7. 0x417ce0 vs 0x417af0: same algorithm with different sub-clippers
   (0x417270/0x417950/0x4177b0 vs 0x417060/0x417600/0x417450). Probably no
   colour interpolation for flat packets; not checked.
8. Water: confirm that water is only animated section grid rows (no extra
   transparent layer), and how reflections (settings +0x2b "water
   reflections") are drawn. Not found in this survey's call tree.
9. World unit scale: the int16 track units relative to metres (needed for a
   physically sized new projection) were not established here.
