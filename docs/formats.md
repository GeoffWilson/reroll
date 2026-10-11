# Rollcage data formats

Data layouts found while reimplementing race setup (0x46a060 and its tree) and the front end, for the planned Vulkan renderer and anything else that reads the game's data. Each section comes from one group's source file; addresses are in Rollcage.exe.

## Race setup

### From src/game/setup_s.c

```
Data formats seen by group S (track loader 0x43fa60's tree and race setup 0x46a060). Offsets in hex.
"rel" = stored as an offset from the file start and turned into a pointer by the loader (+ base).

TRACK FILE (loaded by archive name = track name + 0x4af4c4/0x4af4d4; base at 0x6c1e28, header 0x6c1eb0 = same)
  +04 byte           -> track struct +0x45
  +08 int            N: count of 0x120-byte render records the loader allocates (two 0x90-byte primitives
                     each, colours preset 0x8c / 0xff, flags 0x0c)
  +10 int            section count; +14 rel -> section pointer array (each rel; section +0x50 rel, +0x28..+0x30,
                     +0x3c..+0x44, +0x54..+0x5c rel, +0x20 rel, +0x48 += header+0x3c, +0x38 += header+0x50 (rel),
                     +0x4c = (header+0xc4 rel) + n*0x1c)
  +18 rel            model pointer table, +2c u16 count; each model rel (see MODEL)
  +1c rel            words the type-0 objects' +0x20..+0x24 index (-> object +0x180..+0x188)
  +20 rel, +24 rel   +24: 256 entries of 0x10 bytes (+0 byte, +1 byte, +0xc patched to header+0x6c-2+v*2 unless
                     byte0 == 0xff && byte1 == 0)
  +28 rel
  +2e u16            first texture record of the shared sprites (race setup swaps +4/+8 of records
                     +0x92..+0x95 and +0x87..+0x8e from it: mirrored textures)
  +30..+32 bytes     a colour (x2 -> 0x6cd4b9.. and 0x6cdd29.., and 0x428390)
  +34 +38 +3c +40 +44 rel
  +4c rel            colour table, +66 u16 entries of 4 bytes (r,g,b scaled by x87 factor 0x6ccdc0 * 2, clamp 255)
  +50, +54           +54: ambient colour dword (r | g<<8 | b<<16) -> 0x428350
  +5c, +5e u16       AI route table sizes (0x4044c0: 0x78 and 0x20 bytes each)
  +60 rel, +64 u16   -> 0x404480
  +68 rel            -> track struct +0x38
  +6c +70 +74 rel
  +78 byte, +79 byte count of 0xc-byte entries at +8c (each +8 rel to +98), +7a u16
  +7c rel, +80 rel   +80: racing line nodes (0x10 bytes; +7 = speed byte, read as node*0x10+7)
  +84 rel, +88 rel   +88: dwords indexed by object record +0x14
  +90 rel, +94 byte, +95 byte, +96 u16 count (fixups at track +0x7c: values < 0x10000 are object indices)
  +9c +a0 +a4 +a8 +ac +b0 rel, +b4 byte, +b8 +bc rel
  +b6 byte           camera line count; +f0 rel: 0x10 bytes each: int16 x,y,z (+0), byte type (+6),
                     int16 x2,y2,z2 (+8); type != 1 copied into 0x6cfbe0
  +c0 rel, +c4 rel (0x1c-byte records), +c8 u16 (count of 0x20-byte runtime records), +cc rel
  +d0 rel            LIGHT TYPES, 8 bytes: +0 r, +1 g, +2 b, +3 size, +4 glow flag, +6 u16 texture record
  +d4 +d8 +e4 +e8 rel
  +ec rel            lit face list, 0x14 bytes each: +0 face pointer, +4 object (count 0x5f72a4)
  +f8 +100 +108 +10c rel
  +110 rel           TEXTURE RECORDS (0x6c1e14), 0x10 bytes: +0 dword, +4 dword, +8 u16, +a u16 = the four
                     corners' texture coordinates (copied to a sprite's vertices +0x38/+0x54/+0x70/+0x8c,
                     0x1c apart)
  +114 rel           -> 0x6c1e40
  +118 rel
  +11e int16         extra object pool entries (0x460aa0)

OBJECT/TEXTURE FILE (track name + 0x4af4bc/0x4af4cc)
  +10                -> 0x6c1e10
  +14 int            object record count; +28 rel: OBJECT RECORDS (0x38 bytes)
  +24 rel            16-bit pixel data
  +32 byte, +33 byte first and count of marked texture pages; +34 u16 page count
  +38 rel            TEXTURE PAGES, 0xc bytes: +0 u16 width, +2 u16 height, +4 u16 palette (x0x400 into +3c),
                     +8 pixel byte offset (ignored, 256x256, when the load flags have bit 1)
  +3c rel            palettes, 0x400 bytes (256 x 4)
  +40                one byte pair per render record (-1 & 0x1f -> record words)

OBJECT RECORD (0x38 bytes)
  +0 u16 & 0x3ff     type: 0 object, 2 sprite marker, 0xd light sprite, 0x18 track object (others ignored)
  +1 byte bit 2      has lit polygons (0x478f80)
  +2 byte, +3 byte   +3: model index (header +18 table)
  +6 u16             racing line node or 0xffff (type 0)
  +8 +a +c int16     position (world units; bodies keep <<16)
  +e +10 +12 int16   angles (x in sine-table units, y/z scaled by 0x6488>>9) -> quaternion 0x480a80
  +14 u16            index into header +88
  +16 byte           link to previous type-0 object (chains)
  +18 +1a u16        -> object +0x18e, +0x18c
  +1c, +1d bytes     trigger values (0x4b5e58 list); type 0: -> object +0x1a6/+0x1a7
  +1f byte           type 0x18: kind (3, 5, 8, 9, 10, 11, else); type 0: class (+0x19a: 0 static list,
                     2 blast table)
  +20..+24 u16       type 0: words via header +1c
  +26, +27 bytes     -> object +0x138/+0x13a (type 0x18); +26 -> +0x169 (type 0)
  +28 +2a +2c u16    indices into 0x6bfb58 (per-LOD polygon lists for the lit test)
  +2e 5 x u16        light words: top nibble 2 = light sprite (low 12 bits become the light number)
  type 2: sprite at +8.., texture +6, bytes +4, +5
  type 0xd: light sprite at +8.., texture +6, bytes +4, +0x1c, +0x1d (and the type's high byte), +0x1e

MODEL (pointer table at header +18; relocated per model: +18, +40, +0c, +44 relative to the model)
  +04 byte, +06 u16 polygon count, +08 flags (bit 1: lit), +0a u16 instance count, +0c dword
  +10, +14           part range (0x28-byte parts: (+14 - +10) / 0x28), relative to the model
  +1c, +1e u16       offsets of LOD 1 and LOD 2 (0 = none), same layout
  +27 byte           face count; +34 u16 offset of faces (0x14 bytes)
  +40 dword, +44, +4c u16 offset of light points (8 bytes: position, +7 light type)
  object +0x194: per-face colour words; type (bits 24-27) 4 = lit

TABLES FILLED AT SETUP
  0x6cfbe0  camera lines, 14 dwords: +0 type (1 rail), +4 start, +10 end, +1c midpoint, +28 direction (unit),
            +34 length; count 0x4b65f4, max 40. TV rails (0x40b5b0): 58 hard-coded rails, tracks 0-14.
  0x6d06e0  blast table, 0x10 bytes: int16 x,y,z, +6 +7 +8 record bytes, +9 0, +c object; count 0x6d06c0
  0x4b5e58  triggers (object, value); count 0x4b5fa0
  0x6d0a80  plain objects, 3 dwords; count 0x4b5f98
  0x5d6e00  light sprites by light number; 0x5d6400 type-2 sprite positions (max 40, count 0x4d5800)
  0x4ba700  texture slots, 64 x 0x24: +0 used, +c width, +10 height, +14 page id
  0x6ccd90  ambient colour
  object pool: 0x6b9b90 (128-aligned, (n + 85000) * 32 bytes), used 0x4d087c, objects registered in 0x4d0880
  (max 0x200, count 0x4d1088)
SPRITE OBJECT (0x120 bytes, type 0x98 at +0x1f): +0x10 int16 x,y,z, +0x16 +0x17 bytes, +0x18 primitive header,
  +0x38/+0x54/+0x70/+0x8c corner texture coordinates; light sprite: +0 = 0xd, +0x108..+0x10a colour, +0x10b 0xff,
  +0x10c glow
```

### From src/game/setup_t.c

```
Data formats seen by group T (race setup: car setup and per-race pools). Offsets in bytes.

Draw packets (kept in PlayStation ordering-table style by the PC port)
  +0  dword: low 24 bits link to the next packet, high byte = packet size in dwords after the tag
      (0x04 name panel, 0x09 letter sub-packet, 0x0c most textured quads)
  +4  colour / texture-page word; packet kind byte at +7 (0x98 smoke/shock/flare/marker quads, 0x8e shadow
      and trail quads, 0x8c skid marks, 0x9a full-screen flash, 0x46 name panel, 0x2e letter)
  Texture-page word (0x427ad0): a&0xff00 | b&0xff0000 | blend&0x3f | 0xb4000000.
  Textured quads copy a 16-byte texture entry: dword 0 and 4 (uv + clut/page) into two corners, words 8 and 0xa
  into the other two (0x20 apart in 0x78-byte quads, 0x20/0x40 apart in 0x84-byte ones).

Track texture entries: *0x6c1e14, 16 bytes each, indexed from the track info's base (*0x6c1eb0 +0x2e, u16):
  +0 u8 u0, +1 u8 v0, +6 u16 page/clut, +0xa u8 u1, +0xb u8 v1 (panel width = u1-u0+1, height = v1-v0+1).
  Indices used here: 0x14/0x15 position markers, 0x41 car shadow, 0x4d debris, 0x84 font letter, 0x8f skid marks,
  0x96 car trails, 0x99..0x9c name panels.

Palette: *0x6c1e20, 4 bytes per entry (r, g, b, a?); entries 0x3f0..0x3ff are set to a grey ramp (8..0x80, 0x3c).

Track header (*0x6c1e30)
  +0x18 model pointer array; +0x58 int16 "model slot" table indexing it (slot 1..3 bomblet sizes, 8 wheel,
        20..33 car bodies (even) and car glows (odd) by car kind)
  +0x44 u8 checkpoint count; +0x46 u8 racing line count (line 0 is skipped by the nearest-line search)
  +0x48 start positions, 8 bytes each (int16 x, y, z, pad), indexed by race +0x10 + car
  +0x60 racing lines, 0xc each: +0 u16 node count, +8 node pointer (nodes 0x10: int16 x, y, z, ...)
  +0x70 checkpoints, 0x14 each: +4/+6/+8 int16 position, +0xc/+0xe/+0x10 int16 direction
  +0x90 checkpoint order, dwords (low byte = checkpoint index)

Car kinds (race +0x1c + car, 0..6)
  0x4a7b68 / 0x4a7b78: u16 model slot of the body / glow per kind (20,22,..32 / 21,23,..33)
  0x4a7910: handling per kind, 16 bytes: extra rear drive, grip bonus, spin where drive falls off (16.16),
            weight class (players get +1). 0x4a7980 (entry 7) is used for players when race +0x587 is set.
            Values: k0 (0,-2500,0x110000,2) k1 (40000,0,0xe0000,0) k2 (-40000,0,0xe0000,1) k3 (-40000,5000,
            0xc0000,0) k4 (40000,-2500,0x70000,2) k5 (0,2500,0xa0000,1) k6 (80000,5000,0x120000,1)
  0x4b2434: u16 glow size per kind; 0x4b2480: pointer per kind to an int16 x, y, z offset added once to the
            glow model's vertices (z also -2).
  0x4a7990: the car's 4 attached points, 0x1c each: int16 x, y, z, pad, dx, dy, dz (±6912, ±8192, up 256).
  0x4a7ad8: the car's 5 collision spheres (0x419150 format).

Car object (0x2474 bytes; fields set here)
  +0x58 orientation quaternion, +0x148 callback 0x40eb60, +0x180 attached points (4 x 0x1c), +0x320 reach
  +0x634..+0x6ac race/control state, +0x650 replay record, +0x64e car index, +0x6ac animated texture record
  +0x720 shadow (list 0x4d3de4): +0x60.. two 0x84 packets, +0x97c two texture-page words
  +0x10ac env-map (chrome) overlay packet buffer (0x4920: 0x78 packets of 0x9c, second half at +0x2490; see docs/renderer/objects.md, not tyre trails), +0x1104/+0x1105 flags
  +0x1108 engine glow (0x300): +4 object, +0x2dc picked vertex count, +0x2e0 picked vertices, +0x2e4 flags,
         +0x2ec size, +0x2f4 random phase, +0x2f6 up to 8 face indices
  +0x1408 3 per-car layer records (0x38 each; see docs/renderer/objects.md), +0x14b0 pointer to them, +0x14b8/+0x14ba scales
  +0x2274..+0x2280 handling (above), +0x2290 4 trail packets (0x78), +0x2470 line-up set (mode 4)

Model objects (as 0x460ba0 makes them): +0x10 info (+6 u16 face count), +0x24 vertices (8 bytes: int16 x, y, z,
  pad), +0x28 faces (8 bytes: four 1-based corner bytes, then a dword with a signed 10-bit value), +0x10c s8 /
  +0x140 table giving the model record index (*0x6ce740, 0x48 per model, +0x40 bounding radius), +0x10d u8 vertex
  count.

Replay records (race +0x12a0 + i*0x1e884, or the car itself): +0..+4 words, +8 data, +0x4e28 write pointer,
  channels at +0x4e30 (65000-byte buffer), +0x14c24 and +0x19a50 (20000 each), +0x1e87c, +0x1e880 state
  (2 record, 5 play, 8 recorded race available).

Messages: 43 strings from the PC_IN_GAME_MESSAGES section packed into race +0xb20..+0x12a0, pointer table 0x4d1098
  (64 entries).

Pools set up per race (size; record size): smoke *0x6ccb54 (0x96dc; 5 x 32 pairs of 0x78), shock waves *0x6cc800
  (0x2d3c; 3 x 16 pairs), flares *0x4be1a0 (0x3c00; 128 x 0x78), position markers *0x4be374 (0x3c00) and *0x4be37c
  (0x440), skid marks *0x6002d4 (150 or 300 x 0x124), bomblets *0x600664 (36 x 8) with objects 0x600680, collapsing
  buildings *0x6d04a4 (4 x 0xa8, +0x8c 0x800 buffer), debris *0x5d9290 (64 x 0x180), line-up name panels
  *0x6b8cd8 (4 x 0x298, copy at *0x6b8cdc), flashes 0x6cc960/0x6cc9d8/0x6cca60/0x6ccad8.
```

