#!/bin/bash
# run.sh: starts Rollcage (reroll). Extra environment settings are passed through.
here="$(cd "$(dirname "$0")" && pwd)"
d3d="$here/game/Program Files (x86)/Psygnosis/Rollcage/Direct3D"
[ -f "$d3d/Rollcage.exe" ] || { echo "run ./setup.sh first (it needs your Rollcage PC CD or its image)"; exit 1; }
missing="$(ldd "$here/rollcage" 2>/dev/null | grep 'not found')"
if [ -n "$missing" ]; then
    echo "missing libraries:"; echo "$missing"
    echo "install SDL3, the Vulkan loader, OpenAL Soft and libdecor (see README.txt),"
    echo "e.g. on Arch: sudo pacman -S sdl3 vulkan-icd-loader openal libdecor"
    exit 1
fi
export REROLL_DRIVE_C="$here/game"
cue="$(ls "$here"/cd/*.cue 2>/dev/null | head -1)"
[ -n "$cue" ] && export REROLL_CD_CUE="$cue"
cd "$d3d" && exec "$here/rollcage" "$@"
