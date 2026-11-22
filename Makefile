# ambmpega.library: MPEG audio (MPEG-1, 2 and 2.5; Layers I, II and III)
# decoded to 16-bit PCM by libmad 0.15.1b (third_party/libmad,
# GPL-2.0-or-later) behind mpega.library's functions, for any Amiga
# program. GPL-3.0-or-later, (c) 2026 Brian King; the interface
# (include/, mpga_lib.sfd, mpga.doc) is MIT.
#
#   make            build/ambmpega.library (68020 and up, no FPU: each
#                   fixed-point multiply one MULS.L 32 x 32 -> 64) and
#                   build/ambmpega060.library (68060: libmad's products
#                   from 32-bit multiplies, which a 68060 does itself, where
#                   it has no MULS.L 32 x 32 -> 64 and 68060.library would
#                   emulate each; 2^-24 of full scale from them at most,
#                   the same SNR)
#   make test       the fixtures decoded in several ways (22 and 11 kHz,
#                   mono, half loudness, by name, from a second in) on this
#                   computer and through each library under vamos: the
#                   samples' CRCs alike, and as tests/expected.txt (the
#                   68060's: tests/expected060.txt); and no 64-bit multiply
#                   or divide in the 68060's
#   make snr        each fixture against ffmpeg's floating-point decoder
#                   (the signal-to-noise ratio; needs ffmpeg)
#
# LIBNAME=mpega.library builds the same library under mpega.library's own
# name, as a drop-in for programs that open that.
#
# Needs AmigaPorts' m68k-amigaos-gcc 16.2 (rc24 or later; the continuation
# of bebbo's amiga-gcc), with libnix (-noixemul), a C compiler for this
# computer, and for the tests amitools' vamos.