### From src/game/setup_u.c

```
Data formats seen by group U (race setup) -- for the future Vulkan renderer.

Texture records (*0x6c1e14, 16 bytes each; the track's set starts at record *(u16*)(*0x6c1eb0 + 0x2e)):
  +0  u8 u0, u8 v0      corner 0 (top left)
  +2  u16 clut          (PlayStation CLUT id: palette row/column)
  +4  u8 u1, u8 v1      corner 1
  +6  u16 tpage         (texture page / blend bits)
  +8  u8 u2, u8 v2      corner 2
  +10 u8 u3, u8 v3      corner 3
  Records used relative to the track's first one: 0..15 (quartered into the 0x4b5fa8 pool), 0x2e (snatch
  dots), 0x3f (pickup sides, beams, last trail point), 0x40 (pickup tops), 0x47..0x4c (projectile trails),
  0x58 (driller part trails), 0x5c (snatch quads), 100 (snatch sparks), 0xa2 (circle for the 0x4b5fdc fan).
  The snatch texture page itself is *0x4b24a0 with (u,v) corners 0x8000/0x807f/0xff00/0xff7f (i.e. a
  128x128 image at v 0x80..0xff).

Primitive packets (PlayStation GPU style, built once at setup, filled per frame by the draw code):
  +0  u32 tag: bits 24..31 = 0x0c (words), bits 0..23 = ordering-table link
  +4  u8 r, g, b, u8 code  (0x8e, 0x94, 0x96, 0x98, 0x9a, 0xaa)
  per-vertex blocks 0x1c bytes apart; the texture words sit at +0x20 (u0,v0 + clut), +0x3c (u1,v1 +
  tpage), +0x58 (u2,v2), +0x74 (u3,v3) in the 0x78-byte quads, and +0x20/+0x3c/+0x58 in the 0x60-byte
  triangles (the 0x84-byte snatch quads are 0x20 apart: +0x20, +0x40, +0x60, +0x80, with colour dwords
  at +4, +0x24, +0x44, +0x64). The vertex positions are written by the per-frame draw code (not part
  of setup). So these are PS1 POLY_GT4 / POLY_GT3-like records; a new renderer needs only the texture
  record above plus the positions computed each frame.
  Pools: projectile trails body+0x5dc (64 x 0x78), pickups body+0x190 (2 x 0x78) and +0x280 (6 x 0x78),
  snatch body+0x6ac (60 x 0x84) and +0x259c (12 x 0x60), *0x4b5fa8 (64 x 0x78), *0x4b5fdc (32 x 0x60),
  *0x4be378 (256 x 0x68).

Model face records (model + *(model+0x10) .. + *(model+0x14), 0x28 bytes each), as copied per body
instance by 0x460dc0 into 0x84-byte records: dwords +4, +0xc, +0x14, +0x1c, +0x24 and the word at +0x1e
(the u,v / clut / tpage words of the face; positions are kept in the model's vertex list).
Model header (as used here): +4 u16 vertex count, +6 u16 face count, +0xa u16 instance count, +0xc face
list pointer (8-byte faces: four 1-based vertex numbers, then the face normal packed 10:10:10 =
x bits 20..29, y 10..19, z 0..9, signed, 1.0 = 512), +0x10/+0x14 offsets of the 0x28-byte draw faces,
+0x1c/+0x1e u16 offsets of two linked models (LODs or damage states) from this header, +0x48 vertex
normals (int16 x,y,z,pad per vertex, 1.0 = 4096, made at setup by 0x40f1e0).

World (*0x6c1e30): +0 model pointer list, +0x10 model count (each model +0xc vertex count, the vertex
buffers at 0x5d7de0 are 0x20 bytes per 16 vertices + 1 block), +0x18 model list used for bodies (indexed
by the int16 table at +0x58: +8 leader, +0xa homing missile and driller parts, +0xc pickup, +0x46 dropper),
+0x36 pickup count, +0x60 racing lines.
Track info (*0x6c1eb0): +0x2e first texture record, +0x30..0x32 background colour (RGB, x2), +0xf7 dropper
count.

Split-screen views: car slot +8 x, +0xc y, +0x10 w, +0x14 h (inner, inside a 1-pixel border when split),
+0x18..+0x24 the same grown by the border; screen size u16 0x6cd462 (w) / 0x6cd464 (h); centre 0x6cd466 /
0x6cd468; view mode race +0x552 (0 single, 1 two stacked, 2 two side by side, 3 three (top one centred),
4 four). View order from 0x45ed40 (0x6c0a40[4]).

Pads: binding records 0x6bf180 + set*0x1c0 (0x1b of 0x10 bytes, +4 == 1: bound to a button); controls 0/1
steer, 2 brake, 4 accelerate. Device records 0x4d75c0 (0x43c each, count *0x4d9554): +0 bit 8 a flag
(force feedback capable?), +4 byte 1 the device type (4 not usable).
```

### From src/game/setup_v.c

```
Data formats found by group V (race setup), for the future renderer and tools.

Track list entries (*0x6c1e14, 0x10 bytes; index base = U16 at track info *0x6c1eb0 + 0x2e)
  Texture/sprite descriptors: +0 dword, +4 dword (copied as two dwords: probably u0 v0 page / u1 v1 clut-like pairs),
  +8 u16, +0xa u16. Used entries relative to the base: +0x5a rain drop, +0x66 snow flake, +0xa0 / +0xa1 dry-weather
  screen flakes (alternating). The four fields land at sprite +0x20, +0x3c, +0x58, +0x74 (one per sprite corner, 0x1c
  apart, which is the PlayStation-style 4-vertex sprite layout: each vertex record is 0x1c bytes with its texture word at
  +0x20 + k*0x1c).

Weather sprite pools
  rain: 0xa0 records of 0x3c at *0x5d6340 (end *0x5d633c): +0 tag dword (top byte 0x0c = primitive length, low 24 bits
  link), +4 colour 0x2f0000 with +7 = 0xae (primitive code), +0x20 = 0x3f3f3f (second vertex colour).
  snow: 0xaf records of 0x78 at *0x5d6244 (end *0x5d6240): +0 tag (0x0c << 24), +4 colour 0x606060, +7 code 0x9a, uv
  words at +0x20, +0x3c, +0x58 (u16), +0x74 (u16).
  These are PlayStation GPU primitive packets (tag + code byte): 0xae / 0x9a codes; the renderer reads them as
  semi-transparent textured quads / lines.
  screen flakes: 32 records of 0xa0 at *0x4d5898: +0 and +0x98 cleared, uv words at +0x24, +0x40, +0x5c, +0x78.

Weather emitter (0xca4 bytes, *0x5d6360[i], count *0x5d6248)
  +0x10 camera object (first dword of slot +0x28 camera record), +0x14.. 400 drops of 8 bytes (+0x1a u16 cleared per
  drop), +0xc94 pointer to the drops, +0xc98 u16, +0xc9a u16 spawn rate (0x32 rain, 8 otherwise), +0xc9c u16,
  +0xca0 indoors flag (post_j), +0xca1 voice, +0xca2 u16 depth shift (3).

Special track objects (list *0x6ce738, count *0x6ce6fc; body +0 low 9 bits = kind)
  0x14 screen (has a picture), 0x19 / 0x1f / 0x23 scripted collidable objects, 0x1d four-sphere object, 0x20 spray,
  others: script only. Body +0x138 u16 script number (0 = none), +0x13a s16 script start, +0x184 active flag,
  +0x14 model pointer. Collision sphere sets: 0x4a7120 (one), 0x4a7134 (one, kind 0x23), 0x4b0d50 (four, kind 0x1d).

Screen record (0x6ce700): +0 0, +4 1, +8 body, +0xc u16 row count (3), +0x10 frame list 0x23 0x24 0x25 0xff,
  +0x14 three rows of 8 bytes (u0 v0 u0 v1? as written: x,a,x,b,y,c,y,d where x/y are the stepped heights and a..d the
  model bytes +0x122d/+0x124d/+0x126d/+0x128d), +0x34 colour 80 80 80.
  Screen model: texture coordinate bytes at model +0x122c, +0x126c, +0x12b0, +0x12f0, +0x1334, +0x1374 (row tops and
  bottoms, 0x40/0x44 apart) and +0x122d, +0x124d, +0x126d, +0x128d (the other coordinate of each corner).

Car texture ring table (0x5f72c0): 5 rings x 8 points, point (r, c) at 0x5f72c0 + r*8 + c*0x28, three s16:
  x = sin(c*45deg) * 16r / 14000 (sin table 0x4b24f8, 4096 = 1.0, 0x40 bytes per 45 degrees),
  y = cos(c*45deg) * 16r / 32768 (cos table 0x4b2578), z = 0x50 - (6r)^2 / 16.
  Per car animation records: 0x5f7400 + i*0x1088 (+0x1080 frame, +0x1084 = -1 at start; frames by post_j 0x475430).

Collision event queue (0x6b9860 header, 0x18 bytes): +0 used, +4 capacity (100), +8 entries (0x68 bytes each),
  +0xc order list (capacity + 1 dwords), +0x10 free list (entry i stored as i << 20), +0x14 last free index.
Sweep slot records: 64 x 0xc at 0x6b9880: +0 pointer to a 0x84-byte slot, +4 u16, +6 u16, +8 dword.
Slot map: 0x300 bytes at *0x6ce73c: identity map 0..255, then two zeroed 256-byte tables (*0x6ce744, *0x6ce750).

Pads: car+0x6f8, 0x28 bytes (cleared, +0x20 = 0x10), car+0x70c controller kind (10 player, 8 AI, others as given by
  race +i), car+0x71c cleared for non-players.

Freezer colour map: *0x6c3080, 0x400 u16 entries (0xffff = unused), free colours counted down from 0x3cf (0x6c3e94).

Archive names: compared case-insensitively, with '\' = '/', '_' = '.', and '__' = '/' (so "DATA__CARS_BMP" names
  "DATA/CARS.BMP").
```

### From src/game/setup_w.c

