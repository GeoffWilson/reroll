# Vulkan renderer plan

Goal: a new renderer that keeps Rollcage's look and feel but draws at any
resolution and aspect (3440x1440 ultrawide included), runs first inside the
injected DLL, and later carries over unchanged to a native build.

Background surveys (read these for the details and addresses):

- [pipeline.md](pipeline.md): frame flow, ordering tables, packet formats,
  the Direct3D back end, textures, track data, interception options.
- [objects.md](objects.md): cars, effects, weapons, weather and everything
  else drawn per view, with the state each reads.
- [views.md](views.md): camera and projection maths, viewports and split
  screen, 640x480 / 4:3 assumptions, HUD and 2D, front end, present timing.

## The decision: replace the back end, keep the scene code

The game is a PS1-style ordering-table renderer. `draw_view` 0x479be0 and
the drawers under it build lit, textured primitives ("packets") into
512-bucket ordering tables. `DrawOTag` 0x421860 then walks them and the
back end (0x420be0 and below) turns each packet into Direct3D draw calls.

We replace `DrawOTag` and the back end, and keep everything that builds
packets:

- The 3D packets carry **camera-space float x, y, z per corner** (vertex
  record +0x10, +0x14, +0x04), so the new back end projects them itself at
  any resolution and aspect, with a real depth buffer.
- Every 3D thing (track, sky, scenery, cars, effects) arrives in about 12
  packet formats, already lit and textured, so one decoder keeps the
  original look: colours, PS1 blend modes, overlay layers, sprite sizes,
  draw order.
- The scene code also changes game state as it draws: triggers are queued
  from the model drawer, effects age inside draw code, and beams use the
  game RNG. Keeping it untouched keeps the game behaving exactly as before.
- Textures stay in RAM as 8-bit pixels plus palettes. They get uploaded once
  per page, without the D3D-era quirks.

Replacing the drawers themselves (static GPU buffers for the track, and so
on) is an optional later stage (pipeline.md 6.2, stage 2). It isn't needed
for hi-res or ultrawide.

## Architecture

```
src/render/
  packets.c   decode an ordering table into a neutral draw list
              (no Win32, no Vulkan: builds natively and in the DLL)
  draw.h      draw list: camera-space vertices, uv, page, blend, colour, bucket
  vk.c        Vulkan device, swapchain, pipelines per blend mode, texture
              pages, the 2D layer (surface from a Win32 HWND now, SDL3 later)
  shaders/    GLSL -> SPIR-V at build time (glslc), embedded as arrays
```

- In the DLL, Vulkan comes from Wine's `vulkan-1.dll` (loaded with
  LoadLibrary/GetProcAddress). It already works in this prefix: the game's
  D3D runs through d7vk.
- The decoder is plain C with no platform calls, so the same code serves
  the DLL, an offline test tool and the native build.

## Steps

Each step ends with something you can see or test. The game only needs
launching once per step, because development runs from captured frames.

1. **Frame capture.** Hook `DrawOTag` (still calling the original) and, on
   a hotkey, dump one frame to a file: the ordering tables with their
   packets, the camera, viewport and projection globals, and the texture
   pages and palettes referenced. One play session gives a set of captures
   covering tracks, split screen, weapons, weather, mirror and replay.
   **Done (2026-10-11):** `src/capture.c`, read by `tools/rcap.py`.
   - Files are written relative to the game's working directory, which is
     `IDXData`: `IDXData\captures\frame_<pid>_<n>.rcap`. The game's
     screenshot goes to `C:\RollCage Dumps\Dump<n>.bmp`.
   - 15 captures and their screenshots (as `*_original.png`) are in
     `reroll/captures`: front end, races, mirror, pause and split screen,
     all at 1024x768.
   - **Vertex rule** (from these captures): the x/y fields of a vertex
     record can't always be trusted.
     - Track, models and lines: camera-space x/y re-project exactly onto
       the stored screen x/y.
     - Track seams: a few vertices differ by up to 0.5 px (the seam fix).
     - Billboards from the sprite builder (FT4 0x9a), overlay quads near the
       screen, and flag 0x40 vertices: only screen x/y and z are valid; the
       x/y fields are leftovers.
     - Decoder rule: use camera-space x/y when they re-project within
       0.5 px; otherwise rebuild them as `(s - c) * z / H` with that walk's
       H and centre. Near-clipped vertices (flag 0x10) have no valid screen
       x/y and use x/y as stored.
   - Two packets with code 0 show up in split screen; find out what they
     are in step 2.