CC      = m68k-amigaos-gcc
STRIP   = m68k-amigaos-strip
OBJDUMP = m68k-amigaos-objdump
HOSTCC ?= cc
PYTHON ?= python3
MAD     = third_party/libmad
MAD_SRC = $(addprefix $(MAD)/,bit.c fixed.c frame.c huffman.c layer12.c layer3.c stream.c synth.c timer.c)
SRC     = src/mpga.c $(MAD_SRC)
HDR     = $(wildcard $(MAD)/*.h $(MAD)/*.dat src/port/*.h include/*/*.h)
LIBDATE = 1.4 (21.11.2026) libmad 0.15.1b
# VARIANT 060 (make's own recursion: make build/ambmpega060.library) is
# the 68060's build; otherwise the 68020's
ifeq ($(VARIANT),060)
LIBNAME = ambmpega060.library
LIBID   = $(LIBNAME) $(LIBDATE), 68060
OBJ     = build/060
CPU     = -m68060
FPM     = -DFPM_68060 -DOPT_SPEED
VCPU    = 68040
EXPECT  = tests/expected060.txt
else
LIBNAME = ambmpega.library
LIBID   = $(LIBNAME) $(LIBDATE), 68020+
OBJ     = build/68k
CPU     = -m68020
FPM     = -DFPM_64BIT -DOPT_SPEED
VCPU    = 68020
EXPECT  = tests/expected.txt
endif
AS      = m68k-amigaos-as $(CPU)
LIB     = build/$(LIBNAME)
HOSTOBJ = build/host$(VARIANT)
# ASO_ZEROCHECK: libmad's own option, alias reduction's butterflies left
# out where both lines are zero (they stay zero): the same samples
FLAGS   = -DHAVE_CONFIG_H $(FPM) -DASO_ZEROCHECK -Isrc/port -I$(MAD) -Iinclude
LIBCC   = $(CC) -O2 $(CPU) -noixemul -nostartfiles -fomit-frame-pointer -fno-strict-aliasing
FIX     = $(sort $(wildcard tests/fixtures/*.mp3 tests/fixtures/*.mp2))

.PHONY: all test test-one snr prof clean FORCE

ifeq ($(VARIANT),060)
all: $(LIB)
test: test-one check-060
else
all: $(LIB) build/ambmpega060.library
build/ambmpega060.library: FORCE src/dct32_68k.s src/layer3_68k.s
	@$(MAKE) --no-print-directory VARIANT=060 $@
test: test-one
	@$(MAKE) --no-print-directory VARIANT=060 test
	@mkdir -p build/close
	@for f in $(FIX); do b=$$(basename $$f); \
		build/host/mpga_test -w build/close/$$b.a $$f > /dev/null; \
		build/host060/mpga_test -w build/close/$$b.b $$f > /dev/null; \
		$(PYTHON) tools/maxdiff.py build/close/$$b.a build/close/$$b.b 1 > build/close/$$b.txt \
		|| { echo "$$b: the 68060's build is further off: $$(cat build/close/$$b.txt)"; exit 1; }; done
	@echo "  CHECK the 68060's samples within a step of the 68020's, every fixture"
endif

# the 68k's objects (libmad's own quietly; timer.c's sprintf, which only
# mad_timer_string wants, a stub: no stdio in the library)
$(OBJ)/$(MAD)/%.o: UPSTREAM = -w
$(OBJ)/$(MAD)/timer.o: UPSTREAM = -w -Dsprintf=mpga_nosprintf
$(OBJ)/%.o: %.c $(HDR)
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(LIBCC) -Wall $(FLAGS) '-DMPGA_LIBNAME="$(LIBNAME)"' '-DMPGA_LIBID="$(LIBID)"' $(UPSTREAM) -c -o $@ $<
$(OBJ)/stack.o: src/stack.s
	@mkdir -p $(OBJ)
	@echo "  AS    $<"
	@$(AS) -o $@ $<

# Its Resident's version from src/mpga.c's LibVersion, so the two cannot
# differ: ramlib turns a library away whose Resident is older than the
# version asked for, whatever its lib_Version (make test checks it)
LIBVER = $(shell sed -n 's/^const UWORD LibVersion = \([0-9]*\);.*/\1/p' src/mpga.c)
$(OBJ)/libinit.o: src/libinit.c src/libinit.h src/mpga.c Makefile
	@mkdir -p $(OBJ)
	@echo "  CC    $<"
	@$(CC) -O2 $(CPU) -noixemul -nostartfiles -fomit-frame-pointer -Wall -Wno-array-bounds \
		-Isrc -DLIB_RT_VERSION=$(LIBVER) -c -o $@ $<

$(OBJ)/out_68k.o: src/out_68k.s
	@mkdir -p $(OBJ)
	@echo "  AS    $<"
	@$(AS) -o $@ $<
# the 68k's output writers in assembler (the host builds src/mpga.c's C)
$(OBJ)/src/mpga.o: UPSTREAM = -DMPGA_OUT_ASM
$(OBJ)/synth_68k.o: src/synth_68k.s
	@mkdir -p $(OBJ)
	@echo "  AS    $<"
	@$(AS) -o $@ $<
# the 68k's synthesis window and dct32 in assembler (the host builds
# libmad's C); dct32 written from the C by tools/gen_dct32.py, both builds'
$(OBJ)/$(MAD)/synth.o: UPSTREAM = -w -DMPGA_WINDOW_ASM -DMPGA_DCT_ASM
src/dct32_68k.s: tools/gen_dct32.py tools/gen68k.py $(MAD)/synth.c
	@echo "  GEN   $@"
	@$(PYTHON) tools/gen_dct32.py $(MAD)/synth.c > $@.tmp && mv $@.tmp $@
# the 68k's Layer III IMDCT for long blocks and alias reduction likewise
# (tools/gen_layer3.py)
$(OBJ)/$(MAD)/layer3.o: UPSTREAM = -w -DMPGA_IMDCT_ASM
src/layer3_68k.s: tools/gen_layer3.py tools/gen68k.py $(MAD)/layer3.c
	@echo "  GEN   $@"
	@$(PYTHON) tools/gen_layer3.py $(MAD)/layer3.c > $@.tmp && mv $@.tmp $@
$(OBJ)/layer3_68k.o: src/layer3_68k.s
	@mkdir -p $(OBJ)
	@echo "  AS    $<"
	@$(AS) --defsym MPGA060=$(if $(filter 060,$(VARIANT)),1,0) -o $@ $<
$(OBJ)/dct32_68k.o: src/dct32_68k.s
	@mkdir -p $(OBJ)
	@echo "  AS    $<"
	@$(AS) --defsym MPGA060=$(if $(filter 060,$(VARIANT)),1,0) -o $@ $<

$(LIB): $(OBJ)/libinit.o $(patsubst %.c,$(OBJ)/%.o,$(SRC)) $(OBJ)/stack.o $(OBJ)/synth_68k.o $(OBJ)/dct32_68k.o $(OBJ)/layer3_68k.o $(OBJ)/out_68k.o
	@echo "  LD    $@"
	@$(LIBCC) -o $@.dbg $^ -lamiga
	@$(STRIP) -o $@ $@.dbg

# ---- the test: the same lines on the host and through the library on a
# 68020 (the 68k's multiplies are the same 64-bit products as the host's)
$(HOSTOBJ)/mpga_test: tests/mpga_test.c $(SRC) $(HDR) tests/host/exec/types.h tests/host/utility/hooks.h
	@mkdir -p $(HOSTOBJ)
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w $(FLAGS) -Itests/host -o $@ tests/mpga_test.c $(SRC)
$(OBJ)/mpga_test: tests/mpga_test.c include/libraries/mpega.h include/inline/mpega.h $(LIB)
	@mkdir -p $(OBJ)/prog/Libs
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul -Wall -Iinclude '-DMPGA_TEST_LIB="$(LIBNAME)"' -o $@ tests/mpga_test.c
	@cp $(LIB) $(OBJ)/prog/Libs/
	@cp $@ $(OBJ)/prog/

# the headers programs compile with, from mpga_lib.sfd (sfdc)
include/inline/mpega.h include/clib/mpega_protos.h include/proto/mpega.h: mpga_lib.sfd
	@mkdir -p include/inline include/clib include/proto
	@sfdc --quiet --mode=macros --target=m68k-amigaos -o include/inline/mpega.h $<
	@sfdc --quiet --mode=clib --target=m68k-amigaos -o include/clib/mpega_protos.h $<
	@sfdc --quiet --mode=proto --target=m68k-amigaos -o include/proto/mpega.h $<

test-one: $(HOSTOBJ)/mpga_test $(OBJ)/mpga_test $(LIB) $(FIX)
	@python3 -c "import sys; d=open('$(LIB)','rb').read(); i=d.find(bytes([0x4a,0xfc])); \
		sys.exit(0 if i >= 0 and d[i+11] == $(LIBVER) else 'its Resident says version %d, not $(LIBVER)' % d[i+11])" \
		&& echo "  CHECK the library's Resident: version $(LIBVER), as LibVersion (ramlib turns an older one away)"
	@cd tests/fixtures && ../../$(HOSTOBJ)/mpga_test $(notdir $(FIX)) > ../../$(HOSTOBJ)/crc.txt
	@cmp -s $(HOSTOBJ)/crc.txt $(EXPECT) \
		&& echo "  CHECK MPEG audio decoding (host, $(EXPECT)) ok" || { diff $(HOSTOBJ)/crc.txt $(EXPECT); exit 1; }
	@cp $(FIX) $(OBJ)/prog/
	@rm -rf $(OBJ)/vols; cd $(OBJ)/prog && vamos -C $(VCPU) -m 8192 -s 8 --vols-base-dir $(abspath $(OBJ)/vols) \
		-V prog:$(abspath $(OBJ)/prog) -- prog:mpga_test $(notdir $(FIX)) > ../out.txt 2> ../err.txt; \
		cmp -s ../out.txt ../../../$(EXPECT) \
		&& echo "  CHECK the same through $(LIBNAME) on a $(VCPU) (vamos, an 8 KB stack) ok" \
		|| { diff ../out.txt ../../../$(EXPECT) | head; tail -5 ../err.txt; exit 1; }

# a 68060 has no MULS.L or DIVS.L with a 64-bit product or dividend (in
# objdump's syntax: mulsl, mulul, divsl and divul with three operands)
check-060: $(LIB)
	@n=$$($(OBJDUMP) -m m68k:68060 -d $(LIB).dbg | awk -F'\t' '$$3 ~ /^(mul[su]l|div[su]l) / { split($$3, w, " "); if (split(w[2], o, ",") == 3) print }' | wc -l); \
		[ $$n = 0 ] && echo "  CHECK $(LIBNAME): no 64-bit multiply or divide (a 68060's are emulated)" \
		|| { echo "$(LIBNAME): $$n 64-bit multiplies or divides"; exit 1; }

# ---- accuracy: each fixture's samples against ffmpeg's float decoder
snr: $(HOSTOBJ)/mpga_test $(FIX)
	@mkdir -p build/snr
	@for f in $(FIX); do b=$$(basename $$f); \
		$(HOSTOBJ)/mpga_test -w build/snr/$$b.raw $$f > /dev/null; \
		case $$b in *.mp2) d=mp2float;; *) d=mp3float;; esac; \
		ch=$$(ffprobe -v error -show_entries stream=channels -of csv=p=0 $$f); \
		if [ "$$ch" = 1 ]; then p='pan=stereo|c0=c0|c1=c0'; else p='pan=stereo|c0=c0|c1=c1'; fi; \
		ffmpeg -v error -y -c:a $$d -i $$f -af "$$p" -f s16le build/snr/$$b.ref; \
		printf "  %-20s " $$b; $(PYTHON) tools/snr.py build/snr/$$b.raw build/snr/$$b.ref; done