```
Group W: data formats seen in race setup (for the future renderer). Offsets in bytes.

Track (*0x6c1e30)
  +0x00  pointer to section pointer list (per section: +6 word flags, bits 0-5 = water band height in rows)
  +0x10  dword section count
  +0x18  pointer to the model pointer table (dword per model)
  +0x58  pointer to a word table; +0xe = model index of the shield mesh (+0x10 = another special model)
Section models (*0x6bfac0)[i] (the track's mesh pieces, also the "sections" of the physics)
  +0x06  byte flags (0x40: water runs the other way, the band is at the start of each row)
  +0x08  dword width in quads (row has width+1 vertices)
  +0x0c  dword rows (the low word is used as a count)
  +0x20  pointer to the vertex grid: (rows+1) x (width+1) vertices of 8 bytes:
         int16 x, int16 y, int16 z, uint16 colour word (bits 6-15 = palette index, bits 0-5 = other)
Track info (*0x6c1eb0)
  +0x2e  word: base index of the track's colour/material words (0x6c1e14 + (this+0x4e)*16: the shield
         polygons' three corner words come from there)
  +0x30  byte r, g, b: fog / clear colour (doubled into view +0x19..0x1b, 0x6cd4b9 and 0x6cdd29)
  +0x4c  pointer to the track palette (0x400 entries of r, g, b, x)
  +0xfc  byte count of rippling cells; +0x100 pointer to them: word row, byte section, byte column
Palette (*0x6bfaf0): 0x400 entries of 4 bytes (r, g, b, flags byte 3). Entries 0x3e0..0x3ff (offset 0xf80..0xfff)
  are rewritten at setup: a dark->light ramp for ripples (r=(i+8)*4, g=b=i) or a grey ramp for water
  (0x28 + 3i), entry 3 copied from the first rippling vertex's own colour.
Shield ripple mesh (a model): +4 word vertex count, +6 word polygon count, +0xc polygon list (8 bytes: three
  1-based vertex indices, ...), +0x40 vertex list (8 bytes: int16 x, y, z, pad).
  Per-car ripple record (6 at 0x6002e0, 0x84 bytes): +0 state byte, +4.. height bytes per vertex, +0x34..
  speed bytes, +0x64 -1, +0x68/+0x6c two polygon lists (0x64 bytes per polygon: dword flags with byte 3 =
  9 and byte 7 = 0x9e, corner texture words at +0x20/+0x40/+0x60 (u | v << 8, + 0x8080) and the texture page
  at +0x42), +0x70 vertex colours (dword per vertex), +0x74..+0x80 -1.
  0x4d44a0: 8 bytes per vertex: neighbour count, up to 7 neighbour indices. 0x4d44a8: 64 bytes, the vertex
  nearest each direction ((i&3), (i>>2&3), (i>>4&3) of -0xc0, -0x40, 0x40, 0xc0).
Ripple vertices (0x6c1eb8, 0x18 bytes, count 0x6c1eb4): +0 vertex pointer, +4/+6/+8 rest x, y, z, +0x10
  phase word, +0x12 speed (0..255), +0x14 size (0..3), +0x15 direction (0/1).
Water records (0x5d6378, 0x1c bytes, count 0x5d637c): +0 section, +2 0, +4 columns (width-band+1), +6 rows,
  +8 step (1 or -1), +0xa band, +0xc first vertex of the band, +0x10 edge vertex, +0x14/+0x16/+0x18 the
  section's across direction * 45/64 (from the normalised middle row).
Views (0x6cd4a0 / 0x6cdd10, current at 0x6ce60c; 0x5c bytes copied to 0x4bde98 as the current view):
  +0x5c rect (x, y, w, h words), +0x64 projection centre (x = race+0xa8c/4*60/100, y = race+0xa8e/4),
  +0x68 w, +0x6a h. 0x6cd40c = screen width*256/320 (x scale), 0x6cd410 = width*256/640 (y / text scale).
  Default camera position (0, 0, 0x3e80000), identity matrix 0x1000. 0x6cd480 view matrix (angles 0,
  x scale 0x2000 or 0xe000 when flipped).
Chase camera record (0x5c bytes, 0x6bf880 + slot*0x5c): +0 camera body, +4 car, +8 0, +0xc 0xffff0000,
  +0x10 0, +0x38 0, +0x40 kind (race +0xa90/+0xa91 by player), +0x44/+0x46 distance/height from tables
  0x4a77e4/0x4a77ec[kind], +0x48/+0x4a halves, +0x4c/+0x4d/+0x50 3, +0x5a second kind (race +0xa92/+0xa93).
  Start spots (*0x6bfb08): 8 bytes per car, int16 x, y, z; the camera starts at (x, y-0x3f, z+0xa0) << 16.
Weather tint (race +0x553): 1 = (0x80, 0x80, 0x90), 2+ = (0xc0, 0x60, 0xc0) over the track palette and the
  colour tables 0x4b5fb0 (64 x 0x78, colour at +4), 0x4b5fc8 / 0x4b5fcc (counts 0x6d0a78 / 0x6d0a00, 0x78),
  0x4b5fdc (32 x 0x60), the fog colour and 0x4b5fd0.
Options (*0x4b2198): +0x2a animated textures on, +0x84 replay camera choice, +0x1a1 + player*0x90 car choice.
```

### From src/game/setup_x.c

```
Data formats found by group X (race setup). Offsets in hex; all little-endian.

Track fixed lights (*0x6bfaec, count *0x6bfae8), 0x20 bytes each:
  +0 r, +1 g, +2 b (u8)          colour (alpha 0x3c added)
  +6 x, +8 y, +0xa z (s16)       position in track units (<< 16 for world fixed point); unused when on a path
  +0xc u8 radius / range
  +0xd u8 shape index into the shape table *0x6bfb2c (u16 offsets from the table base; shape = table + off[i],
          count = (off[i+1] - off[i]) / 2 u16 entries); 0 = no shape
  +0xe u8 path (body script) number, 0 = fixed light; with a path the light rides an object (0x460ba0 kind
          0xa00f, size 0x180) started on that path at spot +0xf
  +0xf u8 path spot: track info (*0x6c1eb0) +0xa8 -> 8-byte spots (s16 x, y, z, -)
  +0x10 4 x u32 cells: bits 0-7 section, 8-19 first row << 4 | column, 20-31 last row << 4 | column; 0 ends

Light records (0x5d8000, 0x40 bytes; index 0 unused, fixed lights 1..n, dynamic lights after *0x5d7fe0):
  +0 x, +4 y, +8 z (s32 16.16)   position
  +0xc u32 colour r, g, b, a
  +0x10 u16 radius, +0x12 u16 2
  +0x14 4 x u32 cells (as above)
  +0x24 u16 1 (on), +0x26 u16 0
  +0x28 shape pointer, +0x2c u16 0, +0x2e u16 shape count
  +0x30 object carrying the light, or 0
Light grid: 0x5d7de0[section] -> rows of 8 u32 light keys (0x20 bytes per row, 0 = free). Key = column bits
  (bits 0-10 = cell bits 8-18), bits 11-21 = (cell >> 20) + 1, bits 22-31 = light index.

AI-avoided bodies (0x6d0a80, 12 bytes each, count 0x4b5f98): body, number of tall faces, pointer into the face
  list 0x4b5ea8 (u32 face pointers, max 0x3c, count 0x4b5fa4).
Body model (body +0x10): +0x27 u8 face count, +0x34 u16 offset of 0x14-byte faces; face +8: u8 first edge,
  u8 end edge (into body +0x28 edges, 8 bytes: 4 u8 vertex numbers, 1-based, 0 = none); vertices at body
  +0x24, 8 bytes (s16 x, y, z, -), count body +0x10d; per-face kind words at body +0x194 (bits 24-27, kind 4 =
  not a wall).

AI character skills (0x4a7b88, 8 bytes per character = race +0x1c by car): two words of 3-bit fields copied to
  car +0x6e4 (bits 0-29) and +0x6e8 (bits 0-23).

Fonts (0x6c8180 A, 0x6c92a0 B, 0x6ca3c0 C = copy of A): 0x1120 bytes each:
  +0 kind (A 3, B 1, C 2), +4 texture width (A 0x100, B 0x100 or 0x140, C 0x80), +8 line height, +0xc glyph
  count, +0x10, +0x14, +0x18 (B: 5, 1, 1; A: 0x14, 0, 0)
  +0x1c glyphs, 0x10 bytes each: s16 x0, y0 (ink offset in the cell), w, h, texture page, u, v, -
  +0x101c 256-byte character map: glyph number by character, 0xff = none. Glyph order: A-Z, a-z, 0-9, then the 50
  characters at 0x4ac86c.
  Font table 0x6cb4e0 (pointers), count 0x4be314. Glyph cells come from FONT.BMP: font A 0x70 glyphs 0x2e wide
  from row 0, 0x32 rows; font B 0x70 glyphs 0x10 wide from row 0x36, 100 rows. 0x4432d0 trims each glyph to its
  ink and packs them into 256x256 8-bit textures with the BMP's palette (colours darker than 15 made black and
  their pixels transparent).

Sound listeners (0x4d4560..0x4d458c): per view (up to 4) pointers to the listening car's position (+0x34),
velocity (+0x68) and its camera's matrix (**(slot +0x28) +0xc4).
Engine sound state 0x5ff1e0 (0x18 per car: +4..+7 volumes 0x7f, +0x16 u16 1), tune state 0x5ff280 (0x10 per
car: tune 0xff, 0, 0x7f, 0).

Text sprites 0x5d9b40 (0x380 x 0x84) and text lines 0x5f6940 (+0 font, then 0x1c lines of 0x50 from 0x5f6944,
+8 of each cleared). Car glow 0x5cf860 + car * 0x690: header (0x42a9a0) and 13 sprites of 0x78 whose texture
words (+0x20, +0x3c, +0x58, +0x74) come from *0x6c1e14 + (info +0x2e + 0x40) * 0x10 (a 0x10-byte texture
record: two u32, two u16).
```

## Front end

Layouts found while porting the front end's logic (frontend_frame 0x4302d0 and its tree): menus, items, config/save data, text records, controls and network records.

### From src/game/fe_l.c

```
Front-end data seen by group L (frontend_frame 0x4302d0, menu_screen_build 0x44a2a0). Offsets hex.

MENU SCREENS (0x4af64c, 0..0x81; 0x4af650 = next screen, -1 none; jump table 0x44c4c0, default 0x44c361 -> 4):
  0 error message (PC_ERROR_MESSAGES, index config+0x48 + 2, or text 1 formatted with +0x48/+0x4c when >= 0x80)
  1 bad error, 2 replay error, 4 main menu (items -> 0xc league, 0x1a arcade, 0x23 time attack, 0x2c multiplayer,
  0x5f, 0x60 records, 0x66 options, 0x70 debug (greyed unless config+7), 0x72 quit), 5 replay,
  7/8/0xa/0xc/0xd/0xe league: car, ?, track, confirm; 0xf, 0x12..0x19 league results/next (0x18 league lost);
  0x1a..0x22 arcade, 0x23..0x2b time attack, 0x2c..0x42 split screen (0x31..0x33: players 2..4, config+0x74 = player count),
  0x43..0x5a network (0x48 connection settings by config+0x80: 0 IPX/TCP? name field +0xa0, 1, 2 serial: port, baud,
  stop bits, parity, flow control, 3 modem: number +0xc0; 0x49 chat macros config+0xdce (10 x 0x40), 0x4a system
  messages config+0x104e (29 x 0x40); 0x4b start; 0x4c create; 0x54 find sessions; 0x55 session list (count 0x4cb0d8,
  names 0x4c3018[], flags 0x4c98f4[]: bit 1 full, bit 0 open); 0x58 join confirm; 0x5a waiting for host),
  0x5c..0x5e race results, 0x60..0x65 records (0x65 reset? confirm), 0x66 options, 0x67..0x6b controls (players 0..3),
  0x6c, 0x6d, 0x6e graphics (sliders: config+0x64 1024..65536, +0x68 1..10, +0x70 10..100, +0x6c 24..112), 0x6f setup,
  0x70 debug, 0x73, 0x74 bonus bits (race+0x720 bits 5,7, race+0x721 bits 0..3,6: cheats unlocked), 0x75..0x78 bonus
  leagues (config+0x1d = 0xf, 0x11, 0x10, 0x12), 0x7e, 0x80, 0x81.
  Per screen: config+0x9cc+screen and config+0xacc+screen -> 0x4cb588/0x4cb589 (remembered cursor per screen).
MENU ITEM CALL 0x448140(text or 0, text index or -1/-3, target screen, kind 0/1/2/5, style 1/5/0x31..0x38/-1);
  0x4c9dfc greys and 0x4c9dfd hides the next item, 0x4c9dfb another flag, 0x4c9e00 marks the last.
CONFIG (*0x4b2198): +0x18..+0x1b tracks per player? (+0x18 track), +0x1c league, +0x1d league level/variant, +0x20 replay
  target, +0x40, +0x48/+0x4c error number/argument, +0x51/+0x55/+0x59/+0x5d/+0x61 reset to 0xff on the main menu,
  +0x64..+0x70 graphics sliders, +0x74 players, +0x75, +0x80 connection kind, +0x81/+0x82 mode flags, +0x84, +0x85, +0x87
  (3 = network game type), +0x88 (3 = network), +0x89 results screen, +0x8b, +0x9c/+0x9d league won/lost,
  +0x120 player name, +0x12b session name (0x41), +0x16c, +0x9c0..+0x9cb cleared on 0x2d/0x2e.
FRONTEND FRAME: 0x4ac990 fade (0..0x100), 0x4be330 fade direction (1 in, 2 out), 0x4be334/0x4be338 menu box position,
  0x4be32c box swing mode, 0x4be450 quit countdown, 0x4be328 frames since input (attract demo: level 0x6c8160 rises by 400
  per frame between 0x7e and 0x258, falls after; 0x48d630 starts it), 0x6ccd84 bit 2 disables attract, 0x4ac99c base
  colour (rgb), 0x4ac980 menu page.
```

### From src/game/fe_m.c

