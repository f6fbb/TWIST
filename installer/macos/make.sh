#!/bin/bash
# make.sh - make (repository root) for the Mac application: Intel,
# macOS 10.15 or later, FFTW and PortAudio of build-deps.sh linked in.
#   installer/macos/make.sh DEPS_FOLDER [make targets...]
set -euo pipefail
DEPS=$(cd "$1" && pwd); shift
export MACOSX_DEPLOYMENT_TARGET=10.15
# (CFLAGS from the environment: the Makefile adds its own to it)
CFLAGS="-O2 -arch x86_64 -mmacosx-version-min=10.15 -I$DEPS/include" \
exec make CC=clang PORTAUDIO=1 \
     LIBS="-arch x86_64 -mmacosx-version-min=10.15 -L$DEPS/lib -lfftw3_threads -lfftw3 -lm -pthread" \
     AUDIO_CFLAGS="-DTWL_HAVE_PORTAUDIO -I$DEPS/include" \
     AUDIO_LIBS="$DEPS/lib/libportaudio.a -framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreServices" \
     "$@"