2. **Decoder and offline check.** A native Linux tool reads a capture and
   decodes every packet into the draw list. Exactness test: re-projecting
   the camera-space vertices with the original H and centre must give the
   original's screen x/y stored in the same records. That is the emulator
   tests' equivalent here.
   **Done (2026-10-11):** the decoder is `src/render/packets.c` with the
   draw list in `src/render/draw.h`; the test tool is `tools/rcapdec.c`.
   - Build and run all captures with `make rcapdec`. The decoder also builds
     for the DLL's Windows target.
   - Positions follow the back end's own path choice:
     - all corner flags clear: the stored screen x/y, with camera space
       rebuilt from them;
     - flags set, without 0x40: camera-space x/y/z;
     - 0x40 set: screen x/y again.
   - Colours and alpha follow the disassembled handlers:
     - each corner's colour dword, with its top byte as alpha;
     - corner 0 of GT4, GT3 and FT4 takes its alpha from the packet's +0x22
       byte;
     - FT3 forces 0xff, F4 uses r|g|b, and lines use
       (r/2+g/2+b/2)*10/16.
   - Sprites use the current mode from earlier packets. Codes the
     dispatcher doesn't handle (0 included) are no-ops.
   - **Result over all 15 captures:**
     - Every packet was understood: none unknown or unreadable. The only
       ignored ones are fog markers and two code-0 packets.
     - Every clip-path vertex re-projects exactly.
     - **No 2D packets are used at all.** The HUD, menus, pause panel and
       overlays are 3D packets built in screen space near z = 2. So in the
       new renderer they come out of the same 3D path; laying them out for
       a new aspect relies on the game's internal screen size (step 5).
3. **Offline Vulkan renderer.** The same tool renders a capture with Vulkan
   to a PNG at any size, next to a screenshot of the original frame:
   - opaque primitives with depth testing;
   - translucent ones back to front in bucket order;
   - PS1 blend modes and colour-key texel 0 reproduced;
   - textures with original-looking sampling (nearest; filtering as an
     option).
   Iterate here until 640x480 matches the original, then try 3440x1440.
   **Done (2026-10-11):** `src/render/vkr.c` (Vulkan back end), the
   shaders in `src/render/shaders/`, and the tool `tools/rcaprender.c`.
   - `make rcaprender` renders every capture to `captures/render/`.
     `tools/rcapcompare.py` scores a render against the screenshot and
     writes a side-by-side image with a difference map.
   - Vulkan functions come through vkGetInstanceProcAddr, so the same code
     can run in the DLL.
   - Things that had to match the original:
     - **Z-buffer mode.** On this setup the game runs in it (0x6cce7c,
       0x6cce80 and 0x6cce84 all set): primitives are bucketed by texture
       page; depth test LESSEQUAL on sz = z/65536, interpolated linearly
       across the screen; depth cleared at each view's first table; only
       tm 0 writes depth. Without it, car shadows drew over the cars.
     - **Clear colour** is kept at 0x6ccd8c (set by 0x428390); the sea
       behind the track is just the clear colour.
     - **Clip coordinates** are built straight from camera space (no
       divide), so polygons crossing the camera clip at the near plane like
       the original's clipper does.
     - The blend pairs (additive ONE/ONE and multiply ZERO/SRCCOLOR here),
       colour key, MODULATE texturing and the texel tables are as in
       vkr.c's header.
   - **Result at 1024x768:** 1.4-5% of pixels differ by more than 24 in
     most frames. What's left is rain, sparks and dust, which are random
     each frame, plus sub-pixel edges. The worst are a smoky replay (13%)
     and the paused frame (8%).
   - The pause panel was missing because it's drawn through the second
     DrawOTag wrapper 0x428480 (the PS1-style buffer swap); capture.c now
     hooks that too, so recapture to check it.
   - Renders at 2048x1536 and 3440x1440 (pillarboxed: the captured view is
     4:3) come out sharp.