```
Front end data formats seen by group M (offsets hex).

MENU PAGE
  rows at *0x4cb4b8, count 0x4cb49c, 0x730 bytes each:
    +0 byte; +1 shown (set by layout); +2 free (nobody has it); +4 locked/hidden, +5 taken, +6 taken (other);
    +7 which item is used (2 -> +0x1ac, 3 -> +0x34c, else +0xc); items at +0xc, +0x1ac, +0x34c (0x1a0 each);
    +0x48/+0x4c, +0x1e8/+0x1ec, +0x388/+0x38c reset to -1 by layout; +0x4f4 the row's value (-> 0x4af660 when the
    cursor is on it); +0x500..+0x514 zero; +0x51c -1; +0x54c / +0x550 the row's marker x / y.
  extra row (e.g. "back") at 0x4c9df8; title item 0x4c2920; 64 loose items at 0x4c30e0..0x4c98e0 (0x1a0 each).
  cursor 0x4cb588 (int8), first shown row 0x4cb589 (int8), cursor row bounds 0x4cb514..0x4cb520, its y 0x4cb510,
  0x4cb50c cursor row placed; scroll arrows 0x4cb4ec (up), 0x4cb4f0 (down), forced by 0x4cb4f4 / 0x4cb4f8;
  0x4cb4a4 = no free row hidden (or 0x4af608); 0x4cb4fc horizontal (rows across, at most 4; else 11 down; 0x4af604
  overrides); row size 0x4cb4bc w / 0x4cb4c0 h; box size 0x4cb4cc (<= 600) / 0x4cb4d0 (+0x4cb4d4/d8 extra, or
  0x4af5f4/5f8), anchor 0x4cb4dc/e0 justified by 0x4af5fc/600 (0 left/top, 2 right/bottom, else centre), offset
  0x4cb4e4/e8; box 0x4cb474..0x4cb480 and centre 0x4cb484/488. Origins 0x4cb538..0x4cb54c (three x, y pairs).
  0x4cb524 the page's handler (0x453000 car menu, 0x448d60 time-attack report).

MENU ITEM (0x1a0)
  +0 text string id (-1 none); +4 shown flag; +5 visible; +6 origin pair 0..2 (else none); +8/+0xc x/y
  (-1 = caller's, -100 = box centre); +0x10/+0x14 offset; +0x18/+0x19 h/v justification (-2 = caller's; 3 = keep
  the position as the string's anchor); +0x24..+0x38 zero; +0x2c/+0x30 -1; +0x3c/+0x40 second justification
  (0x42eed0, -1 none); +0x44 flags: bit 27 white (0x4ac99c), bits 28..31 pulsing colours (bits 16..23 the glyph
  slot when bit 28), modes 1 / 4 / 2 of 0x4328c0 for bits 31 / 29 / 30; +0x48 -1; +0x4c 0; +0x50/+0x90/+0xd0/
  +0x110/+0x150 text bytes (cleared); +0x190 colour (4 x RGBx, 0 -> 0x4aca0c); +0x194 font (0x4ac848);
  +0x198/+0x19c where it was placed.

TEXT STRINGS (still original, for the renderer): records of 0x14c at *0x4be310 (flags +0: bit 14 in use, bit 15
shown; +2 first glyph, +0x7a glyph count; +4/+8/+0xc position; +0x78 line count; per line +0xbc/+0xbe glyph range,
+0xfe ...); glyph quads 0x20 bytes (4 corners of int16 x, y) at *0x4be304; per-glyph colour 0x84 bytes per glyph,
two sets at *0x4be308 / *0x4be30c (corner colours at +4, +0x24, +0x44, +0x64).

OPTIONS (*0x4b2198): +0x18..+0x1b each player's car (0..7); +0x1c car / +0x1d track for a mode start; +0x3b
demo runs counted.
Driver menu: cars listed from bytes 0x4af658[0..6] with labels 0x4af614..0x4af62c; car 6 needs race +0x720 bit 4.
Front-end modes (0x4329b0 -> 0x432cd0(mode, a, b, table)): 0 demo (0xa, 0x430ae0(4), 0x4acb88), 1 (0x4acb7c),
2/3/4 (car, track; 0x4acb70 / 0x4acb64 / 0x4acb50), 5 and 7 (car or 0xc when 0x4cb48c, track; 0x4acb30 / 0x4acae0),
6 (0xb, 0x13, 0x4acb20), 8 (0xd, 0, 0x4acacc), 9 / 10 (2, 1; 0x4acb0c / 0x4acaf4).
Network: 0x5d622c flags (bit 0, 2 set on restart, bit 9 0x45f790); send buffer 0x5d611c (0x8000) with end
0x5d6120 / cursor 0x5d6124, length byte 0x5d6118; 0x5d5368.. 64 records of 0x18; 0x5d6128.. 4 strings of 0x41;
0x5d5e18 0x300 bytes. Serial baud choices 0..9 = 1200, 2400, 4800, 9600, 14400, 19200, 38400, 56000, 57600, 115200.
```

### From src/game/fe_n.c

```
Data formats seen by group N (front end). Offsets in hex.

OPTIONS BLOCK (*0x4b2198; settings bytes the setup menu edits)
  +2a shield texture on/off      +2b water reflections on/off   +2d meteors 0..2 (shown as text 2+value)
  +2e rubble on/off              +30 speed glow on/off          +31 LOV on/off
  +32 ice sheet on/off           +35 laps (race), 1..3          +75 cleared by the multiplayer menu
  +85 laps (other mode), 1..3
RACE SETTINGS (*0x4b1960): +a87 catch-up on/off.

MENU OPTION HANDLERS: called as handler(event, arg) by the option code (0x44c6d0 on/off, 0x44c9d0 choice);
  return 1 when handled. Events seen: 0x12 draw/show value, 0x13 select, 0x14 left/down, 0x15 and 0x16
  right/up. On/off handlers use 0x447f20(value, 1, event); counters use 0x450050(event, arg, value, max, step, text).
  Option names are string-table keys ("PC_MENU_OPTIONS_SETUP_METEORS", "PC_MENU_LAPS", ...).

RETURN-TO-MENU FLAGS (0x42dd60): dwords 0x4beb38 (in a championship?), 0x4beb44 (menu open), 0x4beb48 (1),
  0x4beb4c (2), 0x4beb50 (3), 0x4beb5c (4), 0x4beb60 (5), 0x4beb6c (6), 0x4beb64/0x4beb70 (7), 0x4beb74 (9);
  bytes 0x6c1fcf (with 0x4beb38: 0 or 1), 0x6c1ff9 (7), 0x6c1fc1 (8), 0x6c1fce/0x6c2093 (9). Result -> 0x447650.

CAR HANDLING TABLE (0x4a7910, 0x10 bytes per car) as the car choice screen scales it to 0..255:
  +0 grip      (-80000 .. 80000)        +4 acceleration (-0xdac .. 5000)
  +8 top speed (0x40000 .. 0x120000)    +c weight       (-1 .. 2, shown as +1 over 3)

MENU MODEL RECORDS (0x4cba80, 100 x 0x48 bytes): +0 id, +4 body pointer (body +c4 matrix 0x20 bytes,
  +34 position); looked up by id (0x4539d0). 0x4538e0 draws one with a matrix and position.

NETWORK MESSAGES: count at 0x5d2cc4 (cleared when the network menu mode is 1), title buffer 0x4d07f8 (0x80),
  menu mode 0x5d5ccc, 0x5d5cd0 (-1 on mode change).

TEXT RECORDS (parked, for the renderer): 128 records of 0x14c bytes at *0x4be310 (flags +0, 0x4000 in use);
  glyph quads 0x20 bytes at *0x4be304 + n*0x20 and two 0x84-byte packet buffers at *0x4be308 / *0x4be30c per
  character (counter 0x4be2fc, max 0x800); fonts at 0x6cb4e0 (count 0x4be314), glyph table at font+0x1c
  (0x10 bytes per glyph: +2 y offset, +4 width, +6 height, +8 tpage, +a u, +c v), char map at font+0x101c
  (0xff = none), scale font+4, spacing font+0x14. Inline codes: "~D" number, "~0n" padded number, "~S" string,
  "~Pn" argument index, "~>" highlight start, "]" line end, '\t' tab stops at 0x4ac850.
```

### From src/game/fe_o.c

```
Formats seen by group O (front end menu input and dispatcher). Offsets hex.

MENU ITEM (0x730 bytes, *0x4cb4b8, count 0x4cb49c, cursor int8 0x4cb588, column int8 0x4cb589)
  +000 byte  kind: 2 on/off, 3 value (spin), 4 list; others are plain (select goes to a screen)
  +002 byte  selectable
  +007 byte  on/off state: 1 on, 2 off (set by the toggle)
  +4ec int   target screen for select
  +4f0 int   mode: 0 go without history, 1 go and remember, 2 back
  +4f8 int   value passed to handlers
  +500 fn(action) -> handled      item's own action handler (first refusal)
  +504 fn(index, value)           cursor arrived
  +508 fn(value)                  selected
  +50c fn(step 0x14/0x15, 0) -> changed     value item
  +510 fn(0x11 query | 0xf/0x10 set, 0) -> on   on/off item
Menu-wide: 0x4cb524 fn(action) handler, 0x4cb52c fn(value) on select, 0x4cb530 fn(index, value) on move,
0x4cb4fc items laid out sideways (left/right moves the cursor, up/down changes values), 0x4cb4a4 wrap,
0x4cb498 frozen (no item resets), 0x4af5f0 back item (selected on back with an empty history).
Menu actions: -1 none, 0/1 up/down, 2/3 left/right, 4/5 first/last selectable, 6/7 select, 8/9 back, 0xb handlers only.

SCREENS: current 0x4af64c, next 0x4af650 (0..0x81), fade 0x4be32c (1 cut, 2 fade) / 0x4be330 (2 start),
history 16 bytes at 0x4c98e4 with count 0x4cb4a0. Config +0x9cc[screen] / +0xacc[screen]: remembered cursor / column.

KEY REPEAT TIMERS (4 dwords: seen this frame, running, last fire time, wait): 0x4cb5a8 up/down (500, 100 ms),
0x4cb5b8 select (1500, 300), 0x4cb5c8 left/right (500, 100), 0x4cb5d8 page (500, 100).

PADS: per pad 0x43c bytes from 0x4d75e8: +0 button count, +10+4a axis present, +40+4a axis value offset,
value at +b8+offset, +e8+b button down bytes; pad count 0x4d9554 (and 0x48a010, at most 8).
Edge state: buttons 0x4d9658 + pad*0x90 + b (0 up, 1 down), axes 0x4d96d8 + pad*0x90 + axis
(0..5 as returned, 0xff unknown). Axis dead zone +-0x40.

BINDINGS: 0x449bf0(player, action) -> record: +4 kind (0 pad button, 1 pad axis?, 3 pad, 2 key...), +c button.
Bound actions list 0x4af7a0 (8 bytes each, count 0x4af878). Config +0x1a1 + player*0x90: the player's pad.
Menu actions 2/5 select, 3/4/6 back.

CONFIG: +08 pad menus on, +10 attract mode on, +12 u16 attract wait ms (default 30000), +1c/+1d world row values,
+74 player count kind for the split-screen label (2: +76 side by side), +8c+i network human flags.

ATTRACT: 0x4cb534 time it starts; due on screen 4 when config +10 and not quitting (0x4be450).

NETWORK: per player 0x66c bytes from 0x5d2cd0 (+0 connected); flags 0x5d622c (1 in game, 2 joined, 0x20 host);
local player 0x5d535c; sequence u16 0x4d9ae4; packet checksum: byte sum with +4..+7 zeroed.
League table: finishing order 0x4c24c8 (6 int8 car indices), race +1c+car points (<7 shown), race +8+car score.
```

### From src/game/fe_y.c

