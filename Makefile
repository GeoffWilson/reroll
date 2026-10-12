# Builds build/rollcage: Rollcage as a native 32-bit Linux program (docs/native/plan.md),
# with clang (-fwrapv: signed arithmetic wraps, as in the original machine code;
# -fno-strict-aliasing: fields are read and written at mixed widths;
# -fixup-byte-word-insts=false: clang 23 widens "(x & ~0xff) | p[x]" into a movzx that loses x),
# and the tests and tools around it.

CC      := clang
GAME_SRCS := src/main.c src/hook.c src/log.c src/capture.c src/widescreen.c src/crash.c src/vkgame.c src/subunit.c src/render/vkwin.c \
           src/render/vkr.c src/render/packets.c src/runtime.c src/verify.c src/coverage.c src/game/gt20.c src/game/input.c src/game/controls.c src/game/frame.c src/game/interp.c src/game/physics.c src/game/fixed.c src/game/events.c src/game/collide.c src/game/forces.c src/game/wheels.c src/game/drive.c src/game/track.c src/game/sweep.c src/game/detect.c src/game/trackhit.c src/game/pair.c src/game/objhit.c src/game/probes.c src/game/spin.c src/game/rolling.c src/game/script.c src/game/carcontrol.c src/game/carupdate.c src/game/camera.c src/game/ai.c src/game/aiplan.c src/game/orig_pf.c src/game/orig_pe.c src/game/orig_pd.c src/game/orig_pc.c src/game/orig_pb.c src/game/orig_pa.c src/game/orig_nb.c src/game/orig_na.c src/game/crt_cc.c src/game/crt_cb.c src/game/crt_ca.c src/game/gl_gd.c src/game/gl_gc.c src/game/gl_gb.c src/game/gl_ga.c src/game/snd_db.c src/game/snd_da.c src/game/st_sc.c src/game/st_sb.c src/game/st_sa.c src/game/fe_z.c src/game/fe_y.c src/game/fe_o.c src/game/fe_n.c src/game/fe_m.c src/game/fe_l.c src/game/setup_x.c src/game/setup_w.c src/game/setup_v.c src/game/setup_u.c src/game/setup_t.c src/game/setup_s.c src/game/pre_q.c src/game/pre_r.c src/game/pre_p.c src/game/post_h.c src/game/post_k.c src/game/post_j.c src/game/post_i.c src/game/post_g.c src/game/post_f.c src/game/ai_c.c src/game/ai_e.c src/game/ai_d.c src/game/ai_b.c src/game/ai_a.c

GAME_DIR ?= $(HOME)/Games/rollcage/prefix/drive_c/Program Files (x86)/Psygnosis/Rollcage
IDX_DIR  := $(GAME_DIR)/IDXData
EXE      := $(GAME_DIR)/Direct3D/Rollcage.exe
ITER     ?= 100

all: build/rollcage

# Native test: decompress every GT20 entry and compare with the Python tool's output.
build/test_gt20: tests/test_gt20.c src/game/gt20.c src/game/game.h
	@mkdir -p build
	$(CC) -O2 -Wall -Wextra -Isrc tests/test_gt20.c src/game/gt20.c -o $@

test: build/test_gt20
	./build/test_gt20 "$(IDX_DIR)" > build/hashes_c.txt
	python3 -I tools/rollcage_img.py hashes "$(IDX_DIR)" > build/hashes_py.txt
	diff build/hashes_c.txt build/hashes_py.txt && echo "C and Python decoders agree on all entries"

# Whole disassembly, and call sites that use eax after the given functions.
build/rollcage.asm:
	@mkdir -p build
	llvm-objdump -d --no-show-raw-insn --x86-asm-syntax=intel "$(EXE)" > $@

eax-users: build/rollcage.asm
	@python3 -I tools/eax_users.py $< $(ADDR)