# ---- where a 68020's cycles go (vamos counts them), by function: the
# decoder built into the test program, each source its own object, so that
# their static functions can be placed (tools/syms.py)
#   make prof MP3=file [N=300] [WAY=0]
N   ?= 300
PROFDIR = build/prof$(VARIANT)
PROFOBJ = $(PROFDIR)/mpga_test.o $(patsubst %.c,$(PROFDIR)/%.o,$(notdir $(SRC))) $(OBJ)/stack.o \
          $(OBJ)/synth_68k.o $(OBJ)/dct32_68k.o $(OBJ)/layer3_68k.o $(OBJ)/out_68k.o
$(PROFDIR)/mpga_test.o: tests/mpga_test.c $(HDR)
	@mkdir -p $(PROFDIR)
	@$(LIBCC) -w -DMPGA_STATIC $(FLAGS) -c -o $@ $<
$(PROFDIR)/mpga.o: src/mpga.c $(HDR)
	@mkdir -p $(PROFDIR)
	@$(LIBCC) -w -DMPGA_STATIC -DMPGA_OUT_ASM $(FLAGS) -c -o $@ $<
$(PROFDIR)/synth.o: $(MAD)/synth.c $(HDR)
	@mkdir -p $(PROFDIR)
	@$(LIBCC) -w -DMPGA_WINDOW_ASM -DMPGA_DCT_ASM $(FLAGS) -c -o $@ $<
