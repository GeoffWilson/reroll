#!/bin/sh
# A private emulator ELF: the Makefile's EMU_SRCS plus extra sources, with the
# Makefile's emulator flags, built in its own directory so parallel workers
# don't share objects.
#     tools/emu_elf.sh NAME src/game/new_a.c...   ->  build/emu_NAME.elf
set -e
cd "$(dirname "$0")/.."
name=$1
shift
SRCS=$(sed -n 's/^EMU_SRCS := //p' Makefile)
FLAGS=$(sed -n '/^build\/emu_fixed.elf:/,/ld.lld/p' Makefile | grep -o -- '-O2.*-Isrc')
mkdir -p "build/emu_$name"
rm -f "build/emu_$name"/*.o
for f in $SRCS "$@"; do
    clang --target=i686-linux-gnu $FLAGS -c "$f" -o "build/emu_$name/$(basename "$f" .c).o"
done
ld.lld -m elf_i386 -static -e 0 -Ttext=0x10001000 "build/emu_$name"/*.o -o "build/emu_$name.elf"
