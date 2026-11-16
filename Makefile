# amcempega.library: MPEG audio (MPEG-1, 2 and 2.5; Layers I, II and III)
# decoded to 16-bit PCM by libmad 0.15.1b (third_party/libmad,
# GPL-2.0-or-later) behind mpega.library's functions, for any Amiga
# program. GPL-3.0-or-later, (c) 2026 Brian King; the interface
# (include/, mpga_lib.sfd, mpga.doc) is MIT.
#
#   make            build/amcempega.library (68020 and up, no FPU: each
#                   fixed-point multiply one MULS.L 32 x 32 -> 64)
#   make test       the fixtures decoded in several ways (22 and 11 kHz,
#                   mono, half loudness, by name, from a second in) on this
#                   computer and through the library on a 68020 under
#                   vamos: the samples' CRCs alike, and as tests/expected.txt
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
AS      = m68k-amigaos-as -m68020
STRIP   = m68k-amigaos-strip
HOSTCC ?= cc
PYTHON ?= python3
LIBNAME = amcempega.library
LIBID   = $(LIBNAME) 1.0 (15.11.2026) libmad 0.15.1b, 68020+
MAD     = third_party/libmad
LIB     = build/$(LIBNAME)
MAD_SRC = $(addprefix $(MAD)/,bit.c fixed.c frame.c huffman.c layer12.c layer3.c stream.c synth.c timer.c)
SRC     = src/mpga.c $(MAD_SRC)
HDR     = $(wildcard $(MAD)/*.h $(MAD)/*.dat src/port/*.h include/*/*.h)
FPM     = -DFPM_64BIT -DOPT_SPEED
FLAGS   = -DHAVE_CONFIG_H $(FPM) -Isrc/port -I$(MAD) -Iinclude
CPU     = -m68020
LIBCC   = $(CC) -O2 $(CPU) -noixemul -nostartfiles -fomit-frame-pointer -fno-strict-aliasing
FIX     = $(sort $(wildcard tests/fixtures/*.mp3 tests/fixtures/*.mp2))

.PHONY: all test snr clean

all: $(LIB)

# the 68k's objects (libmad's own quietly; timer.c's sprintf, which only
# mad_timer_string wants, a stub: no stdio in the library)
build/68k/$(MAD)/%.o: UPSTREAM = -w
build/68k/$(MAD)/timer.o: UPSTREAM = -w -Dsprintf=mpga_nosprintf
build/68k/%.o: %.c $(HDR)
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(LIBCC) -Wall $(FLAGS) '-DMPGA_LIBNAME="$(LIBNAME)"' '-DMPGA_LIBID="$(LIBID)"' $(UPSTREAM) -c -o $@ $<
build/68k/stack.o: src/stack.s
	@mkdir -p build/68k
	@echo "  AS    $<"
	@$(AS) -o $@ $<

# Its Resident's version from src/mpga.c's LibVersion, so the two cannot
# differ: ramlib turns a library away whose Resident is older than the
# version asked for, whatever its lib_Version (make test checks it)
LIBVER = $(shell sed -n 's/^const UWORD LibVersion = \([0-9]*\);.*/\1/p' src/mpga.c)
build/68k/libinit.o: src/libinit.c src/libinit.h src/mpga.c Makefile
	@mkdir -p build/68k
	@echo "  CC    $<"
	@$(CC) -O2 $(CPU) -noixemul -nostartfiles -fomit-frame-pointer -Wall -Wno-array-bounds \
		-Isrc -DLIB_RT_VERSION=$(LIBVER) -c -o $@ $<

$(LIB): build/68k/libinit.o $(patsubst %.c,build/68k/%.o,$(SRC)) build/68k/stack.o
	@echo "  LD    $@"
	@$(LIBCC) -o $@.dbg $^ -lamiga
	@$(STRIP) -o $@ $@.dbg

# ---- the test: the same lines on the host and through the library on a
# 68020 (the 68k's multiplies are the same 64-bit products as the host's)
build/host/mpga_test: tests/mpga_test.c $(SRC) $(HDR) tests/host/exec/types.h tests/host/utility/hooks.h
	@mkdir -p build/host
	@echo "  CC    $@"
	@$(HOSTCC) -O2 -w $(FLAGS) -Itests/host -o $@ tests/mpga_test.c $(SRC)
build/68k/mpga_test: tests/mpga_test.c include/libraries/mpega.h include/inline/mpega.h $(LIB)
	@mkdir -p build/68k/prog/Libs
	@echo "  CC    $@"
	@$(CC) -O2 $(CPU) -noixemul -Wall -Iinclude '-DMPGA_TEST_LIB="$(LIBNAME)"' -o $@ tests/mpga_test.c
	@cp $(LIB) build/68k/prog/Libs/
	@cp $@ build/68k/prog/

# the headers programs compile with, from mpga_lib.sfd (sfdc)
include/inline/mpega.h include/clib/mpega_protos.h include/proto/mpega.h: mpga_lib.sfd
	@mkdir -p include/inline include/clib include/proto
	@sfdc --quiet --mode=macros --target=m68k-amigaos -o include/inline/mpega.h $<
	@sfdc --quiet --mode=clib --target=m68k-amigaos -o include/clib/mpega_protos.h $<
	@sfdc --quiet --mode=proto --target=m68k-amigaos -o include/proto/mpega.h $<

test: build/host/mpga_test build/68k/mpga_test $(LIB) $(FIX)
	@python3 -c "import sys; d=open('$(LIB)','rb').read(); i=d.find(bytes([0x4a,0xfc])); \
		sys.exit(0 if i >= 0 and d[i+11] == $(LIBVER) else 'its Resident says version %d, not $(LIBVER)' % d[i+11])" \
		&& echo "  CHECK the library's Resident: version $(LIBVER), as LibVersion (ramlib turns an older one away)"
	@cd tests/fixtures && ../../build/host/mpga_test $(notdir $(FIX)) > ../../build/host/crc.txt
	@cmp -s build/host/crc.txt tests/expected.txt \
		&& echo "  CHECK MPEG audio decoding (host) ok" || { diff build/host/crc.txt tests/expected.txt; exit 1; }
	@cp $(FIX) build/68k/prog/
	@rm -rf build/68k/vols; cd build/68k/prog && vamos -C 68020 -m 8192 -s 8 --vols-base-dir $(abspath build/68k/vols) \
		-V prog:$(abspath build/68k/prog) -- prog:mpga_test $(notdir $(FIX)) > ../out.txt 2> ../err.txt; \
		cmp -s ../out.txt ../../../tests/expected.txt \
		&& echo "  CHECK the same through $(LIBNAME) on a 68020 (vamos, an 8 KB stack) ok" \
		|| { diff ../out.txt ../../../tests/expected.txt | head; tail -5 ../err.txt; exit 1; }

# ---- accuracy: each fixture's samples against ffmpeg's float decoder
snr: build/host/mpga_test $(FIX)
	@mkdir -p build/snr
	@for f in $(FIX); do b=$$(basename $$f); \
		build/host/mpga_test -w build/snr/$$b.raw $$f > /dev/null; \
		case $$b in *.mp2) d=mp2float;; *) d=mp3float;; esac; \
		ch=$$(ffprobe -v error -show_entries stream=channels -of csv=p=0 $$f); \
		if [ "$$ch" = 1 ]; then p='pan=stereo|c0=c0|c1=c0'; else p='pan=stereo|c0=c0|c1=c1'; fi; \
		ffmpeg -v error -y -c:a $$d -i $$f -af "$$p" -f s16le build/snr/$$b.ref; \
		printf "  %-20s " $$b; $(PYTHON) tools/snr.py build/snr/$$b.raw build/snr/$$b.ref; done

clean:
	rm -rf build
