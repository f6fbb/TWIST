#!/bin/bash
# build-deps.sh - FFTW, PortAudio and Hamlib for the Mac application,
# built from their sources as static libraries for Intel (x86_64) and
# macOS 10.15 (Catalina) at least: the libraries of Homebrew are built
# for the macOS of the machine that built them and would not load on an
# older one. FFTW (with its threads part: the planner made thread-safe)
# ends up inside each library (libtwcore, libtwist, libopera, libpi4),
# PortAudio inside libtwcore.dylib; Hamlib
# gives rigctld and rigctl, without any library beside them.
#
#   installer/macos/build-deps.sh DEPS_FOLDER
set -euo pipefail
DEPS=$(mkdir -p "$1" && cd "$1" && pwd)
FFTW=3.3.10 PA=19.7.0 HL=4.6.5
export MACOSX_DEPLOYMENT_TARGET=10.15
export CFLAGS="-O2 -arch x86_64 -mmacosx-version-min=10.15"
export CXXFLAGS="$CFLAGS" LDFLAGS="-arch x86_64 -mmacosx-version-min=10.15"
J=$(sysctl -n hw.ncpu)
W=$(mktemp -d)
cd "$W"
# run: the output kept in a log; on failure, its end on the console and
# as a CI annotation (readable without downloading the log)
run() {
  echo "+ $*"
  if ! "$@" > "$W/log" 2>&1; then
    tail -n 60 "$W/log"
    echo "::error title=build-deps: $1 failed::$(tail -n 30 "$W/log" | sed -e 's/%/%25/g' -e 's/\r/%0D/g' | awk '{printf "%s%%0A", $0}')"
    exit 1
  fi
}

if [ ! -f "$DEPS/lib/libfftw3_threads.a" ]; then
  run curl -fsSL -o fftw.tgz "https://www.fftw.org/fftw-$FFTW.tar.gz"
  tar xzf fftw.tgz && cd "fftw-$FFTW"
  # SSE2 / AVX chosen at run time by FFTW from what the processor has
  run ./configure --prefix="$DEPS" --enable-static --disable-shared --with-pic \
              --enable-sse2 --enable-avx --enable-threads --disable-fortran --disable-doc
  run make -j"$J"
  run make install
  cd "$W"
fi

if [ ! -f "$DEPS/lib/libportaudio.a" ]; then
  # (CMake: its configure script looks for old macOS SDKs only)
  run curl -fsSL -o pa.tgz "https://github.com/PortAudio/portaudio/archive/refs/tags/v$PA.tar.gz"
  tar xzf pa.tgz && cd "portaudio-$PA"
  run cmake -S . -B b -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
  run cmake --build b -j"$J"
  mkdir -p "$DEPS/lib" "$DEPS/include"
  cp "$(find b -name 'libportaudio*.a' | head -1)" "$DEPS/lib/libportaudio.a"
  cp include/portaudio.h "$DEPS/include/"
  cd "$W"
fi

if [ ! -x "$DEPS/bin/rigctld" ]; then
  run curl -fsSL -o hl.tgz "https://github.com/Hamlib/Hamlib/releases/download/$HL/hamlib-$HL.tar.gz"
  tar xzf hl.tgz && cd "hamlib-$HL"
  run ./configure --prefix="$DEPS" --enable-static --disable-shared --without-libusb \
              --without-readline --without-xml-support --without-cxx-binding \
              --without-perl-binding --without-python-binding --without-tcl-binding \
              --without-lua-binding --disable-winradio
  run make -j"$J"
  run make install
  cp COPYING* LICENSE* README "$DEPS/" 2>/dev/null || true
  cd "$W"
fi

rm -rf "$W"
ls -l "$DEPS/lib"/*.a "$DEPS/bin/rigctld" "$DEPS/bin/rigctl"
