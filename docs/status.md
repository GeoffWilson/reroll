# reroll status (2026-10-11)

## Where things stand

Rollcage (1999) runs as a **native 32-bit Linux program** (`build/rollcage`)
with a **Vulkan renderer** at native resolution (3440x1440 ultrawide), the
original physics and feel, and about 1350 of the game's functions
reimplemented in C. The Wine + injected DLL route still builds and works
(`make install`, `REROLL_VULKAN=1 ~/Games/rollcage/run-rollcage.sh`).

Run natively: `~/Documents/rollcage/reroll/build/rollcage` (straight to the main menu; no launcher dialog, no intro movies yet)
(build: `make native`; needs lib32-sdl3, lib32-vulkan-icd-loader).

## Done

**Reimplementation (earlier sessions):** physics, car controls, AI, race
set-up, front-end logic, start-up, sound engine and the remaining game
logic: ~1350 hooked functions, checked with the Unicorn emulator tests
(`make emu-test`) and in-game verification (`REROLL_VERIFY=1`, which costs
~10 ms a frame, so play without it). 120 fps render interpolation with the
physics on its original 32 ms step.

**Vulkan renderer (docs/renderer/plan.md, steps 1-7):**
1. Frame capture (F9) of the game's ordering tables and packets.
2. Packet decoder (src/render/packets.c).
3. Offline Vulkan back end (src/render/vkr.c), matching the game's
   screenshots.
4. Live viewer (tools/rview.c) for testing.
5. Widescreen: culling widened to the window's aspect, ultrawide edges
   filled from neighbouring visibility cells, HUD kept in the 4:3 middle.
6. Vulkan in the game's own window (src/vkgame.c): DirectDraw windowed,
   loading pictures through the image tiles, textures uploaded on change,
   exact 16.16 camera (src/subunit.c), per-draw counters kept to the step
   rate. ~113 fps in races at 3440x1440.
7. Screen overlays (lens drops, respawn sparkle) across the ultrawide width;
   pixel caps need no change (the game keeps an internal 1024x768 screen).
8. Longer draw distance (REROLL_DRAW_DISTANCE, default the whole track with
   Vulkan): the track and its static objects beyond the game's visibility
   lists (renderer plan step 8).
9. Options > Display (native build): Mode (Fullscreen / Windowed) and
   Resolution (Desktop or the monitor's modes), Apply. native/user32.c
   applies them to the SDL window (exclusive fullscreen for a non-desktop
   resolution, borderless at the desktop's) and saves reroll_display.cfg;
   the swapchain and the widescreen culling follow the window. The screen
   is 0x7b (unused by the original); labels use the string file's markup
   "Label ~>~P0~S" (value in the right-hand column). Window decorations on
   GNOME/Wayland need lib32-libdecor; its GTK plugin shows no title bar for
   us, so native/loader.c points libdecor at the cairo plugin alone
   (~/.cache/reroll/libdecor) unless LIBDECOR_PLUGIN_DIR is set.
10. Anisotropic filtering (Options > Graphics, native; REROLL_ANISOTROPY=n
   in both builds): Off / 2x / 4x / 8x / 16x, saved in reroll_display.cfg.
   Texture pages get a 4-level mip chain made on the CPU (alpha-weighted
   colour, so colour-key texels don't darken edges; capped as the pages are
   atlases). Off samples the base level only, as before; on, the sampler is
   trilinear with that many samples (vkr_set_anisotropy, between frames).

**Native build (docs/native/plan.md, steps 1-7):**
1. Loader (native/loader.c): maps Rollcage.exe at 0x400000, fills its
   imports from native/*.c, fake thread block on fs:, runs the same
   reimplementation sources built for Linux.
2. KERNEL32/ADVAPI32/WINMM/GDI32 (files with case-insensitive Windows paths,
   threads, sync, registry in Direct3D/reroll_registry.reg imported once
   from Wine, timers).
3. USER32 on one SDL3 window; Vulkan through SDL.
4. Fake DirectDraw/Direct3D (memory surfaces; reports d7vk's device so the
   game makes the same choices; x87 single precision as D3D sets it).
5. DirectInput on SDL3 (keyboard, gamepad laid out as XInput-through-
   DirectInput, as the control settings expect).
6. Sound: QMixer replaced by an SDL3 mixer (native/qmixer.c); CD music from
   the BIN/CUE audio tracks (REROLL_CD_CUE, else the first .cue in
   ~/Games/rollcage or ~/Documents/rollcage). Music volume is the game's
   Music slider.
7. One process: leaving a race restarts the game in place (same window).

Things learned that matter: game memory must stay below 2 GB (bit 31 is an
ordering-table flag) -> native/lowmem.c; the low megabyte is mapped as on
Windows (the original reads through null pointers there); the network
start-up needs a DirectPlay object even in single player; textures need
DDSCAPS_VIDEOMEMORY; GetTempFileNameA must create the file (replays).

**Testing without windows:** `SDL_VIDEO_DRIVER=offscreen` +
`REROLL_SHOT_EVERY=n` (frames saved as shot_*.ppm in the Direct3D folder) +
`REROLL_KEYS="ms:Key[:hold_ms],..."` (scripted key presses),
`REROLL_NO_PADS=1` (ignore real pads), `REROLL_AUDIO_DUMP` /
`REROLL_CD_DUMP` (raw audio). Pause menu: Escape, Up, Return, Down, Return
quits a race. Kill test runs with SIGKILL (SDL turns SIGTERM into a quit the
game ignores in some screens). Player 1 is bound to the pad, so the keyboard
can't drive in a headless race.

## Next steps

1. **Movies** (native plan step 8): the intro/FMV player through the fake
   DirectDraw back buffer.
2. **Features the user asked for:**
   - gamepad force feedback (the game already creates DirectInput force
     effects for devices that report it: implement IDirectInputEffect on
     SDL rumble);

3. Later: Doppler in the mixer; port the remaining original code (scene
   drawing, front-end 3D, C runtime, platform) and then a 64-bit build;
   network play (skipped for now).

Known gaps: none blocking. The pause/menus, races, sound, music, pad and
restarts are confirmed working natively by the user.
