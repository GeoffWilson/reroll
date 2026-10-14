#!/bin/sh
# Compiles game sources for x86-64 (the 64-bit native build, docs/native/
# plan64.md phase 3) and prints what stops them: errors, assembler errors,
# and pointer/integer width warnings.
#     tools/x64check.sh src/game/orig_pt.c ...
cd "$(dirname "$0")/.."
for f in "$@"; do
    clang --target=x86_64-linux-gnu -O2 -c -o /dev/null -fwrapv -fno-strict-aliasing -fno-pic -mcmodel=small \
        -Wall -Wextra -Wno-missing-field-initializers -Wno-unused -Wno-ignored-attributes -Wno-unknown-pragmas \
        -Wpointer-to-int-cast -Wint-to-pointer-cast -Wshorten-64-to-32 \
        -DREROLL_NATIVE -fms-extensions -Isrc -Ibuild/render "$f" 2>&1 | grep -E "error|warning"
done