# Emulated test: fixed.c (built for i386) against the original code, in Unicorn.
EMU_PYTHON ?= $(if $(wildcard build/venv/bin/python),build/venv/bin/python,python3)
EMU_SRCS := src/runtime.c src/coverage.c src/game/fixed.c src/game/physics.c src/game/events.c src/game/collide.c src/game/forces.c src/game/wheels.c src/game/drive.c src/game/track.c src/game/sweep.c src/game/detect.c src/game/trackhit.c src/game/pair.c src/game/objhit.c src/game/probes.c src/game/spin.c src/game/rolling.c src/game/script.c src/game/carcontrol.c src/game/carupdate.c src/game/camera.c src/game/ai.c src/game/aiplan.c src/game/orig_pf.c src/game/orig_pe.c src/game/orig_pd.c src/game/orig_pc.c src/game/orig_pb.c src/game/orig_pa.c src/game/orig_nb.c src/game/orig_na.c src/game/crt_cc.c src/game/crt_cb.c src/game/crt_ca.c src/game/gl_gd.c src/game/gl_gc.c src/game/gl_gb.c src/game/gl_ga.c src/game/snd_db.c src/game/snd_da.c src/game/st_sc.c src/game/st_sb.c src/game/st_sa.c src/game/fe_z.c src/game/fe_y.c src/game/fe_o.c src/game/fe_n.c src/game/fe_m.c src/game/fe_l.c src/game/setup_x.c src/game/setup_w.c src/game/setup_v.c src/game/setup_u.c src/game/setup_t.c src/game/setup_s.c src/game/pre_q.c src/game/pre_r.c src/game/pre_p.c src/game/post_h.c src/game/post_k.c src/game/post_j.c src/game/post_i.c src/game/post_g.c src/game/post_f.c src/game/ai_c.c src/game/ai_e.c src/game/ai_d.c src/game/ai_b.c src/game/ai_a.c
build/emu_fixed.elf: $(EMU_SRCS) src/game/*.h src/runtime.h Makefile
	@mkdir -p build/emu
	for f in $(EMU_SRCS); do \
	    $(CC) --target=i686-linux-gnu -O2 -fwrapv -fno-strict-aliasing -mllvm -fixup-byte-word-insts=false -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -Isrc \
	        -c $$f -o build/emu/$$(basename $$f .c).o || exit 1; done
	ld.lld -m elf_i386 -static -e 0 -Ttext=0x10001000 build/emu/*.o -o $@

# Each test program reruns only when its inputs changed: its script, the
# shared harness, the headers, the core sources (which every group's C may
# call) and its own group's source (tests/emu_ai_a.py <- src/game/ai_a.c).
# A pass leaves build/stamps/<test>.ok. make -j8 emu-test runs them in
# parallel; make -k keeps going after a failure; make emu-test-all reruns
# everything (after changing the harness's behaviour, say).
EMU_TESTS := $(sort $(wildcard tests/emu_*.py))
EMU_STAMPS := $(patsubst tests/%.py,build/stamps/%.ok,$(EMU_TESTS))
CORE_SRCS := $(filter-out src/game/ai_%.c src/game/post_%.c src/game/pre_%.c src/game/setup_%.c src/game/fe_%.c,$(EMU_SRCS))
EMU_DEPS := tests/emu_fixed.py $(wildcard src/game/*.h) src/runtime.h src/coverage.h $(CORE_SRCS)

.SECONDEXPANSION:
build/stamps/%.ok: tests/%.py $(EMU_DEPS) $$(wildcard src/game/$$(subst emu_,,$$*).c) | build/emu_fixed.elf
	@mkdir -p build/stamps
	$(EMU_PYTHON) -I $< "$(EXE)" build/emu_fixed.elf $(ITER)
	@touch $@

emu-test: $(EMU_STAMPS)

emu-test-all:
	rm -f build/stamps/*.ok
	$(MAKE) emu-test

GHIDRA_HEADLESS ?= /opt/ghidra/support/analyzeHeadless

# Push the names in docs/functions.csv into the Ghidra project (ghidra/Rollcage.gpr).
ghidra-names:
	$(GHIDRA_HEADLESS) ghidra Rollcage -process Rollcage.exe -noanalysis \
	    -scriptPath tools/ghidra -postScript ApplyRegistry.java docs/functions.csv 2>&1 \
	    | grep -E 'reroll:|no function at|ERROR'

# Decompile functions: make decompile ADDR="0x497860 0x496590"
decompile:
	@$(GHIDRA_HEADLESS) ghidra Rollcage -process Rollcage.exe -noanalysis -readOnly \
	    -scriptPath tools/ghidra -postScript Decompile.java $(ADDR) 2>&1 \
	    | sed -n '/REROLL-BEGIN/,/REROLL-END/{/REROLL-END/d;s/^.*REROLL-BEGIN/==/;p}'

# Functions reachable from an address, with sizes: make calltree ADDR=0x461410 DEPTH=6
DEPTH ?= 6
calltree:
	@$(GHIDRA_HEADLESS) ghidra Rollcage -process Rollcage.exe -noanalysis -readOnly \
	    -scriptPath tools/ghidra -postScript CallTree.java $(ADDR) depth=$(DEPTH) 2>&1 \
	    | sed -n '/REROLL-BEGIN/,/REROLL-END/{/REROLL-END/d;s/^.*REROLL-BEGIN/==/;p}'

# Functions referencing addresses: make xrefs ADDR="0x4b1c6c"
xrefs:
	@$(GHIDRA_HEADLESS) ghidra Rollcage -process Rollcage.exe -noanalysis -readOnly \
	    -scriptPath tools/ghidra -postScript Xrefs.java $(ADDR) 2>&1 \
	    | sed -n '/REROLL-BEGIN/,/REROLL-END/{/REROLL-END/d;s/^.*REROLL-BEGIN/==/;p}'

# Every function with its size: make function-sizes > build/functions.txt
function-sizes:
	@$(GHIDRA_HEADLESS) ghidra Rollcage -process Rollcage.exe -noanalysis -readOnly \
	    -scriptPath tools/ghidra -postScript FunctionSizes.java 2>&1 \
	    | sed -n '/REROLL-BEGIN/,/REROLL-END/{/REROLL-END/d;/REROLL-BEGIN/d;p}'

# Every function's stack argument bytes and register arguments (decompiler): make param-sizes > build/params.txt
param-sizes:
	@$(GHIDRA_HEADLESS) ghidra Rollcage -process Rollcage.exe -noanalysis -readOnly \
	    -scriptPath tools/ghidra -postScript ParamSizes.java 2>&1 \
	    | sed -n '/REROLL-BEGIN/,/REROLL-END/{/REROLL-END/d;/REROLL-BEGIN/d;p}'

# Argument-count audit of our C against the original's own stack use
# (build/params.txt from make param-sizes): make argcheck
argcheck: build/params.txt
	$(EMU_PYTHON) -I tools/argcheck.py build/params.txt src/main.c src/capture.c src/widescreen.c src/crash.c src/vkgame.c src/subunit.c src/game/*.c

build/params.txt:
	$(MAKE) -s param-sizes > $@

# Disassemble the original: make disasm ADDR=0x461410 LEN=0x100
LEN ?= 0x100
disasm:
	@llvm-objdump -d --x86-asm-syntax=intel --start-address=$$(($(ADDR))) \
	    --stop-address=$$(($(ADDR) + $(LEN))) "$(EXE)" | tail -n +7

clean:
	rm -rf build

.PHONY: all test ghidra-names decompile calltree xrefs disasm emu-test eax-users clean function-sizes param-sizes argcheck emu-test-all

# Renderer: capture decoder test tool (native), docs/renderer/plan.md step 2
build/rcapdec: tools/rcapdec.c tools/rcapfile.c tools/rcapfile.h src/render/packets.c src/render/draw.h
	clang -O2 -g -Wall -Wextra -Isrc tools/rcapdec.c tools/rcapfile.c src/render/packets.c -o $@

rcapdec: build/rcapdec
	build/rcapdec captures/*.rcap

# Renderer: shaders as C arrays, and the offline capture renderer (step 3)
build/render/draw_vert.inc: src/render/shaders/draw.vert
	@mkdir -p build/render
	glslc -mfmt=c $< -o $@

build/render/draw_frag.inc: src/render/shaders/draw.frag
	@mkdir -p build/render
	glslc -mfmt=c $< -o $@

build/rcaprender: tools/rcaprender.c tools/rcapfile.c tools/rcapfile.h src/render/packets.c src/render/vkr.c \
		src/render/vkr.h src/render/draw.h build/render/draw_vert.inc build/render/draw_frag.inc
	clang -O2 -g -Wall -Wextra -Wno-missing-field-initializers -Isrc -Ibuild/render tools/rcaprender.c tools/rcapfile.c src/render/packets.c \
	    src/render/vkr.c -o $@ -lvulkan -lz

# Renders every capture at the game's size next to its screenshot: captures/render/
rcaprender: build/rcaprender
	@mkdir -p captures/render
	@for f in captures/*.rcap; do b=$$(basename $$f .rcap); build/rcaprender $$f captures/render/$$b.png; done

# Renderer: the live viewer (step 4); run the game with REROLL_LIVE=1
build/rview: tools/rview.c tools/rcapfile.c tools/rcapfile.h src/render/packets.c src/render/vkr.c src/render/vkr.h \
		src/render/draw.h build/render/draw_vert.inc build/render/draw_frag.inc
	clang -O2 -g -Wall -Wextra -Wno-missing-field-initializers -Isrc -Ibuild/render tools/rview.c tools/rcapfile.c \
	    src/render/packets.c src/render/vkr.c -o $@ $$(pkg-config --cflags --libs sdl3)

# ---- Native Linux build (docs/native/plan.md): build/rollcage, i386 ----
# The reimplementation's sources (minus src/runtime.c, the C-library stand-ins the
# emulator tests use), plus native/ (the Windows layer). -mstackrealign: the game calls us with 4-byte
# stack alignment. (No -malign-double: system libraries such as Vulkan use the i386 layout.)
NATIVE_FLAGS := --target=i686-linux-gnu -O2 -g -fwrapv -fno-strict-aliasing -mllvm -fixup-byte-word-insts=false \
                -fno-pic -mstackrealign -Wall -Wextra -Wno-missing-field-initializers
NATIVE_GAME_SRCS := $(filter-out src/runtime.c,$(GAME_SRCS))
NATIVE_MODULES := native/kernel32.c native/advapi32.c native/user32.c native/gdi32.c native/winmm.c native/ole32.c native/qmixer.c native/dsound.c native/dplayx.c native/ddraw.c native/dinput.c
NATIVE_SRCS := native/loader.c native/teb.c native/paths.c native/modules.c native/keys.c native/lowmem.c native/profile.c native/exe_coverage.c native/datafile.c $(NATIVE_MODULES)
NATIVE_GAME_OBJS := $(NATIVE_GAME_SRCS:src/%.c=build/native/src/%.o)
NATIVE_OBJS := $(NATIVE_SRCS:native/%.c=build/native/%.o) build/native/exports.o

build/native/src/%.o: src/%.c src/*.h src/game/*.h src/render/*.h Makefile build/render/draw_vert.inc build/render/draw_frag.inc
	@mkdir -p $(dir $@)
	$(CC) $(NATIVE_FLAGS) -DREROLL_NATIVE -fms-extensions -Wno-ignored-attributes -Wno-unknown-pragmas \
	    -Isrc -Ibuild/render -c $< -o $@

build/native/%.o: native/%.c native/*.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(NATIVE_FLAGS) -D_GNU_SOURCE -Inative -c $< -o $@

build/native/exports.c: tools/native_exports.py $(NATIVE_MODULES)
	@mkdir -p build/native
	python3 -I tools/native_exports.py $@ $(NATIVE_MODULES)

build/native/exports.o: build/native/exports.c native/nt.h
	$(CC) $(NATIVE_FLAGS) -Inative -c $< -o $@

build/rollcage: $(NATIVE_OBJS) $(NATIVE_GAME_OBJS)
	$(CC) --target=i686-linux-gnu -no-pie -o $@ $^ -lSDL3 -lvulkan -lpthread -lm

native: build/rollcage
.PHONY: native
