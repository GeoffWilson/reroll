Rollcage (reroll) 1.0.0
=======================

A native Linux version of Rollcage (1999, PC): the game's code has been
rewritten in C, it draws with Vulkan at any resolution (ultrawide included)
and runs at high frame rates, with modern gamepads, OpenAL sound and
network play over the internet.

You need your own copy of the Rollcage PC CD (or an image of it): nothing
from the game is included here. Source code and documentation:
https://github.com/GeoffWilson/reroll

1. Install the libraries (64-bit only, nothing 32-bit):

     Arch:           sudo pacman -S sdl3 vulkan-icd-loader openal libdecor
     Other distros:  SDL 3, the Vulkan loader, OpenAL Soft and libdecor

   plus the Vulkan driver for your GPU (on Arch one of nvidia-utils,
   vulkan-radeon or vulkan-intel). setup.sh also needs python3 and bsdtar
   (libarchive).

2. Copy the game's files from your CD (once):

     ./setup.sh "/path/to/Rollcage.cue"     (a BIN/CUE image: also gives CD music)
     ./setup.sh /path/to/rollcage.iso       (an ISO: no music)
     ./setup.sh /run/media/$USER/ROLLCAGE   (the mounted disc: no music)

   The music is the CD's audio tracks, so only a BIN/CUE image gives music.
   The game expects the original PC release's Direct3D/Rollcage.exe; setup.sh
   warns if yours is a different version.

3. Play:

     ./run.sh

   It opens at your desktop resolution. Options > Display switches between
   fullscreen and a window, picks a resolution and turns the intro movies
   on or off; Options > Graphics has anisotropic filtering. Gamepads work
   and can be plugged in at any time (XInput layout; triggers for the
   throttle, force feedback as rumble).

Network play
------------
Multiplayer > Networked. As in the original there is no server list: one
player hosts, the others type the host's address.

  Host:  Start New Game. Your router must forward UDP port 47700 to your
         PC (only the host needs this).
  Join:  Configure Connection > TCP IP Address: the host's internet IP
         address (or hostname). Then Join Existing Game, pick the game,
         choose a driver and "Enter the Cage". Leave the address empty to
         search your local network instead.

Then the host picks the track and "Lock the Cage !!!" starts the race.
Everyone needs the same version. After each race everyone goes back to the
Network Game menu: host and join again for the next one.

Settings and logs
-----------------
Your settings and league progress are saved next to the game, in
game/Program Files (x86)/Psygnosis/Rollcage/Direct3D (reroll_registry.reg,
reroll_display.cfg), with the logs native.log and reroll.log, which are
useful when reporting a problem.

Races run at up to 500 fps; with variable refresh rate (VRR) turned on in
your display settings they look smoothest.

Useful settings (put in front of ./run.sh, e.g. REROLL_VSYNC=1 ./run.sh):
  REROLL_VSYNC=1       lock to one frame per display refresh instead
  REROLL_MAX_FPS=n     another frame cap (0 = none)
  REROLL_NO_RUMBLE=1   no rumble
  REROLL_WINDOW=WxH    start in a window of that size
  REROLL_NET_PORT=n    host or join on another UDP port than 47700

Rollcage is (c) Psygnosis / Attention to Detail. This project is not
affiliated with or endorsed by them or the current rights holders.
reroll itself is released under the MIT license (LICENSE).