```
Front end formats seen by group Y. Offsets hex.

OPTIONS BLOCK (*0x4b2198, the save data)
  +04 byte    split-screen players 1..4 (0 or >4 resets to 1, and +0x74 = 2, +0x76 = 0)
  +12 u16     attract-mode delay in ms (0 -> 30000)
  +1c byte    league (7..9), +1d last world entry value (0x10..0x12, 0x0f), +1e league track count (3/4)
  +40 byte    last league (0..2), +41 last world (1..6), +43 network names on
  +74, +76    split-screen settings
  +8c[6]      player has a network name
  +120 [11]   player name being edited (name entry copies it to 0x4c2f00)
  +444        records: 16 tracks x 6 places x 0xe bytes: +0 time 3 BCD bytes (likely min, sec, hundredths; "YY\x99" or 0 =
              none), +3 name 10 bytes, +d flags (bit 0 new record, bit 1 the player's)
  +984        player names, 6 x 10 bytes
  +9c0 u16[6] split-screen points
  +9cc[0x82]  per-menu remembered entry (indexed by 0x4af64c)
  +bcc[...]   per-menu remembered records track
  +d8c        network player names, 7 x 11 bytes
RACE BLOCK (*0x4b1960)
  +0..5 car kind (8 AI, 10 player), +8.. kills (deathmatch), +1c.. car model per slot (<7 valid)
  +24 + slot*0xdc: dword times: [1] best lap, [2] race, [3..12] laps 1..10; low 3 bytes BCD (>>16 min, >>8 sec, lo)
  +54d cars, +585 league progress, +720 league open bits (1/2 second, 2/4 third; 3/4 special with +585),
  +721 world open bits 0..3; bit 4 / 5 unlock track 2 of world 3 / track 4 of world 2
TRACK TABLES (exe data): 0x4aca88 track -> world 0..3, 0x4aca9c track -> place in world, 0x4ac964 world track list
  with first index per world at 0x4ac978 (0, 3, 8, 11), counts 3/5/3/4. Tracks: world1 4,9,12; world2 0,3,6,13,14;
  world3 1,8,10; world4 2,5,7,11.
MENU TEXT ITEMS 0x4c30e0, 0x40 x 0x1a0 (count 0x4cb550): +6 colour, +8 x, +c y, +18/+19 flags, +44 flag bits
  (8 for times, 0x40000000 new record, 0x80000000 highlighted car; meanings guessed), +190 palette, +194 extra; text via 0x448260.
MENU ENTRIES *0x4cb4b8, 0x730 each: +1fc text, +4f8/+4fc position, +508 draw handler.
TIME TEXT: template "??:??:??" (0x4a7690), none "--:--:--" (0x4a769c), digits "0123456789??????" (0x4a76a8).
NETWORK PLAYERS: 0x5d2cd0 + slot*0x66c: +0 in use, +4 id, +8 name, +18 slot, +1c -1, +24 0, +3c (0x5d59a4), +40 0;
  list 0x5d5cd8 (count 0x5d5cd4) of 0x40 each.
```

### From src/game/fe_z.c

```
Data formats seen by front end group Z. Addresses in Rollcage.exe; offsets hex.

MENU ITEM TEMPLATE (0x4c9df8, 0x730 bytes; menu_item_commit copies it to *0x4cb4b8 + n*0x730, up to 0x40 items,
count 0x4cb49c, menu open flag 0x4cb498)
  +00 byte    item type (1 plain, 2 choice/toggle, 3 slider, 5 label)
  +03 byte    (0x4c9dfb) flag set by the controls / new record menus
  +04 byte    (0x4c9dfc) name entry item
  +05 byte    (0x4c9dfd) "locked" bit for track items / shuffle mode
  +06 byte    (0x4c9dfe) label item; +07 (0x4c9dff) slider item; in the table +07 = 1 enabled / 2 disabled
  +08 byte    (0x4c9e00) make this the selected item (index to 0x4af5f0)
  +0c..       first text record (0x4c9e04): +0x50 label (63 chars + NUL)
  +25 byte    (0x4c9e1d) 3 for the name entry
  +34 int     (0x4c9e2c) font size of the name entry
  +50 dword   (0x4c9e48) text flags: 0x58000000 editing, bits 16..23 visible character count
  +9c         (0x4c9e94) value text A (64 chars) / callback text 0xc / 0x12; +0xdc (0x4c9ed4) device number text
  +19c        (0x4c9f94) colour pointer of the edit field
  +1ac..      second text record (0x4c9fa4): +0x50 label; +0x23c in the item: callback text 0xd / 0x13
  +23c        (0x4ca034) value text B (64 chars)
  +354..+3f0  (0x4ca14c..) third text record (controls: binding column): +0x354 x offset -100, +0x364 byte 1,
              +0x390 flags (0x40000000), +0x39c (0x4ca194) 64 chars, +0x3dc (0x4ca1d4) 64 chars, +0x4dc colour
  +4ec, +4f0, +4f4  (0x4ca2e4/e8/ec) the commit arguments a1 (kind 0x83/0x84/0x85), a2, a3
  +4f8        (0x4ca2f0) item value (track index, activity, 100 / 0x65 extra tracks)
  +500, +504  (0x4ca2f8/fc) item code pointers (select / draw handlers)
  +50c, +510  (0x4ca304/308) slider / choice callbacks: int cb(int command, char text[64]);
              commands 0xc,0xd (choice texts), 0x11 (enabled?), 0x12,0x13 (slider texts)
  +518..+530  (0x4ca310..) second name entry: active, buffer, length, ... (name_entry_setup 1)

MENU TITLE: item record at 0x4c2920 (+0x50 label). Current text section 0x4cb4b4, next entry 0x4cb4a8.
TEXT ITEMS: 0x40 records of 0x1a0 at 0x4c3100: +0x08 font size, +0x70/+0xb0/+0xf0/+0x130 four 64-char texts.

TEXT TABLE SECTIONS (strings in the exe, read by 0x42e6d0 from the text file, '\r'-separated entries):
  PC_MENU_TRACK_TITLE 0x4afa88 (entries 2, 3: the two extra track entries), PC_MENU_NEW_RECORD 0x4b015c,
  PC_MENU_LAP_RACE_TIME 0x4b00f8, PC_MENU_PLAYER_TITLE 0x4afa24, PC_MENU_OPTIONS_CONTROL 0x4b0568,
  PC_MENU_OPTIONS_AUDIO_AUTO_DETECT 0x4b0524, PC_MENU_AUDIO_SPEAKER_MODE 0x4b04e8, PC_MENU_AUDIO_CD_SHUFFLE_MODE
  0x4b0504, PC_DISABLED_ENABLED 0x4b0078, PCII_ACTIVITY_NAMES 0x4b0580, PC_KEY_DESCRIPTIONS 0x4b059c (entry 0 the
  0x80000000 prefix), PCJOYSTICK_DESCRIPTIONS 0x4b05bc (0..5 axes, 6 "button", 7..10 hat directions),
  PC_NETWORK_MESSAGES 0x4b0e34 (6: players line, a printf format with the player count 0x5d5cc8),
  PC_MENU_TRACK_NEOTO / HARPOON / SAPHIRE / OUTWORLD (series 1..4 track names, 0x4486f0).

CONFIG (*0x4b2198 = 0x5fd740):
  +1c byte    series in use; +1d byte chosen track (100 / 0x65 the two extra entries)
  +41 byte    chosen series 1..6
  +44..+47    chosen track of series 1..4
  +1a1 + p*0x90  player p's controller device (0xff none)
  +410 byte   CD shuffle mode
  +447 + t*0x54 + j*0xe   track t's record entry j (6 per track, 16 tracks): +0 10-byte name, +0xa flags byte
              (bit 0 used, bits 3..6 profile 0..5)
  +984 + s*10 profile s's 10-byte name (0x4053e0)
SERIES TABLES: track counts 3/5/3/4 (0x448670); locked masks from race +0x721 bits 0x20 / 0x10 (0x4486b0).

KEY TABLE 0x6c20c0: 256 dwords per scan code: low byte the character / description index, 0x20000000 no name
(shown 0x%x), 0x800000 described by PC_KEY_DESCRIPTIONS, 0x80000000 with the prefix entry.
BINDING (0x449bf0): +4 input kind (0 joystick button, 1 axis, 2 key, 3 hat, -1 none), +0xc code. Axis codes: low
4 bits axis 0..5, 6 / 7 with bits 4.. selecting the direction text. Activity list 0x4af7a0: pairs {activity, ?},
count 0x4af878.

NETWORK: players 0x5d2cd0 + n*0x66c (in use dword at -8 of 0x5d2cd8, car id +0x14); sessions 0x5d26b0 + n*0x9c
(+0 id, +0x18 name), count 0x5d2cc4. Setting message 0x15: 0x1c bytes {byte 0x15, pad[7], 5 dwords}.
SOUND VOICES 0x6cb500: 0x3c-byte records (+1 flags byte, +0x34 in-use dword), up to 0x6cc440.

MENU WATER (*0x5d1fe0): cols = 0x5d1fe4*3/150, rows = 0x5d1fe8*3/150 cells of 0x38 bytes: +0 speed, +4 height,
+0x24 / +0x28 right / down slopes (smoothed), cursor cell 0x5d200c / 0x5d2010; a drop sets height 0x500000.
```

## Startup

### From src/game/st_sa.c

Startup globals (group SA):
  0x6c7ffc hInstance, 0x6c8000 main window, 0x6c7ff8 start time; 0x4acc38/0x4acc3c/0x4acc40 screen width,
  height, depth (mode list 0x4d5fe4: 12 bytes each = width, height, depth; the mode dialog returns the index
  in its high word); 0x4acc5c windowed (cleared by /W), 0x4acc60, 0x4be45c (/E); 0x4d4650 /FRONTEND chained
  restart (with 0x4d4654 = 0, 0x4d4658 = 1 run the result flow, 0x4ac984 = 1 front end); 0x4be3f0 /PLAYBACK;
  0x4b494c debug (ATDCUNNINGDEBUGVARIABLE, /MONO-, /MONO+); 0x4be4a0 NOCD (-> config +0x410); 0x4be440 skip
  the display dialog (config +0xa); 0x4acc54 CD check result; 0x4d4664 / 0x4d4668 DataPC / DataPSX loaded.
  Main loop flags: 0x4be460 quit, 0x4be450 == 1 display restart (0x435607), 0x4be484 / 0x4be488 frame pacing
  (skip a frame while 0x4be484 and not 0x4be488), 0x4be48c window closing (0x435e99, DestroyWindow),
  0x4ac984 front end (0x4302d0) vs race (0x4440b0), 0x4be464 + 0x4be478 extra blit (0x428850, 0x41efe0),
  0x4af444 / 0x4af440 per-frame 0x43605a. Shutdown: 0x4be4a8 done once, 0x4acc34 restore the cursor,
  0x4d4660 relaunch with /FRONTEND (ATDGFXCHAINING event, 10 s wait).
Options (config, *0x4b2198, 0x1854 bytes, registry Software\Psygnosis\Rollcage\BootSys "DataPC"):
  +0x120 user name (GetUserNameA, 11 bytes; empty -> "???"), +0x12b player name (empty -> the user name),
  +0x36 language (Installer\Language, 0..4), +0x40 series 0..2, +0x48 pending error (-1 none; error dialog
  0x83), +0x1a1 + p*0x90 player p's input device (0xff keyboard), +0x1b0 + p*0x90 + a*4 player p's saved
  binding of activity a (4 bytes: kind 1 unbound / 2 saved, then 3 bytes), +0x410 NOCD, +0x414 -> 0x4db260,
  +0x419..+0x41f, +0x42c/+0x42e/+0x430/+0x432 saved display settings (u16 width, height, depth, ...),
  +0x444 record tables (0x150 dwords of defaults at 0x4a7150): track t's 6 entries at +(t+0xd)*0x54, 14 bytes
  each: BCD minutes, seconds, hundredths, 10-byte name, flags byte (bit 0 new, bits 3..6 profile slot);
  +0x9c0 + s*2 u16 race wins per slot; +0xccc + i*0x20 six replay file names; +0xd8c + i*0xb six player names;
  +0xdce 10 network session names (0x40); +0x104e 30 network messages (0x40); +0x17ce / +0x180e information
  messages 1 and 7 (uninstall prompt text / caption).
Game state (race, *0x4b1960, cleared 0xb85b8 bytes, "DataPSX" 0xaa0 bytes saved): +8 + s points per slot,
  +0x10 + s, +0x54d car count, +0x551 race kind (0 league, 1, 5..7 multiplayer, 8, 9), +0x579 track,
  +0x57c mode (7, 8 time trial, 9), +0x585 difficulty (league won: 1 -> 2 and so on), +0x720 / +0x721 unlock
  bits, +0xa7c u16 12000.
Pad bindings: activity table 0x4af7a0 (0x1b entries of 8 bytes: activity id, mask; count 0x4af878); default
  tables (16-byte entries: mask word, 3 binding dwords) 0x4ac398 / 0x4ac498 / 0x4ac508 / 0x4ac578 per player
  (counts 0x4ac6e8..0x4ac6f4), joystick 0x4ac5e8 (count 0x4ac6f8).
Race result flow: 0x4af650 result menu, 0x4cb554 / 0x4cb558 (time trial) / 0x4cb55c / 0x4cb570 (config
  +0x17 == 1: no records), 0x4ca84c race order (6 slot bytes), 0x4c24c8 championship order, 0x4af668 points
  per place, 0x4c2908 + i new record flag per player, league won texts 0x4ca5ac / 0x4ca52c / 0x4ca6ac /
  0x4ca62c / 0x4ca7ac / 0x4ca72c (0x80 each).
String file: archive entry 8 loaded at 0x4be2ec (size 0x4be2f0): control characters other than tab -> 1,
  "~~" -> NUL; language u16 0x4be2f4.

### From src/game/st_sb.c

Group SB layouts (src/game/st_sb.c).

MOVIE TEXT QUEUE 0x6cc400: up to 0x40 records of 0x10 bytes, count at 0x4be2b8 (a dword, 0 = empty):
 +0 x (screen width / 2 - text width / 2), +4 side * 25 (0 left column, 25 right), +8 y (10, 400, or the credits' rising
 positions), +0xc malloc'd copy of the text (0 when none). Freed by 0x42dc60 after every movie.
 0x4be2bc = 1 when the screen is 400+ high (config flag 0x20/0x60), 0x4ac738 = -1 and 0x4be2b4 = 0 at each movie start.

