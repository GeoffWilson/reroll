# Native build plan

Goal: run Rollcage as a native Linux program, without Wine or DLL injection,
keeping the game's code, data and feel, and then add modern features
(anisotropic filtering, force feedback, longer draw distance, resolution
options in the game's own menus).

## Approach: load the original, replace the platform

The reimplemented C (about 1350 functions) still calls into roughly 800
original functions (scene drawing, front-end 3D, the C runtime, platform
code). Porting all of those first would take a long time and gain little.
Instead, as OpenRCT2 did in its early days:

- **A 32-bit Linux program** (`build/rollcage`, i386, linked with 32-bit
  glibc, SDL3 and the Vulkan loader) maps Rollcage.exe's sections at their
  original addresses (image base 0x400000, up to 0x6d6000).
- **Its imports are ours.** The import table (about 190 functions from
  KERNEL32, USER32, GDI32, WINMM, ADVAPI32, ole32, DDRAW, DSOUND, DINPUT,
  DPLAYX and QMIXER) is filled with small implementations on Linux, SDL3
  and the Vulkan renderer (`native/`). Anything unimplemented goes to a stub
  that logs its name and stops.
- **The same reimplementation code.** The DLL sources (src/main.c hooks,
  src/game/*.c, the renderer) are built for i386 Linux and linked in; they
  patch the mapped code exactly as they patch the running exe today.
- **The game's own C runtime** (inside the exe) keeps working on top of the
  KERNEL32 functions; it does not need mapping to glibc.

Calling conventions: Win32 imports are stdcall; game functions are cdecl.
The i386 System V ABI matches MSVC for these, except:
- stack alignment: game code calls us with 4-byte alignment, so everything is
  built with `-mstackrealign`;
- `double` / `int64` in structs: `-malign-double` (not used after all: it breaks the system libraries' struct layout);
- the exe's SEH and C runtime use `fs:` (the Windows thread block): each
  thread gets a fake TEB through an LDT entry (`modify_ldt`), as Wine does.

## Steps

1. **Loader.** Map the exe, fill the import table (implemented functions,
   else named stubs), fake TEB, run the reroll initialisation (the DllMain
   body: install the hooks), then the exe's entry point (its C runtime
   start-up, then WinMain, which is our C `game_main`).
2. **KERNEL32 and friends.** Heap and virtual memory, files with Windows
   paths (C: = the install, D: = the CD contents; case-insensitive lookup),
   time, threads, events, mutexes, critical sections, TLS, the registry
   (ADVAPI32) as a config file seeded from the Wine prefix, WINMM timers
   (the 16 ms multimedia timer drives front-end animation), GDI stubs.
3. **Window and Vulkan.** USER32 windows and messages on one SDL3 window
   (keyboard events become WM_KEYDOWN for the game's window procedure); the
   renderer presents through SDL3's Vulkan surface (src/vkgame.c without the
   Win32 parts).
4. **DirectDraw / Direct3D as memory.** Fake COM objects for the methods the
   game calls: surfaces are plain memory (Lock returns them), the back buffer
   is ours (so the loading pictures and movies, drawn into it, show
   reliably), Direct3D devices/viewports/textures keep the handles the game
   expects but draw nothing (the packets go to Vulkan); the capability probe
   0x41f720 is skipped (settings keep their quirk flags).
5. **Input.** DirectInput keyboard and joystick on SDL3 (gamepad mapping as
   the current trigger/throttle fixes expect).
6. **Sound.** The QMixer API (25 functions: wave loading, channels, volume,
   frequency, 3D position) as a small mixer on SDL3 audio; DirectSound only
   where the game uses it directly; CD music (MCI) later, from ripped tracks.
7. **One process.** The race / front-end process swap (0x474cf0 starts a new
   "Rollcage.exe /FRONTEND" and exits) becomes a restart in place: reload
   the exe's data section, reset our own state, run WinMain again with the
   same window and Vulkan device.
8. **Movies** through the fake DirectDraw back buffer. Until then movie_play (src/game/st_sb.c) skips them natively, as if the file were missing: the exe's player waits forever at 0x492f59.

Then the features: anisotropic filtering (sampler), force feedback (SDL
rumble from the game's collision/force events), draw distance (far plane and
the visibility lists, building on the step-5 extra runs), and resolution /
display options in the front end.

Network play stays stubbed (DirectPlay creation fails, as with no network).

## Testing

SDL3's offscreen video and dummy audio drivers let the native program run
headless; the renderer can read frames back (vkr_end) for checks, so most
steps can be tested without opening windows. The user plays a build at the
end of each step.