4. **Live side-by-side window.** In the DLL, run the Vulkan back end in a
   second window from the real ordering tables each frame, while the
   original keeps drawing the game window. This proves the live data path,
   timing and texture updates (palette animation, time-warp page) with
   nothing at risk.
   **Built (2026-10-11), as a separate native viewer rather than a second
   window in the game.** The game runs inside a single-window gamescope
   session, so a second window there wouldn't show next to it.
   - With `REROLL_LIVE=1`, capture.c records every drawn frame into shared
     memory: `/dev/shm/reroll_live`, which Wine maps as
     `Z:\dev\shm\reroll_live`. The layout:
     - 64 texture slots, each with a version counter, rewritten only when a
       page's checksum changes;
     - three rotating frame slots in the capture chunk format, each with a
       small BASE.
   - `build/rview` (SDL3 + the same vkr.c, now with swapchain output)
     renders the latest frame in its own resizable window next to the
     gamescope window.
   - `tools/rlive_feed.py` publishes a saved capture there, to test the
     viewer without the game.
   - Live mode costs nothing unless the variable is set.
   - The in-game Vulkan port (no C runtime, Wine's vulkan-1.dll) moves to
     step 6, where it replaces the main window.
   - Run: `build/rview &`, then `REROLL_LIVE=1 ~/Games/rollcage/run-rollcage.sh`.
   - **Confirmed in game (2026-10-11).** First-session fixes:
     - Live frames are published at the flip 0x4876f0 (trampoline), which
       every screen update goes through. Loading screens flip themselves,
       outside present_frame.
     - Single packets drawn with DrawPrim 0x428460 are recorded as
       one-packet walks. That covers the pause panel (pause_draw 0x463f90)
       and the view border quads (the black end-of-race letterbox, from
       0x4284b0 and 0x428770).
     - A flip with no packets sends the back buffer (*0x4d5f4c) as 'SCRN'.
       The viewer shows it as a picture (RD_IMAGE, image slot 64): this is
       how the loading pictures, which are DirectDraw blits, appear.
5. **Ultrawide and resolution.** **Done (2026-10-11), `src/widescreen.c`,
   enabled with `REROLL_ASPECT`** (a ratio such as `2.389`, or a size such as
   `3440x1440`; off by default).
   - **Approach: a virtual wide screen.** The game keeps its real 1024x768
     screen and focal length H, so the middle of every view is the original
     4:3 picture (Hor+: same vertical field of view, more to the sides) and
     the original window stays as it was. Only the culling is widened.
   - **Clip rectangle and frustum.** The clip rectangle's left/right
     (0x6ccd64/68) are widened after 0x41f2d0, for views touching the screen
     edges. The frustum planes (0x417ed0) are built for the widened width,
     keeping the original's "leaves the screen" flag 0x4b7fec.
   - **Integer compares of float bits.** The projection 0x426b00 (reached
     through *0x6ccd80) and the outcode helper 0x426b80 compare screen x/y
     with the bounds as integers on the raw float bits. That only orders
     non-negative floats, so with a negative left bound everything just left
     of the old edge was culled (the stepped holes). In widescreen mode both
     are replaced by assembly copies that use x87 compares, identical for
     non-negative bounds. A coordinate-shift workaround was tried first and
     dropped: the mirror border and countdown don't follow it.
   - **Visibility lists.** The track's per-cell lists are precomputed for the
     4:3 view. In widescreen mode the list manager 0x47a030 is wrapped: before
     the original builds, lights and returns the view's own list, extra runs
     are drawn:
     - **Edge cells:** sections from the lists of the cells under points
       along the view's edges (3 directions per side, 64-4096 units out)
       that the camera cell's list lacks.
     - **Gap rows:** rows of the camera cell's own sections that its list
       leaves out.
   - **Fixed tables.** The game's list steps (0x47a500 textures, 0x47c420 /
     0x47c730 lighting) use fixed tables sized for its own lists. Those are
     0x47a500's stack arrays (~50 runs), 0x5d91c0 (64 runs) and 0x5d7540
     (552 rows), so extra runs go through them one section at a time, in
     chunks of at most 32 runs / 400 rows.
   - **Shared render records.** Track render records (packets) are shared
     between sections no list has together, so drawing an extra section and
     then the normal track would link one packet twice. That loops the
     ordering table and hangs the game. So after each extra chunk is drawn
     (always into the world table 0x6cd510), its new packets are copied into
     a pool of our own and relinked, and the original's "previous list" is
     emptied so it rewrites its rows' texture words.
   - **Countdown.** The countdown 0x46a9e0 uses the clip rectangle's left
     edge as its origin, so it's drawn with the real bounds.
   - **HUD and front end.** The HUD stays laid out for the 4:3 middle (the
     user's preference) and the front end stays 4:3. No FOV cap was added.
   - **Tools.** The viewer opens at the REROLL_ASPECT aspect. The DLL also
     logs faults (src/crash.c) and reports an ordering table that loops.
   - Run: `REROLL_ASPECT=3440x1440 build/rview &`, then
     `REROLL_LIVE=1 REROLL_ASPECT=3440x1440 ~/Games/rollcage/run-rollcage.sh`.
6. **Take over the main window (done).** `REROLL_VULKAN=1` (src/vkgame.c,
   src/render/vkwin.c, src/subunit.c; renderer files built into the DLL
   with shim headers src/render/shim and src/render/heap.c for malloc):
   - **Window.** DirectDraw runs in the game's own windowed mode (the /W
     flags 0x4acc5c / 0x4acc60 = 0: no display mode change), but the
     window is created the fullscreen way (0x4771c0 wrapped: a topmost
     popup at the screen size; Gamescope needs that to show the new
     process after a race). Vulkan comes from vulkan-1.dll and presents to
     the game's window itself (REROLL_VK_CHILD=1 uses a child window over
     it, the automatic fallback if the window's surface is taken; under
     desktop Wine child windows are held to 60 Hz). Mailbox under
     Gamescope; REROLL_VSYNC=1 forces FIFO, 0 allows immediate. Size:
     REROLL_RES (default 3440x1440 in run-rollcage.sh, which sizes
     Gamescope to it), else the screen. Culling widens to its aspect.
   - **Drawing.** DrawOTag (0x428470 / 0x428480), DrawPrim (0x428460) and
     the image tiles of 0x4203a0 (loading pictures: the call at 0x420582,
     the only other caller of the back end 0x420be0) are decoded straight
     from memory and drawn; the flip 0x4876f0 presents and the original
     flip is skipped. Frames with no packets show the back buffer. The
     Direct3D probe 0x41f720 keeps its original flips.
   - **Textures** are read from the slot surfaces when a slot's surface
     changes, after texture_upload 0x41e710 (wrapped), for slots in the
     re-upload queue drained by 0x429070 (hooked) when their checksum
     changed, and one slot per frame by checksum. A surface with no colour
     key gets texel 0 transparent (as the validated captures). vkr batches
     uploads into the frame's command buffer and keeps its staging and
     vertex buffers.
   - **16.16 camera** (src/subunit.c): inside draw_view, after the camera
     matrix 0x423d30 and outside the HUD 0x438310, the transforms
     0x426a20 / 0x426be0 run with TR + R' f (f = the dropped fraction of
     the camera position, kept by a C 0x423ce0).
   - **Per-draw counters** (src/game/interp.c): shake +0x6a8, HUD fade and
     flash 0x6c7fdc..0x6c7fef, the warning beep (held non-zero on extra
     draws so it plays once) and the tint slot +0x256 are put back after
     the extra in-between draws.
   - **Checks.** tools/vkwintest.c (`make vkwintest`) runs the DLL's
     renderer path under Wine on a capture: identical pixels to the native
     tool, and presents to a window. In game: about 8.8 ms a frame in a
     race (120 fps cap) of which ~0.3 ms decode and ~1.5 ms present; the
     log reports this every 1000 frames. Run without REROLL_VERIFY for
     play: verification costs ~10 ms a frame.
   - Known gap: the Rollcage picture shown while the menu process starts
     after a race is missing (its first frames have no packets, and the
     windowed back buffer they fall back to doesn't hold it). Left: the
     native build won't swap processes.
   - Run: `REROLL_VULKAN=1 ~/Games/rollcage/run-rollcage.sh`.
7. **Resolution-dependent effects (done).** The game keeps its own
   1024x768 screen and Vulkan scales it, so everything sized in game
   pixels (the snow 12 px cap, halo 0x80 cap, blast ring radii, particle
   caps, the smoke overdraw budget) behaves exactly as at 1024x768, only
   drawn sharper: nothing to change. What did need work was the ultrawide
   sides (src/widescreen.c):
   - lens flares / sun glare test against the clip rectangle (already
     widened in step 5), and the full-screen flash, tints and time-warp
     tunnel are drawn over it: they cover the sides as they are (the
     tunnel texture is stretched to the wide view; user: looks good);
   - screen flakes / lens drops (0x485140 wrapped): the spawn x is spread
     from the 4:3 width over the wide one (the mover only drifts them
     outwards and respawns them by age);
   - respawn / snatch sparkle (0x490430 wrapped): race +0xa82 (the
     overlay width) set to the widened clip width while it draws, so the
     squares cover the view instead of bunching at its left.
   Checked in game 2026-10-11 (rain, time warp, sparkle).

8. **Draw distance (2026-10-11, src/widescreen.c, `REROLL_DRAW_DISTANCE`).**
   On by default with the Vulkan renderer (65536 = the whole track; `0`
   turns it off, a number limits it in world units). The game's far plane
   is already 65536 (the graphics slider's maximum); the pop-in came from
   the per-cell visibility lists, which stop at a distance.
   - **Far track.** At track load every cell layer's list is scanned
     (the grid rows 0x6bfad4, layers bit 31 of +4) for which rows of each
     section any list reaches and at which detail levels. Each view, the
     sections no list so far has get all their reached rows within the
     distance, at the level for the distance (measured from the game's
     lists: level 1 to ~2000 units, 2 to ~6000, then 3), through the
     game's row test 0x47a1d0 and the step-5 extra-run path.
   - **Shared render records.** The track's records are shared between
     any quads that no one list draws together, even rows of one section
     at one level. The record is the u16 at +0 of each 8-byte quad (LOD n
     list at section +0x4c + 4n; an overlay quad also uses the next
     record). Chunks are split wherever a record would repeat. Some
     sections carry level data that repeats one record across a row
     (levels they don't really have): those levels are rejected at load,
     and runs at them are drawn at level 1. If a chunk still loops, its
     sections drop to level 1 for the rest of the race (logged).
   - **Objects** (`REROLL_FAR_OBJECTS=0` turns them off). 0x459b20
     (static objects and the cars in the visible cells) is wrapped: before
     the original's pass, it runs once more for the cells of the extra rows,
     with no dynamic lights. Then the original runs as always.
     - Like the track's render records, object packet buffers are shared
       between objects never visible together. Drawing a far object next to
       a near one linked its packets twice, which looped the world table:
       a dark, black-track view at a few fps. So the far pass's packets
       are detached into the pool (both 0x6cd510 and 0x6cdd80).
     - Cell chains can share tails, so objects are deduplicated one by one
       (record index). Objects the original's cells have, and cars (their
       own pass), get bit 0x400 cleared for the far call only.
     - The trigger queue (0x6b9ba0..0x6b9cbb, 0x6b9b94) and the GTE /
       projection state 0x6ccbe0..0x6cce3f are put back. A heap diff
       across the far call showed nothing else changes but its packets.
     - Moving cell objects (sprites, particles, light glows) are not
       added.
   - Cost: ~0.1-0.2 ms per view; the detach pools are 8 MB each.

After step 6 the game no longer needs the original back end. Steps 2-3 and
the decoder are what the native build will use (with an SDL3 window, see
the native build notes in the project memory).

## Risks and open questions

- **Z-buffer mode (0x6cce84)** buckets by texture page, not depth. Decide
  whether to force painter's mode, or rely on our own depth and the
  bucket order for translucents.
- **Seams:** the track's T-junction seam fix nudges vertices in screen
  pixels. At high resolution, cracks may show between detail levels; if so,
  redo the fix in camera space.
- **Visibility tables:** if any precomputed visibility assumes the 4:3 FOV,
  ultrawide will pop at the edges. The cell lists cut off by distance, so
  this is probably fine, but it needs checking in step 5.
- **Fog** appears never to be enabled in the D3D build (pipeline.md 3.6).
  Confirm against the original on screen.
- **Split screen** ages effects once per view. That's original behaviour;
  keep it.
- **Movies** stay on the original player for now.
