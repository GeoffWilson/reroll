#!/bin/bash
# setup.sh <your Rollcage PC CD>: copies the game's files from your own disc into ./game
#   <your Rollcage PC CD> can be: a .cue (BIN/CUE image), an .iso, or the mounted disc's folder.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
src="$1"
if [ -z "$src" ]; then
    echo "usage: ./setup.sh <Rollcage .cue | .iso | mounted CD folder>"; exit 1
fi
game="$here/game/Program Files (x86)/Psygnosis/Rollcage"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$game/Direct3D" "$game/IDXData" "$game/movies"

extract_iso() { # iso -> files
    bsdtar -xf "$1" -C "$tmp" DISK1/ROLLCAGE/DIRECT3D/ROLLCAGE.EXE DISK1/ROLLCAGE/IDXDATA DISK1/ROLLCAGE/MOVIES 2>/dev/null ||
        bsdtar -xf "$1" -C "$tmp" "DISK1/ROLLCAGE/DIRECT3D/ROLLCAGE.EXE" "DISK1/ROLLCAGE/IDXDATA/ROLLCAGE.IDX" "DISK1/ROLLCAGE/IDXDATA/ROLLCAGE.IMG"
    from="$tmp/DISK1/ROLLCAGE"
}

case "$src" in
*.cue|*.CUE)
    bin="$(dirname "$src")/$(grep -m1 -i '^FILE' "$src" | sed 's/^FILE "\(.*\)".*/\1/')"
    [ -f "$bin" ] || { echo "cannot find the data track $bin"; exit 1; }
    echo "converting the data track ..."
    python3 - "$bin" "$tmp/disc.iso" <<'PY'
import sys
src, dst = sys.argv[1:3]
with open(src, 'rb') as f, open(dst, 'wb') as o:
    while True:
        s = f.read(2352)
        if len(s) < 2352:
            break
        off = 24 if s[15] == 2 else 16   # mode 2 form 1 / mode 1
        o.write(s[off:off + 2048])
PY
    extract_iso "$tmp/disc.iso"
    # keep the CD image for the music (the audio tracks)
    mkdir -p "$here/cd"
    for f in "$(dirname "$src")"/*.bin "$(dirname "$src")"/*.BIN "$src"; do
        [ -f "$f" ] && ln -sf "$(cd "$(dirname "$f")" && pwd)/$(basename "$f")" "$here/cd/"
    done
    ;;
*.iso|*.ISO)
    extract_iso "$src" ;;
*)
    from="$(find "$src" -maxdepth 2 -iname rollcage -type d -path '*DISK1*' | head -1)"
    [ -n "$from" ] || { echo "no DISK1/ROLLCAGE folder in $src"; exit 1; } ;;
esac

exe="$(find "$from" -maxdepth 2 -iname rollcage.exe -ipath '*direct3d*' | head -1)"
cp "$exe" "$game/Direct3D/Rollcage.exe"
for f in "$(find "$from" -maxdepth 1 -iname idxdata -type d | head -1)"/*; do cp "$f" "$game/IDXData/"; done
movies="$(find "$from" -maxdepth 1 -iname movies -type d | head -1)"
[ -n "$movies" ] && for f in "$movies"/*; do cp "$f" "$game/movies/"; done
sum="$(sha256sum "$game/Direct3D/Rollcage.exe" | cut -d' ' -f1)"
if [ "$sum" != 0648503bf95e692859a96efcacc101b33af9312e4e4ba80c6e9d00692c0e5879 ]; then
    echo "warning: this Rollcage.exe is a different version from the one reroll was made for; it may not work"
fi
echo "done: game files in ./game. Start with ./run.sh"
