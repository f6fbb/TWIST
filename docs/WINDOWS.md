# TWIST on Windows

## Ready-made programs

Every build of `main` on GitHub (Actions › build-and-test › latest run
on `main`) publishes an artifact **twist-windows-x64**, kept 7 days:
the decoders `twistd.exe` (TWIST), `operad.exe` (OPERA), `pi4d.exe`
(PI4), the tools `twistenc.exe` and `twistlive.exe`, the libraries
`libtwcore.dll`, `libtwist.dll`, `libopera.dll`, `libpi4.dll` with their
headers and C# bindings, and the DLLs they need (FFTW, PortAudio). Unzip
it in a folder and run the programs from a command prompt in that
folder. The decoders are installed with TWIST Console, in its `outils`
folder (operad and pi4d with their component, OPERA and PI4).

## Publishing a version (update check)

TWIST Console looks for a new version at start, then once a day (Réglages ›
Interface › Rechercher les nouvelles versions; Aide › Rechercher une
mise à jour at once), in `https://update.f6fbb.org/twist/latest.json`
(`app/Twist.App/Services/Updates.cs`). The repository being private,
the testers' copies cannot read its Releases: the files go on that
server.

1. Publish the Release on GitHub (tag `vX.Y.Z` on `main`). The CI
   attaches `TWIST-Console-X.Y.Z-setup.exe`, `TWIST-Console-X.Y.Z-mise-a-jour.exe`
   (when there is one) and **`latest.json`**: the version, its changes
   (the CHANGELOG section), and each installer's name, size and SHA-256.
2. Copy these files into the `twist/` folder of update.f6fbb.org,
   **`latest.json` last** (the program must never be told of a file
   that is not there yet). Older installers can stay or go.

```
{ "version": "0.25.0", "date": "2026-11-10", "notes": "### Added ...",
  "page": "https://update.f6fbb.org/twist/",
  "windows": { "setup":  { "file": "TWIST-Console-0.25.0-setup.exe", "size": 40123456, "sha256": "…" },
               "update": { "file": "TWIST-Console-0.25.0-mise-a-jour.exe", "size": 6123456, "sha256": "…",
                           "base": "0.22.0" } } }
```

The program offers it in a banner (Nouveautés, Installer, Plus tard).
Installer (Windows) downloads the update installer when the version
installed is `base` or newer, else the whole one, into the temporary
folder; checks its size and SHA-256 (a file only, beside `latest.json`);
then starts it with `/SILENT /SUPPRESSMSGBOXES /CLOSEAPPLICATIONS
/RELAUNCH`: it closes TWIST Console, installs, and starts it again
(`Check: Relaunch` in `installer/*.iss`). Refused while transmitting.
Mac and Linux: the banner opens the `page`.

The server: HTTPS, and `latest.json` not kept in caches, e.g. nginx:

```
location /twist/ {
    location ~ \.json$ { add_header Cache-Control "no-cache"; }
}
```

Nothing is sent but the request (User-Agent `TWIST-Console/<version>`).

## Live decoding with twistlive

```
twistlive -l
```
lists the audio devices with their number, channels, rate and driver
(MME, Windows DirectSound, Windows WASAPI, Windows WDM-KS: the same
device appears once per driver; WASAPI or MME are the usual choice).

```
twistlive -d 12 -c F5ZZO
```
decodes device 12 continuously: a pass every 3 s over the last 45 s of
audio; every frame (classic, TWIST S, TWIST L) is printed once, as soon
as it is decoded, with its UTC start time. A status line every minute
gives the input level (aim for -30 to -15 dBFS peak on noise and
signals), the overruns (should stay 0) and the longest pass. Ctrl-C
stops.

- **From an SDR**: route the SDR program's audio output (USB
  demodulation, beacon at about 1500 Hz) to a virtual audio cable
  (VB-Audio Cable, VAC), and give twistlive the cable's *input*
  (recording side) with `-d`.
- **From a transceiver**: its USB audio codec input, or the sound card
  of a SignaLink-type interface.
- `-f HZ -t HZ`: centre and half-width of the search (1500 ± 200 Hz).
- The PC clock must be right (Windows time synchronization): the UTC
  times printed come from it.

