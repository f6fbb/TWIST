# Makefile - TWIST (Linux, macOS, Windows/MSYS2)
#
# Requires: a C compiler and FFTW3 (double precision)
#   Debian/Ubuntu:  sudo apt install build-essential libfftw3-dev
#   MSYS2 (UCRT64): pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-fftw make
# Optional, for live audio: PortAudio
#   Debian/Ubuntu:  sudo apt install portaudio19-dev pkg-config
#   MSYS2 (UCRT64): pacman -S mingw-w64-ucrt-x86_64-portaudio mingw-w64-ucrt-x86_64-pkgconf
#
#   make              # twistd, operad, pi4d, twistenc, twistlive + the libraries
#                     # (libtwcore, libtwist, libopera, libpi4)
#   make PORTAUDIO=0  # without live audio (twistlive then only replays files)
#   make test         # self-checking test suite (tests/run_tests.sh)
#   make tests        # every test / measurement program
#   sudo make install

CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -Icore -D_FILE_OFFSET_BITS=64
# -MMD: the compiler writes build/x.d, listing the headers x.c includes;
# -MP: one empty rule per header, so deleting a header does not break make
DEPFLAGS = -MMD -MP
LIBS     = -lfftw3_threads -lfftw3 -lm
ifneq ($(OS),Windows_NT)
  LIBS  += -pthread          # the FFTW planner (core/twbb.c)
endif
B        = build

# The libraries of TWIST Console (core/twltypes.h): libtwcore (audio,
# transmission, no protocol), and one library per protocol - libtwist
# (TWIST S, TWIST L), libopera (the classic frame, its users' name for
# it) and libpi4 - each with the common signal processing compiled in,
# none depending on another. Built position-independent; only their
# own API is exported (-fvisibility=hidden, TWL_API).
UNAME := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  SO          = .dll
  LIB_LDFLAGS = -shared -Wl,--out-implib,$@.a
  LIB_RPATH   =
else ifeq ($(UNAME),Darwin)
  SO          = .dylib
  LIB_LDFLAGS = -dynamiclib -install_name @rpath/$@
  LIB_RPATH   = -Wl,-rpath,@loader_path -Wl,-rpath,@loader_path/..
  CFLAGS     += -fPIC -fvisibility=hidden
else
  SO          = .so
  LIB_LDFLAGS = -shared
  LIB_RPATH   = -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$$ORIGIN/..'
  CFLAGS     += -fPIC -fvisibility=hidden
endif
LIBCORE  = libtwcore$(SO)
LIBTW    = libtwist$(SO)
LIBCL    = libopera$(SO)
LIBP4    = libpi4$(SO)
LIBS_ALL = $(LIBCORE) $(LIBTW) $(LIBCL) $(LIBP4)

PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR   = $(DESTDIR)$(PREFIX)/bin
LIBDIR   = $(DESTDIR)$(PREFIX)/lib
INCDIR   = $(DESTDIR)$(PREFIX)/include
INSTALL  = install

# Live audio (PortAudio): built in when pkg-config finds it.
PORTAUDIO ?= $(shell pkg-config --exists portaudio-2.0 2>/dev/null && echo 1 || echo 0)
ifeq ($(PORTAUDIO),1)
  AUDIO_CFLAGS = -DTWL_HAVE_PORTAUDIO $(shell pkg-config --cflags portaudio-2.0)
  AUDIO_LIBS   = $(shell pkg-config --libs portaudio-2.0)
endif
$(B)/twaudio.o: CFLAGS += $(AUDIO_CFLAGS)

# Common signal processing (no protocol)
COMMON   = $(B)/twbb.o $(B)/twsync.o $(B)/twscan.o $(B)/wavread.o $(B)/iqread.o $(B)/twcall.o
# The protocols' own code
CLASSIC  = $(B)/twcenc.o $(B)/twcdec.o $(B)/twcdeep.o
TWIST    = $(B)/twist.o $(B)/ldpc.o
# PI4 (the decoder of pi4d; fano.c and tab.c from WSJT-X, unmodified)
PI4      = $(B)/pi4pipe.o $(B)/pi4sync.o $(B)/pi4demod.o $(B)/pi4dec.o $(B)/pi4snr.o \
           $(B)/pi4mettab.o $(B)/pi4resample.o $(B)/pi4enc.o $(B)/fano.o $(B)/tab.o