MOVIE PLAYER CONFIG (the 0x28-byte stack block passed to 0x493210 with (path, window 0x6c8000, config)):
 +0 flags (0x18c04, or 0x18c0c when no texts queued; |0x10 when the screen is 640+ wide; |0x20 when 400+ high, |0x60 more
 when settings +0x39 is set; |0x1000 when the sound engine has a DirectSound), +0xc 0x4d5f40, +0x10 0x4d5f48,
 +0x14 DirectSound pointer from 0x46f720 (0 for none), +0x24 callback 0x42d8e0; the rest 0.
 Movie names (strings at 0x4ac794..0x4ac838, in "..\movies\"): psyg, atd, intro, credits, coploses, copchal, expert,
 polwin, frenwin, amerwin, japwin, gerwin, italwin, ukwin (.fmv). Winner's movie index = race +0x1c (language 0..6:
 uk, italian, german, japanese, american, french, polish). Path: "..\movies\X", else under the registry
 Software\Psygnosis\Rollcage\Installer value Source + "Rollcage\IDXData\" (0x5ff0c0).

FONT MAP (FONT.BMP, 16-wide cells from row 0x36, 100 rows, 0x46 cells):
 image record 0x4c2408: +2 width s16 (0x4c240a), +4 stride s16 (0x4c240c), +6 height s16 (0x4c240e), +8 palette count s16,
 +0xa bits per pixel (8), +0x10 palette (4 bytes per entry), +0x14 pixels (0x4c241c), +0x18 16-bit palette (0x4c2420),
 +0x20 flags. 0x4c2004: 256 dwords, alpha table of the palette (3 bytes of the entry, 0 -> blank when (a+b+c) / 4 < 15).
 font record 0x6c0a60: +8 height span of the cells (tallest bottom - highest top), +0xc cell count, +0x10 space width
 (0x6c0a70 = 5), +0x14 gap (0x6c0a74 = 1), +0x1c cell records of 0x10 bytes (x, y, w, h as s16; 0x6c0a80 + 0x10 * i
 is the width). Character map 0x6c1a7c: a byte per character, 0xff for none: A-Z 0-25, a-z 26-51, 0-9 52-61, then the
 nine characters at 0x4af5a0 from 62.

ARCHIVE FILES: name table 0x4ad000: count 0x4adaa0, 8-byte records {char *name, s16 entry, 2 pad}; memory files
 0x6c25c0: count 0x4adaa4, 8-byte records {data, position}. A memory file's handle is its data pointer.
 0x43b550 returns the entry in the low 16 bits (0xffff none).

SETTINGS BLOCK (*0x4b2198, 0x1854 bytes; registry value DataPC) fields met here: +5 (0..2), +8, +0xc/+0xe (suppress
 movies), +0xf (view count override 1..4), +0x14 race kind 0..10 (1 invalid; 7 and 8 skip the track choice), +0x15/+0x16
 track choice arguments, +0x17 1 = replay, +0x18..+0x1b controllers of players 1..4, +0x1f track rotation (0 random,
 100 next, else n - 1), +0x21 replay available, +0x26 start grid kind, +0x27 its setting (3 = random), +0x2d, +0x2f,
 +0x35, +0x77, +0x85 laps asked (0 = default), +0x31 high / low level, +0x38 last track, +0x39 windowed, +0x3b,
 +0x48/+0x4c (-1 after a race), +0x50 choice record for all tracks, +0x54/+0x58/+0x5c/+0x60 for worlds 1-4
 (4 bytes {u8 value, u8 step, u16 used bits}; the byte at +1 of the next record +0x51 etc is reset to 0xff),
 +0x6c speed class float (24..112), +0x74 mode (1..4), +0x75, +0x76 (two player), +0x78 u16[4] counts per rotation
 slot, +0x82, +0x88 (1..3 race mode 6/7/8), +0x89 (0x34/0x35, 0x43/0x45), +0x8a, +0x92, +0x9c (credits texts),
 +0x414 = 0x4db260, +0x419..+0x41f and +0x42c..+0x432 the screen mode, +0xccc + 0x20 * i replay file names (6),
 +0x1850 tag "20cp"; +0 tag "10cp".

RACE BLOCK extras (*0x4b1960; registry values DataPSX, DataReplay: 0xaa0 bytes): +0..5 slot kinds (8 = AI car), +8..+0xd
 per-slot records, +0xe race kind byte, +0x10 grid places, +0x1c character per slot (0xff open), +0x54d cars (1..6),
 +0x54e = 1, +0x54f, +0x54c, +0x551 race mode (0..10), +0x552 views, +0x553 weapons (0/1/2), +0x554, +0x555, +0x579
 level, +0x57b = 1, +0x57c = 1, +0x585, +0x586 laps, +0x58c (0 normal, 1, 2 replay), +0x58d, +0x720 flags (0x10),
 +0xa84 speed class byte, +0xa85, +0xa88, +0xa89, +0xa8b; the replay files are read to +0x12a0 + 0x1e884 * i.

BOOT DATA: registry key Software\Psygnosis\Rollcage\BootSys (0x4b2284), values "DataPC" (settings, 0x1854), "DataPSX" (race,
 0xaa0), "DataReplay" (race, 0xaa0). 0x4d46f4 = saved, 0x4d46f8 = replay read, 0x4d466c / 0x4d4670 = what was written.
 Table 0x4b21a0 ends with kind -1: records {kind, field address, other address}; kinds 0 byte, 1 word, 2 and 3 dword,
 4 eleven dwords; 100 copies other -> field, 101 field -> other (here: the sound settings 0x5fdb20.. from 0x4b1c70..).

GAME RANDOM: seed 0x4be34c, 0x4ac998 = 1 until first use (then seeded from the clock 0x436935). x = (x + 0x4d2) * 0x41a7
 & 0x7fffffff; result (x >> 16) % n.

### From src/game/st_sc.c

Group SC (startup / keyboard / controls). Offsets hex.

