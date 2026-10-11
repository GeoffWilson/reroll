#!/usr/bin/env bash
# Install build/QMIXER.DLL into the game's Direct3D folder.
# The original QMIXER.DLL is kept as qmixer_orig.dll; our DLL forwards to it.
set -euo pipefail

dir="${1:?usage: install.sh <game Direct3D dir>}"
here="$(cd "$(dirname "$0")/.." && pwd)"
orig_sha=0d3784fad66e05de38253861efcdfda200da4eb60c154ce23955b4a2945b110e

if [[ ! -f "$dir/qmixer_orig.dll" ]]; then
    actual="$(sha256sum "$dir/QMIXER.DLL" | cut -d' ' -f1)"
    if [[ "$actual" != "$orig_sha" ]]; then
        echo "QMIXER.DLL in $dir is not the original (sha256 $actual); not touching it" >&2
        exit 1
    fi
    mv "$dir/QMIXER.DLL" "$dir/qmixer_orig.dll"
    echo "original QMIXER.DLL kept as qmixer_orig.dll"
fi
# Copy then rename, so a running game keeps its already-loaded copy intact.
cp "$here/build/QMIXER.DLL" "$dir/.QMIXER.DLL.new"
mv -f "$dir/.QMIXER.DLL.new" "$dir/QMIXER.DLL"
echo "installed reroll QMIXER.DLL into $dir"