# The pipeline (twpipe.c, twdec.c), built once per protocol set
PIPE_TW  = $(B)/tw/twpipe.o $(B)/tw/twdec.o
PIPE_CL  = $(B)/cl/twpipe.o $(B)/cl/twdec.o
PIPE_P4  = $(B)/p4/twpipe.o $(B)/p4/twdec.o
PIPE_ALL = $(B)/all/twpipe.o $(B)/all/twdec.o
$(B)/tw/%.o: CFLAGS += -DTW_WITH_TWIST=1
$(B)/cl/%.o: CFLAGS += -DTW_WITH_CLASSIC=1
$(B)/p4/%.o: CFLAGS += -DTW_WITH_PI4=1

# Each library: its API, its pipeline, its protocol, the common part
CORE_O   = $(B)/twcore.o $(B)/twtx.o $(B)/twaudio.o $(B)/twscan.o $(B)/twbb.o
TW_O     = $(B)/libtwist.o $(PIPE_TW) $(TWIST) $(COMMON)
CL_O     = $(B)/twcl.o $(PIPE_CL) $(CLASSIC) $(COMMON)
P4_O     = $(B)/libpi4.o $(PIPE_P4) $(PI4) $(COMMON)
$(B)/twcore.o $(B)/libtwist.o $(B)/twcl.o $(B)/libpi4.o: CFLAGS += -DTWL_BUILD
# Everything at once: the test programs (one pipeline for the three)
ALL_O    = $(PIPE_ALL) $(CLASSIC) $(TWIST) $(PI4) $(COMMON)

TESTS   = tests/test_encoder tests/test_e2e tests/snr_sweep tests/snr_agc tests/test_threads tests/deep_shadow tests/pi4_test tests/false_alarm \
          tests/deep_calib tests/gen_test_wav tests/gen_twins tests/spread_sweep tests/gen_wide \
          tests/interf tests/tw_sweep tests/pad_riff tests/test_libs tests/test_tx tests/test_audio firmware/test_twist_encoder

all: twistd operad pi4d twistenc twistlive $(LIBS_ALL)

$(LIBCORE): $(CORE_O)
	$(CC) $(LIB_LDFLAGS) -o $@ $^ $(LIBS) $(AUDIO_LIBS)
$(LIBTW): $(TW_O)
	$(CC) $(LIB_LDFLAGS) -o $@ $^ $(LIBS)
$(LIBCL): $(CL_O)
	$(CC) $(LIB_LDFLAGS) -o $@ $^ $(LIBS)
$(LIBP4): $(P4_O)
	$(CC) $(LIB_LDFLAGS) -o $@ $^ $(LIBS)

# The decoders: one source for twistd and operad (cli/twistd.c, built
# with its protocol), pi4d its own; each with its library's objects
twistd: $(B)/tw/twistd.o $(PIPE_TW) $(TWIST) $(COMMON)
	$(CC) -o $@ $^ $(LIBS)
operad: $(B)/cl/twistd.o $(PIPE_CL) $(CLASSIC) $(COMMON)
	$(CC) -o $@ $^ $(LIBS)
pi4d: $(B)/pi4d.o $(PI4) $(B)/pi4iqscan.o $(B)/pi4iqmix.o $(COMMON)
	$(CC) -o $@ $^ $(LIBS)

# twistenc: the encoders and the transmitter, linked statically
twistenc: $(B)/twistenc.o $(B)/twtx.o $(TWIST) $(B)/pi4enc.o $(B)/fano.o $(B)/tab.o $(B)/twcall.o
	$(CC) -o $@ $^ -lm
# twistlive: through the shared libraries, as the application (each
# library keeps its pipeline to itself)
twistlive: $(B)/twistlive.o $(B)/wavread.o $(LIBS_ALL)
	$(CC) -o $@ $(B)/twistlive.o $(B)/wavread.o $(LIBS_ALL) $(LIB_RPATH) -lm

$(B)/%.o: core/%.c | $(B)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(B)/%.o: cli/%.c | $(B)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
# (one rule per directory: a pattern rule with several targets would
# be one recipe making them all)
$(B)/tw/%.o: core/%.c | $(B)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(B)/cl/%.o: core/%.c | $(B)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(B)/p4/%.o: core/%.c | $(B)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(B)/all/%.o: core/%.c | $(B)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(B)/tw/%.o: cli/%.c | $(B)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(B)/cl/%.o: cli/%.c | $(B)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(B):
	mkdir -p $(B) $(B)/tw $(B)/cl $(B)/p4 $(B)/all

