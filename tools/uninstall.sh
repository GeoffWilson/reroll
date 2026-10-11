#!/usr/bin/env bash
# Restore the game's original QMIXER.DLL.
set -euo pipefail

dir="${1:?usage: uninstall.sh <game Direct3D dir>}"
if [[ ! -f "$dir/qmixer_orig.dll" ]]; then
    echo "no qmixer_orig.dll in $dir; nothing to restore" >&2
    exit 1
fi
mv -f "$dir/qmixer_orig.dll" "$dir/QMIXER.DLL"
rm -f "$dir/reroll.log"
echo "original QMIXER.DLL restored in $dir"