```
twistlive -s twl,carrier:10,pause:5,classic -C F6FBB -L JN12AX -o 5
```
plays a sequence once on output device 5 (transmitter in USB, PTT by
VOX for now; Ctrl-C stops at once). `-s carrier:30` is a 30 s tune
for setting the audio level and ALC.

## Building with MSYS2

In the MSYS2 UCRT64 shell:
```
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-fftw mingw-w64-ucrt-x86_64-portaudio mingw-w64-ucrt-x86_64-pkgconf make
make CC=gcc
make CC=gcc test
```

# Building twistd on Windows / Visual Studio 2022

**Status**: twistd uses exactly the same build approach as pi4d
(CMake + FFTW3 from vcpkg, hand-rolled argument parser, `gmtime_s`,
`M_PI` fallback), which a user has confirmed working on Windows with
Visual Studio 2022. twistd itself has **not yet been built on
Windows** - only on Linux (gcc 13, `-Wall -Wextra` clean, and with
CMake). If you hit an issue, please report the exact error.

## Overview

Two things are needed beyond what Linux/macOS already have:
1. **FFTW3** for Windows (not bundled with MSVC).
2. A build entry point - either CMake (recommended, works natively
   with VS2022's "Open Folder" feature) or a manually-created Visual
   Studio project.

## Recommended path: vcpkg + CMake + VS2022

[vcpkg](https://vcpkg.io) is Microsoft's C/C++ package manager; it
provides prebuilt FFTW3 packages and integrates directly with
VS2022's built-in CMake support.

### 1. Install vcpkg (if not already installed)

In a Developer Command Prompt or PowerShell:
```
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

### 2. Install FFTW3

```
C:\vcpkg\vcpkg install fftw3:x64-windows
```

### 3. Open the project in Visual Studio 2022

- `File > Open > Folder...`, select the `twist` folder
  (containing `CMakeLists.txt`).
- VS2022 detects `CMakeLists.txt` automatically and configures the
  project. If it does not pick up vcpkg automatically, set the
  toolchain file explicitly:
  - `Project > CMake Settings for twistd`, or edit
    `CMakePresets.json` / pass
    `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
    in the CMake command-line arguments field.
  - Alternatively, run once: `C:\vcpkg\vcpkg integrate install` -
    this makes vcpkg packages available to MSBuild/CMake projects
    system-wide without extra flags.

### 4. Build

- `Build > Build All` (or `Ctrl+Shift+B`).
- The resulting `twistd.exe` appears under `out\build\<config>\`.

### 5. Run

```
out\build\x64-Release\twistd.exe -V
out\build\x64-Release\twistd.exe beacon.wav
```

## Static linking (single self-contained twistd.exe, no fftw3.dll)

By default, `vcpkg install fftw3:x64-windows` (step 2 above) gives
you a **dynamic** FFTW3 - `twistd.exe` will need `fftw3.dll` alongside
it (or on the `PATH`) at runtime. To link FFTW statically into the
executable instead - no separate DLL to ship or install - use vcpkg's
static triplet:

```
C:\vcpkg\vcpkg install fftw3:x64-windows-static-md
```

Then tell CMake to use that triplet when configuring (in VS2022:
`Project > CMake Settings for twistd`, add to the CMake command-line
arguments; or on the command line):

```
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

No change to `CMakeLists.txt` or the source is needed - `find_package
(FFTW3 CONFIG)` picks up whichever triplet's package is active and
`FFTW3::fftw3` links it in accordingly, static or dynamic. The
resulting `twistd.exe` no longer depends on `fftw3.dll`; it still links
the standard MSVC C runtime DLLs (`x64-windows-static-**md**` keeps
the CRT dynamic - present on essentially any Windows machine, or
covered by the VC++ Redistributable).

**Fully static (including the C runtime)**, for a truly single-file
`.exe` with zero external DLL dependencies beyond core Windows system
DLLs:

```
C:\vcpkg\vcpkg install fftw3:x64-windows-static
```

This additionally requires the whole project - not just FFTW - to use
the static CRT (`/MT` instead of the default `/MD`), or you'll get
CRT-mismatch link errors. Add this near the top of `CMakeLists.txt`,
before `add_executable`:

```cmake
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
```

then configure with `-DVCPKG_TARGET_TRIPLET=x64-windows-static` as
above. This variant is untested here (like the rest of the Windows
build - see the disclaimer at the top of this file); the
`-static-md` option above is the simpler, lower-risk one to try
first since it only touches FFTW's linkage, not the whole project's
runtime settings.

## Alternative: manual FFTW3 binaries (no vcpkg)

Note: this route only gets you a **dynamic** FFTW (the `.lib` you
generate below is an import stub for `fftw3.dll`, not a real static
library) - `twistd.exe` will still need the DLL alongside it. For a
statically-linked, single-file `twistd.exe`, use the vcpkg
`-static-md`/`-static` triplets described above instead; building a
true static FFTW3 from source without vcpkg is possible but
considerably more involved, and out of scope here.

If you'd rather not use vcpkg:

1. Download the precompiled Windows FFTW3 DLLs from
   <https://www.fftw.org/install/windows.html> (e.g.
   `fftw-3.3.5-dll64.zip` for 64-bit).
2. Extract it somewhere, e.g. `C:\fftw`.
3. Generate the `.lib` import library MSVC needs (the zip only
   contains `.dll` and `.def` files) - in a Developer Command Prompt,
   from inside `C:\fftw`:
   ```
   lib /machine:x64 /def:libfftw3-3.def
   ```
   This produces `libfftw3-3.lib`.
4. Point CMake at it manually:
   ```
   cmake -B build -DFFTW3_LIBRARY=C:/fftw/libfftw3-3.lib -DFFTW3_INCLUDE_DIR=C:/fftw
   cmake --build build --config Release
   ```
5. Copy `libfftw3-3.dll` next to `twistd.exe` (or add `C:\fftw` to your
   `PATH`) so it can be found at runtime.

## What changed for Windows portability

The Linux/macOS build (`make`) is **unaffected** by any of this - all
the changes below are gated behind `#ifdef _WIN32` / CMake's `WIN32`,
so the already-validated Linux behavior is unchanged bit-for-bit.

- **Argument parsing**: MSVC has no `<unistd.h>`/`getopt()`. Rather than
  reimplementing it, `twistd.c` uses a small hand-rolled,
  order-independent argument parser shared by all platforms (no
  `#ifdef` needed here) - options are recognized regardless of where
  they appear relative to the `.wav` filename (`twistd file.wav -v` and
  `twistd -v file.wav` behave identically), matching a value either
  glued to its flag (`-f800`) or as a separate next argument
  (`-f 800`).
- **`M_PI`**: MSVC doesn't define it in `<math.h>` unless
  `_USE_MATH_DEFINES` is set before the include. Defined as a fallback
  in `core/twdefs.h`, guarded by `#ifndef M_PI` - a no-op on Linux, where it's
  already defined.
- **`gmtime()`**: works on MSVC but triggers a deprecation warning
  (C4996); `gmtime_s()` (the thread-safe variant, different argument
  order) is used on Windows instead. Functionally equivalent either
  way for this single-threaded CLI tool.

## Known caveat: `long` is 32-bit on Windows

Windows uses the LLP64 model, where `long` is 32-bit even in 64-bit
builds (unlike Linux/macOS's LP64, where `long` is 64-bit). This
codebase uses `long` for sample counts and buffer offsets. In
practice this is not a real limitation for `twistd`'s use case - OPERA
frames last 30 s (Op05) to 33 min (Op32);
32-bit `long` only becomes a problem past roughly 2^31 samples, i.e.
around **49 hours** of continuous audio at 12000 Hz (a few hours for
an I/Q capture at 192 kHz). No refactor was
done to close this gap since it does not affect realistic usage, but
it's worth knowing if you ever feed `twistd` an unusually long file on
Windows.

## If the build fails

Please report the exact compiler error - in particular:
- Whether it's a missing-header error (likely an FFTW3 path issue -
  check step 3/4 above)
- Whether it's a missing-header error unrelated to the above (please
  report the exact message)
- Whether it's a warning treated as an error (MSVC's `/W4` is stricter
  than gcc's `-Wall -Wextra` about a few things - worth trying
  `/W3` if `/W4` is too noisy, by editing `CMakeLists.txt`)