KEYBOARD (0x43c420, per frame; source: DirectInput key buffer *0x4b2ca4 = 0x4d9558, 256 bytes, bit 7 = down):
  0x6c1fc0[256] key state: 0 up; on a new press 8, counts down per frame, at 0 -> 3 and a repeat pulse; 0xff = held,
                never repeats (set by other code).
  key tables (0x43c6a0; layout flag 0x4beb7c from config+0x36 == 2 via 0x43d270):
  0x6c24c0[256] character (0 none), 0x6c1ec0[256] Windows virtual key, 0x6c20c0[256] dword game key code
                (ASCII, 0x800000 | n for specials: Esc 0x18, F1-F10 9..0x12, F11/F12 0x13/0x14, arrows/home/... 0x23..0x2c;
                0x80000000 | char for the keypad; 0x80800000 | n for keypad Enter / '='). Second layout = German
                (ß ´ ü + ö ä ^ # y z - <; codes sign-extended, e.g. 0xffffffdf).
  0x4beb78 tables built.
  last key events, 0x18 bytes each: 0x4af438 any key, 0x4af450 keys with a character, 0x4af468 keys without:
     +0 scan code (-1 none; kept while still held), +4 char, +8 VK, +0xc repeat pulse, +0x10 new press, +0x14 game code.
  flags 0x4beb38..0x4beb74 (dword 0/1, either key of the pair): shift, ctrl, alt, up, down, left, right, pgup,
     pgdn, home, end, insert, delete, enter, '=' / keypad +, '-' / keypad -.

CONTROL BINDINGS (controls_read 0x427b10): *0x4ac6fc[player] -> 16-byte records {+0 action mask, +4 kind
  (0 button, 1 axis, 2 key, 3 hat), +8 device (pad), +0xc code}; count = **0x4ac70c[player]. Axis code low nibble:
  0/1 X -/+, 2/3 Y, 4/5 Z, 6/7 axis (code >> 4) -/+. Hat code: low nibble hat, next nibble direction 0..3.
  Player 0's Escape gives 0x800; menus use 0x40 (select) and 0x800 (back).

PADS: 0x4d9554 pad count; records of 0x43c bytes at 0x4d75e8: +0 button count, +0xc hat count, +0xd8 hat values
  (hundredths of a degree, low word 0xffff = centred), +0xe8 button bytes. Pad config 0x90 bytes at 0x4d9658:
  0x80 bytes button map (reset 1), +0x80..+0x88 three axis assignments (-1), +0x8c..+0x8f flags (1).

TIMING: 0x4be3e8 ticks run by the 16 ms timer callback 0x4365ed (timer handle 0x4be458); 0x4be468 inside the
  callback; 0x4be46c catch-up mode (frame_net_step runs (now - 0x4be470) * 60 / 1000 - 0x4be474 ticks);
  0x6c7ff8 last game_time; 0x4be4ac / 0x4be4b0 once-a-second stamp and count. 0x4be114 tick hook function.
  0x4ac990 fade level 0..0x100 (0x4be330: 2 in, 1 out), 0x4be320 tick count. 0x4be48c quit request.
  0x4acc50 game speed scale (config+0x97: 1 -> 45000, 2 -> 20000, else 0x10000).

FRONT-END LOAD: 0x4be344 model block (archive entry 0/2/4/6 by colour depth and 0x6cce7c), offsets relocated:
  +0x18 model table (count u16 +0x2c; each model's +0xc, +0x18, +0x40, +0x44 (if set) relocated), +0x84, +0xcc,
  +0xd8, +0x110; published at 0x6bfad8, 0x6bfb18, 0x6bfb58, 0x6bfb5c, 0x6c1e14 / 0x4be340; 0x6c1e30 = 0x6bfac0,
  0x6c1eb0 = block. 0x4be348 palette image (+0x24, +0x38, +0x3c offsets, +0x32/+0x33 bytes, +0x34 u16).
  Work area 0x60ae0 bytes at 0x4beb84 (halves at 0x6c1e1c/0x6c1e34, end 0x6c1e38).
  Front-end models: table 0x4b0630 (53 x 16: id, model index via set+0x58, flag (0x40f1e0), angle << 4) ->
  records 0x48 bytes from 0x4cba80 (+0 id, +4 mesh, +8 angles x3 (stride 8), +0x28/+0x2c corner offsets, +0x30 phase 0/100/200/300,
  +0x3a 0x400); id lists of 7 at 0x4b0980/0x4b099c/0x4b09b8/0x4b09d4 (corners); 0x4cfe98 top vertex.
  Particles: 255 x 0x4c bytes at *0x4d108c.

## Sound engine

### From src/game/snd_da.c

Data seen by sound engine group DA (0x46f000-0x470fff). Addresses in Rollcage.exe; offsets hex.

QMIXER IMPORT SLOTS: the IAT of QMIXER.dll starts at 0x4a31d0, one dword per import in this order:
  0 StopProfile, 1 StartProfile, 2 SetSpeakerPlacement, 3 EnableChannel, 4 SetOptions, 5 SetDistanceMapping,
  6 SetSourceVelocity, 7 SetPolarPosition, 8 SetVolume, 9 SetFrequency, 10 IsChannelDone, 11 PauseChannel,
  12 FlushChannel, 13 PlayEx, 14 RestartChannel, 15 InitEx, 16 OpenWaveEx, 17 OpenChannel, 18 GetDirectSoundBuffer,
  19 GetChannelParams, 20 SetListenerRolloff, 21 GetDirectSound, 22 Activate, 23 CloseSession, 24 FreeWave.
  All stdcall; arguments used (session = *0x4d453c first): SetSpeakerPlacement(s, mode 0..3), EnableChannel(s, ch, 0,
  flags), SetOptions(s, flags, 7), SetDistanceMapping(s, 0, 1, &desc), SetPolarPosition(s, ch, 0, &float3),
  SetVolume(s, ch, 0, v), SetFrequency(s, ch, 0, v), IsChannelDone(s, ch), Pause/Flush/RestartChannel(s, ch, 0),
  PlayEx(s, ch, 1, wave, loop ? -1 : 0, 0), InitEx(&desc), OpenWaveEx(s, &{format, data, size, 0, index}, 8),
  OpenChannel(s, 0, 1), GetDirectSoundBuffer(s, ch, &out), GetChannelParams(s, ch, &block), SetListenerRolloff(s, bits, 0),
  GetDirectSound(s, &out), Activate(s, 0/1), CloseSession(s), FreeWave(s, wave).

SESSION GLOBALS: 0x4d4520 engine started, 0x4d4524 session open, 0x4d4528 activated, 0x4d452c voices crowded (> 16),
  0x4d4530 wave file block, 0x4d4534 wave count, 0x4d4538 window handle (copied to the description), 0x4d453c session handle,
  0x4d4544 / 0x4d4548 / 0x4d4550 / 0x4d4554 option switches, 0x4d454c speaker mode (0..3), 0x4d4558 profile started.
  Settings: 0x4b1c6c last channel used (round robin), 0x4b1c70 sample rate request, 0x4b1c74 channel count 8..32,
  0x4b1c78 / 0x4b1c7c / 0x4b1c80 / 0x4b1c84 / 0x4b1c88 switches, 0x4b1c90 / 0x4b1c94 distance range (floats), 0x4b1cac flag
  mask, 0x4b1cb0 clear-statistics request, 0x4b1cb4 statistics text format, 0x4b1e48 log format.
  0x4d44e0 (0x40 bytes) InitEx description: +0 0x40 (size), +4 0x18, +8 sample rate, +0x14 0x40, +0x24 window, +0x28 / +0x2c /
  +0x3c 0x20 (channels). 0x4d44c8 (0x10) distance descriptor: {0x10, near, far, 0}.

CHANNEL RECORD (64 at 0x6cb500, 0x3c bytes):
  +00 byte flags: 1 playing, 2 looped, 4 paused, 8 voice flag, 0x10 audible (set by 0x4704b0); bits 5..7 kept
  +01 byte kind (0 unused; set from the QMixer flags: 0x15/0x16, 0x29/0x2a, 0x49/0x4a, 0x85/0x86); bit 4 / 8 tested by 0x4704b0
  +02 byte timer (10 on play / claim, counted down by 0x470ef0); +03 byte wave index (0xff none)
  +08 int frequency cache (-10 reset), +0c int volume cache, +10 / +14 / +18 float position cache, +1c / +30 other caches
  (+1c 0xc7f12000, +30 100.0f after reset), +34 voice data (0x471400), +38 DirectSound buffer (0x46f8f0)

WAVE FILE BLOCK (loaded by 0x496590, pointer 0x4d4530): +0x40 count, records from +0x44 (0x40 bytes), sample data after the
  records (0x6002c8 = block + 0x44 + count * 0x40, 0x6002cc = records). Record: +0 data offset, +4 size, +0xc sample rate,
  +0x10 QMixer wave handle (0x46fa60), +0x14 bits, +0x15 channels, +0x16 byte: forces looping when nonzero.
WAVE FORMAT (0x12 bytes, 0x46fa00): u16 tag (1), u16 channels, u32 rate, u32 bytes per second, u16 block align, u16 bits, u16 0.
CHANNEL PARAMS (GetChannelParams block, 0xdc bytes, +0 size 0xdc): +4 flags (0x7f000000 group, bits 16..23 kind bits),
  +0x5c enable flags.
PROFILE STATISTICS (doubles): 0x600160 sample count; four rows of 8 doubles at 0x600168, 0x6001a8, 0x6001e8, 0x600228
  (totals, averages, minimums, maximums of the 8 timings); four rows of 3 doubles at 0x600268, 0x600280, 0x600298, 0x6002b0.
POOL RECORD (0x124 bytes, ring 0x6002d4..0x6002d0, cursor 0x6002d8; built by 0x46f230, merged by 0x46f080): three int16 boxes
  at +0 (min x y z, +0xc max), a pointer at +0xb8 to the holder's current box, +0x1a flag word. Not sound.

### From src/game/snd_db.c

## Sound engine voice table (group DB)

64 voice records of 0x30 bytes at 0x5ff4a0 (index = device voice; the mixer walks the first *0x4b1c8c):

| off | type | meaning |
|-----|------|---------|
| 0x00 | u8 | state: 0 idle, 0x40 starting, 1 playing, 0x41 stopping (other values: debug print, then stop) |
| 0x01 | u8 | flags: 1 loop (kind 9), 2 linked parent (one child voice per listener 1-3), 4 "flagged" play (0x4d459c), 0x10 linked child, 0x20 positional, nearest listener |
| 0x02 | u8 | kind (9 = looped) |
| 0x03 | u8 | linked voice for listener 1 (0x7f none); in a child: its listener index |
| 0x04 | u16 | frames since start |
| 0x06 | u8 | EAX level choice: 1 = *0x4b1eac, 0 = *0x4d45f8 |
| 0x07 | u8 | sound id |
| 0x08 | s16 | volume left |
| 0x0a | s16 | volume right (the mixer uses (l + r) >> 1) |
| 0x14 | i32 | pitch (0..0x3fff; device rate = pitch * 11025 / 1024) |
| 0x18 | i32 | cleared on start |
| 0x1c | i32 | cleared on start |
| 0x20 | i32 | cleared on start |
| 0x24 | ptr | position (3 x i32), or 0 |
| 0x28 | ptr | velocity (3 x i32), or 0 |
| 0x2c | u8 | linked voice for listener 2 |
| 0x2d | u8 | linked voice for listener 3 |

Related: 0x5ff300[64] u8 "plain volume" per voice (not scaled by the master volume u16 0x4d45a0; volume =
2 * (vol * master / 16384) otherwise 2 * vol); 0x5ff340[64] u8 voice started. Listener slots 0-3:
positions 0x4d4600[4], velocities 0x4d4610[4], matrices 0x4d4620[4], 0x4d4630[4] (passed on to 0x472bb0);
copies at 0x5ff3a0 (12 bytes each), 0x6000a0 (12), 0x6000e0 (0x20), pointers 0x4d45c0/d0/e0, used flags 0x4d45b0.
Sounds are out of range when 2 * isqrt(sum(((a - b) >> 17)^2)) > 0x100000.

EAX: 64 x 0x3c at 0x6cb530: +0 float current level, +4 IKsPropertySet * (0 = none). 0x4d4524 / 0x4d4528: EAX open
/ ready. 0x4b1ca4 / 0x4b1ca8: saved room / room HF (-1 = none).

CD audio: descriptor (*0x5ff384, 0x14 bytes): +0 = 7, +4 = track, rest 0. Info record (*0x6000d0, 0x28 bytes):
+0 flags (0xc0 = volume, 0x300 = channels), +4/+6 0x3fff, +0x10/+0x12 s16 volume left/right (x4 to the MCI player
0x496c90), +0x14/+0x18 channel mapping. 0x4d45a2: CD state (1 playing / paused-able, 2 paused).
Race record (*0x4b1960): +0xa7c u16 master volume, +0xa7e u16 CD volume (0xffff = pick at random), +0x579 track,
+0x61e first CD track. Config (*0x4b2198): +0x410 CD music off, +0x411 sound debug display.

## Remaining game logic

### From src/game/gl_ga.c

## Camera viewports (group GA; see also post_i.c)

4 records of 0x150 bytes at 0x6cf6a0, one per viewport, driven by the camera director 0x408360:

| off | type | meaning |
|-----|------|---------|
| 0x00 | u32 | shot kind: 0/9 the followed car's own chase camera, 1/7 on the car (offset (0,-10,0x14) turned by its matrix, 0x40a720; looking along the car matrix column 2, negated for kind 1), 2/3 overhead (3: up from the car's matrix), 4/5 orbit (5 climbing), 6 eased towards a point 0x640000 above and 0xa00000 behind in z, 8 from the car's matrix-turned offset (0,0,-0x50) +-0x28 in y, 10 nearest trackside spot, 0xb/0xc/0xd event cut-aways (objects from 0x6b9ce0 / 0x6b9cc0 / 0x5d1fc0), 0xe replay Bezier sweep, 0xf fixed camera on a node, 0x10 trackside flyby; others: nothing placed |
| 0x04 | 3 x i32 | fixed camera position (kind 0xf) |
| 0x10 | 3 x i32 | kind 0xf look offset (int16 used: GTE vector); +0x14 = -(sum of node +0x10 -> +0x22 words, max 600) / 2 |
| 0x1c | u32 | 1 in on-car shots (kinds 1/7), else 0 |
| 0x20 | i32 | per-shot counter: kind 2/3 drop (0x780000 - 45000 a step), kind 5 height (from -200, +2 a step), kind 10 random (bit 0: zoom by distance), event cut-away countdown |
| 0x24 | i32 | zoom (0x68 default each step; 0x138 / 0xd0 by config +0x31, 0x7b event shots, __ftol(*0x6cce6c) kinds 1/7/0xe) |
| 0x28 | i32 | kind 0xf zoom source (zoom = (x * 104) >> 8, unsigned) |
| 0x2c | s16 | orbit angle (sin table 0x4b24f8, 256 steps; cos = +0x40) |
| 0x2e | s16 | orbit step: +-1 (kind 4), +-2 (kind 5) |
| 0x30 | u16 | car of the on-car shot |
| 0x32 | s16 | time in the shot (the shot ends, 0x40a9b0, when it passes +0x34) |
| 0x34 | s16 | shot length |
| 0x36 | u16 | car slot followed |
| 0x38 | s16 | event object index |
| 0x3a | s16 | race-start delay (-1: start the kind 4 orbit with 300) |
| 0x3c | i32 | event kind (0 none, 3 pickup cut-away, 4 the 0x5d1fc0 object) |
| 0x40 | 0xd8 | camera body (copy of a car body from +0x34): +0 position, +0x34 velocity, +0x90 matrix (matrix_look) |
| 0x118 | u32 | race tick (0x6c0a50), stamped in event kinds 0xb-0xd while the event kind is not 0 or 4 |
| 0x11c | u32 | race tick of the last flyby step |
| 0x120 | 8 x u32 | last good matrix (camera_look_or_keep) |
| 0x140 | ptr | kind 0xf node (walked up +0x1b0 to the root; siblings +0x19c; +0x10 -> +0x22 a size; +0x34 anchor; +0xc4 matrix) |
| 0x148 | u32 | kind 0xf flag (1 while in that shot; cleared each step) |
| 0x14c | i32 | kind 0xf fade: 500 per step for 20 steps, 10000, then (len - t - 20) * 333 down to 0 |

Per-viewport arrays (12 bytes per view unless noted): 0x4b6050 flyby step, 0x4b6080 camera position, 0x4b60b0 up
vector, 0x4b60f0 eased look-at, 0x4b6130 look direction, 0x4b6248 sweep start, 0x4b6298 (0x3c per view) 5 Bezier
points (from 0x4a7818 turned by the car's matrix; points 3 and 4 follow the car), 0x4b6550 eased up (sweep), 0x4b6580
kind 0xf anchor, 0x4b65c0 eased sweep end; 4 bytes per view: 0x4b6010 sweep speed (0x1b4), 0x4b6040 flyby up-lean
flag, 0x4b6120 sweep grounded steps, 0x4b6540 sweep t (16.16, max 0x10000), 0x4b65b0 trackside spot pointer.
Float GTE matrix copy: 0x6ccd1c, 3 rows of 3 floats 0x10 apart (int16 0x6ccbe0 / 4096).

## Analogue response curve (group GA)

0x4bdf04: 129 i32, entry i = (int)pow(x_i * *(float *)0x4a3324, 2) (~0.0887), x_0 = 25 and x_i+1 = x_i + *(float *)0x4a3320
(~0.806) in float; entry 0 then forced to 0;
built once (flag 0x4be110). Stick value v (0..255, 0x80 centre) with 300/256 gain about the centre: below the centre
0x80 - table[0x80 - e], above table[e - 0x80] + 0x80, clamped to 0..0xff.

### From src/game/gl_gb.c

Data seen by group GB (race and car setup). Addresses in Rollcage.exe; offsets hex.

TRACK FILE HEADER (*0x6c1eb0, loaded by track_load 0x43fa60 from <name>.BL6 / .BLP; *0x6c1e28 = the same address,
  the relocation base). Offsets made addresses by the loader: 0x14 (object kinds, I32 +0x10 of them), 0x18 (model table,
  U16 +0x2c entries, 0 = none), 0x1c, 0x20, 0x24 (0x100 spot records of 0x10), 0x28, 0x34, 0x3c, 0x40, 0x44, 0x4c (light
  colours, U16 +0x66 of 4 bytes, rgb scaled x2 by 0x6ccdc0 and clamped), 0x60 (sky record), 0x6c, 0x70, 0x74, 0x7c, 0x80
  (0x10-byte records, +7 a byte per linked object), 0x84, 0x90, 0x9c, 0xa4, 0xa8, 0xc0, 0xd0, 0xd4, 0xe4, 0xe8, 0xec, 0xf0
  (lights, U8 +0xb6 of 0x10: int16 x,y,z at 0, kind byte 6 (1 = skipped), int16 at 8,a,c), 0xf8, 0x100, 0x108, 0x10c,
  0x110 (texture entries -> *0x6c1e14), 0x114 (-> *0x6c1e40), 0x118. Read relative: 0x68, 0x88 (U16 +0x14 * 4 of object
  records), 0x8c (U8 +0x79 records of 0xc; +8 relocated by +0x98), 0x98, 0xa0, 0xac (U16 +0x96 object slots: values
  < 0x10000 become created objects), 0xb0, 0xb8, 0xbc, 0xc4 (0x1c-byte light records for the object kinds' +0x4c),
  0xcc, 0xd8. Values: 4 byte, 8 packet count (0x120 bytes each, two quads), 0x30..0x32 fog rgb / 2, 0x54 fog colour,
  0x5c / 0x5e sky size, 0x64, 0x78, 0x7a, 0x94, 0x95, 0xb4, 0xc8 count (0x20-byte records at *0x6c1e3c), 0x11e pool size.
  Models (0x18 table): +0xc, +0x18, +0x40, +0x44 (if not 0) self-relative, +0xa cleared, U16 +0x1c / +0x1e sub-models
  (+0xc, +0x18, +0x44 self-relative). Object kinds: +0x20, +0x28..0x30, +0x3c..0x44, +0x50, +0x54..0x5c file-relative,
  +0x48 += header +0x3c, +0x38 += header +0x50 + base, +0x4c light index -> address.
TEXTURE FILE (<name>.BT6 / .BTP, freed after loading): +0x10 texture count (-> 0x6c1e10), +0x14 object record count,
  +0x24 / +0x38 / +0x3c offsets for the upload 0x428130(U16 +0x34, U8 +0x32, U8 +0x33, ..), +0x28 object records
  (0x38 bytes), +0x40 one byte per track packet (texture page + 1).
OBJECT RECORD (0x38): U16 0 kind (low 10 bits: 0 object, 2 flare, 0xd glow, 0x18 pick-up), 3 model index, 4 / 5
  bytes, U16 6 link / texture (0xffff = none), int16 8, a, c position, int16 e, 10, 12 angles, U16 14 index into
  header +0x88, 16 linked flag, U16 18 / 1a, 1c..1e bytes, 1f object class (0 static, 2 special), U16 20..24 offsets
  into header +0x1c, 26 byte, U16 28..2c.
TRACK RECORD (0x6bfac0, *0x6c1e30): 0 object kinds, 4 packets, 8 header, 0xc..0x9c copies of the header's (relocated)
  fields (see track_load), 0x14 spots, 0x36 / 0x44..0x47 bytes, 0x5c colour count, 0x5e.
SKY ARCHIVE (header +0x60 / the texture file passed to 0x404550): I32 +0x1c offset of 0x22-byte quads: 4 vertices of
  3 int16 at 8-byte steps, U16 +0x20 texture. 0x6d0a78 backdrop quads, then 0x6d0a00 cloud quads.
WHEEL RECORD (car +0x324, 4 of 0xc4): 0 / 1 flags, 4..0x48 cleared, 0x34..0x3c car offset * 16, 0x58 / 0x5c (1000),
  0x6c wheel object, 0x70..0x74 offset from 0x4a7b40 + 8 * wheel, 0x80 0x240000, 0x84..0x98 int16 extents (0x4a7b60 /
  0x4a7b62 / 0x4a7b64, swapped for odd wheels), 0x9c side, 0x9d index, 0xa8 bit 15 cleared, 0xb8 -1, 0xbc |v|^2 * car
  +0xa8, 0xc0 1 / |v| (v = car +0x180 + 0x1c * wheel, int16 * 256).
HUD (0x4377c0): int16 x,y pairs from 0x6c7dc8 (one per item, 0x43 items, texture numbers 0x6c7ed4, shown flags 0x6c7f5a),
  lap digit rows 0x6c7dfc (4 x 8 pairs), sprite packets 0x6c3ec0 (2 x 0x78 per item: corners as floats +0x10 / +0x14,
  +0x2c, +0x30, +0x48, +0x4c, +0x64, +0x68; u/v bytes +0x20.., z 0x4000a3d7).
SHOCK WAVE: 0x6ccbac live, 0x6ccba0 centre (16.16), 0x6ccbb0 strength; records at 0x6d06e0 (0x10 bytes, int16 x,y,z,
  strength byte +6), count 0x6d06c0.

### From src/game/gl_gc.c

Data formats found by group GC (in-race leftovers). Offsets in hex; little-endian.

Dynamic lights (records at 0x5d8000, see group X): +0x12 u16 countdown (per step, to 0), +0x24 u16 kind:
  1 colour shape (+0x28 u16 15-bit colours, +0x2c index, +0x2e count; colour = 5-bit channels << 2),
  2 / 3 fading (+0x26 s16 steps left; 2 orange, 3 flicker; freed at 0 by 0x441370),
  5 on object +0x34 (colour object +0x1b4..+0x1b6 >> 3; freed when object +0x188 == 0x10),
  7 on object +0x34 (colour +0x2a38..+0x2a3a >> 1; freed when +0x188 == 0),
  8 car head light (car +0x34: colour +0x13f8..+0x13fa, pulse wave index +0x13fc, +0x13fd odd = -150; dimmed below
    energy +0x654 0x400000; at (0, -20, -150) in the car frame).
  Kind 1 lights with +0x30 an object are moved with it (script step + velocity); without, path positions
  0x6c1b88 + (i - 1) * 8 (s16 x, y, z, u16 moved flag) re-place them when the flag is 1.
Light countdown list 0x5d71e0 (u32, count *0x5d7fe4): top 3 bits a counter decremented per step; at 0 the entry is
  replaced by the last.
Queued light spawns 0x5d7300 (0x24 bytes, count *0x5d72e0, emptied per step): +0 .. passed to 0x441220 (record,
  record + 0xc, u16 +0x10); then the light gets +0x24 = u16 +0x12, +0x26 = u16 +0x14, position +0x34.. = +0x18..+0x23.

Trigger queue 0x6b9ba0 (u32 objects, count *0x6b9cb8, max 70); 0x6b9b94 cleared. Object +0x16a: up to 5 u16
  actions, kind = bits 12-15 (0 ends), value = bits 0-11: 1 a light (its colour << 1 goes to the action-2 target),
  2 object 0x5d6e00[value] (+0x10c on: +0x108 = colour), 3 track point info +0x108 (12 bytes: +6 s16 counter, every
  3rd a glint 0x1b, +0xb colour), 4 track point info +0x10c (glint 0x1c).

Skid marks: wheel record (+0x9c car slot, +0x9e marking, +0xa0 s16 x, y, z last point, +0xa8 bits 0-14 packed
  side direction (5 bits each, bit 15 set = marking), +0xac velocity at the point, +0xb8 segment or -1,
  +0x10 normal, +0x1c offset, +0x40 velocity, +0x58 slip measure). Segments: 0x124-byte records *0x6002d8 (next),
  *0x6002d4 first, *0x6002d0 end: +0 old point (12 bytes), +0xc new point, +0x18 u16 (0 while marking), +0x1a u16 1.

Pause screen: 0x6b8d60 paused, 0x6b8d64 s16 page, 0x6b8d66 s16 item, 0x6b8d68 blink counter, 0x6b8d6c / 0x6b8d70
  screen centre x / y (* 2), 0x6b8d74 pad, 0x6b8d78 new presses, 0x6b8d7c previous pad, 0x6b8d80 car ** of the
  pausing player, 0x6b8d84 CD volume, 0x6b8d88 master volume (0..0x3fff), 0x6b9584 s16 item count, 0x6b9586 bytes
  item enabled, 0x6b95a2 s16 return page, 0x6b983a / 0x6b983b slider sprites; panel sprites 0x6b8d8c (9 x 0x78)
  from table 0x4b15f0 (8 bytes: s16 x, y, u8 w, h, -, texture). Pad bits: packet byte 4 bits 0-5 -> 0x1000 up,
  0x4000 down, 0x8000 left, 0x2000 right, 0x40 select, 0x800 back. Items by page << 4 | item: 0x00 / 0x10 / 0x20 CD
  volume, 0x01 / 0x11 / 0x21 volume, 0x02 / 0x12 / 0x22 page 0xa, 0x03 page 0xb, 0x23 page 0x14, 0x04 / 0x14 / 0x24
  resume, 0xa0 / 0xa1 back / quit, 0xb1 restart, 0x140-0x145 camera choice.

Car camera choices: car +0x2470 -> 6 setting pointers at +0, views 0x300 bytes from +0x18, +0x1218 s8 current.
Snatch object: +0x33c radius (* 2), +0x340 / +0x341 spin bytes, +0x342 wave, +0x344 72 world points (8 bytes),
  +0x6a4 centre (s16 x3), +0x2a1c 6 ring colours (u32), +0x2a34 s16 grab level (to 0x1000 by 0x80 while +0x188 is
  0x40 / 0x80).
Car damage record (car +0x6ac, -1 = none): +0 active, +1 s8 hit counter, +3 first-blast timer, +4.. damage cells
  (index from the byte table *0x4d44a8 by blast direction).
Step clock: 0x4be390 start time, 0x4be3e0 steps done (33 ms each).

### From src/game/gl_gd.c

Group GD data layouts.

Direct3D device table (filled by d3d_enum_device 0x41d280): 0x4b86c4 + i * 0x338, count at 0x4ba6f4.
  +0x000 name (string, from the enumeration's name argument, 0x104 bytes)
  +0x104 description string (0x104 bytes)
  +0x208 copy of the 0xfc-byte device description used (hardware, or software if the hardware one has no colour model)
         fields read: +0x210 (dword, == 2 -> flag +0x328), +0x214 (bit 0 clear -> flag +0x32c), +0x27c / +0x280 /
         +0x288 / +0x28c / +0x294 capability bit words, +0x2a8 (nonzero -> +0x31c)
  +0x304 device GUID (16 bytes)
  +0x314 colour model of the hardware description (dword, 0: software only)
  +0x318 bit 0 of +0x28c; +0x31c (+0x2a8 != 0); +0x320 cleared when +0x330 is 0; +0x324 name contains the string at
         0x4a80bc ("MMX"); +0x328 (+0x210 == 2); +0x32c (~+0x214 & 1); +0x330 capability flags (1, 2, 4, 8);
         +0x334 (+0x28c >> 5) & 1.
  DDBD depth flags: 0x41d640 maps 1:0x4000 2:0x2000 4:0x1000 8:0x800 15 / 16:0x400 24:0x200 32:0x100 else -1;
  the current depth is 0x4d5f70, "software only" 0x4d5fa4.

Video mode table: count 0x4d5fd4, entries of 3 ints (width, height, depth) from 0x4d5fe4; current mode 0x4acc38 /
  0x4acc3c / 0x4acc40. Combo box messages: 0x14b reset, 0x161 init storage, 0x143 add string, 0x14e set selection.

Joystick tables (joystick count 0x4d9554):
  0x4d74a0 + joy * 0x43c: joystick record; +0x150 (0x4d75f0) counts the axis objects found (max 12), then three
  arrays of 12 ints indexed by that count: +0x158 (0x4d75f8) present = 1, +0x188 (0x4d7628) the object's id
  (object +0x14), +0x1b8 (0x4d7658) the object's flags & 1.
  0x4d667c + joy * 0x224: device records (first dword read by 0x488d60; count 0x4d623c).
  0x4d96e4 + joy * 0x90 + dir / 16: POV latch bytes (edge detection of the four POV directions).
  0x5d5cd4 count, 0x5d5cd8 + n * 0x40 joystick names.
  Binding capture result: 0x5d6234 joystick, 0x5d6238 stage (1 axes, 3 POV, 0 buttons, -1 none), 0x5d623c code
  (axis * 16 | 6 negative / | 7 positive, POV 0 / 0x10 / 0x20 / 0x30, button number unshifted).

Sound channel table (0x470ef0): 64 entries of 0x3c bytes at 0x6cb500.
  +0 flags (1 playing, 2 keep, 4 fixed, 8 paused, 0x10 positional), +1 voice id (0: unused), +2 countdown (signed),
  +4 next entry in the priority list (-1 end), +5 rank (-1 unranked), +0xc int, +0x14 float (probably distance),
  +0x28 float (that clamped), +0x2c float priority (+0xc / that squared), +0x30 float level (names are guesses).
  Globals: 0x4d453c QMixer session, 0x4b1c74 channels allowed, 0x4b1c90 / 0x4b1c94 distance clamp, 0x4b1c9c /
  0x4b1ca0 / 0x4a341c loudness thresholds, 0x4d455c EAX room active. QMixer import slots: 0x4a31f8 IsChannelDone,
  0x4a31fc PauseChannel, 0x4a3200 FlushChannel, 0x4a3208 (resume), 0x4a3290 user32 SendDlgItemMessageA.

Menu item float fields (item 0x730 bytes at *0x4cb4b8): +0x514 callback, +0x534 min, +0x538 max, +0x53c scale,
  +0x540 value, +0x544 step, +0x548 coarse step. The item template (0x4c9df8) holds the same as 0x4ca32c lo,
  0x4ca330 hi, 0x4ca334 range, 0x4ca338 value, 0x4ca33c step, 0x4ca340 coarse step, 0x4ca30c callback.

Cheat codes: typed characters shift into 0x4be3bc (33 characters, newest first); the codes are the strings at
  0x4acd5c.. (stored reversed so a match is at the buffer start): modes 0x4acd5c / 6c / 7c (config +0x97 = 2 / 1 /
  0), 0x4acd94 config +7, 0x4acda4 config +0x96, 0x4acdac config +0x95, race +0x721 bit 6 / 7, race +0x720 bits
  7 / 1 / 2 / 4 / 5 / 3.