$(PROFDIR)/timer.o: $(MAD)/timer.c $(HDR)
	@mkdir -p $(PROFDIR)
	@$(LIBCC) -w $(FLAGS) -c -o $@ $<
$(PROFDIR)/layer3.o: $(MAD)/layer3.c $(HDR)
	@mkdir -p $(PROFDIR)
	@$(LIBCC) -w -DMPGA_IMDCT_ASM $(FLAGS) -c -o $@ $<
$(PROFDIR)/%.o: $(MAD)/%.c $(HDR)
	@mkdir -p $(PROFDIR)
	@$(LIBCC) -w $(FLAGS) -c -o $@ $<
$(PROFDIR)/mpga_prof: $(PROFOBJ)
	@echo "  LD    $@"
	@$(CC) $(CPU) -noixemul -Wl,-Map=$@.map -o $@ $^
	@$(PYTHON) tools/syms.py $@.map $^ > $@.syms
# vamos's own Python: a python beside vamos (a venv), or the one its
# wrapper script runs, with its PYTHONPATH
VAMOS_PY ?= $(shell v=$$(command -v vamos); d=$$(dirname $$v); if [ -x $$d/python ]; then echo $$d/python; \
	else sed -n 's/^PYTHONPATH=\([^ ]*\) exec \([^ ]*\) .*/env PYTHONPATH=\1 \2/p' $$v; fi)
prof: $(PROFDIR)/mpga_prof
	@rm -rf $(PROFDIR)/run && mkdir -p $(PROFDIR)/run && cp $(PROFDIR)/mpga_prof $(MP3) $(PROFDIR)/run/
	@cd $(PROFDIR)/run && $(VAMOS_PY) ../../../tools/$(if $(INSTR),prof_instr.py,prof.py) ../mpga_prof.syms -- \
		-C 68020 -m 8192 -s 64 --vols-base-dir vols -V prog:. -- prog:mpga_prof -n $(N) -way $(or $(WAY),0) \
		prog:$(notdir $(MP3)) 2>&1 | grep -v "WARN\|INFO\|orphan" | head -30

clean:
	rm -rf build