# header dependencies generated by -MMD (absent before the first build)
-include $(wildcard $(B)/*.d $(B)/*/*.d)

# --- tests and tools --------------------------------------------------
TCFLAGS = $(CFLAGS) -O3 -Itests
# The test programs are compiled straight from their .c files: they are
# rebuilt whenever any header changes (coarse, but never stale).
# $(SRCS) keeps only the .c/.o prerequisites for the command line.
HDRS    = $(wildcard core/*.h tests/*.h firmware/*.h)
SRCS    = $(filter %.c %.o,$^)
$(TESTS): $(HDRS)

tests/test_encoder: tests/test_encoder.c $(B)/twcenc.o $(B)/twcall.o
	$(CC) $(TCFLAGS) -o $@ $(SRCS)
tests/gen_twins: tests/gen_twins.c $(B)/twcenc.o $(B)/twcdec.o $(B)/twcall.o $(B)/twsync.o $(B)/twbb.o
	$(CC) $(TCFLAGS) -o $@ $(SRCS) $(LIBS)
tests/gen_test_wav tests/gen_wide: tests/%: tests/%.c tests/synth.c $(TWIST) $(B)/twcenc.o $(B)/twcall.o
	$(CC) $(TCFLAGS) -o $@ $(SRCS) -lm
tests/test_e2e tests/snr_sweep tests/snr_agc tests/deep_shadow tests/false_alarm tests/deep_calib tests/spread_sweep tests/interf tests/tw_sweep: tests/%: tests/%.c tests/synth.c $(ALL_O)
	$(CC) $(TCFLAGS) -o $@ $(SRCS) $(LIBS)
tests/pi4_test: tests/pi4_test.c tests/synth.c tests/synth_pi4.c $(ALL_O)
	$(CC) $(TCFLAGS) -o $@ $(SRCS) $(LIBS)
tests/test_threads: tests/test_threads.c tests/synth.c $(ALL_O)
	$(CC) $(TCFLAGS) -pthread -o $@ $(SRCS) $(LIBS)
tests/pad_riff: tests/pad_riff.c
	$(CC) $(TCFLAGS) -o $@ $(SRCS)
# linked against the shared libraries, as the application uses them
tests/test_libs tests/test_tx tests/test_audio: tests/%: tests/%.c tests/synth.c $(LIBS_ALL)
	$(CC) $(TCFLAGS) -o $@ $(SRCS) $(LIBS_ALL) $(LIB_RPATH) -lm
firmware/test_twist_encoder: firmware/test_twist_encoder.c firmware/twist_encoder.c
	$(CC) -std=c99 -O2 -Wall -Wextra -Ifirmware -o $@ $(SRCS)

tests: $(TESTS)

test: twistd operad pi4d twistenc twistlive tests/test_encoder tests/snr_agc tests/test_threads tests/deep_shadow tests/pi4_test tests/gen_test_wav tests/gen_wide tests/pad_riff tests/test_libs tests/test_tx tests/test_audio firmware/test_twist_encoder
	sh tests/run_tests.sh

install: twistd operad pi4d $(LIBS_ALL)
	$(INSTALL) -d $(BINDIR) $(LIBDIR) $(INCDIR)
	$(INSTALL) -m 755 twistd operad pi4d $(BINDIR)/
	$(INSTALL) -m 755 $(LIBS_ALL) $(LIBDIR)/
	$(INSTALL) -m 644 core/twltypes.h core/twcore.h core/libtwist.h core/twcl.h core/libpi4.h $(INCDIR)/

uninstall:
	rm -f $(BINDIR)/twistd $(BINDIR)/operad $(BINDIR)/pi4d
	cd $(LIBDIR) && rm -f $(LIBS_ALL)
	cd $(INCDIR) && rm -f twltypes.h twcore.h libtwist.h twcl.h libpi4.h

clean:
	rm -rf $(B) twistd operad pi4d twistenc twistlive $(LIBS_ALL) *.dll.a $(TESTS) tests/*.exe firmware/*.exe *.exe

.PHONY: all tests test install uninstall clean
